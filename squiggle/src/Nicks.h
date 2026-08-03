// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <windows.h>

#include <string>

// Known nicknames, so the chat checker never underlines the people you are
// talking to.
//
// The obvious source would be HOOK_USER_ONLINE, but that hook is declared in the
// plugin API and never actually fired by DC++, so subscribing to it does
// nothing. HOOK_UI_CHAT_DISPLAY does carry the sender, but only for users who
// have spoken, and it means trusting the host to lay out the payload exactly as
// documented -- a wrong guess reads a bad pointer on the client's GUI thread.
//
// The hub's own user list is complete and costs nothing to be wrong about, so
// that is what is read instead.
namespace Nicks {

// Nicknames are kept per chat frame rather than in one process-wide set. A name
// only counts as a name in the hub it belongs to: pooling every hub's user list
// together means one person called "sedan" anywhere silently switches that word
// off in every other hub, forever.
//
// Case-insensitive. Also matches the alphabetic parts of decorated nicks, so
// "[SE]Pelle_42" makes a bare "Pelle" acceptable too.
bool Contains(HWND chatInput, const std::wstring& word);

// Drops the names harvested for one chat frame, called when its input control
// goes away.
void Forget(HWND chatInput);

void Clear();
size_t Count();

// Reads the user list belonging to the hub or PM frame that owns `chatInput`,
// replacing whatever was known about that frame. Must be called on the GUI
// thread. Rate-limited per frame, so calling it on every focus change is fine.
void HarvestFrom(HWND chatInput);

}  // namespace Nicks
