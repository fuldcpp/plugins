// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>
#include <vector>

// A half-open range [start, start+len) of UTF-16 code units within the edit text.
struct Range {
    int start = 0;
    int len = 0;

    int end() const { return start + len; }
    bool contains(int idx) const { return idx >= start && idx < end(); }
    bool operator==(const Range& o) const { return start == o.start && len == o.len; }
};

// Splits chat text into candidate words, already filtered through the rules that
// keep DC chatter from being flagged: URLs, magnet links, TTH hashes, /commands,
// nicks, filenames, smileys, anything with digits, and ALL-CAPS shouting.
std::vector<Range> TokenizeForSpelling(const std::wstring& text);

std::wstring Slice(const std::wstring& text, const Range& r);
