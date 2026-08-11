// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "PluginDefs.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#include "Cache.h"
#include "ChatInput.h"
#include "Hubs.h"
#include "Incoming.h"
#include "Languages.h"
#include "Marshal.h"
#include "MessageFilter.h"
#include "Nicks.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "TranslateQueue.h"

namespace {

// The guid must stay fixed forever: the host keys settings, the install
// directory and update detection off it.
const char* const kGuid = "{2f6b19c4-7a83-4d51-b0e7-9c48d3a1f652}";
const char* const kName = "Babelfish";
const char* const kAuthor = "Kaje";
const char* const kDescription =
    "Translates what you write into English before it is sent. Ctrl+G in the message box "
    "translates and sends; plain Enter still sends exactly what you typed. Works out of the "
    "box with no account and no API key.";
const char* const kWeb = "";
const double kVersion = 1.0;

HINSTANCE g_instance = nullptr;
DCHooksPtr g_hooks = nullptr;
DCLogPtr g_log = nullptr;
DCConfigPtr g_config = nullptr;
DCHubPtr g_hubs = nullptr;

subsHandle g_chatOutSub = nullptr;
subsHandle g_pmOutSub = nullptr;
subsHandle g_commandSub = nullptr;
subsHandle g_commandPmSub = nullptr;
subsHandle g_timerSub = nullptr;
subsHandle g_uiSub = nullptr;
subsHandle g_hubOnSub = nullptr;
subsHandle g_hubOffSub = nullptr;
subsHandle g_chatInSub = nullptr;

bool g_installed = false;

// Outgoing-chat hook traffic, for "/tr probe".
//
// reentrantSkips is the interesting one. The spec suspected the guard might be
// unnecessary if send_message bypassed the hook chain; it does not. Every
// translation the plugin sends comes straight back through this hook, and
// without the guard each one would be translated again, forever.
int g_chatOutCalls = 0;
int g_reentrantSkips = 0;

// Which client commands actually reach the plugin. "/me" is the question: it
// may be dispatched to HOOK_UI_CHAT_COMMAND like any other, or handled inside
// the client and never offered to us at all. Those need different answers and
// nothing so far distinguishes them.
int g_cmdCalls = 0;
std::string g_lastCmd;

// What the last Ctrl+G on a chat line read, and what became of it.
//
// Kept for "/tr probe" rather than only the log, because the one branch that
// cannot answer in the chat window -- the lookup failing, where the hub is
// exactly what is not known -- is the branch that leaves a keypress looking
// like it did nothing. Asking for two commands to find that out is one command
// too many when somebody is trying to read a hub.
std::mutex g_incomingMutex;
std::string g_lastIncomingLine;
std::string g_lastIncomingWhy;

void NoteIncoming(const std::string& line, const char* why) {
    std::lock_guard<std::mutex> lock(g_incomingMutex);
    // Enough to recognise the row, short enough to leave the probe readable.
    g_lastIncomingLine = line.size() > 48 ? line.substr(0, 48) + "..." : line;
    g_lastIncomingWhy = why;
}

// Section 3. Set around our own send so that, if the host runs the outgoing
// hook for messages the plugin itself sends, the message is not queued again.
// It costs nothing and saves an infinite loop on the first run if it turns out
// send_message does go back through the hook chain.
thread_local bool t_reentrant = false;

struct ReentrantGuard {
    ReentrantGuard() { t_reentrant = true; }
    ~ReentrantGuard() { t_reentrant = false; }
};

// The host's logger writes into a window inside the client, not to any file on
// disk, which makes the plugin's own diagnostics awkward to get at just when
// they matter most. Everything logged is therefore also kept here, and "/tr log"
// prints it into the chat window where it can be read and copied.
constexpr size_t kRecentLogMax = 40;
std::mutex g_logMutex;
std::deque<std::string> g_recentLog;

void Remember(const std::string& line) {
    try {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_recentLog.push_back(line);
        while (g_recentLog.size() > kRecentLogMax) g_recentLog.pop_front();
    } catch (...) {
        // A full log is not worth failing an operation over.
    }
}

std::vector<std::string> RecentLog() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return std::vector<std::string>(g_recentLog.begin(), g_recentLog.end());
}

void LogLine(const std::string& message) {
    Remember(message);
    if (g_log && g_log->log) g_log->log(("Babelfish: " + message).c_str());
}

// Allocates nothing on the path that matters, so it still works while reporting
// a bad_alloc; the remembering is best-effort and swallows its own failures.
void LogRaw(const char* message) {
    if (g_log && g_log->log) g_log->log(message);
    Remember(message);
}

// A hook procedure is called by the host, often from inside its own send path
// and sometimes with Windows frames in between. An exception let loose here
// does not unwind to anything that can handle it -- it ends the client. Every
// procedure the host calls into runs through this.
//
// This is not hypothetical: a std::system_error escaping the marshalling
// window's creation path crashed FulDC++ once a second until it was fixed.
template <typename Body>
Bool Guarded(Body&& body) {
    try {
        return body();
    } catch (const std::exception& error) {
        try {
            LogLine(std::string("internal error: ") + error.what());
        } catch (...) {
            LogRaw("Babelfish: internal error");
        }
    } catch (...) {
        LogRaw("Babelfish: internal error");
    }
    // False means "not handled": the host carries on and the user's message
    // goes out exactly as they wrote it.
    return False;
}

// ---------------------------------------------------------------------------
// Reading the outgoing message out of a chat hook
// ---------------------------------------------------------------------------
//
// PluginDefs.h documents the object for HOOK_CHAT_OUT and HOOK_CHAT_PM_OUT but
// says nothing about pData, and the FulDC++ core was not available to check
// against. The two shapes it can plausibly be are a bare char* and a
// StringData*, whose first field is a const char*.
//
// Rather than guess, the shape is probed once and then remembered. The probe is
// reliable because the two cases are nothing alike in memory: read the first
// eight bytes as a pointer, and for a bare char* those bytes are the message
// text itself, which as a pointer value is a handful of ASCII bytes and never
// points at committed memory.

