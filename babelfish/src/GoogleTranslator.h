// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include "ITranslator.h"

// Section 5. Requires a GCP project with billing enabled and the user's own
// API key. There is deliberately no key-free path here: the undocumented
// translate_a endpoint breaks Google's terms and is not implemented.
class GoogleTranslator : public ITranslator {
public:
    explicit GoogleTranslator(std::string key) : key_(std::move(key)) {}

    TranslateResult Translate(Http::Client& http, const std::string& text,
                              const std::string& sourceLang,
                              const std::string& targetLang) override;

    const char* Name() const override { return "Google Cloud Translation"; }
    bool Usable(std::string& whyNot) const override;

private:
    std::string key_;
};
