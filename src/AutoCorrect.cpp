// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "AutoCorrect.h"

#include <windows.h>

#include "Strings.h"
#include "TextFile.h"

#include <algorithm>
#include <cwctype>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace AutoCorrect {
namespace {

// Deliberately short. Guessing at someone else's typos mostly produces
// corrections they have to undo, so this is a demonstration of the format
// rather than an attempt at a complete list. The header is localised; the rules
// themselves are English typos and stay as they are.
const wchar_t* const kStarterRules =
    L"\n"
    L"teh=the\n"
    L"adn=and\n"
    L"recieve=receive\n"
    L"seperate=separate\n"
    L"definately=definitely\n"
    L"occurence=occurrence\n";

std::mutex g_mutex;
std::unordered_map<std::wstring, std::wstring> g_rules;
std::unordered_set<std::wstring> g_suppressed;
std::wstring g_path;

std::wstring Fold(const std::wstring& s) {
    std::wstring out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c))); });
    return out;
}

void Trim(std::wstring& s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r')) s.pop_back();
}

void ParseInto(const std::wstring& text, std::unordered_map<std::wstring, std::wstring>& out) {
    std::wstringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line)) {
        Trim(line);
        if (line.empty() || line[0] == L'#') continue;

        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;

        std::wstring from = line.substr(0, eq);
        std::wstring to = line.substr(eq + 1);
        Trim(from);
        Trim(to);
        if (from.empty() || to.empty() || from == to) continue;

        out[Fold(from)] = to;
    }
}

// Replaces the leading comment block with the current one.
//
// The header is documentation, not data: it carries the plugin's name and is
// written in the interface language. A file created by an older version keeps
// saying the old name forever otherwise, and migrating the file across a rename
// preserved exactly that. Only the block at the very top is touched, so rules
// and any comments the user wrote further down survive untouched.
std::wstring RefreshHeader(const std::wstring& text, const std::wstring& header) {
    size_t body = 0;
    while (body < text.size()) {
        const size_t lineEnd = text.find(L'\n', body);
        const size_t next = (lineEnd == std::wstring::npos) ? text.size() : lineEnd + 1;

        std::wstring line = text.substr(body, next - body);
        Trim(line);
        if (!line.empty() && line[0] != L'#') break;  // first real content

        body = next;
    }

    // Exactly one blank line between the header and the first rule, whatever the
    // old file happened to have.
    std::wstring rest = text.substr(body);
    while (!rest.empty() && (rest.front() == L'\n' || rest.front() == L'\r')) rest.erase(rest.begin());

    return header + L"\n" + rest;
}


// Mirrors the capitalisation of what was typed onto the replacement, so
// autocorrect never changes the shape of a sentence.
std::wstring MatchCase(const std::wstring& typed, const std::wstring& replacement) {
    if (typed.empty() || replacement.empty()) return replacement;

    const bool allUpper =
        typed.size() > 1 && std::none_of(typed.begin(), typed.end(), [](wchar_t c) {
            return std::iswlower(static_cast<wint_t>(c)) != 0;
        });
    if (allUpper) {
        std::wstring out = replacement;
        std::transform(out.begin(), out.end(), out.begin(), [](wchar_t c) {
            return static_cast<wchar_t>(std::towupper(static_cast<wint_t>(c)));
        });
        return out;
    }

    if (std::iswupper(static_cast<wint_t>(typed.front()))) {
        std::wstring out = replacement;
        out.front() = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(out.front())));
        return out;
    }

    return replacement;
}

}  // namespace

void SetPath(std::wstring path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = std::move(path);
}

void Load() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_path.empty()) return;

    const std::wstring header = Strings::T(Strings::Str::AutoCorrectFileHeader);

    std::wstring text = TextFile::Read(g_path);
    if (text.empty()) {
        text = header + kStarterRules;
        TextFile::Write(g_path, text);
    } else {
        const std::wstring refreshed = RefreshHeader(text, header);
        if (refreshed != text) {
            TextFile::Write(g_path, refreshed);
            text = refreshed;
        }
    }

    g_rules.clear();
    ParseInto(text, g_rules);
}

void SaveText(const std::wstring& text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    TextFile::Write(g_path, text);
    g_rules.clear();
    ParseInto(text, g_rules);
}

std::wstring AsText() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_path.empty() ? std::wstring() : TextFile::Read(g_path);
}

bool Lookup(const std::wstring& word, std::wstring& replacement) {
    if (word.empty()) return false;

    std::lock_guard<std::mutex> lock(g_mutex);
    const std::wstring folded = Fold(word);
    if (g_suppressed.count(folded)) return false;

    const auto it = g_rules.find(folded);
    if (it == g_rules.end()) return false;

    replacement = MatchCase(word, it->second);
    return replacement != word;
}

void Suppress(const std::wstring& word) {
    if (word.empty()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_suppressed.insert(Fold(word));
}

size_t Count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_rules.size();
}

}  // namespace AutoCorrect
