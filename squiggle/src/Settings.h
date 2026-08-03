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

// Plugin settings, persisted through the host's own config store so they survive
// reinstalls and land in the client's settings file rather than a stray file of
// our own.
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

    void Load();
    void Save() const;
};

// The one live instance, read by the painter and the checker.
Settings& CurrentSettings();
