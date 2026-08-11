// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "MyMemoryTranslator.h"

#include "Json.h"

#include <cctype>

namespace {

bool MentionsQuota(const std::string& detail) {
    // The service reports an exhausted allowance in responseDetails with HTTP
    // 200, so the only way to find it is to read the text.
    static const char* const kMarkers[] = {"MYMEMORY WARNING", "USED ALL AVAILABLE FREE",
                                           "QUOTA", "DAILY LIMIT", "TOO MANY REQUESTS"};
    std::string upper;
    upper.reserve(detail.size());
    for (unsigned char c : detail) upper += static_cast<char>(std::toupper(c));

    for (const char* marker : kMarkers) {
        if (upper.find(marker) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TranslateResult MyMemoryTranslator::Translate(Http::Client& http, const std::string& text,
                                              const std::string& sourceLang,
                                              const std::string& targetLang) {
    TranslateResult result;

    // langpair needs both halves; the service has no autodetect, so an unknown
    // source is not something this backend can paper over.
    if (sourceLang.empty()) {
        result.error = "MyMemory needs a source language (/tr from <code>)";
        return result;
    }

    std::string url = "https://api.mymemory.translated.net/get?q=";
    url += Http::Client::UrlEncode(text);
    url += "&langpair=";
    url += Http::Client::UrlEncode(sourceLang + "|" + targetLang);
    if (!email_.empty()) {
        url += "&de=";
        url += Http::Client::UrlEncode(email_);
    }

    const Http::Response response = http.Request(L"GET", url, {}, {});
    if (!response.ok) {
        result.error = response.error;
        return result;
    }

    Json::Value root;
    if (!Json::Parse(response.body, root)) {
        result.error = "unreadable response";
        return result;
    }

    const std::string details = root.GetString("responseDetails");

    // Section 5: HTTP 200 with responseStatus set to something else happens,
    // and it is the field that actually decides whether this worked.
    const double status = root.GetNumber("responseStatus", static_cast<double>(response.status));
    if (status != 200) {
        result.quotaExceeded = MentionsQuota(details) || status == 429;
        result.error = details.empty() ? "translation refused" : details;
        return result;
    }

    if (MentionsQuota(details)) {
        result.quotaExceeded = true;
        result.error = details;
        return result;
    }

    const Json::Value* data = root.Find("responseData");
    if (!data) {
        result.error = "no translation in response";
        return result;
    }

    const std::string translated = data->GetString("translatedText");
    if (translated.empty()) {
        result.error = "empty translation";
        return result;
    }

    result.ok = true;
    result.text = translated;
    result.confidence = data->GetNumber("match", 1.0);
    return result;
}
