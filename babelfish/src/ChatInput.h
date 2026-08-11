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

// Section 7.1, the primary trigger.
//
//   Enter                       send as written; the plugin does not touch it
//   Ctrl+Enter / Shift+Enter    line break; must reach the control untouched
//   Ctrl+G                      translate into English and send
//
// The spec asks for Ctrl+T, and Ctrl+T is indeed free as far as Windows and
// RichEdit are concerned -- but not in this client. FulDC++ binds it in its own
// accelerator table (#123, command 42784), and accelerators are translated in
// the message loop before the key ever reaches the focused control, so a
// subclass here would never see it. Ctrl+G is absent from that table and means
// nothing to an edit control. Ctrl+J and Ctrl+M are avoided for the same kind
// of reason: they are line feed and carriage return.
//
// There is no mode to remember and no indicator to read, so there is no way to
// end up sending translated text to the wrong person -- and writing in your own
// language stays the default.
//
// Discovery works the same way as in Squiggle: the host exposes no API for its
// input controls, so a thread-local CallWndProc hook on the GUI thread finds
// them and each one is subclassed.
namespace ChatInput {

void SetModule(HINSTANCE instance);

// Installs the discovery hook. Safe to call repeatedly and from any thread; it
// locates the host's GUI thread itself. Returns true once the hook is live.
bool EnsureInstalled();

void Uninstall();

// True when this outgoing message is the one Ctrl+G was pressed for. Matched on
// the text rather than on timing, because the client can sit on a message for
// seconds before the hook runs.
// wasAction comes back true when the match only succeeded once a leading
// "/me " was allowed for: the client removes that prefix before the hook sees
// the message, so the caller has to put it back to keep the line an action.
bool ConsumeTranslateRequest(const std::string& outgoingText, bool* wasAction = nullptr);

// Called on the GUI thread when Ctrl+G is pressed in a chat log rather than in
// a message box, with the whole line the caret or selection sits on.
//
// The same key in two controls: in the message box it translates and sends, in
// the log it translates what you are looking at and shows it to you only. There
// is no mode to be in and nothing to remember.
using IncomingHandler = void (*)(const std::string& displayedLine);
void SetIncomingHandler(IncomingHandler handler);

// One line describing what discovery actually found: whether the hook is
// installed, which thread it is on, how many message boxes were located and how
// many carry our subclass. Reported by "/tr probe".
std::string Diagnostics();

}  // namespace ChatInput
