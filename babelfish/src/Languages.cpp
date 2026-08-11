// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Languages.h"

#include <windows.h>

#include <algorithm>
#include <cctype>

namespace Languages {
namespace {

struct Entry {
    const char* code;
    const char* name;
};

// Deliberately not the full ISO-639-1 list. These are the codes every backend
// in section 5 accepts, which is what the prefix has to validate against: a
// code accepted here but rejected by the service turns into a failed request
// and an untranslated message, which is worse than not recognising the prefix.
constexpr Entry kTable[] = {
    {"ar", "Arabic"},    {"bg", "Bulgarian"},  {"cs", "Czech"},      {"da", "Danish"},
    {"de", "German"},    {"el", "Greek"},      {"en", "English"},    {"es", "Spanish"},
    {"et", "Estonian"},  {"fa", "Persian"},    {"fi", "Finnish"},    {"fr", "French"},
    {"he", "Hebrew"},    {"hi", "Hindi"},      {"hr", "Croatian"},   {"hu", "Hungarian"},
    {"id", "Indonesian"},{"is", "Icelandic"},  {"it", "Italian"},    {"ja", "Japanese"},
    {"ko", "Korean"},    {"lt", "Lithuanian"}, {"lv", "Latvian"},    {"nl", "Dutch"},
    {"no", "Norwegian"}, {"pl", "Polish"},     {"pt", "Portuguese"}, {"ro", "Romanian"},
    {"ru", "Russian"},   {"sk", "Slovak"},     {"sl", "Slovenian"},  {"sr", "Serbian"},
    {"sv", "Swedish"},   {"th", "Thai"},       {"tr", "Turkish"},    {"uk", "Ukrainian"},
    {"vi", "Vietnamese"},{"zh", "Chinese"},
};

std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

bool IsKnown(const std::string& code) {
    const std::string lower = Lower(code);
    for (const Entry& entry : kTable) {
        if (lower == entry.code) return true;
    }
    return false;
}

const char* NameOf(const std::string& code) {
    const std::string lower = Lower(code);
    for (const Entry& entry : kTable) {
        if (lower == entry.code) return entry.name;
    }
    return "";
}

std::string FromLocaleName(const std::string& localeName) {
    std::string out;
    for (char c : localeName) {
        if (c == '-' || c == '_') break;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // "nb"/"nn" are the real Norwegian tags but every service here wants "no".
    if (out == "nb" || out == "nn") out = "no";
    return IsKnown(out) ? out : std::string();
}

std::wstring DisplayName(const std::string& code, bool inEnglish) {
    std::wstring wide;
    for (char c : code) wide += static_cast<wchar_t>(c);

    // SENGLISHLANGUAGENAME is always English; SLOCALIZEDLANGUAGENAME is in the
    // Windows interface language. Either way Windows supplies the names, which
    // is why there is no table of translated language names in this project.
    wchar_t buffer[128] = {};
    const LCTYPE what = inEnglish ? LOCALE_SENGLISHLANGUAGENAME : LOCALE_SLOCALIZEDLANGUAGENAME;
    if (::GetLocaleInfoEx(wide.c_str(), what, buffer,
                          static_cast<int>(std::size(buffer))) > 0) {
        if (buffer[0] != L'\0') return buffer;
    }

    const char* english = NameOf(code);
    if (english && *english) {
        std::wstring out;
        for (const char* p = english; *p; ++p) out += static_cast<wchar_t>(*p);
        return out;
    }
    return wide;
}

std::vector<Choice> Choices(bool inEnglish) {
    std::vector<Choice> out;
    for (const Entry& entry : kTable) {
        if (std::string(entry.code) == kTarget) continue;
        out.push_back(Choice{entry.code, DisplayName(entry.code, inEnglish)});
    }
    std::sort(out.begin(), out.end(),
              [](const Choice& a, const Choice& b) { return a.name < b.name; });
    return out;
}

std::string DetectSource() {
    wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
    if (::GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) == 0) return {};

    std::string narrow;
    for (const wchar_t* p = buf; *p; ++p) {
        if (*p > 127) return {};  // locale names are ASCII; anything else is junk
        narrow += static_cast<char>(*p);
    }
    return FromLocaleName(narrow);
}

}  // namespace Languages
