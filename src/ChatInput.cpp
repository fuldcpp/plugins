// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "ChatInput.h"

#include "AutoCorrect.h"
#include "Nicks.h"
#include "Settings.h"
#include "Speller.h"
#include "Strings.h"
#include "Tokenizer.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace ChatInput {
namespace {

constexpr UINT_PTR kSubclassId = 0x57A7;   // "STAV"

// Menu command ids. TrackPopupMenu runs with TPM_RETURNCMD so these never reach
// the host's message loop.
constexpr UINT kCmdSuggestionBase = 0x7100;
constexpr UINT kCmdAddToDictionary = 0x7000;
constexpr UINT kCmdIgnore = 0x7001;

// Wave geometry. Colour and thickness come from the settings dialog; a 1px line
// is too faint against FulDC++'s dark chat theme, so the default is thicker.
constexpr int kSquiggleAmplitude = 2;
constexpr int kSquiggleStep = 2;  // half a period

HINSTANCE g_instance = nullptr;
Speller* g_speller = nullptr;
HHOOK g_hook = nullptr;

// The most recent automatic replacement, kept so Ctrl+Z can put the original
// word back. The edit control's own undo buffer is only one level deep and the
// boundary character typed after the correction already occupies it, so undo has
// to be handled here or the correction cannot be taken back at all.
struct Correction {
    bool valid = false;
    int start = 0;
    std::wstring original;
    std::wstring replacement;
};

// Per-control state, owned by the subclass and freed on WM_NCDESTROY.
struct Context {
    std::wstring checkedText;
    std::vector<Range> bad;
    Correction lastCorrection;
};

std::wstring ClassOf(HWND hwnd) {
    wchar_t buf[64] = {};
    ::GetClassNameW(hwnd, buf, static_cast<int>(std::size(buf)));
    return buf;
}

std::wstring TextOf(HWND hwnd) {
    const int len = ::GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    const int got = ::GetWindowTextW(hwnd, s.data(), len + 1);
    s.resize(got < 0 ? 0 : static_cast<size_t>(got));
    return s;
}

// ---------------------------------------------------------------------------
// Checking
// ---------------------------------------------------------------------------

int CaretPos(HWND hwnd) {
    const DWORD sel = static_cast<DWORD>(::SendMessageW(hwnd, EM_GETSEL, 0, 0));
    return static_cast<int>(HIWORD(sel));
}

// caretPos suppresses the word currently being typed: a squiggle that appears
// under a half-finished word and vanishes again on the next keystroke is pure
// noise. Word behaves the same way. Clicking into the middle of a flagged word
// still shows it, because the caret is then not at the word's end.
// Reopens the spell checkers when there are none, from the thread that actually
// uses them.
//
// They are COM objects, and the plugin is loaded before the UI exists, on a
// thread the host picks for startup. When that goes wrong the checkers are never
// created and nothing gets underlined at all -- which is exactly what a client
// restart could produce, and why toggling a language in the settings repaired
// it: that reopened them from the GUI thread.
//
// Doing it here makes recovery depend only on the user typing, not on the host's
// timer hook firing. Throttled so a machine with no dictionaries at all does not
// retry COM on every keystroke.
void EnsureSpellerReady() {
    if (!g_speller) return;
    if (g_speller->Ready() && g_speller->MissingLanguages().empty()) return;

    static ULONGLONG lastAttempt = 0;
    const ULONGLONG now = ::GetTickCount64();
    if (lastAttempt != 0 && now - lastAttempt < 5000) return;
    lastAttempt = now;

    g_speller->RefreshLanguages();
}

std::vector<Range> FindMisspellings(const std::wstring& text, int caretPos) {
    std::vector<Range> bad;

    EnsureSpellerReady();
    if (!g_speller || !g_speller->Ready()) return bad;

    for (const Range& r : TokenizeForSpelling(text)) {
        if (r.end() == caretPos) continue;

        const std::wstring word = Slice(text, r);
        if (Nicks::Contains(word)) continue;
        if (!g_speller->IsCorrect(word)) bad.push_back(r);
    }
    return bad;
}

// Re-checks the control. Returns true when the set of misspellings moved, which
// means old squiggles must be erased rather than simply redrawn.
//
// This runs synchronously on every keystroke. Checking one chat line costs a
// tokenizer pass plus a handful of dictionary lookups that are almost always
// cache hits, and doing it inline is what keeps the squiggles pinned to the
// right characters as the text shifts around them.
bool Recheck(HWND hwnd, Context& ctx) {
    std::wstring text = TextOf(hwnd);
    std::vector<Range> bad = FindMisspellings(text, CaretPos(hwnd));

    const bool changed = (bad != ctx.bad);
    ctx.checkedText = std::move(text);
    ctx.bad = std::move(bad);
    return changed;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void DrawWave(HDC hdc, int x1, int x2, int y) {
    if (x2 <= x1) return;

    ::MoveToEx(hdc, x1, y, nullptr);
    bool up = true;
    for (int x = x1; x < x2; x += kSquiggleStep) {
        const int nx = std::min(x + kSquiggleStep, x2);
        ::LineTo(hdc, nx, up ? y - kSquiggleAmplitude : y);
        up = !up;
    }
}

int LineHeightOf(HWND hwnd, HDC hdc) {
    HFONT font = reinterpret_cast<HFONT>(::SendMessageW(hwnd, WM_GETFONT, 0, 0));
    HGDIOBJ old = font ? ::SelectObject(hdc, font) : nullptr;
    TEXTMETRICW tm = {};
    ::GetTextMetricsW(hdc, &tm);
    if (old) ::SelectObject(hdc, old);
    return tm.tmHeight;
}

// Horizontal extent of [from, to) on a single visual line. EM_POSFROMCHAR
// reports -1 for the position just past the last character, so fall back to
// measuring the text when that happens.
bool SegmentExtent(HWND hwnd, HDC hdc, const std::wstring& text, int from, int to, int& x1, int& x2, int& y) {
    const LRESULT p1 = ::SendMessageW(hwnd, EM_POSFROMCHAR, static_cast<WPARAM>(from), 0);
    if (p1 == -1) return false;

    x1 = static_cast<short>(LOWORD(p1));
    y = static_cast<short>(HIWORD(p1));

    const LRESULT p2 = ::SendMessageW(hwnd, EM_POSFROMCHAR, static_cast<WPARAM>(to), 0);
    if (p2 != -1 && static_cast<short>(HIWORD(p2)) == y) {
        x2 = static_cast<short>(LOWORD(p2));
        return x2 > x1;
    }

    HFONT font = reinterpret_cast<HFONT>(::SendMessageW(hwnd, WM_GETFONT, 0, 0));
    HGDIOBJ old = font ? ::SelectObject(hdc, font) : nullptr;
    SIZE sz = {};
    const int count = to - from;
    if (count > 0 && from + count <= static_cast<int>(text.size())) {
        ::GetTextExtentPoint32W(hdc, text.c_str() + from, count, &sz);
    }
    if (old) ::SelectObject(hdc, old);

    x2 = x1 + sz.cx;
    return x2 > x1;
}

void DrawSquiggles(HWND hwnd, HDC hdc, const Context& ctx) {
    if (ctx.bad.empty()) return;

    RECT format = {};
    ::SendMessageW(hwnd, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
    if (::IsRectEmpty(&format)) ::GetClientRect(hwnd, &format);

    const int saved = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, format.left, format.top, format.right, format.bottom);

    const int lineHeight = LineHeightOf(hwnd, hdc);
    const Settings& settings = CurrentSettings();
    HPEN pen = ::CreatePen(PS_SOLID, settings.thickness, settings.colour);
    HGDIOBJ oldPen = ::SelectObject(hdc, pen);

    const std::wstring& text = ctx.checkedText;
    const int textLen = static_cast<int>(text.size());

    for (const Range& r : ctx.bad) {
        if (r.start < 0 || r.end() > textLen) continue;

        // A wrapped word straddles two visual lines, so underline it per line.
        const int firstLine = static_cast<int>(::SendMessageW(hwnd, EM_LINEFROMCHAR, static_cast<WPARAM>(r.start), 0));
        const int lastLine = static_cast<int>(::SendMessageW(hwnd, EM_LINEFROMCHAR, static_cast<WPARAM>(r.end() - 1), 0));

        for (int line = firstLine; line <= lastLine; ++line) {
            const int lineStart = static_cast<int>(::SendMessageW(hwnd, EM_LINEINDEX, static_cast<WPARAM>(line), 0));
            if (lineStart < 0) continue;
            const int lineLen = static_cast<int>(::SendMessageW(hwnd, EM_LINELENGTH, static_cast<WPARAM>(lineStart), 0));

            const int from = std::max(r.start, lineStart);
            const int to = std::min(r.end(), lineStart + lineLen);
            if (to <= from) continue;

            int x1 = 0, x2 = 0, y = 0;
            if (!SegmentExtent(hwnd, hdc, text, from, to, x1, x2, y)) continue;

            // Sits just above the bottom of the line box, leaving room for the
            // wave's amplitude and pen width without clipping.
            const int baseline = y + lineHeight - 2;
            if (baseline < format.top || baseline > format.bottom) continue;

            DrawWave(hdc, x1, std::min(x2, static_cast<int>(format.right)), baseline);
        }
    }

    ::SelectObject(hdc, oldPen);
    ::DeleteObject(pen);
    ::RestoreDC(hdc, saved);
}

// Edit controls repaint the edited line directly from WM_CHAR instead of going
// through WM_PAINT, which wipes whatever we drew on top. Redrawing immediately
// after the control has updated is what keeps a squiggle visible while the user
// carries on typing other words.
void DrawNow(HWND hwnd, const Context& ctx) {
    if (ctx.bad.empty()) return;
    if (HDC hdc = ::GetDC(hwnd)) {
        DrawSquiggles(hwnd, hdc, ctx);
        ::ReleaseDC(hwnd, hdc);
    }
}

// Recheck plus the right kind of repaint: erase when the misspellings moved,
// otherwise just paint over what the control blanked.
void RefreshAfterEdit(HWND hwnd, Context& ctx) {
    if (Recheck(hwnd, ctx)) {
        ::InvalidateRect(hwnd, nullptr, TRUE);
    } else {
        DrawNow(hwnd, ctx);
    }
}

// ---------------------------------------------------------------------------
// Autocorrect
// ---------------------------------------------------------------------------

// Characters that end a word. Autocorrect fires on these, before the character
// itself is inserted, so the caret is sitting exactly at the end of the word the
// user just finished.
bool EndsWord(wchar_t c) {
    if (std::iswspace(static_cast<wint_t>(c))) return true;
    return wcschr(L".,!?;:)]}\"'", c) != nullptr;
}

void ApplyAutoCorrect(HWND hwnd, Context& ctx) {
    if (!CurrentSettings().autoCorrect) return;

    // Never touch the text while something is selected: the replacement would
    // silently eat the selection.
    const DWORD sel = static_cast<DWORD>(::SendMessageW(hwnd, EM_GETSEL, 0, 0));
    if (LOWORD(sel) != HIWORD(sel)) return;

    const std::wstring text = TextOf(hwnd);
    const int caret = static_cast<int>(HIWORD(sel));
    if (caret <= 0 || caret > static_cast<int>(text.size())) return;

    int start = caret;
    while (start > 0 && std::iswalpha(static_cast<wint_t>(text[start - 1]))) --start;
    if (start == caret) return;

    const std::wstring word = text.substr(static_cast<size_t>(start),
                                          static_cast<size_t>(caret - start));
    std::wstring replacement;
    if (!AutoCorrect::Lookup(word, replacement)) return;

    ::SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(start), static_cast<LPARAM>(caret));
    ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replacement.c_str()));

    ctx.lastCorrection = Correction{true, start, word, replacement};
}

