// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include "ITranslator.h"

// Section 5. The user's own key; the F0 tier gives two million characters a
// month and returns 403/429 at the ceiling rather than billing for the excess.
class AzureTranslator : public ITranslator {
public:
    AzureTranslator(std::string key, std::string region)
        : key_(std::move(key)), region_(std::move(region)) {}

    TranslateResult Translate(Http::Client& http, const std::string& text,
                              const std::string& sourceLang,
                              const std::string& targetLang) override;

    const char* Name() const override { return "Azure Translator"; }
    bool Usable(std::string& whyNot) const override;

private:
    std::string key_;
    std::string region_;
};
