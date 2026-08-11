// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <windows.h>

// The settings dialog the host opens from its plugin list, through ON_CONFIGURE.
//
// Everything here can also be set with /tr, and for most of it that is a
// perfectly good way to do it. The language cannot: asking somebody to know
// that Polish is "pl" is asking them to give up. A list of language names, in
// their own language, is the whole reason this dialog exists.
//
// Returns true when the user pressed OK and the settings were saved.
bool ShowSettingsDialog(HINSTANCE instance, HWND parent);