// Puts back the word autocorrect replaced, but only while the replacement is
// still sitting untouched where it was written.
bool UndoAutoCorrect(HWND hwnd, Context& ctx) {
    Correction& last = ctx.lastCorrection;
    if (!last.valid) return false;

    const std::wstring text = TextOf(hwnd);
    const int end = last.start + static_cast<int>(last.replacement.size());
    if (last.start < 0 || end > static_cast<int>(text.size())) return false;
    if (text.compare(static_cast<size_t>(last.start), last.replacement.size(), last.replacement) != 0) {
        return false;
    }

    ::SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(last.start), static_cast<LPARAM>(end));
    ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(last.original.c_str()));

    // Undoing a correction means "I meant that word", so stop correcting it for
    // the rest of the session. Otherwise the next space would replace it again.
    AutoCorrect::Suppress(last.original);
    last.valid = false;
    return true;
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

const Range* HitTest(HWND hwnd, const Context& ctx, POINT screenPt, POINT& clientPt) {
    clientPt = screenPt;
    ::ScreenToClient(hwnd, &clientPt);

    const LRESULT hit = ::SendMessageW(hwnd, EM_CHARFROMPOS, 0,
                                       MAKELPARAM(clientPt.x, clientPt.y));
    if (hit == -1) return nullptr;

    const int index = static_cast<int>(LOWORD(hit));
    for (const Range& r : ctx.bad) {
        if (r.contains(index)) return &r;
    }
    return nullptr;
}

