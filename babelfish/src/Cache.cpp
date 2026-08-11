// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Cache.h"

#include "MessageFilter.h"

#include <windows.h>

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Cache {
namespace {

// Section 7.5. Five thousand short phrases is well under a megabyte on disk and
// covers years of chat.
constexpr size_t kMaxEntries = 5000;

std::mutex g_mutex;
std::wstring g_path;

// Classic LRU: a list in most-recently-used order, plus a map into it so a
// lookup does not walk the list.
using Order = std::list<std::pair<std::string, std::string>>;  // key -> translation
Order g_order;
std::unordered_map<std::string, Order::iterator> g_index;

long long g_hits = 0;
long long g_misses = 0;

std::string MakeKey(const std::string& backend, const std::string& text,
                    const std::string& sourceLang) {
    return backend + "\x1f" + sourceLang + "\x1f" + MessageFilter::Normalise(text);
}

// Bumped when the key format changes, so entries written under the old scheme
// are dropped rather than sitting in the file forever, unmatched and taking up
// room in an LRU that would never evict them.
constexpr const char* kFormatMarker = "#babelfish-cache 3";

// The file is one entry per line, key and value separated by a tab. Both halves
// can contain newlines, so they are escaped rather than written raw.
std::string EscapeField(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string UnescapeField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        switch (s[++i]) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case '\\': out += '\\'; break;
            default: out += s[i]; break;
        }
    }
    return out;
}

std::string ReadWholeFile(const std::wstring& path) {
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};

    std::string contents;
    char buffer[8192];
    DWORD read = 0;
    while (::ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        contents.append(buffer, read);
    }
    ::CloseHandle(file);
    return contents;
}

bool WriteWholeFile(const std::wstring& path, const std::string& contents) {
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()),
                                &written, nullptr);
    ::CloseHandle(file);
    return ok && written == contents.size();
}

// Assumes the lock is held.
void Touch(std::unordered_map<std::string, Order::iterator>::iterator it) {
    g_order.splice(g_order.begin(), g_order, it->second);
}

void InsertLocked(std::string key, std::string value) {
    auto existing = g_index.find(key);
    if (existing != g_index.end()) {
        existing->second->second = std::move(value);
        Touch(existing);
        return;
    }

    g_order.emplace_front(key, std::move(value));
    g_index[std::move(key)] = g_order.begin();

    while (g_index.size() > kMaxEntries) {
        g_index.erase(g_order.back().first);
        g_order.pop_back();
    }
}

}  // namespace

void Load(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = path;
    g_order.clear();
    g_index.clear();
    if (g_path.empty()) return;

    const std::string contents = ReadWholeFile(g_path);

    // The file is written most-recently-used first, so inserting in reverse
    // rebuilds the same order the cache had when it was saved.
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= contents.size()) {
        const size_t end = contents.find('\n', start);
        const size_t stop = (end == std::string::npos) ? contents.size() : end;
        if (stop > start) {
            size_t trimmed = stop;
            if (contents[trimmed - 1] == '\r') --trimmed;
            if (trimmed > start) lines.push_back(contents.substr(start, trimmed - start));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }

    // A file without the current marker was written by an older key format.
    // Reading it would fill the cache with entries nothing can ever match.
    if (lines.empty() || lines.front() != kFormatMarker) return;

    for (auto it = lines.rbegin(); it != lines.rend() - 1; ++it) {
        const size_t tab = it->find('\t');
        if (tab == std::string::npos) continue;
        InsertLocked(UnescapeField(it->substr(0, tab)), UnescapeField(it->substr(tab + 1)));
    }
}

void Save() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_path.empty()) return;

    std::string out = std::string(kFormatMarker) + "\n";
    for (const auto& entry : g_order) {
        out += EscapeField(entry.first);
        out += '\t';
        out += EscapeField(entry.second);
        out += '\n';
    }
    WriteWholeFile(g_path, out);
}

bool Lookup(const std::string& backend, const std::string& text,
            const std::string& sourceLang, std::string& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_index.find(MakeKey(backend, text, sourceLang));
    if (it == g_index.end()) {
        ++g_misses;
        return false;
    }
    out = it->second->second;
    Touch(it);
    ++g_hits;
    return true;
}

void Store(const std::string& backend, const std::string& text,
           const std::string& sourceLang, const std::string& translation) {
    if (translation.empty()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    InsertLocked(MakeKey(backend, text, sourceLang), translation);
}

Stats GetStats() {
    std::lock_guard<std::mutex> lock(g_mutex);
    Stats stats;
    stats.entries = g_index.size();
    stats.hits = g_hits;
    stats.misses = g_misses;
    return stats;
}

void Clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_order.clear();
    g_index.clear();
    g_hits = 0;
    g_misses = 0;
    if (!g_path.empty()) ::DeleteFileW(g_path.c_str());
}

}  // namespace Cache
