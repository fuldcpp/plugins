// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Tokenizer.h"

#include <algorithm>
#include <cwctype>

namespace {

// Letters plus the in-word punctuation Swedish and English actually use.
// Apostrophes and hyphens only count when they sit between two letters, which
// the tokenizer enforces by trimming them off the ends afterwards.
bool IsLetter(wchar_t c) {
    return std::iswalpha(static_cast<wint_t>(c)) != 0;
}

bool IsDigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

// A "run" is a maximal stretch of characters that could belong to one token,
// including the separators that make URLs and paths hang together. Splitting on
// whitespace alone would tear "http://x.se/a" into pieces we could not recognise.
bool IsRunChar(wchar_t c) {
    if (IsLetter(c) || IsDigit(c)) return true;
    switch (c) {
        case L'\'': case L'’':
        case L'-': case L'_': case L'.': case L':': case L'/':
        case L'\\': case L'@': case L'#': case L'?': case L'=':
        case L'&': case L'%': case L'+': case L'~':
            return true;
        default:
            return false;
    }
}

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c))); });
    return s;
}

bool StartsWith(const std::wstring& s, const wchar_t* prefix) {
    const std::wstring p(prefix);
    return s.size() >= p.size() && ToLower(s.substr(0, p.size())) == p;
}

// True when the whole run is something we must never spell-check.
bool IsIgnoredRun(const std::wstring& run) {
    if (run.empty()) return true;

    // Protocol links and magnets.
    static const wchar_t* kPrefixes[] = {
        L"http://", L"https://", L"ftp://", L"magnet:", L"dchub://",
        L"adc://", L"adcs://", L"nmdc://", L"www.", L"irc://", L"mailto:",
    };
    for (const wchar_t* p : kPrefixes) {
        if (StartsWith(run, p)) return true;
    }

    // Anything holding a digit: version numbers, sizes, ports, "5st", hashes.
    if (std::any_of(run.begin(), run.end(), IsDigit)) return true;

    // Paths, e-mail-ish things, hub addresses, filenames with an extension.
    if (run.find(L'/') != std::wstring::npos) return true;
    if (run.find(L'\\') != std::wstring::npos) return true;
    if (run.find(L'@') != std::wstring::npos) return true;
    if (run.find(L':') != std::wstring::npos) return true;
    if (run.find(L'.') != std::wstring::npos) return true;
    if (run.find(L'_') != std::wstring::npos) return true;

    // TTH base32 hashes are 39 chars of upper-case letters and digits; the digit
    // rule above catches most, this catches the rest along with other long noise.
    if (run.size() >= 24) return true;

    // ALL CAPS is shouting or an acronym, not a spelling mistake worth flagging.
    const bool hasLower = std::any_of(run.begin(), run.end(),
                                      [](wchar_t c) { return std::iswlower(static_cast<wint_t>(c)) != 0; });
    if (!hasLower && run.size() > 1) return true;

    // MixedCase in the middle of a word is almost always a nick (DarkAngel).
    for (size_t i = 1; i + 1 < run.size(); ++i) {
        if (std::iswupper(static_cast<wint_t>(run[i])) && std::iswlower(static_cast<wint_t>(run[i - 1]))) {
            return true;
        }
    }

    return false;
}

// Strip leading/trailing characters that are not letters, so "hej," and "(hej)"
// both reduce to "hej" while keeping the offsets pointing at the real word.
void TrimToLetters(const std::wstring& text, Range& r) {
    while (r.len > 0 && !IsLetter(text[r.start])) {
        ++r.start;
        --r.len;
    }
    while (r.len > 0 && !IsLetter(text[r.start + r.len - 1])) {
        --r.len;
    }
}

}  // namespace

std::wstring Slice(const std::wstring& text, const Range& r) {
    if (r.start < 0 || r.len <= 0 || r.start + r.len > static_cast<int>(text.size())) return {};
    return text.substr(static_cast<size_t>(r.start), static_cast<size_t>(r.len));
}

std::vector<Range> TokenizeForSpelling(const std::wstring& text) {
    std::vector<Range> out;
    const int n = static_cast<int>(text.size());

    // A line starting with '/' is a client command, not prose.
    bool lineIsCommand = false;
    bool atLineStart = true;

    int i = 0;
    while (i < n) {
        const wchar_t c = text[i];

        if (c == L'\n' || c == L'\r') {
            atLineStart = true;
            lineIsCommand = false;
            ++i;
            continue;
        }

        if (atLineStart) {
            if (c == L'/') lineIsCommand = true;
            if (!std::iswspace(static_cast<wint_t>(c))) atLineStart = false;
        }

        if (!IsRunChar(c)) {
            ++i;
            continue;
        }

        const int runStart = i;
        while (i < n && IsRunChar(text[i])) ++i;
        Range run{runStart, i - runStart};

        if (lineIsCommand) continue;

        const std::wstring runText = Slice(text, run);
        if (IsIgnoredRun(runText)) continue;

        // The run survived the filters; split it on hyphens so "e-post" checks
        // both halves, then trim each piece down to its letters.
        int segStart = run.start;
        for (int j = run.start; j <= run.end(); ++j) {
            const bool boundary = (j == run.end()) || text[j] == L'-';
            if (!boundary) continue;

            Range word{segStart, j - segStart};
            TrimToLetters(text, word);
            if (word.len >= 2) out.push_back(word);
            segStart = j + 1;
        }
    }

    return out;
}