void ReplaceRange(HWND hwnd, const Range& r, const std::wstring& replacement) {
    ::SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(r.start), static_cast<LPARAM>(r.end()));
    // wParam TRUE keeps the edit's undo stack intact, so Ctrl+Z still works.
    ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replacement.c_str()));
}

bool ShowSuggestions(HWND hwnd, Context& ctx, POINT screenPt) {
    POINT clientPt = {};
    const Range* hitPtr = HitTest(hwnd, ctx, screenPt, clientPt);
    if (!hitPtr || !g_speller) return false;

    const Range hit = *hitPtr;  // copy: the menu loop may re-enter and rebuild ctx.bad
    const std::wstring word = Slice(ctx.checkedText, hit);
    if (word.empty()) return false;

    const std::vector<std::wstring> suggestions = g_speller->Suggest(word);

    HMENU menu = ::CreatePopupMenu();
    if (!menu) return false;

    if (suggestions.empty()) {
        ::AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, Strings::T(Strings::Str::MenuNoSuggestions));
    } else {
        for (size_t i = 0; i < suggestions.size(); ++i) {
            ::AppendMenuW(menu, MF_STRING, kCmdSuggestionBase + i, suggestions[i].c_str());
        }
    }

    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kCmdAddToDictionary,
                  Strings::T(Strings::Str::MenuAddToDictionary));
    ::AppendMenuW(menu, MF_STRING, kCmdIgnore, Strings::T(Strings::Str::MenuIgnoreSession));

    const UINT choice = static_cast<UINT>(::TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        screenPt.x, screenPt.y, 0, hwnd, nullptr));
    ::DestroyMenu(menu);

    if (choice == 0) return true;  // dismissed, but we still handled the click

    if (choice == kCmdAddToDictionary) {
        g_speller->AddToPersonal(word);
    } else if (choice == kCmdIgnore) {
        g_speller->IgnoreForSession(word);
    } else if (choice >= kCmdSuggestionBase) {
        const size_t index = choice - kCmdSuggestionBase;
        if (index < suggestions.size()) ReplaceRange(hwnd, hit, suggestions[index]);
    }

    Recheck(hwnd, ctx);
    ::InvalidateRect(hwnd, nullptr, TRUE);
    return true;
}

