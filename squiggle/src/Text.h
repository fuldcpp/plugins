// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>

// Case folding for everything that has to compare words: the personal word list,
// the session ignore list, the nick list and the autocorrect rules.
//
// This exists because towlower and towupper are not usable for it. In the CRT's
// default "C" locale they map ASCII and nothing else, so folding "Åke" leaves the
// Å alone and it stops matching "åke" -- silently, and only for the letters this
// plugin was written for. iswalpha and iswlower do handle them, which is what
// hides the problem: the tokenizer looks right while every lookup that should be
// case-insensitive quietly is not.
//
// The Win32 character functions have no such gap.
namespace Text {

std::wstring Fold(std::wstring s);
std::wstring Upper(std::wstring s);

}  // namespace Text
