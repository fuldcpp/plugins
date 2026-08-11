// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include "ITranslator.h"

// Section 5. Haiku, because latency matters more than nuance for a chat line.
// The system prompt is what keeps the output usable: a model asked to translate
// will otherwise happily add "Here is the translation:" in front of it.
class ClaudeTranslator : public ITranslator {
public:
    explicit ClaudeTranslator(std::string key) : key_(std::move(key)) {}

    TranslateResult Translate(Http::Client& http, const std::string& text,
                              const std::string& sourceLang,
                              const std::string& targetLang) override;

    const char* Name() const override { return "Claude"; }
    bool Usable(std::string& whyNot) const override;

private:
    std::string key_;
};
