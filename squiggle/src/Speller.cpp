// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Speller.h"

#include <windows.h>
#include <spellcheck.h>

#include "Text.h"
#include "TextFile.h"

#include <algorithm>
#include <sstream>

namespace {

// Declared locally so the build does not depend on the SDK exporting the symbol.
const CLSID kSpellCheckerFactory =
    {0x7ab36653, 0x1796, 0x484b, {0xbd, 0xfa, 0xe7, 0x4f, 0x1d, 0xb7, 0xc1, 0xdc}};

ISpellChecker* AsChecker(void* p) { return static_cast<ISpellChecker*>(p); }

// The host loads plugins on whichever thread it uses for startup, which is not
// necessarily the GUI thread and is not necessarily an initialised COM apartment
// yet. Calling this before every CoCreateInstance means the checkers work on
// whatever thread ends up asking for them. RPC_E_CHANGED_MODE just means the
// host already picked a different apartment model, which is fine for an
// in-process server.
//
// Deliberately never uninitialised: unbalancing the host's own apartment would
// be far worse than leaving one reference behind until the process exits.
void EnsureComInitialised() {
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
}

// Pulls every string out of an IEnumString, which is how the API returns both
// the supported-language list and the suggestion list.
std::vector<std::wstring> DrainEnum(IEnumString* e, size_t maxCount) {
    std::vector<std::wstring> out;
    if (!e) return out;

    LPOLESTR item = nullptr;
    ULONG fetched = 0;
    while (out.size() < maxCount && e->Next(1, &item, &fetched) == S_OK && fetched == 1) {
        if (item) {
            out.emplace_back(item);
            ::CoTaskMemFree(item);
            item = nullptr;
        }
    }
    return out;
}

}  // namespace

Speller::~Speller() {
    Shutdown();
}

std::wstring Speller::Fold(const std::wstring& word) {
    return Text::Fold(word);
}

bool Speller::Init(const std::vector<std::wstring>& languageTags) {
    requestedTags_ = languageTags;
    OpenCheckers();
    return !checkers_.empty();
}

