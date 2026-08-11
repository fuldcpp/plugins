// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Settings.h"

#include "PluginDefs.h"

#include <windows.h>
#include <shlobj.h>
#include <wincred.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {

DCConfigPtr g_config = nullptr;
const char* g_guid = nullptr;
HINSTANCE g_instance = nullptr;
Settings g_settings;

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

std::vector<std::string> SplitSemicolon(const std::string& s) {
    std::vector<std::string> out;
    std::string current;
    for (char c : s) {
        if (c == ';') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

std::string JoinSemicolon(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        if (!out.empty()) out += ';';
        out += item;
    }
    return out;
}

std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x80) c = static_cast<char>(std::tolower(u));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
//
// Settings live in %LOCALAPPDATA%\Babelfish\, outside everything the client
// owns. Three attempts got here, and the first two were each undone by the same
// measurement.
//
// The host's set_cfg/get_cfg do work -- an earlier note here said otherwise,
// having read an empty Plugins.xml belonging to a different client. The real one
// holds every setting the plugin ever saved.
//
// But installing a new version of a plugin destroys both of the obvious places.
// The install directory is emptied, taking the settings file and the cache; and
// the plugin's entry in Plugins.xml is removed and recreated with no <Setting>
// children at all. Neither is a place a plugin can keep anything across its own
// update, which is precisely when keeping things matters.
//
// So: the file here, the host config still mirrored on save because it costs
// nothing and answers when the file is new, and the API keys in Credential
// Manager, which is Windows' own and is not the client's to clear.

constexpr const wchar_t* kSettingsFile = L"babelfish-settings.txt";

// What the file was called while the plugin was called Translate.
//
// The install directory is keyed by guid rather than by name, so the old file
// is still sitting right next to where the new one goes -- and it holds an API
// key somebody typed in once and would otherwise have to find again.
constexpr const wchar_t* kFormerSettingsFile = L"translate-settings.txt";

std::string ReadWholeFile(const std::wstring& path) {
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};

    std::string contents;
    char buffer[4096];
    DWORD read = 0;
    while (::ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        contents.append(buffer, read);
    }
    ::CloseHandle(file);
    return contents;
}

// The API keys, in Windows Credential Manager, because it is the only place
// within reach that an install does not clear.
//
// Measured, twice, each time after being wrong. Installing a new version empties
// the plugin's own directory: the pre-rename DLL, the old settings file and the
// cache had all gone, and the plugin deletes none of them. The obvious answer
// was to mirror the keys into the host's config, encrypted, since everything
// else there survives a restart -- but the same install rewrites Plugins.xml
// with the plugin's entry recreated and every <Setting> gone. Both plugins lost
// every setting they had. So the second location was cleared by the same event
// as the first, and the encryption, which worked, protected nothing.
//
// Credential Manager belongs to Windows rather than to the client. It encrypts
// at rest against the logged-in account, survives anything the client does to
// its own files, and the user can see and delete what is stored there in a
// Windows dialog rather than having to take a plugin's word for it.
std::wstring CredentialName(const std::string& service) {
    std::wstring name = L"Babelfish/";
    for (char c : service) name += static_cast<wchar_t>(c);  // service names are ASCII
    return name;
}

void StoreKey(const std::string& service, const std::string& key) {
    const std::wstring target = CredentialName(service);

    if (key.empty()) {
        // A key the user cleared has to go, or it would come back at the next
        // install as a helpful resurrection of a deliberate deletion.
        ::CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
        return;
    }

    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(key.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(key.data()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;  // never roam a secret
    cred.UserName = const_cast<LPWSTR>(L"Babelfish");
    ::CredWriteW(&cred, 0);
}

std::string LoadKey(const std::string& service) {
    PCREDENTIALW cred = nullptr;
    if (!::CredReadW(CredentialName(service).c_str(), CRED_TYPE_GENERIC, 0, &cred)) return {};

    std::string key;
    if (cred->CredentialBlob && cred->CredentialBlobSize > 0) {
        key.assign(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
    }
    ::CredFree(cred);
    return key;
}

// MyMemory is not here: it never takes a key.
const char* const kKeyedServices[] = {"azure", "deepl", "google", "claude"};

// The settings, from wherever they are still to be found.
//
// Three places, newest first: the folder outside the client's reach, then the
// plugin's install directory where they used to live, then that same directory
// under the name they had before the plugin was renamed. Each fallback is read
// once and never written back to; the next save lands in the new place, and the
// old copies are left where they are rather than deleted, because a settings
// file nobody reads costs nothing and a deleted one cannot be recovered.
std::string ReadSettingsFile(const std::wstring& dir, const std::wstring& formerDir) {
    std::string contents = ReadWholeFile(dir + kSettingsFile);
    if (!contents.empty()) return contents;

    if (formerDir.empty() || formerDir == dir) return {};
    contents = ReadWholeFile(formerDir + kSettingsFile);
    if (contents.empty()) contents = ReadWholeFile(formerDir + kFormerSettingsFile);
    return contents;
}

// error comes back as the code that explains a false return. It is read before
// CloseHandle, which sets its own.
bool WriteWholeFile(const std::wstring& path, const std::string& contents,
                    DWORD* error = nullptr) {
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = ::GetLastError();
        return false;
    }

    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()),
                                &written, nullptr);
    const DWORD code = ok ? 0u : ::GetLastError();
    ::CloseHandle(file);
    if (error) *error = code;
    return ok && written == contents.size();
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), n,
                          nullptr, nullptr);
    return out;
}

