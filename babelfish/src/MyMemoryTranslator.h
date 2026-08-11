// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include "ITranslator.h"

// Section 5, the default. Needs no account and no key, which is what makes a
// clean install translate straight away. An optional e-mail address raises the
// daily allowance from 5 000 to 50 000 characters.
class MyMemoryTranslator : public ITranslator {
public:
    explicit MyMemoryTranslator(std::string email) : email_(std::move(email)) {}

    TranslateResult Translate(Http::Client& http, const std::string& text,
                              const std::string& sourceLang,
                              const std::string& targetLang) override;

    const char* Name() const override { return "MyMemory"; }

    // The documented cap on the q parameter.
    size_t MaxRequestBytes() const override { return 500; }

    // langpair carries both halves and there is no autodetect endpoint.
    bool DetectsSource() const override { return false; }

private:
    std::string email_;
};
