// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "PluginDefs.h"

#include <windows.h>

#include <atomic>
#include <cwctype>
#include <string>
#include <vector>

#include "AutoCorrect.h"
#include "ChatInput.h"
#include "Nicks.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "Speller.h"
#include "Strings.h"

namespace {

// Identity. The guid must stay fixed forever: the host keys settings, the
// install directory and update detection off it.
const char* const kGuid = "{8f3a7c21-5d94-4e0b-9a6f-2c1b8e7d4a35}";
const char* const kName = "Squiggle";
const char* const kAuthor = "kaje";
const char* const kDescriptionSv =
    "Stavningskontroll i chatten: rod vaglinje under felstavade ord medan du "
    "skriver, hogerklick ger forslag. Flera sprak samtidigt.";
const char* const kDescriptionEn =
    "Spell checking in chat: a red wavy underline under misspelled words as you "
    "type, right-click for suggestions. Several languages at once.";
const char* const kWeb = "https://github.com/kaje-home/Squiggle";

HINSTANCE g_instance = nullptr;
DCHooksPtr g_hooks = nullptr;
DCLogPtr g_log = nullptr;
DCConfigPtr g_config = nullptr;
subsHandle g_timerSub = nullptr;
subsHandle g_uiSub = nullptr;
Speller g_speller;

// Written from the host's timer thread as well as at load time.
std::atomic<bool> g_installed{false};

void Log(const std::string& message) {
    if (g_log && g_log->log) g_log->log(("Squiggle: " + message).c_str());
}

// Word lists live next to the plugin binary, inside the per-plugin install
// directory the host already creates for us.
std::wstring PluginFilePath(const wchar_t* name) {
    wchar_t path[MAX_PATH] = {};
    const DWORD len = ::GetModuleFileNameW(g_instance, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};

    std::wstring s(path, len);
    const size_t slash = s.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return s.substr(0, slash + 1) + name;
}

// Renames a data file from an older release, but never over a file that already
// exists, so an upgrade cannot clobber current content.
void MigrateFile(const wchar_t* oldName, const wchar_t* newName) {
    const std::wstring from = PluginFilePath(oldName);
    const std::wstring to = PluginFilePath(newName);
    if (from.empty() || to.empty()) return;

    if (::GetFileAttributesW(from.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (::GetFileAttributesW(to.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    ::MoveFileW(from.c_str(), to.c_str());
}

// DCConfig::get_language is not part of the plugin API every DCAPI 8 host was
// built against. FulDC++ has it; stock DC++ and its other forks do not, and both
// report DCINTF_CONFIG_VER 1, so the version check cannot tell them apart.
// Reading the slot on a host that lacks it lands on whatever field follows in
// that host's own struct -- a real, non-null function pointer -- and calling it
// jumps into unrelated code with a garbage argument.
//
// Nothing in the interface distinguishes them, so the host is identified by its
// own module instead. Anything unrecognised simply never touches the field and
// falls back to the Windows UI language, which is what stock DC++ users would
// want anyway; the explicit choice in the settings dialog overrides either way.
bool HostHasLanguageQuery() {
    wchar_t path[MAX_PATH] = {};
    const DWORD len = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;

    std::wstring exe(path, len);
    const size_t slash = exe.find_last_of(L"\\/");
    if (slash != std::wstring::npos) exe.erase(0, slash + 1);
    for (wchar_t& c : exe) c = static_cast<wchar_t>(::towlower(static_cast<wint_t>(c)));

    // FulDC++ and the AirDC++ tree it is built from; both carry get_language.
    return exe.rfind(L"fuldc", 0) == 0 || exe.rfind(L"airdc", 0) == 0;
}

// Asks the host what language it is running in, then lets any explicit user
// choice win over that.
void ApplyInterfaceLanguage() {
    if (g_config && HostHasLanguageQuery() && g_config->get_language) {
        ConfigStrPtr tag = g_config->get_language();
        Strings::DetectLanguage(tag ? tag->value : nullptr);
        if (tag && g_config->release) {
            g_config->release(reinterpret_cast<ConfigValuePtr>(tag));
        }
    } else {
        Strings::DetectLanguage(nullptr);
    }

    Strings::SetLanguageOverride(CurrentSettings().uiLanguage);
}

void LogActiveLanguages() {
    std::string langs;
    for (const std::wstring& tag : g_speller.ActiveLanguages()) {
        if (!langs.empty()) langs += ", ";
        // Language tags are plain ASCII, so a narrowing copy is safe here.
        for (wchar_t c : tag) langs += static_cast<char>(c);
    }
    Log(langs.empty() ? "inga sprak aktiva" : "aktiva sprak: " + langs);
}

// Picks up a language installed in Windows after the client started. Runs on the
// GUI thread, posted there from the timer hook.
void RefreshLanguagesOnGui() {
    if (g_speller.MissingLanguages().empty()) return;
    if (!g_speller.RefreshLanguages()) return;

    LogActiveLanguages();
    ChatInput::InvalidateAll();
}

// Discovery has to be retried: at ON_LOAD the host's windows do not exist yet,
// and hub frames appear later as the user connects. The language check is
// retried too, so a language installed in Windows while the client is running
// starts working without a restart.
//
// This fires on the host's TimerManager thread, not the GUI thread. Nothing here
// may touch the speller, a subclassed control or its context directly: the
// speller has no locking, and its checkers are apartment-threaded COM objects
// that belong to the GUI thread. Installing the hook is the one exception, and
// it deliberately does no subclassing of its own.
Bool DCAPI OnTimer(dcptr_t, dcptr_t, dcptr_t, Bool*) {
    if (!g_installed) {
        g_installed = ChatInput::EnsureInstalled();
        if (g_installed) Log("kopplad till chattfalten");
    }

    static int ticks = 0;
    if (++ticks >= 30) {
        ticks = 0;
        ChatInput::RunOnGui(&RefreshLanguagesOnGui);
    }

    return True;
}

Bool DCAPI OnUiCreated(dcptr_t, dcptr_t, dcptr_t, Bool*) {
    if (!g_installed) g_installed = ChatInput::EnsureInstalled();
    return True;
}

bool Startup(DCCorePtr core) {
    g_hooks = reinterpret_cast<DCHooksPtr>(
        core->query_interface(DCINTF_HOOKS, DCINTF_HOOKS_VER));
    if (!g_hooks) return false;

    g_log = reinterpret_cast<DCLogPtr>(
        core->query_interface(DCINTF_LOGGING, DCINTF_LOGGING_VER));

    g_config = reinterpret_cast<DCConfigPtr>(
        core->query_interface(DCINTF_CONFIG, DCINTF_CONFIG_VER));
    Settings::Bind(g_config, kGuid);
    CurrentSettings().Load();

    // Follow the client's own language, so the plugin speaks whatever the rest
    // of the window does. The user can override it in the settings dialog.
    ApplyInterfaceLanguage();

    // Swedish comes from Windows' own engine, which handles compounds and
    // inflections properly. English is in the defaults too so it is used
    // wherever the machine has it installed. A missing language is not fatal:
    // the timer keeps retrying, so installing one in Windows later is enough.
    if (!g_speller.Init(CurrentSettings().languages)) {
        Log("ingen stavningsmotor tillganglig an - installera sprakfunktioner i Windows");
    }

    // The word lists had Swedish names while the plugin did; carry them over so
    // nobody loses words they added before the rename.
    MigrateFile(L"egna-ord.txt", L"personal-words.txt");
    MigrateFile(L"autokorrigering.txt", L"autocorrect.txt");

    g_speller.SetPersonalPath(PluginFilePath(L"personal-words.txt"));
    g_speller.LoadPersonal();

    AutoCorrect::SetPath(PluginFilePath(L"autocorrect.txt"));
    AutoCorrect::Load();

    LogActiveLanguages();

    ChatInput::SetModule(g_instance);
    ChatInput::SetSpeller(&g_speller);

    g_installed = ChatInput::EnsureInstalled();

    g_uiSub = g_hooks->bind_hook(HOOK_UI_CREATED, OnUiCreated, nullptr);
    g_timerSub = g_hooks->bind_hook(HOOK_TIMER_SECOND, OnTimer, nullptr);

    return true;
}

void ShutdownSpellerOnGui() {
    g_speller.Shutdown();
}

void Teardown() {
    if (g_hooks) {
        if (g_timerSub) g_hooks->release_hook(g_timerSub);
        if (g_uiSub) g_hooks->release_hook(g_uiSub);
    }
    g_timerSub = nullptr;
    g_uiSub = nullptr;

    // The checkers are apartment-threaded COM objects belonging to the GUI
    // thread, so they have to be released from there -- and before
    // ChatInput::Uninstall, which takes down the only route to that thread.
    if (!ChatInput::RunOnGui(&ShutdownSpellerOnGui, /*blocking=*/true)) g_speller.Shutdown();

    ChatInput::Uninstall();
    ChatInput::SetSpeller(nullptr);
    Nicks::Clear();

    g_installed = false;
    g_hooks = nullptr;
    g_log = nullptr;

    // The host's config interface goes away with the plugin; leaving the pointer
    // bound means a later Save() calls through it.
    Settings::Bind(nullptr, nullptr);
    g_config = nullptr;
}

void ShowConfiguration() {
    // ON_CONFIGURE arrives on the GUI thread, so the host's active window is the
    // right parent for a modal dialog.
    if (!ShowSettingsDialog(g_instance, ::GetActiveWindow(), g_speller)) return;

    g_speller.SetLanguages(CurrentSettings().languages);
    LogActiveLanguages();
    ChatInput::InvalidateAll();
}

Bool DCAPI PluginMain(PluginState state, DCCorePtr core, dcptr_t) {
    switch (state) {
        case ON_INSTALL:
        case ON_LOAD:
        case ON_LOAD_RUNTIME:
            return Startup(core) ? True : False;

        case ON_CONFIGURE:
            ShowConfiguration();
            return True;

        case ON_UNLOAD:
        case ON_UNINSTALL:
            Teardown();
            return True;

        default:
            return False;
    }
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        ::DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" DCEXP DCMAIN DCAPI pluginInit(MetaDataPtr info) {
    // The host has not handed us its config interface yet, so the description
    // shown in the plugin list can only go by the Windows UI language.
    Strings::DetectLanguage(nullptr);

    info->name = kName;
    info->author = kAuthor;
    info->description =
        (Strings::Current() == Strings::Lang::Swedish) ? kDescriptionSv : kDescriptionEn;
    info->web = kWeb;
    info->guid = kGuid;
    info->dependencies = nullptr;
    info->numDependencies = 0;
    info->apiVersion = DCAPI_CORE_VER;
    info->version = 2.3;

    return PluginMain;
}
