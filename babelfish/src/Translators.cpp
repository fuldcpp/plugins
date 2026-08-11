// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "ITranslator.h"

#include "AzureTranslator.h"
#include "ClaudeTranslator.h"
#include "DeepLTranslator.h"
#include "GoogleTranslator.h"
#include "MyMemoryTranslator.h"

bool BackendDetectsSource(const std::string& backend) {
    TranslatorConfig probe;
    probe.backend = backend;
    return MakeTranslator(probe)->DetectsSource();
}

std::unique_ptr<ITranslator> MakeTranslator(const TranslatorConfig& config) {
    if (config.backend == "azure") {
        return std::make_unique<AzureTranslator>(config.apiKey, config.azureRegion);
    }
    if (config.backend == "deepl") {
        return std::make_unique<DeepLTranslator>(config.apiKey);
    }
    if (config.backend == "google") {
        return std::make_unique<GoogleTranslator>(config.apiKey);
    }
    if (config.backend == "claude") {
        return std::make_unique<ClaudeTranslator>(config.apiKey);
    }
    // Anything unrecognised, including an empty setting on a clean install,
    // lands on the backend that works without a key.
    return std::make_unique<MyMemoryTranslator>(config.email);
}
