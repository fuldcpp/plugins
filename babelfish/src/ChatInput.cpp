// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "ChatInput.h"

#include "Marshal.h"
#include "Nicks.h"

#include <commctrl.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace ChatInput {
namespace {

constexpr UINT_PTR kSubclassId = 0x54524E53;  // "TRNS"
constexpr UINT_PTR kLogSubclassId = 0x544C4F47;  // "TLOG"

// The trigger. Not Ctrl+T: FulDC++ has that in its own accelerator table, and
// an accelerator is translated in the message loop before the key reaches the
// control, so this procedure would never run. See the header.
constexpr WPARAM kTriggerKey = 'G';

HINSTANCE g_instance = nullptr;
HHOOK g_hook = nullptr;
IncomingHandler g_incoming = nullptr;

// Section 7.1. Set by the trigger, claimed by the outgoing-chat hook.
//
// Two measurements shaped this. First, it was thread-local, on the reasoning
// that the key press and the send both happen on the GUI thread: they do not,
// and the hook read the flag as unset. Then it became one process-wide
// timestamp with a two-second expiry, and the hook found it 3609 ms old --
// this client sits on an outgoing line for seconds, almost certainly its own
// anti-flood delay.
//
// So timing is the wrong mechanism altogether: the delay varies with how
// recently you last spoke, and any threshold is either too tight to work or
// loose enough to colour a message sent much later by hand. The request is
// matched on the text instead. What was in the message box when Ctrl+G was
// pressed is what the client is about to send, so the outgoing hook can
// recognise its own message exactly, however long the client held on to it.
// The timeout that remains is only a backstop for a request that never went
// anywhere at all.
constexpr ULONGLONG kTranslateRequestTimeoutMs = 60000;
std::mutex g_pendingMutex;
std::string g_pendingText;
ULONGLONG g_pendingAt = 0;

// The control whose Return is waiting for Ctrl to come back up.
//
// Posting the Return straight away does not work: Ctrl is still physically down
// at the moment the client processes it, so the client sees Ctrl+Return, which
// is its line-break combination -- section 7.1's own rule, arriving from the
// wrong direction. It inserted a blank line and sent nothing, and the message
// only went out when the user pressed Return themselves afterwards.
//
// So the Return waits for the key that triggered it to be released. If that
// release never arrives -- focus moves away, say -- nothing is posted and the
// request simply stays pending, which degrades to exactly the behaviour above:
// the next Return the user presses sends the message, translated.
HWND g_awaitSendHwnd = nullptr;

// Line endings and surrounding space differ between what the edit control holds
// and what reaches the hook, and neither difference makes it another message.
std::string Normalise(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c != '\r') out += c;
    }

    auto space = [](char c) { return c == ' ' || c == '\n' || c == '\t'; };
    size_t begin = 0;
    size_t end = out.size();
    while (begin < end && space(out[begin])) ++begin;
    while (end > begin && space(out[end - 1])) --end;
    return out.substr(begin, end - begin);
}

std::string Utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), n,
                          nullptr, nullptr);
    return out;
}

std::string TextOf(HWND hwnd) {
    const int len = ::GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};

    std::wstring wide(static_cast<size_t>(len) + 1, L'\0');
    const int got = ::GetWindowTextW(hwnd, wide.data(), len + 1);
    wide.resize(got < 0 ? 0 : static_cast<size_t>(got));
    if (wide.empty()) return {};

    return Utf8(wide);
}

// Counted so "/tr probe" can say whether the key reached the subclass at all.
// Without this, "Ctrl+G does nothing" cannot be told apart from "the key never
// arrived", and that ambiguity has already cost one round of guessing.
std::atomic<int> g_keySeen{0};      // any G, auto-repeat excluded
std::atomic<int> g_triggerFired{0}; // Ctrl+G accepted and Return posted
std::atomic<int> g_claimed{0};      // outgoing messages that claimed the flag

// The message box was empty when Ctrl+G arrived, so the key was let through.
//
// This is the whole of "Ctrl+G does nothing on somebody else's line": the click
// in the chat window does not necessarily move keyboard focus out of the
// message box, so the key reaches this subclass instead of the log's, finds
// nothing typed, and steps aside without a word.
std::atomic<int> g_emptyBox{0};

