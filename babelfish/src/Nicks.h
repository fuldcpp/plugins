// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

// The nicks in the hub whose message box the user is typing in.
//
// A translation service has no idea that "mango" is a person, and DC chat is
// full of names. This is the same trick Squiggle uses to keep the spell checker
// from underlining everybody's nick: the host exposes no API for the user list,
// so it is found in the window tree by its structure and read directly.
//
// The difference here is what gets stored. Squiggle only needs to answer "is
// this word a nick"; the translator needs the nicks themselves, spelled the way
// their owners spell them, so they can be taken out of a message and put back
// afterwards.
namespace Nicks {

void HarvestFrom(HWND chatInput);

// Longest first, so "LonelyPirate" is matched before "Pirate" would be.
std::vector<std::string> All();

void Clear();
size_t Count();

}  // namespace Nicks
