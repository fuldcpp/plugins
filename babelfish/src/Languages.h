// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>
#include <vector>

namespace Languages {

// The target is fixed. Section 7.4: a Swedish and a German user get identical
// behaviour without touching a setting, so this is not a config key.
inline constexpr const char* kTarget = "en";

// The stored source language when the service is to work it out for itself.
// Not a language code, and deliberately not two letters so it can never collide
// with one.
inline constexpr const char* kAutoSource = "auto";

// True for a two-letter code the backends are known to accept as a source
// language. Used by the "xx:" prefix so that "hej: du dar" and "http: nej" fall
// through as ordinary text instead of being eaten as a language override.
bool IsKnown(const std::string& code);

// Human-readable name for a code, or the code itself when unrecognised.
const char* NameOf(const std::string& code);

// Maps a Windows locale name ("sv-SE") to a bare language code ("sv").
// Returns an empty string when the locale cannot be read.
std::string FromLocaleName(const std::string& localeName);

// Reads the current user's locale and reduces it as above. Section 7.4: this is
// where sourceLang comes from on first run.
std::string DetectSource();

// A language as it should be offered to a person rather than to an API.
struct Choice {
    std::string code;
    std::wstring name;
};

// The languages that can be translated from, named by Windows so that no table
// of translated names has to be maintained here. Sorted by name, and English is
// left out because it is the target.
//
// inEnglish picks which naming: the dialog decides, not the machine. A client
// running in English on a Swedish Windows was listing "Svenska" among otherwise
// English labels, which is the sort of seam that makes software feel unfinished.
std::vector<Choice> Choices(bool inEnglish);

// One language's name. Falls back to the built-in English name, and to the code
// itself if even that is unknown.
std::wstring DisplayName(const std::string& code, bool inEnglish = true);

}  // namespace Languages
