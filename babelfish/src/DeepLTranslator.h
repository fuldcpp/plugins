// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include "ITranslator.h"

// Section 5. The best Swedish output of the five, and the user's own key. The
// host name is decided by the key itself: a key ending in ":fx" belongs to the
// free tier and only works against api-free.deepl.com.
class DeepLTranslator : public ITranslator {
public:
    explicit DeepLTranslator(std::string key) : key_(std::move(key)) {}

    TranslateResult Translate(Http::Client& http, const std::string& text,
                              const std::string& sourceLang,
                              const std::string& targetLang) override;

    const char* Name() const override { return "DeepL"; }
    bool Usable(std::string& whyNot) const override;

private:
    std::string key_;

    bool IsFreeKey() const;
};
