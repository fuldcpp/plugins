// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <windows.h>

#include <cstdint>

// Section 4: the worker thread never calls into the host. It posts a job id to
// a message-only window, and the window procedure -- which runs on whichever
// thread created the window -- does the sending.
//
// The window has to be created from the host's GUI thread, or its messages are
// never dispatched. EnsureWindow is therefore called from the hook procedures
// and the chat-input subclass, all of which the host invokes on that thread,
// rather than from pluginInit, which the host is free to run anywhere.
namespace Marshal {

using Handler = void (*)(uint64_t jobId);

void SetModule(HINSTANCE instance);

// Installs the callback run on the GUI thread for each posted id.
void SetHandler(Handler handler);

// Records which thread is allowed to own the window. Set by ChatInput once it
// has located the host's GUI thread. Without this the guard in EnsureWindow is
// inactive and any caller is trusted.
void SetGuiThread(DWORD threadId);

// Creates the window if it does not exist. Must be called from the thread that
// pumps messages for the host's windows; a call from anywhere else is refused
// rather than silently creating a window whose messages are never dispatched.
// Returns true once it exists.
bool EnsureWindow();

// True once EnsureWindow has succeeded.
bool Ready();

// Queues an id for the GUI thread. Safe from any thread. Returns false when
// there is no window yet, in which case the caller has to hold on to the
// result and retry.
bool Post(uint64_t jobId);

// Tears the window down. Callable from any thread: when it is not the owning
// one, the destroy is bounced through the window itself, because DestroyWindow
// only works from the thread that created the window and leaving a live window
// behind with its procedure inside a DLL that is about to unload is a crash.
void Destroy();

}  // namespace Marshal