// ---------------------------------------------------------------------------
// Subclass
// ---------------------------------------------------------------------------

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                          UINT_PTR id, DWORD_PTR ref) {
    Context* ctx = reinterpret_cast<Context*>(ref);

    switch (msg) {
        case WM_NCDESTROY: {
            ::RemoveWindowSubclass(hwnd, EditProc, id);
            delete ctx;
            return ::DefSubclassProc(hwnd, msg, wParam, lParam);
        }

        case WM_PAINT: {
            const LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            if (ctx && !ctx->bad.empty()) {
                if (HDC hdc = ::GetDC(hwnd)) {
                    DrawSquiggles(hwnd, hdc, *ctx);
                    ::ReleaseDC(hwnd, hdc);
                }
            }
            return result;
        }

        // Clicking into the message box is the natural moment to refresh the
        // nick list: it is on the GUI thread, and it happens right before the
        // user types the names of the people they are talking to.
        case WM_SETFOCUS: {
            const LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            Nicks::HarvestFrom(hwnd);
            if (ctx) RefreshAfterEdit(hwnd, *ctx);
            return result;
        }

        case WM_CONTEXTMENU: {
            if (!ctx) break;
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pt.x == -1 && pt.y == -1) {
                // Keyboard-invoked: anchor on the caret.
                const DWORD sel = static_cast<DWORD>(::SendMessageW(hwnd, EM_GETSEL, 0, 0));
                const LRESULT p = ::SendMessageW(hwnd, EM_POSFROMCHAR, HIWORD(sel), 0);
                pt.x = (p == -1) ? 0 : static_cast<short>(LOWORD(p));
                pt.y = (p == -1) ? 0 : static_cast<short>(HIWORD(p));
                ::ClientToScreen(hwnd, &pt);
            }
            if (ShowSuggestions(hwnd, *ctx, pt)) return 0;
            break;  // not on a misspelling: let the host show its own menu
        }

        // Autocorrect has to run before the control sees the key, because the
        // host sends the chat message on Enter and the correction must already
        // be in the text by then.
        case WM_CHAR: {
            const wchar_t ch = static_cast<wchar_t>(wParam);
            const bool boundary = EndsWord(ch);
            if (ctx && boundary) ApplyAutoCorrect(hwnd, *ctx);

            const LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);

            // Typing on past the correction retires the chance to undo it.
            if (ctx && !boundary) ctx->lastCorrection.valid = false;
            if (ctx) RefreshAfterEdit(hwnd, *ctx);
            return result;
        }

        case WM_KEYDOWN: {
            if (ctx && wParam == 'Z' && (::GetKeyState(VK_CONTROL) & 0x8000)) {
                if (UndoAutoCorrect(hwnd, *ctx)) {
                    RefreshAfterEdit(hwnd, *ctx);
                    return 0;
                }
            }
            // Enter may be consumed here rather than reaching WM_CHAR.
            if (ctx && wParam == VK_RETURN) ApplyAutoCorrect(hwnd, *ctx);

            const LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            if (ctx) RefreshAfterEdit(hwnd, *ctx);
            return result;
        }

        // Anything else that can change the text or move the caret. WM_KEYUP is
        // in the list because Delete never produces a WM_CHAR.
        case WM_KEYUP:
        case WM_PASTE:
        case WM_CUT:
        case WM_CLEAR:
        case WM_UNDO:
        case WM_SETTEXT:
        case WM_LBUTTONUP:
        case EM_REPLACESEL: {
            const LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            if (ctx) RefreshAfterEdit(hwnd, *ctx);
            return result;
        }

        // Scrolling blits the existing pixels, so stale squiggles would smear
        // along with the text unless the whole control is repainted.
        case WM_VSCROLL:
        case WM_HSCROLL:
        case WM_MOUSEWHEEL:
        case WM_SIZE:
        case EM_LINESCROLL:
        case EM_SCROLL: {
            const LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            if (ctx && !ctx->bad.empty()) ::InvalidateRect(hwnd, nullptr, TRUE);
            return result;
        }

        default:
            break;
    }

    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

