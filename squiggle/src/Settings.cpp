// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Settings.h"

#include "PluginDefs.h"
#include "TextFile.h"

#include <shlobj.h>

#include <cstdlib>
#include <cwchar>
#include <map>
#include <sstream>

namespace {

DCConfigPtr g_config = nullptr;
const char* g_guid = nullptr;
HINSTANCE g_instance = nullptr;
Settings g_settings;

constexpr const wchar_t* kSettingsFile = L"squiggle-settings.txt";

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

// Stands in for "the user ticked nothing", which an empty string cannot express
// because that is also what an unwritten setting reads back as. No language tag
// is a bare hyphen, so it cannot collide with a real value.
const wchar_t* const kNoLanguages = L"-";

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

// One "key=value" per line. No value here contains a newline: the language list
// is comma-joined and the rest are single words or numbers. Keys are ASCII, so
// they are narrowed to match the names the host config uses.
std::map<std::string, std::wstring> ParseFile(const std::wstring& text) {
    std::map<std::string, std::wstring> out;

    std::wstringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();

        const size_t equals = line.find(L'=');
        if (equals == std::wstring::npos || equals == 0) continue;

        std::string key;
        for (size_t i = 0; i < equals; ++i) {
            if (line[i] < 0x80) key += static_cast<char>(line[i]);
        }
        out[key] = line.substr(equals + 1);
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

void Settings::SetModule(void* instance) {
    g_instance = static_cast<HINSTANCE>(instance);
}

std::wstring Settings::SettingsDirectory() {
    wchar_t* base = nullptr;
    if (::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base) != S_OK || !base) {
        if (base) ::CoTaskMemFree(base);
        return DataDirectory();  // better the directory that empties than nowhere
    }

    std::wstring dir(base);
    ::CoTaskMemFree(base);
    dir += L"\\Squiggle\\";

    // CreateDirectory rather than a check first: it fails harmlessly when the
    // directory is already there, and the write that follows is the real test.
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring Settings::DataDirectory() {
    // GetModuleFileNameW rather than the host's get_install_path(guid): the
    // plugin API does not say who owns the ConfigStr that one returns, and both
    // name the same directory anyway.
    wchar_t path[MAX_PATH] = {};
    const DWORD len = ::GetModuleFileNameW(g_instance, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};

    std::wstring s(path, len);
    const size_t slash = s.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return s.substr(0, slash + 1);
}

void Settings::Load() {
    // The file wins where it has an answer; the host's config is consulted only
    // for the keys it does not carry. That is the path a fresh install takes,
    // and the one that carries the settings over from the releases that kept
    // them in the client alone.
    const std::wstring dir = SettingsDirectory();
    const std::map<std::string, std::wstring> file =
        dir.empty() ? std::map<std::string, std::wstring>()
                    : ParseFile(TextFile::Read(dir + kSettingsFile));

    auto value = [&](const char* key) -> std::wstring {
        const auto it = file.find(key);
        if (it != file.end()) return it->second;
        return Widen(ReadSetting(key));
    };

    // An empty stored value means "never configured", so the defaults stand. A
    // user who deliberately unticks every language stores the sentinel instead,
    // otherwise turning the plugin off that way silently undoes itself on the
    // next start.
    const std::wstring langs = value("Languages");
    if (langs == kNoLanguages) {
        languages.clear();
    } else if (!langs.empty()) {
        languages = SplitCsv(langs);
    }

    const std::wstring colourText = value("Colour");
    if (!colourText.empty()) {
        const unsigned long packed = std::wcstoul(colourText.c_str(), nullptr, 16);
        // Stored as RRGGBB for readability; COLORREF wants BGR.
        colour = RGB((packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF);
    }

    const std::wstring thicknessText = value("Thickness");
    if (!thicknessText.empty()) {
        const int width = static_cast<int>(std::wcstol(thicknessText.c_str(), nullptr, 10));
        if (width >= 1 && width <= 4) thickness = width;
    }

    const std::wstring autoText = value("AutoCorrect");
    if (!autoText.empty()) autoCorrect = (autoText != L"0");

    uiLanguage = value("UiLanguage");
}

void Settings::Save() const {
    // Deliberately-none is stored as the sentinel in both places, so unticking
    // every language survives a restart instead of falling back to the defaults.
    const std::wstring langs = languages.empty() ? std::wstring(kNoLanguages)
                                                 : JoinCsv(languages);

    char colourText[16] = {};
    ::wsprintfA(colourText, "%06X",
                (GetRValue(colour) << 16) | (GetGValue(colour) << 8) | GetBValue(colour));

    char thicknessText[16] = {};
    ::wsprintfA(thicknessText, "%d", thickness);

    // The file first: it is the copy that will still be there after the next
    // update of the plugin.
    const std::wstring dir = SettingsDirectory();
    if (!dir.empty()) {
        std::wstring out;
        out += L"Languages=" + langs + L"\n";
        out += L"Colour=" + Widen(colourText) + L"\n";
        out += L"Thickness=" + Widen(thicknessText) + L"\n";
        out += std::wstring(L"AutoCorrect=") + (autoCorrect ? L"1" : L"0") + L"\n";
        out += L"UiLanguage=" + uiLanguage + L"\n";
        TextFile::Write(dir + kSettingsFile, out);
    }

    // The host's config as well. It costs nothing, it is what answers on a
    // machine where the file has not been written yet, and it keeps the settings
    // visible to anyone reading the client's own configuration.
    WriteSetting("Languages", Narrow(langs));
    WriteSetting("Colour", colourText);
    WriteSetting("Thickness", thicknessText);
    WriteSetting("AutoCorrect", autoCorrect ? "1" : "0");
    WriteSetting("UiLanguage", Narrow(uiLanguage));
}