bool IsReadable(const void* address, size_t bytes) {
    if (!address) return false;

    MEMORY_BASIC_INFORMATION info = {};
    if (::VirtualQuery(address, &info, sizeof(info)) == 0) return false;
    if (info.State != MEM_COMMIT) return false;

    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((info.Protect & readable) == 0) return false;
    if (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;

    const auto start = static_cast<const unsigned char*>(info.BaseAddress);
    const auto want = static_cast<const unsigned char*>(address);
    return static_cast<size_t>(want - start) + bytes <= info.RegionSize;
}

// A plausible NUL-terminated UTF-8 string within the first kProbeLimit bytes.
bool LooksLikeString(const char* candidate) {
    constexpr size_t kProbeLimit = 4096;
    if (!IsReadable(candidate, 1)) return false;

    for (size_t i = 0; i < kProbeLimit; ++i) {
        if (!IsReadable(candidate + i, 1)) return false;
        if (candidate[i] == '\0') return true;
    }
    return false;
}

const char* OutgoingText(dcptr_t pData) {
    // The chat hooks pass pData as a plain UTF-8, NUL-terminated char* -- that is
    // the documented contract (PluginDefs.h, HOOK_CHAT_*). An earlier version
    // probed the first bytes as a possible pointer to guard against a StringData
    // layout, but that meant dereferencing the first eight bytes of an incoming,
    // attacker-controlled chat line as an address -- and caching that verdict
    // process-wide. The contract is the plain char*; trust it.
    return reinterpret_cast<const char*>(pData);
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// HubData and UserData are only ever read through their first fields, and
// nothing found through find_hub or find_user is ever released.
//
// This is not caution, it is measured. A diagnostic build logged what find_hub
// handed back, and the answer was:
//
//     Babelfish: find_hub -> 000002CBCED9B0E0 isManaged=73
//
// isManaged is a dcBool. It can be 0 or 1. It read 73, so the field at that
// offset is not isManaged, which means the struct FulDC++ passes does not have
// the layout PluginDefs.h describes -- that header came from the DC++ SDK, and
// this client is an AirDC++ fork that is free to have extended the struct.
//
// That retro-explains the second crash: "if (!hub->isManaged) release(hub)" was
// deciding whether to free the host's memory by reading a garbage byte, and
// sooner or later that byte was zero. The heap corruption followed.
//
// What is proven to work is url at offset 0 -- /tr status reads it, find_hub
// accepts it, and local_message delivers to the right window. Everything past
// the leading pointers is treated as unknown. Leaking a small struct per
// message is a trade worth making; guessing at an offset is not.
// ---------------------------------------------------------------------------

// Writes into the chat window of a hub the caller already holds.
//
// Preferred over the url form below wherever a live pointer is in hand, which
// is every synchronous path: a hook is handed its own HubDataPtr in pObject and
// that object is valid for the length of the call. Looking the same hub up
// again through find_hub is work the host has already done, and it is where two
// separate crashes landed -- both inside the same routine in FulDC.exe, ~0x20
// bytes apart, once dereferencing -1 and once null. Whatever find_hub returns
// for a given url is evidently not always something local_message will accept.
void LocalMessage(HubDataPtr hub, const std::string& message) {
    if (!g_hubs || !hub) return;
    g_hubs->local_message(hub, message.c_str(), MSG_SYSTEM);
}

// A non-null result from find_hub is not enough to hand back to the host.
//
// Measured, twice, from two crash dumps: the client died inside strlen, called
// from PluginApiImpl::sendLocalMessage, with the string register holding null
// once and stale bytes the other time. The message pointer comes from a
// std::string and cannot be either, so what sendLocalMessage was reading came
// out of the HubData -- meaning find_hub had returned an object whose url was
// null or dangling. "if (!hub)" does not catch that, because the object pointer
// itself is fine.
//
// So the object is checked before it goes back across the boundary. url at
// offset 0 is the one field of this struct proven to be where the header says
// (see the layout note above), which is exactly what makes this test possible.
bool UsableHub(HubDataPtr hub) {
    return hub && LooksLikeString(hub->url) && hub->url[0] != '\0';
}

// The url form, for the cases with no live pointer to hand: a private message,
// whose hook gives a user rather than a hub, and a translation coming back on
// the worker's schedule long after its hook returned.
//
// Returns false when the line could not be shown. Nothing that goes on the wire
// depends on this -- it only writes into a window here -- so the callers decide
// for themselves whether losing it is worth mentioning.
bool LocalMessage(const std::string& hubUrl, const std::string& message) {
    if (!g_hubs || hubUrl.empty()) return false;

    // find_hub returns a copy we own; it must be released through the API (a
    // host-side free), never leaked and never free()'d across the CRT boundary.
    HubDataPtr hub = g_hubs->find_hub(hubUrl.c_str());
    if (!UsableHub(hub)) {
        if (hub) g_hubs->release(hub);
        return false;
    }

    g_hubs->local_message(hub, message.c_str(), MSG_SYSTEM);
    g_hubs->release(hub);
    return true;
}

class HostSink : public Queue::Sink {
public:
    void Deliver(const Queue::Job& job, const std::string& text, bool translated) override {
        if (!g_hubs) return;

        if (job.incoming) {
            // Straight back into the window the line came from, marked as the
            // plugin talking rather than as a person. Nothing goes on the wire.
            if (!translated) return;  // nothing useful to add to what is already there
            const std::string line = job.nick.empty() ? text : job.nick + ": " + text;

            // Resolved by url, not through the borrowed hook pointer -- see Deliver's
            // note below. LocalMessage(url) finds, uses and releases its own copy.
            LocalMessage(job.hubUrl, line);
            return;
        }


        ReentrantGuard guard;
        const Bool thirdPerson = job.thirdPerson ? True : False;

        // The hook's own pObject (job.hubHandle / job.userHandle) is a borrow that
        // was valid only for the length of the hook; this runs on the worker's
        // schedule long after, so it is never dereferenced here. The destination is
        // resolved fresh from the url/cid the job carried across, and the copy the
        // host hands back is released the moment we are done with it.
        if (job.isPm) {
            UserDataPtr target = g_hubs->find_user(job.cid.c_str(), job.hubUrl.c_str());
            if (!target || !LooksLikeString(target->nick)) {
                if (target) g_hubs->release_user(target);
                LogLine("user " + job.nick + " is gone, dropping: " + text);
                return;
            }
            g_hubs->send_private_message(target, text.c_str(), thirdPerson);
            g_hubs->release_user(target);
            return;
        }

        HubDataPtr target = g_hubs->find_hub(job.hubUrl.c_str());
        if (!UsableHub(target)) {
            if (target) g_hubs->release(target);
            LogLine("hub " + job.hubUrl + " is gone, dropping: " + text);
            return;
        }
        g_hubs->send_message(target, text.c_str(), thirdPerson);
        g_hubs->release(target);
    }

    void Notice(const Queue::Job& job, const std::string& message) override {
        // A notice is something the user needs to read - a spent quota, a
        // source language that looks wrong. If the window cannot be reached it
        // goes to the log rather than nowhere.
        //
        // A notice about a private message has the same problem the echo had:
        // main chat is the only window a plugin can write to. It goes there
        // anyway, because the alternative is saying nothing about a translation
        // that did not happen -- but it says which conversation it is about, so
        // a line surfacing in the wrong window is at least not a mystery.
        const std::string shown =
            job.isPm ? message + " (about a private message" +
                           (job.nick.empty() ? "" : " to " + job.nick) + ")"
                     : message;
        if (!LocalMessage(job.hubUrl, shown)) LogLine(shown);
    }

    void Log(const std::string& message) override { LogLine(message); }
};

HostSink g_sink;

// Hubs cannot reach the plugin API itself, so it asks through here.
bool ProbeHubConnected(const std::string& url) {
    if (!g_hubs || url.empty()) return false;
    HubDataPtr hub = g_hubs->find_hub(url.c_str());
    const bool connected = UsableHub(hub);
    if (hub) g_hubs->release(hub);
    return connected;
}

void OnQueueResult(uint64_t jobId) {
    Queue::DeliverResult(jobId);
}

// ---------------------------------------------------------------------------
// Queueing an outgoing message
// ---------------------------------------------------------------------------

// Everything the two chat hooks have in common. Returns true when the message
// has been taken over by the plugin and must not go out as written.
bool QueueOutgoing(const std::string& hubUrl, const std::string& cid, const std::string& nick,
                   bool isPm, const std::string& raw, bool userAsked, HubDataPtr liveHub,
                   UserDataPtr liveUser) {
    // Every caller is a hook running on the GUI thread, which makes this the
    // right moment to be sure the window the worker posts results to exists.
    // Ctrl+G does this in the chat-input subclass; "/tr once" and auto mode
    // reach the queue without ever passing through there.
    Marshal::EnsureWindow();

    Settings& settings = CurrentSettings();

    if (isPm && !settings.translatePM && !userAsked) return false;
    if (!userAsked && !settings.IsAutoHub(hubUrl)) return false;

    // "auto" means the service decides. The one that cannot is told what the
    // machine says instead, which is the old behaviour and better than refusing.
    std::string configuredSource = settings.sourceLang;
    if (configuredSource == Languages::kAutoSource) {
        if (BackendDetectsSource(settings.backend)) {
            configuredSource.clear();  // empty: let the service work it out
        } else {
            configuredSource = Languages::DetectSource();
            static bool told = false;
            if (!told) {
                told = true;
                LogLine(settings.backend +
                        " cannot detect the source language; using " + configuredSource);
            }
        }
    }

    // Section 7.4: an English-speaking user has nothing for this plugin to do.
    if (configuredSource.empty() && settings.sourceLang != Languages::kAutoSource) {
        LogLine("no source language set; use /tr from <code>");
        return false;
    }
    if (configuredSource == Languages::kTarget) {
        LogLine("source language is English; nothing to translate");
        return false;
    }

    // The brake. Checked before the job is queued, so the message goes out as
    // written rather than disappearing into a service that will not answer.
    if (settings.dailyLimitChars > 0 && settings.quotaChars >= settings.dailyLimitChars) {
        static std::string stoppedOn;
        if (stoppedOn != settings.quotaDate) {
            stoppedOn = settings.quotaDate;
            char line[220] = {};
            snprintf(line, sizeof(line),
                     "Babelfish: daily limit of %lld characters reached - sending everything as "
                     "written until tomorrow. Raise or remove it in Configure.",
                     settings.dailyLimitChars);
            if (liveHub) {
                LocalMessage(liveHub, line);
            } else {
                LocalMessage(hubUrl, line);
            }
            LogLine(line);
        }
        return false;
    }

    // Section 7.2: the prefix corrects the source language, and only matters
    // when a translation is actually going to happen.
    std::string sourceLang = configuredSource;
    std::string text = raw;
    std::string prefixLang;
    std::string rest;
    if (MessageFilter::ParseLangPrefix(text, prefixLang, rest)) {
        sourceLang = prefixLang;
        text = rest;
    }

    const bool thirdPerson = MessageFilter::IsThirdPerson(text);
    if (thirdPerson) text = MessageFilter::StripThirdPerson(text);

    if (!MessageFilter::ShouldTranslate(text, userAsked)) return false;

    Queue::Job job;
    job.hubUrl = hubUrl;
    job.cid = cid;
    job.nick = nick;
    job.isPm = isPm;
    job.hubHandle = liveHub;
    job.userHandle = liveUser;
    job.text = text;
    job.thirdPerson = thirdPerson;
    job.sourceLang = sourceLang;
    job.config.backend = settings.backend;
    job.config.apiKey = settings.ApiKeyFor(settings.backend);
    job.config.azureRegion = settings.azureRegion;
    job.config.email = settings.email;
    job.nicks = MessageFilter::MergeMaskWords(Nicks::All(), settings.keepWords);

    if (settings.dailyLimitChars > 0) {
        const long long warnAt = (settings.dailyLimitChars * 4) / 5;
        if (settings.quotaChars >= warnAt) {
            static std::string warnedOn;
            if (warnedOn != settings.quotaDate) {
                warnedOn = settings.quotaDate;
                char line[200] = {};
                snprintf(line, sizeof(line),
                         "Babelfish: %lld of today's %lld characters used.", settings.quotaChars,
                         settings.dailyLimitChars);
                if (liveHub) LocalMessage(liveHub, line);
            }
        }
    }

    if (Queue::Push(std::move(job)) == 0) {
        LogLine("queue is not running; sending as written");
        return false;
    }

    // Section 3: without a local echo the message box empties and nothing
    // appears for the best part of a second, which reads as a broken client.
    //
    // Never for a private message. DCHub::local_message is the only way a
    // plugin can write a line of its own, and it takes a HubDataPtr -- the SDK
    // has no equivalent for a private window. So the echo of a private message
    // appeared in the hub's main chat: the right text in a window nobody meant
    // it for, which is worse than the second of silence it was there to fill.
    // The message itself is unaffected; it goes out through
    // send_private_message and lands where it should.
    if (settings.echoOriginal && !isPm) {
        // The caller's live pointer wherever there is one, which is every hub
        // path that reaches here.
        if (liveHub) {
            LocalMessage(liveHub, "\xE2\x86\x92 " + raw);
        } else {
            LocalMessage(hubUrl, "\xE2\x86\x92 " + raw);
        }
    }
    return true;
}

// The nick that introduced the matched message, which is "<nick>" in every DC
// client there has ever been.
//
// Taken from immediately before the match rather than from the start of what
// was captured. A selection spanning two rows begins with somebody else's line,
// and labelling Tonka's message with Kaje's name is the sort of small wrongness
// that makes a feature untrustworthy.
std::string NickBefore(const std::string& line, size_t matchPos) {
    const size_t limit = matchPos == std::string::npos ? line.size() : matchPos;
    const size_t close = line.rfind('>', limit == 0 ? 0 : limit - 1);
    if (close == std::string::npos) return {};

    const size_t open = line.rfind('<', close);
    if (open == std::string::npos || close - open > 64 || close <= open + 1) return {};
    return line.substr(open + 1, close - open - 1);
}

// Ctrl+G in a chat log rather than in the message box: translate the line
// somebody else wrote, for the user's eyes only.
//
// This runs the other way round from everything else in the plugin. Outgoing
// chat always goes into English because that is the language of the hubs. A
// line to be understood goes into the language the user reads, and the source
// is whatever the writer happened to use -- which is why it is left to the
// service to work out.
void TranslateIncoming(const std::string& displayedLine) {
    NoteIncoming(displayedLine, "read, not looked up yet");

    Incoming::Line found;
    size_t matchPos = std::string::npos;
    if (!Incoming::Find(displayedLine, found, &matchPos)) {
        NoteIncoming(displayedLine, "no arrival matches that line");

        // Older than the ring, or not a chat message at all -- a join notice,
        // the plugin's own output, a line from before the plugin loaded.
        //
        // That last one is much the most common, and it is not obvious from the
        // outside: installing a new build restarts the plugin and empties the
        // ring, so every line already on screen becomes untranslatable at once.
        // Half an hour was spent on a line from before a reinstall, pressing a
        // key that could not possibly work and said nothing about why.
        const std::string why =
            Incoming::Count() == 0
                ? "Babelfish: nothing has arrived since I was loaded, so there is no line I "
                  "can place yet. Try one that comes in from now on."
                : "Babelfish: that line arrived before I did, or has fallen out of the last "
                  "hundred. I can only translate lines that came in while I was running.";

        LogLine("cannot place that line: " + displayedLine);
        if (!LocalMessage(Incoming::NewestHub(), why)) LogLine(why);
        return;
    }

    Settings& settings = CurrentSettings();

    if (settings.dailyLimitChars > 0 && settings.quotaChars >= settings.dailyLimitChars) {
        NoteIncoming(displayedLine, "daily limit");
        LocalMessage(found.hubUrl, "Babelfish: daily limit reached; nothing translated.");
        return;
    }

    // Into whatever the user reads. "auto" is a statement about the source and
    // says nothing about the target, so the machine's language stands in.
    std::string target = settings.sourceLang;
    if (target.empty() || target == Languages::kAutoSource) target = Languages::DetectSource();
    if (target.empty()) target = Languages::kTarget;

    // Out of whatever the writer used. Only MyMemory has to be told, and in
    // these hubs English is the overwhelming answer.
    std::string source;
    if (!BackendDetectsSource(settings.backend)) source = Languages::kTarget;

    if (!source.empty() && source == target) {
        NoteIncoming(displayedLine, "same language both ways");
        LocalMessage(found.hubUrl,
                     "Babelfish: that would be a translation into the same language.");
        return;
    }

    // userAsked, so the English heuristic does not apply -- English is exactly
    // what wants translating here. The rules that stop nonsense still do.
    if (!MessageFilter::ShouldTranslate(found.text, true)) {
        NoteIncoming(displayedLine, "the filter rejected it");
        LocalMessage(found.hubUrl, "Babelfish: nothing worth translating in that line.");
        return;
    }

    Queue::Job job;
    job.hubUrl = found.hubUrl;
    job.hubHandle = found.hubHandle;
    job.nick = NickBefore(displayedLine, matchPos);
    job.incoming = true;
    job.text = found.text;
    job.sourceLang = source;
    job.targetLang = target;
    job.config.backend = settings.backend;
    job.config.apiKey = settings.ApiKeyFor(settings.backend);
    job.config.azureRegion = settings.azureRegion;
    job.config.email = settings.email;
    job.nicks = MessageFilter::MergeMaskWords(Nicks::All(), settings.keepWords);

    NoteIncoming(displayedLine, "queued");
    Queue::Push(std::move(job));
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

// Section 3: two separate procedures, not one generic one. The objects are
// different types and merging them buys a cast bug that only shows up in
// production.
Bool DCAPI OnChatOut(dcptr_t pObject, dcptr_t pData, dcptr_t, Bool* bBreak) {
    return Guarded([&]() -> Bool {
        HubDataPtr hub = reinterpret_cast<HubDataPtr>(pObject);
        const char* raw = OutgoingText(pData);

        bool wasAction = false;
        const bool userAsked = ChatInput::ConsumeTranslateRequest(raw ? raw : "", &wasAction);

        // The client took "/me " off before handing the line over. Putting it
        // back here means the filter recognises the action, strips it again for
        // the translator, and the send goes out with thirdPerson set -- so the
        // line stays an action instead of turning into ordinary chat.
        std::string text = raw ? raw : "";
        if (wasAction) text = "/me " + text;

        // Counted rather than logged: this runs for every line the user sends,
        // and a log entry per message would push everything worth reading out
        // of the ring buffer. "/tr probe" reports the totals.
        ++g_chatOutCalls;
        if (hub && hub->url) Hubs::Seen(hub->url);
        if (t_reentrant) {
            ++g_reentrantSkips;
            return False;
        }
        if (!hub || !hub->url || !raw) return False;

        if (!QueueOutgoing(hub->url, "", "", false, text, userAsked, hub, nullptr)) return False;

        if (bBreak) *bBreak = True;
        return True;
    });
}

Bool DCAPI OnPrivateMessageOut(dcptr_t pObject, dcptr_t pData, dcptr_t, Bool* bBreak) {
    return Guarded([&]() -> Bool {
        UserDataPtr user = reinterpret_cast<UserDataPtr>(pObject);
        const char* raw = OutgoingText(pData);
        const bool userAsked = ChatInput::ConsumeTranslateRequest(raw ? raw : "");
        if (t_reentrant) return False;
        if (!user || !raw) return False;

        const std::string hubUrl = user->hubHint ? user->hubHint : "";
        const std::string cid = user->cid ? user->cid : "";
        const std::string nick = user->nick ? user->nick : "";
        if (hubUrl.empty() || cid.empty()) {
            LogLine("private message with no hub hint or cid; sending as written");
            return False;
        }

        if (!QueueOutgoing(hubUrl, cid, nick, true, raw, userAsked, nullptr, user)) return False;

        if (bBreak) *bBreak = True;
        return True;
    });
}

// Every line that arrives, with the hub that carried it. This is what lets a
// line the user points at later be traced back to a hub without find_hub.
Bool DCAPI OnChatIn(dcptr_t pObject, dcptr_t pData, dcptr_t, Bool*) {
    return Guarded([&]() -> Bool {
        HubDataPtr hub = reinterpret_cast<HubDataPtr>(pObject);
        const char* raw = OutgoingText(pData);
        if (!hub || !hub->url || !raw) return False;

        Hubs::Seen(hub->url);

        Incoming::Line line;
        line.hubUrl = hub->url;
        line.hubHandle = hub;
        line.text = raw;
        Incoming::Remember(line);
        return False;  // never handled: incoming chat is displayed as it always was
    });
}

// DCHub cannot list the hubs the client is on, so these two keep the tally the
// settings dialog needs to show a list worth ticking.
Bool DCAPI OnHubOnline(dcptr_t pObject, dcptr_t, dcptr_t, Bool*) {
    return Guarded([&]() -> Bool {
        HubDataPtr hub = reinterpret_cast<HubDataPtr>(pObject);
        if (hub && hub->url) Hubs::Online(hub->url);
        return True;
    });
}

Bool DCAPI OnHubOffline(dcptr_t pObject, dcptr_t, dcptr_t, Bool*) {
    return Guarded([&]() -> Bool {
        HubDataPtr hub = reinterpret_cast<HubDataPtr>(pObject);
        if (hub && hub->url) Hubs::Offline(hub->url);
        return True;
    });
}

// Discovery has to be retried: at ON_LOAD the host's windows do not exist yet,
// and hub frames appear later as the user connects.
Bool DCAPI OnTimer(dcptr_t, dcptr_t, dcptr_t, Bool*) {
    return Guarded([]() -> Bool {
        if (!g_installed) {
            g_installed = ChatInput::EnsureInstalled();
            if (g_installed) LogLine("Ctrl+G is live in the message boxes");
        }

        // A read-only plugin folder is invisible from the inside: everything
        // works, and then nothing was kept. Saying it here costs one comparison
        // a second and turns a baffling symptom into a sentence.
        static bool saidWriteFailure = false;
        if (!saidWriteFailure) {
            const std::string failure = Settings::WriteFailure();
            if (!failure.empty()) {
                saidWriteFailure = true;
                LogLine("settings cannot be saved - cannot write " + failure +
                        ". The plugin's folder is read-only, so nothing you change will "
                        "survive a restart. Running the client from somewhere other than "
                        "Program Files fixes it.");
            }
        }
        return True;
    });
}

Bool DCAPI OnUiCreated(dcptr_t, dcptr_t, dcptr_t, Bool*) {
    return Guarded([]() -> Bool {
        // Logged here as well as in the timer. Whichever of the two wins the
        // race sets the flag, and only one of them used to say so -- which made
        // an empty log look like proof that nothing was installed. It was not.
        if (!g_installed) {
            g_installed = ChatInput::EnsureInstalled();
            if (g_installed) LogLine("Ctrl+G is live in the message boxes");
        }
        return True;
    });
}

// ---------------------------------------------------------------------------
// /tr
// ---------------------------------------------------------------------------

std::vector<std::string> SplitWords(const std::string& text, size_t limit) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size() && out.size() + 1 < limit) {
        while (i < text.size() && text[i] == ' ') ++i;
        const size_t start = i;
        while (i < text.size() && text[i] != ' ') ++i;
        if (i > start) out.push_back(text.substr(start, i - start));
    }
    while (i < text.size() && text[i] == ' ') ++i;
    if (i < text.size()) out.push_back(text.substr(i));
    return out;
}

std::string StatusText(const std::string& hubUrl) {
    const Settings& settings = CurrentSettings();
    const Cache::Stats stats = Cache::GetStats();

    const long long lookups = stats.hits + stats.misses;
    const int hitRate = lookups > 0 ? static_cast<int>((stats.hits * 100) / lookups) : 0;

    // The configured service and the one that last actually answered are two
    // different facts, and only the second one settles "am I really using the
    // service I paid for".
    const std::string last = Queue::LastService();

    char limitBuffer[32] = {};
    snprintf(limitBuffer, sizeof(limitBuffer), "%lld", settings.dailyLimitChars);
    const std::string sourceText =
        settings.sourceLang.empty()
            ? std::string("(unset)")
            : (settings.sourceLang == Languages::kAutoSource
                   ? std::string("auto") +
                         (BackendDetectsSource(settings.backend) ? "" : " (unsupported, using ") +
                         (BackendDetectsSource(settings.backend) ? "" : Languages::DetectSource()) +
                         (BackendDetectsSource(settings.backend) ? "" : ")")
                   : settings.sourceLang);

    const std::string limitText =
        settings.dailyLimitChars > 0 ? std::string(limitBuffer) : std::string("unlimited");

    char buffer[600] = {};
    snprintf(buffer, sizeof(buffer),
             "Babelfish: backend %s, %s -> en, auto %s for this hub, "
             "%lld of %s characters today, cache %zu entries / %d%% hits, %zu in flight, "
             "%zu nicks known, last answered by %s",
             settings.backend.c_str(),
             sourceText.c_str(),
             settings.IsAutoHub(hubUrl) ? "on" : "off", settings.quotaChars, limitText.c_str(),
             stats.entries, hitRate, Queue::Pending(), Nicks::Count(),
             last.empty() ? "nobody yet this session" : last.c_str());

    std::string text = buffer;
    const std::string failure = Settings::WriteFailure();
    if (!failure.empty()) {
        text += ". SETTINGS ARE NOT BEING SAVED: cannot write " + failure +
                " - that folder is read-only, so nothing here survives a restart";
    }
    return text;
}

// Every command on one line, which is what /tr help answers with.
//
// One line because forty-one local_message calls in a row from inside the
// command hook killed the client once already, and because a wall of text in a
// hub window is worse to read than a dense line.
std::string HelpText() {
    // The key first. Somebody typing /tr help is asking how to work this, and
    // every command below is a detail next to the one keystroke it turns on.
    return "Ctrl+G in the message box translates and sends; Ctrl+G on a line in the "
           "chat translates that line for you alone. Commands: "
           "/tr status | help | log | probe | from <code> | auto on|off | once <text> | "
           "backend <name> | key <key> | region <name> | email <address> | pm on|off | "
           "echo on|off | limit <n> | keep <word> | cache clear";
}

// Runs a /tr command. Returns the reply to show locally, or an empty string
// when the command produced no output of its own.
std::string RunCommand(const std::string& params, const std::string& hubUrl,
                       const std::string& cid, const std::string& nick, bool isPm,
                       HubDataPtr liveHub, UserDataPtr liveUser) {
    Settings& settings = CurrentSettings();
    const std::vector<std::string> words = SplitWords(params, 2);
    const std::string verb = words.empty() ? std::string("status") : words[0];
    const std::string rest = words.size() > 1 ? words[1] : std::string();

    if (verb == "status") return StatusText(hubUrl);

    if (verb == "help" || verb == "?") return "Babelfish: " + HelpText();

    if (verb == "from") {
        if (rest == Languages::kAutoSource) {
            settings.sourceLang = Languages::kAutoSource;
            settings.Save();
            return "Babelfish: the service will work out the source language itself";
        }
        if (!Languages::IsKnown(rest)) {
            return "Babelfish: unknown language code '" + rest + "'";
        }
        settings.sourceLang = rest;
        settings.Save();
        return std::string("Babelfish: source language is now ") + Languages::NameOf(rest);
    }

    if (verb == "auto") {
        if (rest != "on" && rest != "off") return "Babelfish: use /tr auto on|off";
        settings.SetAutoHub(hubUrl, rest == "on");
        return "Babelfish: automatic translation " + rest + " for " + hubUrl;
    }

    if (verb == "once") {
        if (rest.empty()) return "Babelfish: use /tr once <text>";
        if (!QueueOutgoing(hubUrl, cid, nick, isPm, rest, true, liveHub, liveUser)) {
            return "Babelfish: nothing to translate in that";
        }
        return {};
    }

    if (verb == "backend") {
        if (rest != "mymemory" && rest != "azure" && rest != "deepl" && rest != "google" &&
            rest != "claude") {
            return "Babelfish: backend must be mymemory, azure, deepl, google or claude";
        }
        settings.backend = rest;
        settings.Save();
        return "Babelfish: backend is now " + rest;
    }

    if (verb == "key") {
        if (settings.backend == "mymemory") {
            return "Babelfish: MyMemory needs no key. Choose another service first "
                   "with /tr backend <name>.";
        }
        // Belongs to whichever service is selected, so switching away and back
        // finds it again.
        settings.SetApiKeyFor(settings.backend, rest);
        settings.Save();
        return rest.empty() ? "Babelfish: API key for " + settings.backend + " cleared"
                            : "Babelfish: API key saved for " + settings.backend;
    }

    if (verb == "region") {
        settings.azureRegion = rest;
        settings.Save();
        return "Babelfish: Azure region set to '" + rest + "'";
    }

    if (verb == "email") {
        settings.email = rest;
        settings.Save();
        return rest.empty() ? "Babelfish: e-mail cleared"
                            : "Babelfish: MyMemory will be called with " + rest;
    }

    if (verb == "keep") {
        if (rest.empty()) {
            if (settings.keepWords.empty()) {
                return "Babelfish: no words are being kept out of translation. "
                       "/tr keep <word> adds one, /tr keep -<word> removes it.";
            }
            std::string list;
            for (const std::string& word : settings.keepWords) {
                if (!list.empty()) list += ", ";
                list += word;
            }
            return "Babelfish: kept out of translation: " + list;
        }

        // The cache is looked up before masking, so a phrase already translated
        // keeps coming back with the word still translated in it. Telling the
        // user to run /tr cache clear was not enough -- the advice was printed,
        // read, and reasonably ignored, because clearing up after a setting is
        // not the user's job. Changing the list clears the cache itself.
        if (rest[0] == '-') {
            const std::string word = rest.substr(1);
            if (!settings.RemoveKeepWord(word)) {
                return "Babelfish: '" + word + "' was not in the list";
            }
            settings.Save();
            Cache::Clear();
            return "Babelfish: '" + word + "' will be translated again. Cache cleared.";
        }

        if (!settings.AddKeepWord(rest)) {
            return "Babelfish: '" + rest +
                   "' is too short to keep. Under three characters a word turns up "
                   "inside too many others to mask safely.";
        }
        settings.Save();
        Cache::Clear();
        return "Babelfish: '" + rest +
               "' will be left alone from now on. Cache cleared, so anything already "
               "translated with it in gets asked again.";
    }

    if (verb == "pm") {
        if (rest != "on" && rest != "off") return "Babelfish: use /tr pm on|off";
        settings.translatePM = (rest == "on");
        settings.Save();
        return "Babelfish: private messages " + rest;
    }

    if (verb == "echo") {
        if (rest != "on" && rest != "off") return "Babelfish: use /tr echo on|off";
        settings.echoOriginal = (rest == "on");
        settings.Save();
        return "Babelfish: local echo " + rest;
    }

    if (verb == "limit") {
        if (rest.empty()) return "Babelfish: use /tr limit <characters per day>, or 0 for none";
        settings.dailyLimitChars = std::strtoll(rest.c_str(), nullptr, 10);
        if (settings.dailyLimitChars < 0) settings.dailyLimitChars = 0;
        settings.Save();
        return settings.dailyLimitChars > 0
                   ? "Babelfish: daily limit set to " + std::to_string(settings.dailyLimitChars) +
                         " characters"
                   : "Babelfish: daily limit removed";
    }

    if (verb == "cache" && rest == "clear") {
        Cache::Clear();
        return "Babelfish: cache cleared";
    }

    if (verb == "probe") {
        char tail[160] = {};
        snprintf(tail, sizeof(tail), " chatOut=%d reentrantSkips=%d cmds=%d lastCmd=%s",
                 g_chatOutCalls, g_reentrantSkips, g_cmdCalls,
                 g_lastCmd.empty() ? "(none)" : g_lastCmd.c_str());
        // The folder is worth printing every time rather than only when a
        // write has failed: somebody reporting a problem can paste it, and
        // where this plugin keeps its files depends entirely on where the
        // client was installed.
        std::string incoming;
        {
            std::lock_guard<std::mutex> lock(g_incomingMutex);
            incoming = g_lastIncomingWhy.empty()
                           ? std::string(" lastLine=(no Ctrl+G on a chat line yet)")
                           : " lastWhy=" + g_lastIncomingWhy + " lastLine=[" +
                                 g_lastIncomingLine + "]";
        }

        return "Babelfish probe: " + ChatInput::Diagnostics() + tail + incoming +
               " files=" + Settings::SettingsDirectoryUtf8();
    }

    if (verb == "log") {
        // One message, on one line, exactly like every other command here.
        //
        // The first version of this printed each line with its own
        // local_message call, and forty-one of those in a row from inside the
        // command hook crashed the client. Whatever the host does per message,
        // it does not survive being driven that hard re-entrantly -- and a
        // debugging aid that crashes the thing being debugged is worse than
        // none at all.
        const std::vector<std::string> lines = RecentLog();
        if (lines.empty()) return "Babelfish: nothing logged yet";

        constexpr size_t kMaxLines = 10;
        constexpr size_t kMaxChars = 900;
        const size_t first = lines.size() > kMaxLines ? lines.size() - kMaxLines : 0;

        std::string joined = "Babelfish log:";
        for (size_t i = first; i < lines.size(); ++i) {
            joined += " | ";
            joined += lines[i];
            if (joined.size() > kMaxChars) break;
        }
        if (joined.size() > kMaxChars) joined.resize(kMaxChars);
        return joined;
    }

    // Unknown verbs land here too, which is how "/tr help" worked before there
    // was a help command: by not being one. That is fine until somebody adds a
    // "help" feature and it quietly stops answering.
    return "Babelfish: no command called '" + verb + "'. " + HelpText();
}

std::string CommandName(const CommandDataPtr command) {
    if (!command || !command->command) return {};
    std::string name = command->command;
    if (!name.empty() && name[0] == '/') name.erase(0, 1);
    return name;
}

Bool DCAPI OnChatCommand(dcptr_t pObject, dcptr_t pData, dcptr_t, Bool* bBreak) {
    return Guarded([&]() -> Bool {
        CommandDataPtr command = reinterpret_cast<CommandDataPtr>(pData);
        const std::string name = CommandName(command);
        HubDataPtr hub = reinterpret_cast<HubDataPtr>(pObject);

        ++g_cmdCalls;
        g_lastCmd = name;
        if (hub && hub->url) Hubs::Seen(hub->url);

        // "/me" is deliberately not handled here. It was, briefly -- but the
        // counters showed it never reaches this hook at all; it goes through
        // the outgoing-chat hook with its prefix already removed, and that is
        // where it is dealt with.
        if (name != "tr" && name != "bf") return False;

        const std::string hubUrl = (hub && hub->url) ? hub->url : "";

        const std::string reply =
            RunCommand(command->params ? command->params : "", hubUrl, "", "", false, hub, nullptr);
        // Replying through the hook's own pointer, never through find_hub.
        if (!reply.empty()) LocalMessage(hub, reply);

        if (bBreak) *bBreak = True;
        return True;
    });
}

Bool DCAPI OnChatCommandPm(dcptr_t pObject, dcptr_t pData, dcptr_t, Bool* bBreak) {
    return Guarded([&]() -> Bool {
        CommandDataPtr command = reinterpret_cast<CommandDataPtr>(pData);
        const std::string name = CommandName(command);
        if (name != "tr" && name != "bf") return False;

        UserDataPtr user = reinterpret_cast<UserDataPtr>(pObject);
        const std::string hubUrl = (user && user->hubHint) ? user->hubHint : "";
        const std::string cid = (user && user->cid) ? user->cid : "";
        const std::string nick = (user && user->nick) ? user->nick : "";

        const std::string reply =
            RunCommand(command->params ? command->params : "", hubUrl, cid, nick, true, nullptr, user);
        if (!reply.empty()) LocalMessage(hubUrl, reply);

        if (bBreak) *bBreak = True;
        return True;
    });
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool Startup(DCCorePtr core) {
    g_hooks = reinterpret_cast<DCHooksPtr>(core->query_interface(DCINTF_HOOKS, DCINTF_HOOKS_VER));
    if (!g_hooks) return false;

    g_log = reinterpret_cast<DCLogPtr>(
        core->query_interface(DCINTF_LOGGING, DCINTF_LOGGING_VER));
    g_config = reinterpret_cast<DCConfigPtr>(
        core->query_interface(DCINTF_CONFIG, DCINTF_CONFIG_VER));
    g_hubs = reinterpret_cast<DCHubPtr>(
        core->query_interface(DCINTF_DCPP_HUBS, DCINTF_DCPP_HUBS_VER));

    if (!g_hubs) {
        // Without the hub interface there is no way to send anything, so there
        // is no point taking messages over.
        LogLine("host has no hub interface; plugin disabled");
        return false;
    }

    Settings::SetModule(g_instance);
    Settings::Bind(g_config, kGuid);
    CurrentSettings().Load();

    // Section 7.4: first run picks the source language up from Windows, so a
    // clean install needs no configuration at all.
    Settings& settings = CurrentSettings();
    if (settings.sourceLang.empty()) {
        settings.sourceLang = Languages::DetectSource();
        settings.Save();
        if (settings.sourceLang.empty()) {
            LogLine("could not tell what language Windows is in; set one with /tr from <code>");
        } else {
            LogLine("source language detected as " + settings.sourceLang);
        }
    }
    if (settings.sourceLang == Languages::kTarget) {
        LogLine("Windows is in English, so everything will pass through untouched until you "
            "set a source language with /tr from <code>");
    }

    // The commands are otherwise only in the Read-me, and nobody reads a
    // Read-me to find out that there is something to read about.
    LogLine("ready. Ctrl+G in the message box translates and sends; Ctrl+G on a line in "
            "the chat translates that line for you. /tr help lists every command");

    // Beside the settings, outside the directory the client empties. The cache
    // is money already spent; throwing it away on every update was a small,
    // silent charge for installing a new version.
    const std::wstring dataDir = Settings::SettingsDirectory();
    if (!dataDir.empty()) Cache::Load(dataDir + L"babelfish-cache.txt");

    Hubs::SetProbe(ProbeHubConnected);

    Marshal::SetModule(g_instance);
    Marshal::SetHandler(OnQueueResult);
    Queue::Start(&g_sink);

    ChatInput::SetModule(g_instance);
    ChatInput::SetIncomingHandler(TranslateIncoming);
    g_installed = ChatInput::EnsureInstalled();

    g_chatOutSub = g_hooks->bind_hook(HOOK_CHAT_OUT, OnChatOut, nullptr);
    g_pmOutSub = g_hooks->bind_hook(HOOK_CHAT_PM_OUT, OnPrivateMessageOut, nullptr);
    g_commandSub = g_hooks->bind_hook(HOOK_UI_CHAT_COMMAND, OnChatCommand, nullptr);
    g_commandPmSub = g_hooks->bind_hook(HOOK_UI_CHAT_COMMAND_PM, OnChatCommandPm, nullptr);
    g_chatInSub = g_hooks->bind_hook(HOOK_CHAT_IN, OnChatIn, nullptr);
    g_hubOnSub = g_hooks->bind_hook(HOOK_HUB_ONLINE, OnHubOnline, nullptr);
    g_hubOffSub = g_hooks->bind_hook(HOOK_HUB_OFFLINE, OnHubOffline, nullptr);
    g_uiSub = g_hooks->bind_hook(HOOK_UI_CREATED, OnUiCreated, nullptr);
    g_timerSub = g_hooks->bind_hook(HOOK_TIMER_SECOND, OnTimer, nullptr);

    return true;
}

void Teardown() {
    // Section 2, in this order. The hooks go first so nothing new can arrive,
    // then the worker is stopped and joined, and only then does anything the
    // worker might still have been touching get taken away.
    if (g_hooks) {
        if (g_chatOutSub) g_hooks->release_hook(g_chatOutSub);
        if (g_pmOutSub) g_hooks->release_hook(g_pmOutSub);
        if (g_commandSub) g_hooks->release_hook(g_commandSub);
        if (g_commandPmSub) g_hooks->release_hook(g_commandPmSub);
        if (g_chatInSub) g_hooks->release_hook(g_chatInSub);
        if (g_hubOnSub) g_hooks->release_hook(g_hubOnSub);
        if (g_hubOffSub) g_hooks->release_hook(g_hubOffSub);
        if (g_uiSub) g_hooks->release_hook(g_uiSub);
        if (g_timerSub) g_hooks->release_hook(g_timerSub);
    }
    g_chatOutSub = nullptr;
    g_pmOutSub = nullptr;
    g_commandSub = nullptr;
    g_commandPmSub = nullptr;
    g_uiSub = nullptr;
    g_timerSub = nullptr;
    g_chatInSub = nullptr;
    g_hubOnSub = nullptr;
    g_hubOffSub = nullptr;
    Incoming::Clear();
    Hubs::SetProbe(nullptr);
    Hubs::Clear();
    Nicks::Clear();

    Queue::Stop();

    ChatInput::Uninstall();
    Marshal::Destroy();

    Cache::Save();

    g_installed = false;
    g_hubs = nullptr;
    g_config = nullptr;
    g_hooks = nullptr;
    g_log = nullptr;
}

Bool DCAPI PluginMain(PluginState state, DCCorePtr core, dcptr_t) {
    return Guarded([&]() -> Bool {
    switch (state) {
        case ON_INSTALL:
        case ON_LOAD:
        case ON_LOAD_RUNTIME:
            return Startup(core) ? True : False;

        case ON_CONFIGURE:
            // ON_CONFIGURE arrives on the GUI thread, so the host's active
            // window is the right parent for a modal dialog.
            ShowSettingsDialog(g_instance, ::GetActiveWindow());
            return True;

        case ON_UNLOAD:
        case ON_UNINSTALL:
            Teardown();
            return True;

        default:
            return False;
    }
    });
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        // Not DisableThreadLibraryCalls: the worker thread is ours and the
        // notifications cost nothing, but leaving them on keeps the CRT's own
        // per-thread setup working on the thread we create.
    }
    return TRUE;
}

extern "C" DCEXP DCMAIN DCAPI pluginInit(MetaDataPtr info) {
    info->name = kName;
    info->author = kAuthor;
    info->description = kDescription;
    info->web = kWeb;
    info->guid = kGuid;
    info->dependencies = nullptr;
    info->numDependencies = 0;
    info->apiVersion = DCAPI_CORE_VER;
    info->version = kVersion;

    return PluginMain;
}