// The message box in every FulDC++ hub and PM frame is a plain multiline Edit.
// The hub's user-list filter and the chat find bar are Edits too, but neither
// carries ES_MULTILINE, which is what tells them apart.
bool LooksLikeChatInput(HWND hwnd) {
    if (ClassOf(hwnd) != L"Edit") return false;

    const LONG style = ::GetWindowLongW(hwnd, GWL_STYLE);
    if (!(style & ES_MULTILINE)) return false;
    if (style & ES_READONLY) return false;

    return true;
}

void MaybeAttach(HWND hwnd) {
    if (!hwnd || !::IsWindow(hwnd)) return;

    DWORD_PTR existing = 0;
    if (::GetWindowSubclass(hwnd, EditProc, kSubclassId, &existing)) return;  // already ours
    if (!LooksLikeChatInput(hwnd)) return;

    auto ctx = std::make_unique<Context>();
    if (::SetWindowSubclass(hwnd, EditProc, kSubclassId, reinterpret_cast<DWORD_PTR>(ctx.get()))) {
        Context* raw = ctx.release();
        Nicks::HarvestFrom(hwnd);
        Recheck(hwnd, *raw);
    }
}

LRESULT CALLBACK CallWndProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const CWPSTRUCT* cwp = reinterpret_cast<const CWPSTRUCT*>(lParam);
        // WM_SETFOCUS is the first moment the control is guaranteed to be fully
        // built and parented, which WM_CREATE is not.
        if (cwp && (cwp->message == WM_SETFOCUS || cwp->message == WM_MOUSEACTIVATE)) {
            MaybeAttach(cwp->hwnd);
        }
    }
    return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

