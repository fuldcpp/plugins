// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "AzureTranslator.h"

#include "Json.h"

bool AzureTranslator::Usable(std::string& whyNot) const {
    if (key_.empty()) {
        whyNot = "Azure needs your own subscription key (/tr key <key>)";
        return false;
    }
    return true;
}

TranslateResult AzureTranslator::Translate(Http::Client& http, const std::string& text,
                                           const std::string& sourceLang,
                                           const std::string& targetLang) {
    TranslateResult result;
    if (!Usable(result.error)) return result;

    std::string url = "https://api.cognitive.microsofttranslator.com/translate?api-version=3.0&to=";
    url += Http::Client::UrlEncode(targetLang);
    if (!sourceLang.empty()) {
        url += "&from=";
        url += Http::Client::UrlEncode(sourceLang);
    }

    std::vector<std::string> headers = {
        "Content-Type: application/json; charset=utf-8",
        "Ocp-Apim-Subscription-Key: " + key_,
    };
    // Section 5: the region header is mandatory for multi-service keys and is
    // the usual reason a key that looks right still returns 401.
    if (!region_.empty()) headers.push_back("Ocp-Apim-Subscription-Region: " + region_);

    // Both request and response are arrays at the top level.
    const std::string body = "[{\"Text\":\"" + Json::Escape(text) + "\"}]";

    const Http::Response response = http.Request(L"POST", url, headers, body);
    if (!response.ok) {
        result.error = response.error;
        return result;
    }

    if (response.status == 403 || response.status == 429) {
        result.quotaExceeded = true;
        result.error = "Azure quota reached";
        return result;
    }

    Json::Value root;
    if (!Json::Parse(response.body, root)) {
        result.error = "unreadable response";
        return result;
    }

    if (response.status != 200) {
        const std::string message = root.GetString("error", "message");
        result.error = message.empty() ? ("HTTP " + std::to_string(response.status)) : message;
        return result;
    }

    const Json::Value* first = root.At(0);
    const Json::Value* translations = first ? first->Find("translations") : nullptr;
    const Json::Value* translation = translations ? translations->At(0) : nullptr;
    if (!translation) {
        result.error = "no translation in response";
        return result;
    }

    result.text = translation->GetString("text");
    if (result.text.empty()) {
        result.error = "empty translation";
        return result;
    }

    result.ok = true;
    return result;
}
