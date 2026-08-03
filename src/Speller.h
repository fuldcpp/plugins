// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Spell-checking front end.
//
// A word counts as correctly spelled when *any* enabled backend accepts it. That
// is the whole point of this plugin: Swedish hubs code-switch into English mid
// sentence, and a checker with one active language flags half of every message.
//
// Backends, in priority order for suggestions:
//   1. Windows' own engine (ISpellChecker) for every installed language
//   2. the user's personal word list
//   3. words ignored for this session only
class Speller {
public:
    ~Speller();

    // Brings up COM and creates a checker per requested language that Windows
    // actually has installed. Returns false only if no language could be opened.
    bool Init(const std::vector<std::wstring>& languageTags);
    void Shutdown();

    bool Ready() const { return !checkers_.empty(); }

    // Language tags that ended up with a live checker, in priority order.
    const std::vector<std::wstring>& ActiveLanguages() const { return activeTags_; }

    // Requested languages that Windows does not have installed yet.
    std::vector<std::wstring> MissingLanguages() const;

    // Re-opens the checkers so languages installed after startup are picked up
    // without restarting the client. Returns true when the active set changed.
    bool RefreshLanguages();

    // Replaces the requested language list and re-opens the checkers.
    void SetLanguages(const std::vector<std::wstring>& tags);

    bool IsCorrect(const std::wstring& word);
    std::vector<std::wstring> Suggest(const std::wstring& word, size_t maxCount = 8);

    // Every language Windows has a spell checker for, whether or not it is
    // currently enabled. Drives the language list in the settings dialog.
    static std::vector<std::wstring> InstalledLanguages();

    void SetPersonalPath(std::wstring path);
    const std::wstring& PersonalPath() const { return personalPath_; }
    void LoadPersonal();
    void AddToPersonal(const std::wstring& word);
    void IgnoreForSession(const std::wstring& word);

    // The personal list as editable text, one word per line.
    std::wstring PersonalAsText() const;
    void SavePersonalText(const std::wstring& text);

private:
    struct Checker {
        std::wstring tag;
        void* checker = nullptr;  // ISpellChecker*, kept opaque to keep the header light
    };

    static std::wstring Fold(const std::wstring& word);

    // Releases the current checkers and opens one per installed requested
    // language. Safe to call repeatedly.
    void OpenCheckers();
    void CloseCheckers();

    // ISpellChecker is an apartment-threaded COM object: it may only be called
    // from the thread that created it. The plugin is loaded on whichever thread
    // the host uses for startup, but every lookup comes from the GUI thread, and
    // a cross-apartment call fails with RPC_E_WRONG_THREAD. The failure is
    // indistinguishable from "this word is misspelled with no suggestions", so
    // it has to be prevented rather than detected.
    void EnsureOnCurrentThread();

    std::vector<Checker> checkers_;
    std::vector<std::wstring> requestedTags_;
    std::vector<std::wstring> activeTags_;
    std::unordered_set<std::wstring> personal_;
    std::unordered_set<std::wstring> ignored_;
    std::unordered_map<std::wstring, bool> cache_;
    std::wstring personalPath_;

    // Thread the current checkers were created on. 0 when there are none.
    unsigned long creatorThread_ = 0;
};
