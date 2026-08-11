// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>

// Section 7.5. Chat phrases repeat heavily; without this the same "tack" and
// "nite all" cost quota every single evening.
//
// Keyed on normalised text plus source language, capped and evicted
// least-recently-used, and written to a plain text file in the plugin's own
// directory so it survives a restart.
namespace Cache {

// Loads from disk. Safe to call before the path is known: it does nothing then.
void Load(const std::wstring& path);

// Writes the current contents back out. Called on unload.
void Save();

// Returns true and fills out when the phrase is known. Counts towards the hit
// rate reported by /tr status.
//
// The backend is part of the key. Without it, switching from MyMemory to Claude
// went on serving MyMemory's answers for everything already said, the new
// service was never asked, and /tr status kept naming the old one because a
// cache hit reaches nobody. Different services give different translations, so
// they get different entries -- and switching back finds the old ones intact.
bool Lookup(const std::string& backend, const std::string& text,
            const std::string& sourceLang, std::string& out);

// Records a translation. Evicts the oldest entry when full.
void Store(const std::string& backend, const std::string& text,
           const std::string& sourceLang, const std::string& translation);

struct Stats {
    size_t entries = 0;
    long long hits = 0;
    long long misses = 0;
};

Stats GetStats();

// Drops everything, including the file on disk.
void Clear();

}  // namespace Cache
