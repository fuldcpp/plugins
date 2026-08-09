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

// The last write that failed, as a path with the Windows error code, and how
// many have failed. Empty and zero while all is well.
//
// Every caller of Write discards its result, and the ones that matter are
// AddToPersonal and the dialog's OK button: there, a failure means a word the
// user typed is gone with nothing on screen changing to say so. The plugin's
// files sit beside its own DLL, wherever the host installed it -- and under
// Program Files that folder is read-only to anyone not running elevated.
//
// Recorded here rather than at each call site, so that a seventh caller added
// later is covered without anybody having to remember this.
std::wstring WriteFailure();
unsigned FailureCount();

}  // namespace TextFile