// The other half, which "/tr probe" could not see at all until now: the chat
// log has its own subclass, its own key handling and its own way of failing,
// and none of it was counted. Two days of this plugin's history say that an
// uninstrumented path is one you will guess wrong about.
std::atomic<int> g_logKeySeen{0};  // Ctrl+G accepted by the chat log's subclass
std::atomic<int> g_logLineRead{0}; // and a non-empty line came back from it

// What the claim actually saw, so "the hook ran but found nothing" can be told
// apart from "the hook never asked".
std::atomic<int> g_consumeCalls{0};
std::atomic<unsigned long long> g_lastSeenAt{0};
std::atomic<long long> g_lastAge{-1};

std::wstring ClassOf(HWND hwnd) {
    wchar_t buf[64] = {};
    ::GetClassNameW(hwnd, buf, static_cast<int>(std::size(buf)));
    return buf;
}

// The message box in every FulDC++ hub and PM frame is a plain multiline Edit.
// The user-list filter and the chat find bar are Edits too but not multiline,
// and the chat output pane is read-only, which is what separates them. The
// RichEdit classes are accepted as well so a build that uses one still gets
// the trigger; the read-only test keeps us off its chat log either way.
bool LooksLikeChatControl(HWND hwnd) {
    const std::wstring cls = ClassOf(hwnd);
    if (cls != L"Edit" && cls != L"RichEdit20W" && cls != L"RICHEDIT50W") return false;
    return (::GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE) != 0;
}

bool LooksLikeChatInput(HWND hwnd) {
    if (!LooksLikeChatControl(hwnd)) return false;
    return (::GetWindowLongW(hwnd, GWL_STYLE) & ES_READONLY) == 0;
}

// The chat log is the same control with ES_READONLY: same class, same frame,
// and the only other multiline edit a hub window has.
bool LooksLikeChatLog(HWND hwnd) {
    if (!LooksLikeChatControl(hwnd)) return false;
    return (::GetWindowLongW(hwnd, GWL_STYLE) & ES_READONLY) != 0;
}

// The whole line the caret sits on, or the lines a selection spans.
//
// Whole lines rather than the exact selection: half a sentence translates
// badly, and it means clicking anywhere in a line is enough. Fetching only the
// lines involved also avoids pulling an entire evening of chat out of the
// control to read one row of it.
std::string LineAtCaret(HWND hwnd) {
    DWORD from = 0;
    DWORD to = 0;
    ::SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&from),
                   reinterpret_cast<LPARAM>(&to));

    const int firstLine =
        static_cast<int>(::SendMessageW(hwnd, EM_LINEFROMCHAR, static_cast<WPARAM>(from), 0));
    const int lastLine =
        static_cast<int>(::SendMessageW(hwnd, EM_LINEFROMCHAR, static_cast<WPARAM>(to), 0));
    if (firstLine < 0) return {};

    std::string out;
    for (int line = firstLine; line <= lastLine && line - firstLine < 20; ++line) {
        const int length = static_cast<int>(::SendMessageW(hwnd, EM_LINELENGTH,
            static_cast<WPARAM>(::SendMessageW(hwnd, EM_LINEINDEX, static_cast<WPARAM>(line), 0)),
            0));
        if (length <= 0) continue;

        // EM_GETLINE wants the buffer size in its first word, and overwrites it.
        std::vector<wchar_t> buffer(static_cast<size_t>(length) + 2, L'\0');
        *reinterpret_cast<WORD*>(buffer.data()) = static_cast<WORD>(buffer.size());
        const int got = static_cast<int>(::SendMessageW(hwnd, EM_GETLINE,
                                                        static_cast<WPARAM>(line),
                                                        reinterpret_cast<LPARAM>(buffer.data())));
        if (got <= 0) continue;

        if (!out.empty()) out += ' ';
        out += Utf8(std::wstring(buffer.data(), static_cast<size_t>(got)));
    }
    return out;
}

