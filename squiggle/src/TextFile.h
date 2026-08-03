// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>

// UTF-8 file access.
//
// std::wifstream and std::wofstream look like they handle wide text, but with
// the default locale they convert through the ANSI code page: writing "hjärna"
// to a word list mangles it, and reading it back mangles it again. Every file
// this plugin owns holds Swedish words, so that had to go.
//
// Written with a BOM, because Notepad guesses ANSI without one and these files
// are meant to be editable by hand.
namespace TextFile {

// Returns an empty string when the file is missing. Any BOM is stripped.
std::wstring Read(const std::wstring& path);

bool Write(const std::wstring& path, const std::wstring& text);

}  // namespace TextFile
