// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "TranslateQueue.h"

#include "Cache.h"
#include "Http.h"
#include "Languages.h"
#include "Marshal.h"
#include "MessageFilter.h"
#include "Settings.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Queue {
namespace {

// Section 5: three seconds from picking the job up to giving up on it,
// regardless of how many pieces a long message was split into.
constexpr auto kJobDeadline = std::chrono::milliseconds(3000);

// A message split into more pieces than this is not a chat line, and running
// twenty requests for it would burn the day's allowance in one go.
constexpr size_t kMaxPieces = 8;

// A MyMemory match below this means the translation memory had nothing close.
// Section 5 says use it anyway but say so in the log.
constexpr double kWeakMatch = 0.5;

struct Outcome {
    bool ok = false;
    std::string text;
    std::string error;
    bool quotaExceeded = false;

    // Filled by the worker, acted on by the GUI thread. Neither the settings
    // store nor the host's logger is documented as thread-safe, so the worker
    // records what happened and the GUI thread is the only one that writes.
    size_t billedChars = 0;
    std::string note;

    // Which backend actually answered, for /tr status.
    std::string service;

    // The service handed back exactly what it was given. Worth saying in the
    // window rather than only the log, because the fix is a command away.
    bool sourceLanguageSuspect = false;

    // A line somebody else wrote, handed to a service that cannot work out what
    // language it is in. Not a mystery and not the user's fault, so it gets its
    // own words rather than the general ones.
    bool incomingNeedsDetection = false;
};

std::mutex g_mutex;
std::condition_variable g_wake;
std::deque<Job> g_pending;
std::unordered_map<uint64_t, Job> g_inFlight;      // id -> job, for the send side
std::unordered_map<uint64_t, Outcome> g_outcomes;  // id -> result, filled by the worker

std::string g_lastService;

std::thread g_worker;
bool g_running = false;
bool g_stopping = false;
uint64_t g_nextId = 1;
Sink* g_sink = nullptr;

// Owned by the worker thread, but Cancel() is called from Stop() on another
// thread. That is the whole reason Http::Client is a class.
Http::Client g_http;

Outcome Process(const Job& job) {
    Outcome outcome;

    std::unique_ptr<ITranslator> translator = MakeTranslator(job.config);
    if (!translator->Usable(outcome.error)) return outcome;

    const auto deadline = std::chrono::steady_clock::now() + kJobDeadline;

    const size_t limit = translator->MaxRequestBytes();
    std::vector<std::string> pieces = MessageFilter::SplitForLimit(job.text, limit);
    if (pieces.empty()) return outcome;

    if (pieces.size() > kMaxPieces) {
        outcome.error = "message too long to translate";
        return outcome;
    }

    std::string assembled;

    for (const std::string& piece : pieces) {
        if (std::chrono::steady_clock::now() >= deadline) {
            outcome.error = "timed out";
            return outcome;
        }

        std::string lead;
        std::string core;
        std::string tail;
        MessageFilter::TrimParts(piece, lead, core, tail);

        if (core.empty()) {
            // Whitespace-only piece: nothing to translate, but the spacing has
            // to survive so multi-line messages come out with their breaks.
            assembled += piece;
            continue;
        }

        std::string translated;
        const std::string cacheLang = job.sourceLang + ">" + job.targetLang;
        if (Cache::Lookup(job.config.backend, core, cacheLang, translated)) {
            assembled += lead + translated + tail;
            continue;
        }

        // Nicks come out before the service ever sees them and go back after.
        const MessageFilter::Masked masked = MessageFilter::MaskNicks(core, job.nicks);

        // Everything worth translating was a name or a kept word. Nothing to
        // ask, nothing to bill, and nothing to cache -- the answer is the
        // message itself.
        if (!MessageFilter::AnythingToTranslate(masked.text)) {
            assembled += lead + core + tail;
            continue;
        }

        const TranslateResult result =
            translator->Translate(g_http, masked.text, job.sourceLang, job.targetLang);
        if (!result.ok) {
            outcome.error = result.error;
            outcome.quotaExceeded = result.quotaExceeded;
            return outcome;
        }

        std::string restored;
        if (!MessageFilter::UnmaskNicks(masked, result.text, restored)) {
            // A message that lost somebody's name is worse than one that was
            // never translated, so this joins the other reasons to send the
            // original: the service did not give the placeholders back.
            outcome.error = "a nick did not survive translation of: " + core;
            return outcome;
        }

        if (!MessageFilter::PlausibleTranslation(core, restored)) {
            // Section 5 says never swallow a message: the original goes out.
            outcome.error = "discarded an implausible translation of: " + core;
            return outcome;
        }

        // A service that hands back exactly what it was given did not translate
        // anything. Nearly always that means it was told the wrong source
        // language -- Polish typed while sourceLang is still "sv" comes back
        // untouched, a request spent for nothing and no hint as to why.
        //
        // The text still goes out, because it is the user's own words and
        // section 5 is clear about that. But it is not cached as a translation,
        // and the reason is said out loud so "/tr from" is an obvious next move.
        const bool unchanged =
            MessageFilter::Normalise(restored) == MessageFilter::Normalise(core);

        if (unchanged) {
            // Do not blame the source language. Two days of logs said "sv is
            // probably wrong" about "haha", "LOL", "pitt", "You're welcome!" and
            // a Jamtlandic dish nobody has ever put in a translation memory --
            // and it was wrong every single time. A service returning the text
            // untouched usually just means it had nothing, which is not a fault
            // and certainly not a diagnosis.
            //
            // It is still worth mentioning once, because a genuinely wrong
            // language does look exactly like this, and the note carries no
            // accusation now.
            outcome.sourceLanguageSuspect = true;
            if (job.incoming && !job.sourceLang.empty()) {
                // The source was assumed rather than detected, which for an
                // incoming line means the assumption was simply wrong: Italian
                // handed to a service told to expect English.
                outcome.incomingNeedsDetection = true;
            }
            if (outcome.note.empty()) {
                outcome.note = outcome.incomingNeedsDetection
                                   ? job.config.backend + " was told to expect " +
                                         job.sourceLang + " and that line is not, so nothing "
                                         "came back for: " + core
                                   : "no translation came back for: " + core;
            }
        } else {
            if (result.confidence < kWeakMatch && outcome.note.empty()) {
                outcome.note = "weak translation-memory match for: " + core;
            }
            outcome.service = translator->Name();
            Cache::Store(job.config.backend, core, cacheLang, restored);
        }

        outcome.billedChars += core.size();
        assembled += lead + restored + tail;
    }

    outcome.ok = true;
    outcome.text = assembled;
    return outcome;
}

void WorkerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_wake.wait(lock, [] { return g_stopping || !g_pending.empty(); });
            if (g_stopping) return;