// Finds the thread that owns the host's windows. The plugin may be loaded before
// the UI exists, so this can legitimately fail and be retried later.
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

// Walks the existing window tree once at install time, so controls that already
// have focus get picked up without waiting for the user to click around.
void AttachExisting(DWORD threadId) {
    ::EnumThreadWindows(
        threadId,
        [](HWND top, LPARAM) -> BOOL {
            ::EnumChildWindows(
                top,
                [](HWND child, LPARAM) -> BOOL {
                    MaybeAttach(child);
                    return TRUE;
                },
                0);
            return TRUE;
        },
        0);
}

}  // namespace

void SetModule(HINSTANCE instance) {
    g_instance = instance;
}

void SetSpeller(Speller* speller) {
    g_speller = speller;
}

bool EnsureInstalled() {
    if (g_hook) return true;

    const DWORD thread = FindGuiThread();
    if (thread == 0) return false;

    g_hook = ::SetWindowsHookExW(WH_CALLWNDPROC, CallWndProc, g_instance, thread);
    if (!g_hook) return false;

    AttachExisting(thread);
    return true;
}

void Uninstall() {
    if (g_hook) {
        ::UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }

    // Detach from whatever is still alive; each control frees its own context.
    const DWORD thread = FindGuiThread();
    if (thread == 0) return;

    ::EnumThreadWindows(
        thread,
        [](HWND top, LPARAM) -> BOOL {
            ::EnumChildWindows(
                top,
                [](HWND child, LPARAM) -> BOOL {
                    DWORD_PTR ref = 0;
                    if (::GetWindowSubclass(child, EditProc, kSubclassId, &ref)) {
                        ::RemoveWindowSubclass(child, EditProc, kSubclassId);
                        delete reinterpret_cast<Context*>(ref);
                        ::InvalidateRect(child, nullptr, TRUE);
                    }
                    return TRUE;
                },
                0);
            return TRUE;
        },
        0);
}

void InvalidateAll() {
    const DWORD thread = FindGuiThread();
    if (thread == 0) return;

    ::EnumThreadWindows(
        thread,
        [](HWND top, LPARAM) -> BOOL {
            ::EnumChildWindows(
                top,
                [](HWND child, LPARAM) -> BOOL {
                    DWORD_PTR ref = 0;
                    if (::GetWindowSubclass(child, EditProc, kSubclassId, &ref)) {
                        auto* ctx = reinterpret_cast<Context*>(ref);
                        Recheck(child, *ctx);
                        ::InvalidateRect(child, nullptr, TRUE);
                    }
                    return TRUE;
                },
                0);
            return TRUE;
        },
        0);
}

}  // namespace ChatInput
