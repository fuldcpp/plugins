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
#include <vector>

// Plugin settings, kept in a file of our own under %LOCALAPPDATA%\Squiggle\ and
// mirrored into the host's config store.
//
// The mirror was the whole of it until installing a new version was measured:
// the client empties the plugin's install directory and recreates its entry in
// Plugins.xml without a single <Setting> child, so colour, thickness and the
// language ticks all went back to their defaults on every update. The file is
// the copy that is still there afterwards; the host's config is read for
// whatever the file does not carry, which is what brings the settings across
// from the versions that had nowhere else to put them.
//
// Everything is stored as a string. The config API hands back an empty value for
// a setting that was never written, and an empty string is the only unambiguous
// "not configured yet" marker across all the types we need.
struct Settings {
    std::vector<std::wstring> languages{L"sv-SE", L"en-US", L"en-GB"};
    COLORREF colour = RGB(255, 64, 64);
    int thickness = 2;

    // Off until asked for. Automatic replacement is the one feature here that
    // changes what you wrote without being asked, so it should be a decision
    // rather than something you discover mid-sentence.
    bool autoCorrect = false;

    // Interface language: "sv", "en", or empty for auto-detection from the host.
    std::wstring uiLanguage;

    // Binds to the host config interface, passed as void* so this header does
    // not have to drag in the plugin API. Without it the settings still work,
    // they just do not persist.
    static void Bind(void* config, const char* guid);

    // The plugin's own module handle, which is what names both directories
    // below. Passed as void* for the same reason.
    static void SetModule(void* instance);

    // %LOCALAPPDATA%\Squiggle\, created if it is not there yet. Trailing
    // backslash. Empty only if Windows will not say where LocalAppData is and
    // the plugin's own directory cannot be found either.
    static std::wstring SettingsDirectory();

    // The directory holding the DLL, which is the per-plugin install directory
    // the host made for us. Where everything used to live, and where an install
    // still clears it out from under us.
    static std::wstring DataDirectory();

    void Load();
    void Save() const;
};

// The one live instance, read by the painter and the checker.
Settings& CurrentSettings();