            job = g_pending.front();
            g_pending.pop_front();
        }

        Outcome outcome = Process(job);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_stopping) return;
            g_outcomes[job.id] = std::move(outcome);
        }

        // Posting can fail because the marshalling window does not exist yet:
        // it is created from the GUI thread the first time a message hook runs,
        // and a job queued very early can finish before that has happened.
        //
        // This used to be a single call with a comment claiming the next hook
        // would re-post the id. Nothing re-posted anything -- the result simply
        // stayed in the map and the user's message vanished, which is precisely
        // what section 5 says must never happen. Retrying here is the whole fix;
        // the worker has nothing else to do while it waits.
        bool posted = false;
        for (int attempt = 0; attempt < 30; ++attempt) {
            if (Marshal::Post(job.id)) {
                posted = true;
                break;
            }
            {
                std::unique_lock<std::mutex> lock(g_mutex);
                if (g_stopping) return;
                g_wake.wait_for(lock, std::chrono::milliseconds(100),
                                [] { return g_stopping; });
                if (g_stopping) return;
            }
        }

        if (!posted) {
            // Three seconds without a window means it is never coming. Drop the
            // stored outcome rather than leak it, and say so.
            std::lock_guard<std::mutex> lock(g_mutex);
            g_outcomes.erase(job.id);
            g_inFlight.erase(job.id);
            if (g_sink) g_sink->Log("no marshalling window; lost: " + job.text);
        }
    }
}

}  // namespace

void Start(Sink* sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running) return;

    g_sink = sink;
    g_stopping = false;
    g_running = true;
    g_worker = std::thread(WorkerMain);
}

