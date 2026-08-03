// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>

// Automatic replacement of typos as you finish a word.
//
// Stored as a plain "fel=ratt" text file next to the plugin so it can be edited
// by hand as well as through the settings dialog.
namespace AutoCorrect {

void SetPath(std::wstring path);

// Reads the file, creating it with a small starter list the first time.
void Load();

// Replaces the whole list from the settings dialog's text box.
void SaveText(const std::wstring& text);

// The list as editable text, for the settings dialog.
std::wstring AsText();

// Looks up a completed word. Matching ignores case; the replacement keeps the
// capitalisation the user typed, so "Teh" becomes "The".
bool Lookup(const std::wstring& word, std::wstring& replacement);

// Stops correcting one word for the rest of the session. Used when the user
// undoes a correction, which is the clearest possible statement that they meant
// what they typed.
void Suppress(const std::wstring& word);

size_t Count();

}  // namespace AutoCorrect
