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

// Finds every chat message box in the host client and gives it Word-style
// spell checking: a red wavy underline under misspelled words while you type,
// and a right-click menu with suggestions.
//
// The host exposes no API for its input controls, so discovery is done with a
// thread-local CallWndProc hook on the GUI thread. FulDC++ builds each hub and
// PM frame with a plain multiline Win32 "Edit" for the message box, which is
// what we latch onto. Plain Edit controls have no native wavy underline, so the
// squiggles are drawn by hand after the control has painted itself.
namespace ChatInput {

// Remembers the module handle used when installing the hook.
void SetModule(HINSTANCE instance);

// Points the checker at the shared speller. Must be called before Install.
void SetSpeller(Speller* speller);

// Installs the discovery hook. Safe to call repeatedly and from any thread:
// it locates the host's GUI thread itself and does nothing once installed.
// Returns true once the hook is live.
bool EnsureInstalled();

// Detaches from every control and removes the hook.
void Uninstall();

// Drops cached results so the next paint re-checks with current settings.
void InvalidateAll();

}  // namespace ChatInput