void Speller::OpenCheckers() {
    CloseCheckers();

    EnsureComInitialised();

    ISpellCheckerFactory* factory = nullptr;
    const HRESULT hr = ::CoCreateInstance(kSpellCheckerFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return;

    for (const std::wstring& tag : requestedTags_) {
        BOOL supported = FALSE;
        if (FAILED(factory->IsSupported(tag.c_str(), &supported)) || !supported) continue;

        ISpellChecker* checker = nullptr;
        if (FAILED(factory->CreateSpellChecker(tag.c_str(), &checker)) || !checker) continue;

        checkers_.push_back(Checker{tag, checker});
        activeTags_.push_back(tag);
    }

    factory->Release();

    if (!checkers_.empty()) creatorThread_ = ::GetCurrentThreadId();
}

void Speller::EnsureOnCurrentThread() {
    if (checkers_.empty()) return;
    if (creatorThread_ == ::GetCurrentThreadId()) return;

    // Rebind to the calling thread. Everything that matters -- the requested
    // languages, the personal list -- survives; only the COM objects and the
    // cached verdicts are rebuilt, and those verdicts were worthless anyway
    // because every call through them was failing.
    OpenCheckers();
}

void Speller::CloseCheckers() {
    for (Checker& c : checkers_) {
        if (c.checker) {
            AsChecker(c.checker)->Release();
            c.checker = nullptr;
        }
    }
    checkers_.clear();
    activeTags_.clear();
    creatorThread_ = 0;

    // Verdicts from the old set of languages are no longer valid: a word that
    // was wrong in Swedish alone may be right once English is available.
    cache_.clear();
}

std::vector<std::wstring> Speller::MissingLanguages() const {
    std::vector<std::wstring> missing;
    for (const std::wstring& tag : requestedTags_) {
        if (std::find(activeTags_.begin(), activeTags_.end(), tag) == activeTags_.end()) {
            missing.push_back(tag);
        }
    }
    return missing;
}

bool Speller::RefreshLanguages() {
    const std::vector<std::wstring> before = activeTags_;
    OpenCheckers();
    return activeTags_ != before;
}

void Speller::SetLanguages(const std::vector<std::wstring>& tags) {
    requestedTags_ = tags;
    OpenCheckers();
}

void Speller::Shutdown() {
    CloseCheckers();
    requestedTags_.clear();
}

bool Speller::IsCorrect(const std::wstring& word) {
    if (word.empty()) return true;

    const std::wstring folded = Fold(word);
    if (personal_.count(folded) || ignored_.count(folded)) return true;

    EnsureOnCurrentThread();

    if (auto it = cache_.find(word); it != cache_.end()) return it->second;

    bool anyAnswered = false;
    bool correct = false;
    for (Checker& c : checkers_) {
        IEnumSpellingError* errors = nullptr;
        if (FAILED(AsChecker(c.checker)->Check(word.c_str(), &errors)) || !errors) continue;

        anyAnswered = true;

        ISpellingError* first = nullptr;
        const HRESULT hr = errors->Next(&first);
        // S_FALSE (and a null result) means the enumerator was empty: no mistakes.
        const bool clean = (hr == S_FALSE) || (hr == S_OK && first == nullptr);
        if (first) first->Release();
        errors->Release();

        if (clean) {
            correct = true;
            break;
        }
    }

    // If not one checker managed to answer, the engines are broken rather than
    // the word. Saying "correct" keeps a failure silent instead of painting the
    // whole message red, and nothing is cached so it recovers by itself.
    if (!anyAnswered) return true;

    // Keep the cache from growing without bound during a long session.
    if (cache_.size() > 20000) cache_.clear();
    cache_[word] = correct;
    return correct;
}

std::vector<std::wstring> Speller::Suggest(const std::wstring& word, size_t maxCount) {
    std::vector<std::wstring> out;
    if (word.empty()) return out;

    EnsureOnCurrentThread();

    for (Checker& c : checkers_) {
        IEnumString* e = nullptr;
        if (FAILED(AsChecker(c.checker)->Suggest(word.c_str(), &e)) || !e) continue;

        for (std::wstring& s : DrainEnum(e, maxCount)) {
            if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(std::move(s));
        }
        e->Release();

        if (out.size() >= maxCount) break;
    }

    if (out.size() > maxCount) out.resize(maxCount);
    return out;
}

std::vector<std::wstring> Speller::InstalledLanguages() {
    std::vector<std::wstring> out;

    // This is called from the settings dialog, which may well be the first thing
    // this thread does with COM; without it the factory refuses to be created
    // and the language list comes up empty for no visible reason.
    EnsureComInitialised();

    ISpellCheckerFactory* factory = nullptr;
    if (FAILED(::CoCreateInstance(kSpellCheckerFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory))) ||
        !factory) {
        return out;
    }

    IEnumString* e = nullptr;
    if (SUCCEEDED(factory->get_SupportedLanguages(&e)) && e) {
        out = DrainEnum(e, 256);
        e->Release();
    }
    factory->Release();

    std::sort(out.begin(), out.end());
    return out;
}

void Speller::SetPersonalPath(std::wstring path) {
    personalPath_ = std::move(path);
}

std::wstring Speller::PersonalAsText() const {
    if (personalPath_.empty()) return {};

    return TextFile::Read(personalPath_);
}

void Speller::SavePersonalText(const std::wstring& text) {
    TextFile::Write(personalPath_, text);
    LoadPersonal();
    cache_.clear();
}

void Speller::LoadPersonal() {
    personal_.clear();
    if (personalPath_.empty()) return;

    std::wstringstream stream(TextFile::Read(personalPath_));
    std::wstring line;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        if (!line.empty()) personal_.insert(Fold(line));
    }
}

void Speller::AddToPersonal(const std::wstring& word) {
    if (word.empty()) return;

    personal_.insert(Fold(word));
    cache_.erase(word);

    // Read-modify-write: the list is short, and it keeps every write going
    // through the same UTF-8 path.
    std::wstring text = TextFile::Read(personalPath_);
    if (!text.empty() && text.back() != L'\n') text += L'\n';
    text += word;
    text += L'\n';
    TextFile::Write(personalPath_, text);
}

void Speller::IgnoreForSession(const std::wstring& word) {
    if (word.empty()) return;
    ignored_.insert(Fold(word));
    cache_.erase(word);
}
