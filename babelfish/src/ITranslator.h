// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <memory>
#include <string>

#include "Http.h"

// Everything a backend needs to authenticate, snapshotted on the GUI thread
// when the job is queued. The worker never touches the live Settings object,
// which is what keeps /tr from <code> safe to run while a job is in flight.
struct TranslatorConfig {
    std::string backend = "mymemory";
    std::string apiKey;
    std::string azureRegion;
    std::string email;
};

// Section 5. The spec's interface returns an optional string; this carries a
// little more, because sections 5 and 7.6 both require telling a quota refusal
// apart from an ordinary failure -- one has to be reported to the user, the
// other is just a silent fall back to the original text.
struct TranslateResult {
    bool ok = false;
    std::string text;          // the translation, valid only when ok
    std::string error;         // short human-readable reason, empty when ok
    bool quotaExceeded = false;
    double confidence = 1.0;   // MyMemory's match score; 1.0 when not reported
};

struct ITranslator {
    virtual ~ITranslator() = default;

    // Translates one piece. sourceLang may be empty to let the service detect
    // it; targetLang is always "en" in this plugin.
    virtual TranslateResult Translate(Http::Client& http, const std::string& text,
                                      const std::string& sourceLang,
                                      const std::string& targetLang) = 0;

    virtual const char* Name() const = 0;

    // Largest request this backend accepts, in UTF-8 bytes. Zero means the
    // limit is high enough not to matter for chat.
    virtual size_t MaxRequestBytes() const { return 0; }

    // True when the service works out the source language itself, given none.
    //
    // Everything here can except MyMemory, whose langpair needs both halves.
    // Letting the service decide removes the one setting a user can have wrong:
    // Polish typed while the plugin still believes in Swedish comes back
    // untranslated, and nothing about that is obvious from the outside.
    virtual bool DetectsSource() const { return true; }

    // False when the backend cannot work with what is configured, with the
    // reason in whyNot. Checked before queueing so the user is told at once
    // instead of after a failed request.
    virtual bool Usable(std::string& whyNot) const {
        (void)whyNot;
        return true;
    }
};

// Builds the backend named in config. Never returns null: an unknown name falls
// back to MyMemory, which is the one that works with no configuration at all.
std::unique_ptr<ITranslator> MakeTranslator(const TranslatorConfig& config);

// The same question without building one, for the code that decides what to put
// in a job before the worker picks it up.
bool BackendDetectsSource(const std::string& backend);