void Stop() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_running) return;
        g_stopping = true;
    }

    // Order matters. The flag is set first so the worker cannot start another
    // request, the in-flight request is aborted so it returns now rather than
    // in three seconds, and only then is the worker woken and joined.
    g_http.Cancel();
    g_wake.notify_all();

    if (g_worker.joinable()) g_worker.join();

    std::lock_guard<std::mutex> lock(g_mutex);
    g_running = false;
    g_sink = nullptr;
    g_pending.clear();
    g_inFlight.clear();
    g_outcomes.clear();
}

uint64_t Push(Job job) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_running || g_stopping) return 0;

    job.id = g_nextId++;
    const uint64_t id = job.id;
    g_inFlight[id] = job;
    g_pending.push_back(std::move(job));
    g_wake.notify_one();
    return id;
}

size_t Pending() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_inFlight.size();
}

std::string LastService() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_lastService;
}

void DeliverResult(uint64_t jobId) {
    Job job;
    Outcome outcome;
    Sink* sink = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto jobIt = g_inFlight.find(jobId);
        auto outcomeIt = g_outcomes.find(jobId);
        if (jobIt == g_inFlight.end() || outcomeIt == g_outcomes.end()) return;

        job = std::move(jobIt->second);
        outcome = std::move(outcomeIt->second);
        g_inFlight.erase(jobIt);
        g_outcomes.erase(outcomeIt);
        sink = g_sink;
    }

    if (!sink) return;

    if (!outcome.service.empty()) {
        SYSTEMTIME now = {};
        ::GetLocalTime(&now);
        char stamp[16] = {};
        snprintf(stamp, sizeof(stamp), " %02u:%02u", now.wHour, now.wMinute);

        std::lock_guard<std::mutex> lock(g_mutex);
        g_lastService = outcome.service + stamp;
    }

    // This touches state the worker deliberately left alone.
    if (outcome.billedChars > 0) CurrentSettings().AddQuotaChars(outcome.billedChars);

    // Once per session, both of them.
    //
    // An untranslatable word is ordinary - dialect, a name, a swear word, a
    // greeting nobody has filed anywhere. Saying so every time filled two days
    // of the client's system log with one line per "haha", which buries
    // everything actually worth reading. The information is still worth having
    // exactly once, because a genuinely wrong source language looks the same.
    static bool saidNothingCameBack = false;

    if (outcome.incomingNeedsDetection) {
        // Said every time, unlike the general one. It is not noise: it happens
        // only when somebody asked for a specific line and got nothing, and the
        // answer is a different service rather than patience.
        sink->Log(outcome.note);
        sink->Notice(job, "Babelfish: " + job.config.backend +
                              " cannot tell what language a line is in and has to be told. "
                              "For chat in other languages, pick Claude, DeepL, Azure or "
                              "Google in Configure - those work it out themselves.");
    } else if (outcome.sourceLanguageSuspect) {
        if (!saidNothingCameBack) {
            saidNothingCameBack = true;
            if (!outcome.note.empty()) sink->Log(outcome.note);
            sink->Notice(job, "Babelfish: some messages come back untranslated. Usually the "
                              "service simply has no translation for them - but if you are not "
                              "writing " + job.sourceLang +
                              ", change that in Configure or with /tr from <code>.");
        }
    } else if (!outcome.note.empty()) {
        sink->Log(outcome.note);
    }

    if (outcome.ok) {
        // An incoming line that came back exactly as it went is not a
        // translation, and printing it under the original claims otherwise:
        //
        //     <Kaje> Mangerò tra poco.
        //     *** Kaje: Mangerò tra poco.
        //
        // It looks like the plugin did something. The notice above says what
        // actually happened, which is the honest half of this.
        if (job.incoming && outcome.sourceLanguageSuspect) return;

        sink->Deliver(job, outcome.text, true);
        return;
    }

    // Section 5: a failed translation never eats the message. The original goes
    // out, and the reason is logged. A spent quota is also said out loud in the
    // chat window, because silently reverting to untranslated Swedish is
    // exactly the confusing behaviour section 7.6 rules out.
    if (outcome.quotaExceeded) {
        sink->Notice(job, "Babelfish: quota spent for today - sending the original. " +
                              outcome.error);
    }
    sink->Log("translation failed (" + outcome.error + "), sent original: " + job.text);
    sink->Deliver(job, job.text, false);
}

}  // namespace Queue