// Removes any "ApiKey.<service>=..." line from a settings file in place, leaving
// the rest untouched. Used once, at load, to clear the cleartext keys an earlier
// version wrote after they have been moved into Credential Manager -- so no
// plaintext copy of a paid key is left on disk.
void StripApiKeyLines(const std::wstring& path) {
    const std::string contents = ReadWholeFile(path);
    if (contents.empty()) return;

    std::string cleaned;
    cleaned.reserve(contents.size());
    bool changed = false;
    size_t start = 0;
    while (start < contents.size()) {
        const size_t nl = contents.find('\n', start);
        const size_t end = (nl == std::string::npos) ? contents.size() : nl + 1;
        if (contents.compare(start, 7, "ApiKey.") == 0) {
            changed = true;  // drop this line
        } else {
            cleaned.append(contents, start, end - start);
        }
        start = end;
    }
    if (changed) WriteWholeFile(path, cleaned);
}

// Set once, by the first save that fails, and never cleared: a folder that was
// read-only a moment ago is read-only now, and one report is the useful number.
std::string g_writeFailure;

// One "key=value" per line. No value here ever contains a newline: the hub list
// is semicolon-joined and everything else is a word or a key.
std::map<std::string, std::string> ParseFile(const std::string& contents) {
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start <= contents.size()) {
        size_t end = contents.find('\n', start);
        const size_t stop = (end == std::string::npos) ? contents.size() : end;

        size_t lineEnd = stop;
        if (lineEnd > start && contents[lineEnd - 1] == '\r') --lineEnd;

        if (lineEnd > start) {
            const std::string line = contents.substr(start, lineEnd - start);
            const size_t equals = line.find('=');
            if (equals != std::string::npos && equals > 0) {
                out[line.substr(0, equals)] = line.substr(equals + 1);
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

// Which service a key belongs to, read off the key itself.
//
// Guessing from "whichever service is selected right now" put an Anthropic key
// under MyMemory, because the service had been switched before the upgrade ran.
// The key says who it belongs to far more reliably than the moment it was
// noticed does.
std::string ServiceForKey(const std::string& key) {
    auto begins = [&](const char* prefix) {
        const size_t n = std::strlen(prefix);
        return key.size() > n && key.compare(0, n, prefix) == 0;
    };

    if (begins("sk-ant-")) return "claude";
    if (begins("AIza")) return "google";
    if (key.size() > 3 && key.compare(key.size() - 3, 3, ":fx") == 0) return "deepl";
    return {};
}

std::string Today() {
    SYSTEMTIME now = {};
    ::GetLocalTime(&now);
    char buf[16] = {};
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u", now.wYear, now.wMonth, now.wDay);
    return buf;
}

}  // namespace

Settings& CurrentSettings() {
    return g_settings;
}

std::string Settings::ApiKeyFor(const std::string& service) const {
    const auto it = apiKeys.find(service);
    return it == apiKeys.end() ? std::string() : it->second;
}

void Settings::SetApiKeyFor(const std::string& service, const std::string& key) {
    if (key.empty()) {
        apiKeys.erase(service);
    } else {
        apiKeys[service] = key;
    }
}

void Settings::Bind(void* config, const char* guid) {
    g_config = static_cast<DCConfigPtr>(config);
    g_guid = guid;
}

void Settings::SetModule(void* instance) {
    g_instance = static_cast<HINSTANCE>(instance);
}

void Settings::Load() {
    const std::wstring dir = SettingsDirectory();
    const std::map<std::string, std::string> file =
        dir.empty() ? std::map<std::string, std::string>()
                    : ParseFile(ReadSettingsFile(dir, DataDirectory()));

    // The file wins where it has an answer; the host config is consulted only
    // for keys it does not carry, so nothing is lost for anyone whose client
    // does persist them.
    auto value = [&](const char* key) -> std::string {
        const auto it = file.find(key);
        if (it != file.end()) return it->second;
        return ReadSetting(key);
    };

    const std::string storedBackend = Lower(value("Backend"));
    if (!storedBackend.empty()) backend = storedBackend;

    sourceLang = Lower(value("SourceLang"));
    email = value("Email");
    azureRegion = value("AzureRegion");

    // Keys live in Credential Manager, which is the store read here. A file entry
    // is only ever a legacy cleartext copy from before the fix; see below.
    apiKeys.clear();
    for (const char* service : kKeyedServices) {
        const std::string recovered = LoadKey(service);
        if (!recovered.empty()) apiKeys[service] = recovered;
    }

    // One-time migration. An earlier version wrote "ApiKey.<service>=..." into the
    // settings file in cleartext. Import any still present (a file copy is the more
    // recent thing the user typed, so it wins), move them into Credential Manager,
    // and strip them from every settings file so no plaintext key is left on disk.
    bool foundPlaintextKey = false;
    const std::string prefix = "ApiKey.";
    for (const auto& entry : file) {
        if (entry.first.compare(0, prefix.size(), prefix) == 0 &&
            entry.first.size() > prefix.size() && !entry.second.empty()) {
            apiKeys[entry.first.substr(prefix.size())] = entry.second;
            foundPlaintextKey = true;
        }
    }
    if (foundPlaintextKey) {
        for (const auto& kv : apiKeys) StoreKey(kv.first, kv.second);
        const std::wstring former = DataDirectory();
        if (!dir.empty()) StripApiKeyLines(dir + kSettingsFile);
        if (!former.empty() && former != dir) {
            StripApiKeyLines(former + kSettingsFile);
            StripApiKeyLines(former + kFormerSettingsFile);
        }
    }

    const std::string legacy = value("ApiKey");
    if (!legacy.empty()) {
        // The key's own shape first; the selected service only as a fallback,
        // and never MyMemory, which needs no key at all.
        std::string owner = ServiceForKey(legacy);
        if (owner.empty() && backend != "mymemory") owner = backend;
        if (!owner.empty() && apiKeys.find(owner) == apiKeys.end()) {
            apiKeys[owner] = legacy;
        }
    }

    // MyMemory works without an account and is never given a key, so anything
    // filed under it arrived by mistake. Move it to whichever service the key
    // actually belongs to, and drop it if that cannot be told.
    const auto stray = apiKeys.find("mymemory");
    if (stray != apiKeys.end()) {
        const std::string owner = ServiceForKey(stray->second);
        if (!owner.empty() && apiKeys.find(owner) == apiKeys.end()) {
            apiKeys[owner] = stray->second;
        }
        apiKeys.erase(stray);
    }
    autoHubs = SplitSemicolon(value("AutoHubs"));
    keepWords = SplitSemicolon(value("KeepWords"));

    const std::string pm = value("TranslatePM");
    if (!pm.empty()) translatePM = (pm != "0");

    const std::string echo = value("EchoOriginal");
    if (!echo.empty()) echoOriginal = (echo != "0");

    const std::string limit = value("DailyLimit");
    if (!limit.empty()) dailyLimitChars = std::strtoll(limit.c_str(), nullptr, 10);

    quotaDate = value("QuotaDate");
    const std::string chars = value("QuotaChars");
    quotaChars = chars.empty() ? 0 : std::strtoll(chars.c_str(), nullptr, 10);
    if (quotaDate != Today()) {
        quotaDate = Today();
        quotaChars = 0;
    }
}

void Settings::Save() const {
    // The file first: it is the copy that will actually be there next time.
    const std::wstring dir = SettingsDirectory();
    if (!dir.empty()) {
        char chars[32] = {};
        snprintf(chars, sizeof(chars), "%lld", quotaChars);

        std::string out;
        out += "Backend=" + backend + "\n";
        out += "SourceLang=" + sourceLang + "\n";
        out += "Email=" + email + "\n";
        out += "AzureRegion=" + azureRegion + "\n";
        // API keys are deliberately NOT written here. They live only in Windows
        // Credential Manager (StoreKey, below), which encrypts them per-account.
        // Writing them to this file would leave a paid key in cleartext on disk,
        // recoverable by any backup, sync tool or other process -- and an earlier
        // version did exactly that. Load() migrates any such legacy plaintext key
        // into Credential Manager and it disappears from the file on this save.
        out += "AutoHubs=" + JoinSemicolon(autoHubs) + "\n";
        out += "KeepWords=" + JoinSemicolon(keepWords) + "\n";
        out += std::string("TranslatePM=") + (translatePM ? "1" : "0") + "\n";
        out += std::string("EchoOriginal=") + (echoOriginal ? "1" : "0") + "\n";
        char limit[32] = {};
        snprintf(limit, sizeof(limit), "%lld", dailyLimitChars);
        out += std::string("DailyLimit=") + limit + "\n";
        out += "QuotaDate=" + quotaDate + "\n";
        out += std::string("QuotaChars=") + chars + "\n";
        DWORD error = 0;
        if (!WriteWholeFile(dir + kSettingsFile, out, &error) && g_writeFailure.empty()) {
            char code[32] = {};
            snprintf(code, sizeof(code), " (Windows error %lu)", error);
            g_writeFailure = Narrow(dir + kSettingsFile) + code;
        }
    }

    WriteSetting("Backend", backend);
    WriteSetting("SourceLang", sourceLang);
    WriteSetting("Email", email);
    WriteSetting("AzureRegion", azureRegion);
    // Written every time rather than only on change: a service whose key was
    // cleared has to lose its stored copy too, or the next install would bring
    // back a key the user deliberately removed.
    for (const char* service : kKeyedServices) {
        const auto it = apiKeys.find(service);
        StoreKey(service, it == apiKeys.end() ? std::string() : it->second);
    }

    WriteSetting("AutoHubs", JoinSemicolon(autoHubs));
    WriteSetting("KeepWords", JoinSemicolon(keepWords));
    WriteSetting("TranslatePM", translatePM ? "1" : "0");
    WriteSetting("EchoOriginal", echoOriginal ? "1" : "0");
    WriteSetting("QuotaDate", quotaDate);

    char buf[32] = {};
    snprintf(buf, sizeof(buf), "%lld", quotaChars);
    WriteSetting("QuotaChars", buf);
}

bool Settings::IsAutoHub(const std::string& hubUrl) const {
    const std::string needle = Lower(hubUrl);
    for (const std::string& hub : autoHubs) {
        if (Lower(hub) == needle) return true;
    }
    return false;
}

void Settings::SetAutoHub(const std::string& hubUrl, bool on) {
    const std::string needle = Lower(hubUrl);
    auto it = std::find_if(autoHubs.begin(), autoHubs.end(),
                           [&](const std::string& hub) { return Lower(hub) == needle; });

    if (on) {
        if (it == autoHubs.end() && !hubUrl.empty()) autoHubs.push_back(hubUrl);
    } else if (it != autoHubs.end()) {
        autoHubs.erase(it);
    }
    Save();
}

long long Settings::AddQuotaChars(size_t count) {
    const std::string today = Today();
    if (quotaDate != today) {
        quotaDate = today;
        quotaChars = 0;
    }
    quotaChars += static_cast<long long>(count);
    Save();
    return quotaChars;
}

std::string Settings::HostLanguage() {
    if (!g_config || !g_config->get_language) return {};

    ConfigStrPtr tag = g_config->get_language();
    if (!tag) return {};

    const std::string out = tag->value ? tag->value : "";
    g_config->release(reinterpret_cast<ConfigValuePtr>(tag));
    return out;
}

bool Settings::AddKeepWord(const std::string& word) {
    // Three bytes, matching what the masking will actually act on. A shorter
    // word would be accepted here and quietly ignored there.
    if (word.size() < 3) return false;

    for (const std::string& have : keepWords) {
        if (have.size() == word.size() && _stricmp(have.c_str(), word.c_str()) == 0) return true;
    }
    keepWords.push_back(word);
    return true;
}

bool Settings::RemoveKeepWord(const std::string& word) {
    for (auto it = keepWords.begin(); it != keepWords.end(); ++it) {
        if (it->size() == word.size() && _stricmp(it->c_str(), word.c_str()) == 0) {
            keepWords.erase(it);
            return true;
        }
    }
    return false;
}

std::string Settings::WriteFailure() { return g_writeFailure; }

std::string Settings::SettingsDirectoryUtf8() { return Narrow(SettingsDirectory()); }

std::wstring Settings::SettingsDirectory() {
    wchar_t* base = nullptr;
    if (::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base) != S_OK || !base) {
        if (base) ::CoTaskMemFree(base);
        return DataDirectory();  // better the folder that empties than nowhere
    }

    std::wstring dir(base);
    ::CoTaskMemFree(base);
    dir += L"\\Babelfish\\";

    // CreateDirectory rather than a check first: it fails harmlessly when the
    // folder is already there, and the write that follows is the real test.
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring Settings::DataDirectory() {
    // The directory holding this DLL, which is the per-plugin install directory
    // the host created for us -- so the cache lands exactly where it should.
    //
    // The obvious alternative, get_install_path(guid), is deliberately not used.
    // PluginDefs.h does not say who owns the ConfigStr it returns, and calling
    // release() on a pointer the host still owns frees memory out from under the
    // plugin manager: the damage surfaces seconds later, somewhere else, as heap
    // corruption. That is exactly what FulDC++ died of on the second live run.
    // Squiggle has always taken the module-handle route and has never had the
    // problem, so this does too. Nothing is lost -- both name the same folder.
    wchar_t module[MAX_PATH] = {};
    const DWORD len = ::GetModuleFileNameW(g_instance, module, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};

    std::wstring s(module, len);
    const size_t slash = s.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return s.substr(0, slash + 1);
}
