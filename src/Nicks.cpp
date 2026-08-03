// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Nicks.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace Nicks {
namespace {

// A hub with more users than this is not worth stalling the GUI thread for.
constexpr int kMaxUsers = 20000;
constexpr ULONGLONG kMinIntervalMs = 10000;
constexpr size_t kMinPartLength = 3;

std::mutex g_mutex;
std::unordered_set<std::wstring> g_nicks;
ULONGLONG g_lastHarvest = 0;

std::wstring Fold(const std::wstring& s) {
    std::wstring out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c))); });
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

// Adds the nick itself plus each alphabetic run inside it, so tags and
// separators in "[SE]Pelle_42" do not hide the name people actually type.
void AddLocked(const std::wstring& nick) {
    if (nick.empty()) return;
    g_nicks.insert(Fold(nick));

    std::wstring part;
    for (wchar_t c : nick) {
        if (std::iswalpha(static_cast<wint_t>(c))) {
            part += c;
        } else {
            if (part.size() >= kMinPartLength) g_nicks.insert(Fold(part));
            part.clear();
        }
    }
    if (part.size() >= kMinPartLength && part.size() != nick.size()) {
        g_nicks.insert(Fold(part));
    }
}

}  // namespace

bool Contains(const std::wstring& word) {
    if (word.empty()) return false;
    const std::wstring folded = Fold(word);

    std::lock_guard<std::mutex> lock(g_mutex);
    return g_nicks.count(folded) != 0;
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
    std::vector<std::wstring> found;
    found.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        LVITEMW item = {};
        item.iSubItem = 0;
        item.pszText = buffer.data();
        item.cchTextMax = static_cast<int>(buffer.size());

        const LRESULT len = ::SendMessageW(list, LVM_GETITEMTEXTW, static_cast<WPARAM>(i),
                                           reinterpret_cast<LPARAM>(&item));
        if (len > 0) found.emplace_back(buffer.data(), static_cast<size_t>(len));
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (const std::wstring& nick : found) AddLocked(nick);
}

}  // namespace Nicks
