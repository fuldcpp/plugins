// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Settings.h"

#include "PluginDefs.h"

#include <cstdlib>
#include <sstream>

namespace {

DCConfigPtr g_config = nullptr;
const char* g_guid = nullptr;
Settings g_settings;

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

void WriteSetting(const char* key, const std::string& value) {
    if (!g_config || !g_guid) return;
    ConfigStr cfg{CFG_TYPE_STRING, value.c_str()};
    g_config->set_cfg(g_guid, key, reinterpret_cast<ConfigValuePtr>(&cfg));
}

std::string ReadSetting(const char* key) {
    if (!g_config || !g_guid) return {};

    ConfigValuePtr raw = g_config->get_cfg(g_guid, key, CFG_TYPE_STRING);
    if (!raw) return {};

    std::string out;
    if (raw->type == CFG_TYPE_STRING) {
        const ConfigStrPtr str = reinterpret_cast<ConfigStrPtr>(raw);
        if (str->value) out = str->value;
    }
    g_config->release(raw);
    return out;
}

std::vector<std::wstring> SplitCsv(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstringstream stream(s);
    std::wstring item;
    while (std::getline(stream, item, L',')) {
        while (!item.empty() && item.front() == L' ') item.erase(item.begin());
        while (!item.empty() && item.back() == L' ') item.pop_back();
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::wstring JoinCsv(const std::vector<std::wstring>& items) {
    std::wstring out;
    for (const std::wstring& item : items) {
        if (!out.empty()) out += L',';
        out += item;
    }
    return out;
}

}  // namespace

Settings& CurrentSettings() {
    return g_settings;
}

void Settings::Bind(void* config, const char* guid) {
    g_config = static_cast<DCConfigPtr>(config);
    g_guid = guid;
}

void Settings::Load() {
    const std::wstring langs = Widen(ReadSetting("Languages"));
    if (!langs.empty()) languages = SplitCsv(langs);

    const std::string colourText = ReadSetting("Colour");
    if (!colourText.empty()) {
        const unsigned long packed = std::strtoul(colourText.c_str(), nullptr, 16);
        // Stored as RRGGBB for readability; COLORREF wants BGR.
        colour = RGB((packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF);
    }

    const std::string thicknessText = ReadSetting("Thickness");
    if (!thicknessText.empty()) {
        const int value = std::atoi(thicknessText.c_str());
        if (value >= 1 && value <= 4) thickness = value;
    }

    const std::string autoText = ReadSetting("AutoCorrect");
    if (!autoText.empty()) autoCorrect = (autoText != "0");

    uiLanguage = Widen(ReadSetting("UiLanguage"));
}

void Settings::Save() const {
    WriteSetting("Languages", Narrow(JoinCsv(languages)));

    char buf[16] = {};
    ::wsprintfA(buf, "%06X",
                (GetRValue(colour) << 16) | (GetGValue(colour) << 8) | GetBValue(colour));
    WriteSetting("Colour", buf);

    ::wsprintfA(buf, "%d", thickness);
    WriteSetting("Thickness", buf);

    WriteSetting("AutoCorrect", autoCorrect ? "1" : "0");
    WriteSetting("UiLanguage", Narrow(uiLanguage));
}