LRESULT CALLBACK LogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                         DWORD_PTR) {
    try {
        if (msg == WM_NCDESTROY) {
            ::RemoveWindowSubclass(hwnd, LogProc, id);
            return ::DefSubclassProc(hwnd, msg, wParam, lParam);
        }

        if (msg == WM_KEYDOWN && wParam == kTriggerKey && !(lParam & 0x40000000)) {
            const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
            if (ctrl && !shift && !alt && g_incoming) {
                ++g_logKeySeen;
                Marshal::EnsureWindow();
                const std::string line = LineAtCaret(hwnd);
                if (!line.empty()) {
                    ++g_logLineRead;
                    g_incoming(line);
                }
                return 0;
            }
        }
    } catch (...) {
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

void MaybeAttachLog(HWND hwnd) {
    if (!hwnd || !::IsWindow(hwnd)) return;

    DWORD_PTR existing = 0;
    if (::GetWindowSubclass(hwnd, LogProc, kLogSubclassId, &existing)) return;
    if (!LooksLikeChatLog(hwnd)) return;

    ::SetWindowSubclass(hwnd, LogProc, kLogSubclassId, 0);
}

// Hands the host the key it listens for.
//
// This posts rather than calling DefSubclassProc directly. The direct call goes
// straight into the control and skips the message loop -- and a WTL client like
// this one commonly handles Return in PreTranslateMessage, which lives in that
// loop. Ctrl+G did nothing at all for exactly that reason: the flag was set, the
// key went to the edit control, and the client's send logic never ran.
//
// Posting covers both designs: the message goes through PreTranslateMessage
// first and reaches the control's procedure afterwards if nobody claimed it.
void TriggerSend(HWND hwnd) {
    // Repeat count 1, scan code 0x1C for Return.
    constexpr LPARAM kEnterLParam = 0x001C0001;
    ::PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, kEnterLParam);
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                          DWORD_PTR) {
    // See the note on CallWndProc: nothing may be thrown back into user32.
    try {
        switch (msg) {
            case WM_NCDESTROY:
                if (g_awaitSendHwnd == hwnd) g_awaitSendHwnd = nullptr;
                ::RemoveWindowSubclass(hwnd, EditProc, id);
                return ::DefSubclassProc(hwnd, msg, wParam, lParam);

            case WM_KEYUP:
                // Ctrl is up, so a posted Return now reads as a plain Return.
                if (wParam == VK_CONTROL && g_awaitSendHwnd == hwnd) {
                    g_awaitSendHwnd = nullptr;
                    TriggerSend(hwnd);
                }
                break;

            case WM_KEYDOWN: {
                if (wParam != kTriggerKey) break;

                // Bit 30 is the previous key state: set means auto-repeat from
                // a held key. Acting on those would send the same line several
                // times over, which "triggered=2 from one press" showed.
                if (lParam & 0x40000000) break;
                ++g_keySeen;

                const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
                if (!ctrl || shift || alt) break;

                // This is the GUI thread by construction, so it is the right
                // place to make sure the window the worker posts results to
                // exists.
                Marshal::EnsureWindow();
                Nicks::HarvestFrom(hwnd);

                // A multi-line message built with Shift+Enter is translated as
                // one unit: the text is still sitting in the control and goes
                // to the host in one piece, so the backend sees the line breaks
                // and the whole thing costs one request.
                // Recorded before the send is triggered: this is exactly the
                // text the client is about to put on the wire.
                {
                    std::lock_guard<std::mutex> lock(g_pendingMutex);
                    g_pendingText = Normalise(TextOf(hwnd));
                    g_pendingAt = ::GetTickCount64();
                }
                if (g_pendingText.empty()) {
                    // Nothing typed. Let the key through, but count it: this is
                    // the shape "Ctrl+G is broken" takes when the user meant a
                    // line in the chat window above.
                    ++g_emptyBox;
                    break;
                }

                ++g_triggerFired;
                if (::GetKeyState(VK_CONTROL) & 0x8000) {
                    g_awaitSendHwnd = hwnd;
                } else {
                    TriggerSend(hwnd);
                }
                return 0;
            }

            default:
                break;
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        g_pendingText.clear();
        g_pendingAt = 0;
    }

    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

void MaybeAttach(HWND hwnd) {
    if (!hwnd || !::IsWindow(hwnd)) return;

    DWORD_PTR existing = 0;
    if (::GetWindowSubclass(hwnd, EditProc, kSubclassId, &existing)) return;
    if (!LooksLikeChatInput(hwnd)) return;

    ::SetWindowSubclass(hwnd, EditProc, kSubclassId, 0);
}

// A message hook is called by user32 from inside SendMessage, on frames that
// belong to the host and to Windows. An exception thrown here does not unwind
// to a handler, it ends the process -- so this procedure, like every other
// callback the system makes into the plugin, swallows everything.
LRESULT CALLBACK CallWndProc(int code, WPARAM wParam, LPARAM lParam) {
    try {
        if (code == HC_ACTION) {
            // This procedure runs on the host's GUI thread by construction,
            // which makes it the one place guaranteed to be a valid home for
            // the marshalling window.
            Marshal::EnsureWindow();

            const CWPSTRUCT* cwp = reinterpret_cast<const CWPSTRUCT*>(lParam);
            // WM_SETFOCUS is the first moment the control is guaranteed to be
            // fully built and parented, which WM_CREATE is not.
            if (cwp && (cwp->message == WM_SETFOCUS || cwp->message == WM_MOUSEACTIVATE)) {
                MaybeAttach(cwp->hwnd);
                MaybeAttachLog(cwp->hwnd);

                // Clicking into a message box is the natural moment to read the
                // user list: it is the GUI thread, and it happens just before
                // the user types the names of the people they are talking to.
                // Nicks throttles itself, so this is cheap.
                Nicks::HarvestFrom(cwp->hwnd);
            }
        }
    } catch (...) {
    }
    return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

// Finds the thread that owns the host's windows. The plugin may be loaded
// before the UI exists, so this can legitimately fail and be retried later.
DWORD FindGuiThread() {
    struct Search {
        DWORD pid;
        DWORD thread;
    } search{::GetCurrentProcessId(), 0};

    ::EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* s = reinterpret_cast<Search*>(param);
            DWORD pid = 0;
            const DWORD thread = ::GetWindowThreadProcessId(hwnd, &pid);
            if (pid == s->pid && ::IsWindowVisible(hwnd) && ::GetWindow(hwnd, GW_OWNER) == nullptr) {
                s->thread = thread;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));

    return search.thread;
}

void ForEachChild(DWORD threadId, void (*action)(HWND)) {
    struct Context {
        void (*action)(HWND);
    } context{action};

    ::EnumThreadWindows(
        threadId,
        [](HWND top, LPARAM param) -> BOOL {
            ::EnumChildWindows(
                top,
                [](HWND child, LPARAM inner) -> BOOL {
                    reinterpret_cast<Context*>(inner)->action(child);
                    return TRUE;
                },
                param);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
}

void Detach(HWND hwnd) {
    DWORD_PTR unused = 0;
    if (::GetWindowSubclass(hwnd, EditProc, kSubclassId, &unused)) {
        ::RemoveWindowSubclass(hwnd, EditProc, kSubclassId);
    }
    if (::GetWindowSubclass(hwnd, LogProc, kLogSubclassId, &unused)) {
        ::RemoveWindowSubclass(hwnd, LogProc, kLogSubclassId);
    }
}

}  // namespace

void SetModule(HINSTANCE instance) {
    g_instance = instance;
}

void SetIncomingHandler(IncomingHandler handler) {
    g_incoming = handler;
}

bool EnsureInstalled() {
    if (g_hook) return true;

    const DWORD thread = FindGuiThread();
    if (thread == 0) return false;

    g_hook = ::SetWindowsHookExW(WH_CALLWNDPROC, CallWndProc, g_instance, thread);
    if (!g_hook) return false;

    Marshal::SetGuiThread(thread);

    // Pick up controls that already exist, so the trigger works without waiting
    // for the user to click around first.
    ForEachChild(thread, MaybeAttach);
    ForEachChild(thread, MaybeAttachLog);
    return true;
}

void Uninstall() {
    if (g_hook) {
        ::UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }

    const DWORD thread = FindGuiThread();
    if (thread == 0) return;
    ForEachChild(thread, Detach);
}

bool ConsumeTranslateRequest(const std::string& outgoing, bool* wasAction) {
    if (wasAction) *wasAction = false;
    ++g_consumeCalls;

    std::lock_guard<std::mutex> lock(g_pendingMutex);
    g_lastSeenAt.store(g_pendingAt, std::memory_order_relaxed);
    if (g_pendingAt == 0) {
        g_lastAge.store(-1, std::memory_order_relaxed);
        return false;
    }

    const long long age = static_cast<long long>(::GetTickCount64() - g_pendingAt);
    g_lastAge.store(age, std::memory_order_relaxed);

    if (age > static_cast<long long>(kTranslateRequestTimeoutMs)) {
        g_pendingText.clear();
        g_pendingAt = 0;
        return false;
    }

    // Only the message this request was made for may claim it. Anything else
    // the user sends in the meantime goes out exactly as written, and the
    // request stays pending for its own message.
    const std::string incoming = Normalise(outgoing);
    bool action = false;

    if (incoming != g_pendingText) {
        // "/me dricker kaffe" arrives at the hook as "dricker kaffe": the
        // client strips the prefix on its way past and carries the third
        // person separately. Measured -- the claim failed with a request that
        // was pending and only 235 ms old. So the stripped form counts as a
        // match, and the caller is told to put the prefix back.
        const std::string kMe = "/me ";
        if (g_pendingText.compare(0, kMe.size(), kMe) != 0) return false;
        if (Normalise(g_pendingText.substr(kMe.size())) != incoming) return false;
        action = true;
    }

    g_pendingText.clear();
    g_pendingAt = 0;
    ++g_claimed;
    if (wasAction) *wasAction = action;
    return true;
}

std::string Diagnostics() {
    const DWORD gui = FindGuiThread();

    struct Tally {
        int candidates = 0;
        int attached = 0;
        int logs = 0;
        int logsAttached = 0;
        std::string classes;
    } tally;

    if (gui != 0) {
        // Same walk MaybeAttach uses, but reporting instead of subclassing, so
        // "/tr probe" answers whether the message boxes were found at all.
        ::EnumThreadWindows(
            gui,
            [](HWND top, LPARAM param) -> BOOL {
                ::EnumChildWindows(
                    top,
                    [](HWND child, LPARAM inner) -> BOOL {
                        auto* t = reinterpret_cast<Tally*>(inner);
                        DWORD_PTR unused = 0;

                        // The read-only half. Reported separately because it is
                        // a different control with a different subclass, and
                        // asking about one told us nothing about the other.
                        if (LooksLikeChatLog(child)) {
                            ++t->logs;
                            if (::GetWindowSubclass(child, LogProc, kLogSubclassId, &unused)) {
                                ++t->logsAttached;
                            }
                            return TRUE;
                        }

                        if (!LooksLikeChatInput(child)) return TRUE;
                        ++t->candidates;

                        if (::GetWindowSubclass(child, EditProc, kSubclassId, &unused)) {
                            ++t->attached;
                        }
                        const std::wstring cls = ClassOf(child);
                        std::string narrow;
                        for (wchar_t c : cls) narrow += static_cast<char>(c);
                        if (t->classes.find(narrow) == std::string::npos) {
                            if (!t->classes.empty()) t->classes += ",";
                            t->classes += narrow;
                        }
                        return TRUE;
                    },
                    param);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&tally));
    }

    char buffer[480] = {};
    snprintf(buffer, sizeof(buffer),
             "hook=%s guiThread=%lu inputs=%d subclassed=%d logs=%d logsSubclassed=%d "
             "classes=%s keySeen=%d triggered=%d emptyBox=%d logKeys=%d logLines=%d "
             "claimed=%d consume=%d lastAt=%llu lastAge=%lld",
             g_hook ? "yes" : "NO", static_cast<unsigned long>(gui), tally.candidates,
             tally.attached, tally.logs, tally.logsAttached,
             tally.classes.empty() ? "(none)" : tally.classes.c_str(), g_keySeen.load(),
             g_triggerFired.load(), g_emptyBox.load(), g_logKeySeen.load(),
             g_logLineRead.load(), g_claimed.load(), g_consumeCalls.load(),
             g_lastSeenAt.load(), g_lastAge.load());
    return buffer;
}

}  // namespace ChatInput
