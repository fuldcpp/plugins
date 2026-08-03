// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Nicks.h"

#include <commctrl.h>

#include "Text.h"

#include <algorithm>
#include <cwctype>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Nicks {
namespace {

// A hub with more users than this is not worth stalling the GUI thread for.
constexpr int kMaxUsers = 20000;
constexpr ULONGLONG kMinIntervalMs = 10000;
constexpr size_t kMinPartLength = 3;

// One entry per chat frame. Replaced wholesale on each harvest, so names do not
// accumulate as people come and go.
struct Frame {
    std::unordered_set<std::wstring> nicks;
    ULONGLONG lastHarvest = 0;
};

std::mutex g_mutex;
std::unordered_map<HWND, Frame> g_frames;

std::wstring Fold(const std::wstring& s) {
    return Text::Fold(s);
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
void Add(std::unordered_set<std::wstring>& into, const std::wstring& nick) {
    if (nick.empty()) return;
    into.insert(Fold(nick));

    std::wstring part;
    for (wchar_t c : nick) {
        if (std::iswalpha(static_cast<wint_t>(c))) {
            part += c;
        } else {
            if (part.size() >= kMinPartLength) into.insert(Fold(part));
            part.clear();
        }
    }
    if (part.size() >= kMinPartLength && part.size() != nick.size()) {
        into.insert(Fold(part));
    }
}

// The frame a chat input belongs to. Everything about a hub -- its input box and
// its user list -- hangs off the same parent.
HWND FrameOf(HWND chatInput) {
    return chatInput ? ::GetParent(chatInput) : nullptr;
}

}  // namespace

bool Contains(HWND chatInput, const std::wstring& word) {
    if (word.empty()) return false;

    const HWND frame = FrameOf(chatInput);
    if (!frame) return false;

    const std::wstring folded = Fold(word);

    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_frames.find(frame);
    return it != g_frames.end() && it->second.nicks.count(folded) != 0;
}

void Forget(HWND chatInput) {
    const HWND frame = FrameOf(chatInput);
    if (!frame) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_frames.erase(frame);
}

void Clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_frames.clear();
}

size_t Count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    size_t total = 0;
    for (const auto& entry : g_frames) total += entry.second.nicks.size();
    return total;
}

void HarvestFrom(HWND chatInput) {
    const HWND frame = FrameOf(chatInput);
    if (!frame) return;

    const ULONGLONG now = ::GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Frame& entry = g_frames[frame];
        if (entry.lastHarvest != 0 && now - entry.lastHarvest < kMinIntervalMs) return;
        entry.lastHarvest = now;
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

    std::unordered_set<std::wstring> fresh;
    for (const std::wstring& nick : found) Add(fresh, nick);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_frames[frame].nicks = std::move(fresh);
}

}  // namespace Nicks
