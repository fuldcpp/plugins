// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "ClaudeTranslator.h"

#include "Json.h"
#include "Languages.h"

namespace {

constexpr const char* kModel = "claude-haiku-4-5-20251001";
constexpr const char* kApiVersion = "2023-06-01";

// A chat line is short, and the translation of a short line is short. Capping
// this is also the cheapest guard against a model that decides to explain
// itself at length.
constexpr int kMaxTokens = 1024;

// Section 5. The glossary matters more than it looks: a general-purpose model
// turns "slots" into something about calendars and "share" into a verb, both of
// which read as nonsense to the person on the other end.
std::string BuildSystemPrompt(const std::string& sourceLang, const std::string& targetLang) {
    std::string prompt =
        "You translate one chat message at a time for a Direct Connect file-sharing client.\n"
        "Return ONLY the translation. No preamble, no explanation, no surrounding quotes, "
        "no notes about what you did.\n"
        "Keep the tone and register of the original, including slang, brevity and profanity. "
        "A terse message stays terse.\n"
        "Leave nicknames, file names, URLs, magnet links and TTH hashes exactly as they are.\n"
        "Preserve line breaks.\n"
        "If the text is already in the target language, return it unchanged.\n"
        "Direct Connect terms, to be kept as the English words they are: share (a user's "
        "shared files), hub (a server), slots (upload slots), filelist (a user's file "
        "listing), op (operator), TTH (a file hash), magnet (a download link).\n"
        // "kesfil" came back as "kesfile": the compound was not recognised, so it
        // was split, "kes" was dropped and "fil" was read as the computing sense
        // of the word. Naming the failure is what this line is for -- an
        // untranslatable word is not a licence to build a plausible-looking one.
        "A food, place, brand or cultural term with no equivalent in the target language "
        "stays in its original spelling. Never translate such a word piece by piece, and "
        "never invent a compound for it.\n";

    const char* sourceName = Languages::NameOf(sourceLang);
    const char* targetName = Languages::NameOf(targetLang);
    prompt += "Translate from ";
    prompt += (sourceName && *sourceName) ? sourceName : "the language of the message";
    prompt += " into ";
    prompt += (targetName && *targetName) ? targetName : "English";
    prompt += ".";
    return prompt;
}

}  // namespace

bool ClaudeTranslator::Usable(std::string& whyNot) const {
    if (key_.empty()) {
        whyNot = "Claude needs your own Anthropic API key (/tr key <key>)";
        return false;
    }
    return true;
}

TranslateResult ClaudeTranslator::Translate(Http::Client& http, const std::string& text,
                                            const std::string& sourceLang,
                                            const std::string& targetLang) {
    TranslateResult result;
    if (!Usable(result.error)) return result;

    std::string body = "{\"model\":\"";
    body += kModel;
    body += "\",\"max_tokens\":";
    body += std::to_string(kMaxTokens);
    body += ",\"system\":\"";
    body += Json::Escape(BuildSystemPrompt(sourceLang, targetLang));
    body += "\",\"messages\":[{\"role\":\"user\",\"content\":\"";
    body += Json::Escape(text);
    body += "\"}]}";

    const std::vector<std::string> headers = {
        "Content-Type: application/json",
        "x-api-key: " + key_,
        std::string("anthropic-version: ") + kApiVersion,
    };

    const Http::Response response = http.Request(L"POST", "https://api.anthropic.com/v1/messages",
                                                 headers, body);
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
        result.quotaExceeded = (response.status == 429);
        result.error = message.empty() ? ("HTTP " + std::to_string(response.status)) : message;
        return result;
    }

    // content is a list of blocks; the first text block is the answer.
    const Json::Value* content = root.Find("content");
    if (!content || !content->IsArray()) {
        result.error = "no translation in response";
        return result;
    }

    for (size_t i = 0; i < content->array.size(); ++i) {
        const Json::Value& block = content->array[i];
        if (block.GetString("type") == "text") {
            result.text = block.GetString("text");
            break;
        }
    }

    if (result.text.empty()) {
        result.error = "empty translation";
        return result;
    }

    result.ok = true;
    return result;
}
