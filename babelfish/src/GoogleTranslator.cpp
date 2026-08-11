// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "GoogleTranslator.h"

#include "Json.h"

#include <cstring>

namespace {

// Even with format=text the v2 endpoint escapes a few characters on the way
// back out. Undoing them is cheaper than the alternative of sending HTML.
std::string UnescapeHtml(const std::string& text) {
    struct Entity {
        const char* name;
        const char* value;
    };
    static const Entity kEntities[] = {
        {"&quot;", "\""}, {"&#39;", "'"},  {"&apos;", "'"}, {"&lt;", "<"},
        {"&gt;", ">"},    {"&nbsp;", " "}, {"&amp;", "&"},
    };

    std::string out = text;
    for (const Entity& entity : kEntities) {
        const std::string name = entity.name;
        size_t pos = 0;
        while ((pos = out.find(name, pos)) != std::string::npos) {
            out.replace(pos, name.size(), entity.value);
            pos += strlen(entity.value);
        }
    }
    return out;
}

}  // namespace

bool GoogleTranslator::Usable(std::string& whyNot) const {
    if (key_.empty()) {
        whyNot = "Google needs your own API key (/tr key <key>)";
        return false;
    }
    return true;
}

TranslateResult GoogleTranslator::Translate(Http::Client& http, const std::string& text,
                                            const std::string& sourceLang,
                                            const std::string& targetLang) {
    TranslateResult result;
    if (!Usable(result.error)) return result;

    const std::string url =
        "https://translation.googleapis.com/language/translate/v2?key=" +
        Http::Client::UrlEncode(key_);

    std::string body = "{\"q\":\"" + Json::Escape(text) + "\"";
    if (!sourceLang.empty()) body += ",\"source\":\"" + Json::Escape(sourceLang) + "\"";
    body += ",\"target\":\"" + Json::Escape(targetLang) + "\"";
    body += ",\"format\":\"text\"}";

    const std::vector<std::string> headers = {"Content-Type: application/json; charset=utf-8"};

    const Http::Response response = http.Request(L"POST", url, headers, body);
    if (!response.ok) {
        result.error = response.error;
        return result;
    }

    Json::Value root;
    if (!Json::Parse(response.body, root)) {
        result.error = "unreadable response";
        return result;
    }

    if (response.status != 200) {
        const std::string message = root.GetString("error", "message");
        // 403 covers both a disabled API and a spent daily cap, so the message
        // is the only thing that tells them apart.
        result.quotaExceeded = (response.status == 429) ||
                               (response.status == 403 &&
                                message.find("quota") != std::string::npos);
        result.error = message.empty() ? ("HTTP " + std::to_string(response.status)) : message;
        return result;
    }

    const Json::Value* data = root.Find("data");
    const Json::Value* translations = data ? data->Find("translations") : nullptr;
    const Json::Value* first = translations ? translations->At(0) : nullptr;
    if (!first) {
        result.error = "no translation in response";
        return result;
    }

    result.text = UnescapeHtml(first->GetString("translatedText"));
    if (result.text.empty()) {
        result.error = "empty translation";
        return result;
    }

    result.ok = true;
    return result;
}
