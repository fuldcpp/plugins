// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "DeepLTranslator.h"

#include "Json.h"

#include <cctype>

namespace {

std::string Upper(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) out += static_cast<char>(std::toupper(c));
    return out;
}

// DeepL rejects a bare "EN" as a target and wants the variant spelled out.
std::string TargetCode(const std::string& target) {
    const std::string upper = Upper(target);
    if (upper == "EN") return "EN-GB";
    if (upper == "PT") return "PT-PT";
    return upper;
}

}  // namespace

bool DeepLTranslator::IsFreeKey() const {
    return key_.size() > 3 && key_.compare(key_.size() - 3, 3, ":fx") == 0;
}

bool DeepLTranslator::Usable(std::string& whyNot) const {
    if (key_.empty()) {
        whyNot = "DeepL needs your own auth key (/tr key <key>)";
        return false;
    }
    return true;
}

TranslateResult DeepLTranslator::Translate(Http::Client& http, const std::string& text,
                                           const std::string& sourceLang,
                                           const std::string& targetLang) {
    TranslateResult result;
    if (!Usable(result.error)) return result;

    const std::string url = IsFreeKey() ? "https://api-free.deepl.com/v2/translate"
                                        : "https://api.deepl.com/v2/translate";

    std::string body = "text=" + Http::Client::UrlEncode(text);
    body += "&target_lang=" + Http::Client::UrlEncode(TargetCode(targetLang));
    if (!sourceLang.empty()) {
        body += "&source_lang=" + Http::Client::UrlEncode(Upper(sourceLang));
    }

    const std::vector<std::string> headers = {
        "Content-Type: application/x-www-form-urlencoded",
        // The key goes in the header rather than the form body, which is the
        // shape DeepL documents today. auth_key in the body still works, but
        // there is no reason to put a credential in a request body.
        "Authorization: DeepL-Auth-Key " + key_,
    };

    const Http::Response response = http.Request(L"POST", url, headers, body);
    if (!response.ok) {
        result.error = response.error;
        return result;
    }

    // 456 is DeepL's own "character limit reached"; 429 is rate limiting.
    if (response.status == 456 || response.status == 429) {
        result.quotaExceeded = true;
        result.error = (response.status == 456) ? "DeepL character limit reached"
                                                : "DeepL rate limit reached";
        return result;
    }

    if (response.status == 403) {
        result.error = "DeepL rejected the key";
        return result;
    }

    Json::Value root;
    if (!Json::Parse(response.body, root)) {
        result.error = "unreadable response";
        return result;
    }

    if (response.status != 200) {
        const std::string message = root.GetString("message");
        result.error = message.empty() ? ("HTTP " + std::to_string(response.status)) : message;
        return result;
    }

    const Json::Value* translations = root.Find("translations");
    const Json::Value* first = translations ? translations->At(0) : nullptr;
    if (!first) {
        result.error = "no translation in response";
        return result;
    }

    result.text = first->GetString("text");
    if (result.text.empty()) {
        result.error = "empty translation";
        return result;
    }

    result.ok = true;
    return result;
}
