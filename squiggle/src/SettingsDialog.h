// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <windows.h>

class Speller;

// Modal settings dialog. Returns true when the user pressed OK, in which case
// the live settings, personal word list and autocorrect rules have all been
// updated and saved.
bool ShowSettingsDialog(HINSTANCE instance, HWND parent, Speller& speller);
