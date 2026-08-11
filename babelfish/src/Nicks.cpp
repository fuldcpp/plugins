// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Nicks.h"

#include <commctrl.h>

#include <algorithm>
#include <mutex>
#include <set>

namespace Nicks {
namespace {

// A hub with more users than this is not worth stalling the GUI thread for.
constexpr int kMaxUsers = 20000;
constexpr ULONGLONG kMinIntervalMs = 10000;

// Below this a nick is more likely to collide with an ordinary word than to be
// worth protecting: masking every "jo" and "pa" in a sentence would do more
// damage than the occasional mistranslated name.
constexpr size_t kMinNickLength = 3;

std::mutex g_mutex;
std::set<std::string> g_nicks;
ULONGLONG g_lastHarvest = 0;

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), n,
                          nullptr, nullptr);
    return out;
}

std::wstring ClassOf(HWND hwnd) {
    wchar_t buf[64] = {};
    ::GetClassNameW(hwnd, buf, static_cast<int>(std::size(buf)));
    return buf;
}

// The user list is superclassed by the host, so its window class is an "ATL:..."
// name that changes between runs and cannot be matched on. What does hold is the
// structure: a list view owns a SysHeader32 child.
bool HasHeaderChild(HWND hwnd) {
    bool found = false;
    ::EnumChildWindows(
        hwnd,
        [](HWND child, LPARAM param) -> BOOL {
            if (ClassOf(child) == L"SysHeader32") {
                *reinterpret_cast<bool*>(param) = true;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

HWND FindUserList(HWND chatInput) {
    HWND frame = ::GetParent(chatInput);
    if (!frame) return nullptr;

    HWND result = nullptr;
    ::EnumChildWindows(
        frame,
        [](HWND child, LPARAM param) -> BOOL {
            if (!HasHeaderChild(child)) return TRUE;

            // Confirm it really answers list-view messages before trusting it.
            const LRESULT count = ::SendMessageW(child, LVM_GETITEMCOUNT, 0, 0);
            if (count < 0 || count > kMaxUsers) return TRUE;

            *reinterpret_cast<HWND*>(param) = child;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&result));

    return result;
}

}  // namespace

void HarvestFrom(HWND chatInput) {
    const ULONGLONG now = ::GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_lastHarvest != 0 && now - g_lastHarvest < kMinIntervalMs) return;
        g_lastHarvest = now;
    }

    HWND list = FindUserList(chatInput);
    if (!list) return;

    const int count = static_cast<int>(::SendMessageW(list, LVM_GETITEMCOUNT, 0, 0));
    if (count <= 0) return;

    // Column 0 is the nick in every DC++-derived user list.
    std::vector<wchar_t> buffer(128);
    std::vector<std::string> found;
    found.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        LVITEMW item = {};
        item.iSubItem = 0;
        item.pszText = buffer.data();
        item.cchTextMax = static_cast<int>(buffer.size());

        const LRESULT len = ::SendMessageW(list, LVM_GETITEMTEXTW, static_cast<WPARAM>(i),
                                           reinterpret_cast<LPARAM>(&item));
        if (len > 0) {
            const std::string nick = Narrow(std::wstring(buffer.data(), static_cast<size_t>(len)));
            if (nick.size() >= kMinNickLength) found.push_back(nick);
        }
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (const std::string& nick : found) g_nicks.insert(nick);
}

std::vector<std::string> All() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> out(g_nicks.begin(), g_nicks.end());

    // Longest first: "LonelyPirate" has to be taken out before anything would
    // match "Pirate" inside it and leave a fragment behind.
    std::sort(out.begin(), out.end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    return out;
}

void Clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_nicks.clear();
    g_lastHarvest = 0;
}

size_t Count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_nicks.size();
}

}  // namespace Nicks
