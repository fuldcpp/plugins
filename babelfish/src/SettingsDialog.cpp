// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "SettingsDialog.h"

#include "Cache.h"
#include "Hubs.h"
#include "ITranslator.h"
#include "Languages.h"
#include "Settings.h"

#include "../res/resource.h"

#include <commctrl.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

// Swedish or English, decided by the Windows interface language. Squiggle grows
// a whole string module for this; two columns is enough here, and keeping it in
// one table means a caption cannot be changed in one language and forgotten in
// the other.
struct Text {
    const wchar_t* sv;
    const wchar_t* en;
};

constexpr Text kCaption      = {L"Babelfish", L"Babelfish"};
constexpr Text kGroupLang    = {L"Språk", L"Language"};
constexpr Text kLabelLang    = {L"Du skriver på:", L"You write in:"};
constexpr Text kLangHint     = {L"Pluginen översätter alltid till engelska. MyMemory kan "
                                L"inte gissa själv och får då datorns språk.",
                                L"The plugin always translates into English. MyMemory cannot "
                                L"work it out itself and is given the machine's language."};
constexpr Text kAutoLang     = {L"Automatiskt — tjänsten avgör",
                                L"Automatic — let the service decide"};
constexpr Text kGroupService = {L"Tjänst", L"Service"};
constexpr Text kLabelService = {L"Tjänst:", L"Service:"};
constexpr Text kLabelKey     = {L"API-nyckel:", L"API key:"};
constexpr Text kLabelRegion  = {L"Azure-region:", L"Azure region:"};
constexpr Text kLabelEmail   = {L"E-post:", L"E-mail:"};
constexpr Text kLabelLimit   = {L"Tak per dygn:", L"Daily limit:"};
constexpr Text kKeyHint      = {L"Nyckeln sparas per tjänst. Taket är tecken per dygn — 0 "
                                L"stänger av det. En betald tjänst har ingen egen gräns.",
                                L"The key is remembered per service. The limit is characters per "
                                L"day; 0 turns it off. A paid service has no limit of its own."};
constexpr Text kPm           = {L"Låt automatiskt läge gälla även privata meddelanden",
                                L"Let automatic mode cover private messages too"};
constexpr Text kEcho         = {L"Visa mitt original i fönstret medan översättningen hämtas",
                                L"Show my original in the window while the translation is fetched"};
constexpr Text kGroupAuto    = {L"Automatiskt läge", L"Automatic mode"};
constexpr Text kAutoHint     = {L"Kryssa för de hubbar där allt du skriver ska översättas "
                                L"utan Ctrl+G.",
                                L"Tick the hubs where everything you write should be "
                                L"translated without Ctrl+G."};
constexpr Text kColHub       = {L"Hubb", L"Hub"};
constexpr Text kColState     = {L"Status", L"Status"};
constexpr Text kOnline       = {L"ansluten", L"connected"};
constexpr Text kOffline      = {L"ej ansluten", L"not connected"};
constexpr Text kClearCache   = {L"Töm cachen", L"Clear cache"};
constexpr Text kOk           = {L"OK", L"OK"};
constexpr Text kCancel       = {L"Avbryt", L"Cancel"};
// The key the entire plugin turns on, and the command that lists the rest.
//
// Neither was anywhere a user would look. Ctrl+G appeared in one line of the
// client's system log at startup, and in a Read-me nobody opens without already
// suspecting there is something in it. The second use -- pressing it on
// somebody else's line -- was not even in that log line. This dialog is where
// people go to find out what a plugin does, so it is where both belong.
constexpr Text kCommandHint  = {
    L"Ctrl+G i meddelanderutan: översätt och skicka.\n"
    L"Ctrl+G på en rad i chatten: översätt den, bara åt dig.\n"
    L"Skriv /tr help i ett chattfönster för alla kommandon.",
    L"Ctrl+G in the message box: translate and send.\n"
    L"Ctrl+G on a line in the chat: translate it, for you only.\n"
    L"Type /tr help in any chat window for every command."};

constexpr Text kStatus       = {L"%lld tecken använda idag · %zu fraser i cachen",
                                L"%lld characters used today · %zu phrases cached"};

bool Swedish() {
    // Asked once: it cannot change while the dialog is open, and the host is
    // not obliged to make this cheap.
    static const bool swedish = [] {
        const std::string tag = Settings::HostLanguage();
        if (tag.size() >= 2) {
            // "sv", "sv-SE", "sv-FI" -- only the primary tag matters.
            return (tag[0] == 's' || tag[0] == 'S') && (tag[1] == 'v' || tag[1] == 'V');
        }
        // No answer from the host: fall back to Windows, which is better than
        // nothing but is exactly the guess that got this wrong to begin with --
        // an English client on a Swedish Windows was showing a Swedish dialog.
        return PRIMARYLANGID(::GetUserDefaultUILanguage()) == LANG_SWEDISH;
    }();
    return swedish;
}

const wchar_t* T(const Text& text) {
    return Swedish() ? text.sv : text.en;
}

void SetText(HWND dialog, int id, const wchar_t* text) {
    ::SetDlgItemTextW(dialog, id, text);
}

std::string ReadNarrow(HWND dialog, int id) {
    const int len = ::GetWindowTextLengthW(::GetDlgItem(dialog, id));
    if (len <= 0) return {};

    std::wstring wide(static_cast<size_t>(len) + 1, L'\0');
    const int got = ::GetDlgItemTextW(dialog, id, wide.data(), len + 1);
    wide.resize(got < 0 ? 0 : static_cast<size_t>(got));
    if (wide.empty()) return {};

    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), n,
                          nullptr, nullptr);
    return out;
}

void WriteNarrow(HWND dialog, int id, const std::string& value) {
    if (value.empty()) {
        ::SetDlgItemTextW(dialog, id, L"");
        return;
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                        static_cast<int>(value.size()), nullptr, 0);
    if (n <= 0) return;
    std::wstring wide(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), wide.data(),
                          n);
    ::SetDlgItemTextW(dialog, id, wide.c_str());
}

// The service list, in the order somebody should consider them: the one that
// works without an account first.
struct Backend {
    const char* key;
    Text label;
    bool needsKey;
    bool needsRegion;
};

// needsKey is what greys the field out. MyMemory takes none, and leaving an
// editable box next to a service that ignores it is how an Anthropic key ended
// up filed under MyMemory in the first place.
constexpr Backend kBackends[] = {
    {"mymemory", {L"MyMemory — gratis, ingen nyckel", L"MyMemory — free, no key"}, false, false},
    {"deepl", {L"DeepL — egen nyckel", L"DeepL — your own key"}, true, false},
    {"azure", {L"Azure Translator — egen nyckel", L"Azure Translator — your own key"}, true, true},
    {"google", {L"Google Cloud Translation — egen nyckel",
                L"Google Cloud Translation — your own key"}, true, false},
    {"claude", {L"Claude — egen nyckel", L"Claude — your own key"}, true, false},
};

// One row per hub the dialog can offer. Edited in the dialog and written back
// only on OK, so Cancel means cancel.
struct HubRow {
    std::string url;
    bool connected = false;
};

std::vector<HubRow>& HubRows() {
    static std::vector<HubRow> rows;
    return rows;
}

std::wstring Widen(const std::string& text) {
    if (text.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                        nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

// Everything worth offering: what the client is on now, plus anything already
// chosen that happens to be offline. Dropping the latter would quietly turn the
// setting off for a hub just because it was not connected at the time.
void BuildHubRows() {
    std::vector<HubRow>& rows = HubRows();
    rows.clear();

    for (const std::string& url : Hubs::Connected()) {
        rows.push_back(HubRow{url, true});
    }
    for (const std::string& url : CurrentSettings().autoHubs) {
        bool known = false;
        for (const HubRow& row : rows) {
            if (row.url == url) known = true;
        }
        if (!known) rows.push_back(HubRow{url, Hubs::LooksConnected(url)});
    }
}

// Edited here and written back only on OK, like the hub list.
std::map<std::string, std::string>& WorkingKeys() {
    static std::map<std::string, std::string> keys;
    return keys;
}

// Which service the key field is currently showing, so its contents can be put
// back in the right place before the field is reused for another one.
int& ShownBackend() {
    static int index = 0;
    return index;
}

// The language list with "Automatic" first. The combo no longer sorts, so this
// order is what the user sees: the choice that removes the setting altogether
// belongs at the top rather than filed under A.
std::vector<Languages::Choice>& LanguageList() {
    static std::vector<Languages::Choice> list = [] {
        std::vector<Languages::Choice> all;
        all.push_back(Languages::Choice{Languages::kAutoSource, T(kAutoLang)});
        for (const Languages::Choice& choice : Languages::Choices(!Swedish())) {
            all.push_back(choice);
        }
        return all;
    }();
    return list;
}

void FillLanguages(HWND dialog) {
    HWND combo = ::GetDlgItem(dialog, IDC_LANG);

    // Whichever language is in force: the saved one normally, and what Windows
    // suggests if nothing has been saved yet. There is deliberately no "detect
    // automatically" entry -- detection has already happened by the time anyone
    // opens this, and an entry that silently undoes a deliberate choice later
    // is a trap rather than a convenience.
    std::string current = CurrentSettings().sourceLang;
    if (current.empty()) current = Languages::DetectSource();

    const std::vector<Languages::Choice>& list = LanguageList();
    int selected = -1;

    for (size_t slot = 0; slot < list.size(); ++slot) {
        const int index = static_cast<int>(::SendMessageW(
            combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(list[slot].name.c_str())));
        if (index < 0) continue;

        // The list is sorted by name, so the position in the box is not the
        // position in the vector. The item data carries the real one.
        ::SendMessageW(combo, CB_SETITEMDATA, index, static_cast<LPARAM>(slot));
        if (list[slot].code == current) selected = index;
    }

    ::SendMessageW(combo, CB_SETCURSEL, selected < 0 ? 0 : selected, 0);
    SetText(dialog, IDC_LANGHINT, T(kLangHint));
}

void FillAutoHubs(HWND dialog) {
    HWND list = ::GetDlgItem(dialog, IDC_AUTOHUBS);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    ListView_DeleteAllItems(list);

    // Columns are only added once; re-entering the dialog would otherwise stack
    // them up.
    if (Header_GetItemCount(ListView_GetHeader(list)) == 0) {
        RECT client = {};
        ::GetClientRect(list, &client);
        const int width = client.right - client.left;

        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(T(kColHub));
        column.cx = (width * 7) / 10;
        ListView_InsertColumn(list, 0, &column);

        column.pszText = const_cast<wchar_t*>(T(kColState));
        column.cx = width - column.cx - 4;
        ListView_InsertColumn(list, 1, &column);
    }

    const std::vector<HubRow>& rows = HubRows();
    const Settings& settings = CurrentSettings();

    for (size_t i = 0; i < rows.size(); ++i) {
        const std::wstring url = Widen(rows[i].url);

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(url.c_str());
        const int index = ListView_InsertItem(list, &item);
        if (index < 0) continue;

        ListView_SetItemText(list, index, 1,
                             const_cast<wchar_t*>(rows[i].connected ? T(kOnline) : T(kOffline)));
        ListView_SetCheckState(list, index, settings.IsAutoHub(rows[i].url) ? TRUE : FALSE);
    }
}

void ShowStatus(HWND dialog) {
    const Cache::Stats stats = Cache::GetStats();
    wchar_t status[192] = {};
    _snwprintf_s(status, _TRUNCATE, T(kStatus), CurrentSettings().quotaChars, stats.entries);
    SetText(dialog, IDC_STATUS, status);
}

void FillBackends(HWND dialog) {
    HWND combo = ::GetDlgItem(dialog, IDC_BACKEND);
    const Settings& settings = CurrentSettings();

    for (size_t i = 0; i < std::size(kBackends); ++i) {
        ::SendMessageW(combo, CB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(T(kBackends[i].label)));
        if (settings.backend == kBackends[i].key) {
            ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
        }
    }
    if (::SendMessageW(combo, CB_GETCURSEL, 0, 0) == CB_ERR) {
        ::SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

// Moves the key field's contents into the service it was shown for, then puts
// the newly selected service's key in its place.
void SwitchBackend(HWND dialog) {
    const int previous = ShownBackend();
    if (previous >= 0 && static_cast<size_t>(previous) < std::size(kBackends)) {
        const std::string typed = ReadNarrow(dialog, IDC_APIKEY);
        if (typed.empty()) {
            WorkingKeys().erase(kBackends[previous].key);
        } else {
            WorkingKeys()[kBackends[previous].key] = typed;
        }
    }

    const LRESULT now = ::SendDlgItemMessageW(dialog, IDC_BACKEND, CB_GETCURSEL, 0, 0);
    if (now == CB_ERR || static_cast<size_t>(now) >= std::size(kBackends)) return;

    ShownBackend() = static_cast<int>(now);
    const auto it = WorkingKeys().find(kBackends[now].key);
    WriteNarrow(dialog, IDC_APIKEY, it == WorkingKeys().end() ? std::string() : it->second);

    // Greyed rather than hidden, so the layout does not jump and it is obvious
    // the field exists but does not apply here.
    ::EnableWindow(::GetDlgItem(dialog, IDC_APIKEY), kBackends[now].needsKey ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dialog, IDC_REGION), kBackends[now].needsRegion ? TRUE : FALSE);
}

void Load(HWND dialog) {
    const Settings& settings = CurrentSettings();

    ::SetWindowTextW(dialog, T(kCaption));
    SetText(dialog, IDC_GRP_LANG, T(kGroupLang));
    SetText(dialog, IDC_LBL_LANG, T(kLabelLang));
    SetText(dialog, IDC_GRP_SERVICE, T(kGroupService));
    SetText(dialog, IDC_LBL_BACKEND, T(kLabelService));
    SetText(dialog, IDC_LBL_APIKEY, T(kLabelKey));
    SetText(dialog, IDC_LBL_REGION, T(kLabelRegion));
    SetText(dialog, IDC_LBL_EMAIL, T(kLabelEmail));
    SetText(dialog, IDC_LBL_LIMIT, T(kLabelLimit));
    SetText(dialog, IDC_KEYHINT, T(kKeyHint));
    SetText(dialog, IDC_GRP_AUTO, T(kGroupAuto));
    SetText(dialog, IDC_AUTOHINT, T(kAutoHint));
    SetText(dialog, IDC_CMDHINT, T(kCommandHint));
    SetText(dialog, IDC_CLEARCACHE, T(kClearCache));
    SetText(dialog, IDC_PM, T(kPm));
    SetText(dialog, IDC_ECHO, T(kEcho));
    SetText(dialog, IDOK, T(kOk));
    SetText(dialog, IDCANCEL, T(kCancel));

    BuildHubRows();

    FillLanguages(dialog);
    FillBackends(dialog);
    FillAutoHubs(dialog);

    WorkingKeys() = settings.apiKeys;
    ShownBackend() = static_cast<int>(
        ::SendDlgItemMessageW(dialog, IDC_BACKEND, CB_GETCURSEL, 0, 0));
    WriteNarrow(dialog, IDC_APIKEY, settings.ApiKeyFor(settings.backend));
    WriteNarrow(dialog, IDC_REGION, settings.azureRegion);

    for (size_t i = 0; i < std::size(kBackends); ++i) {
        if (settings.backend != kBackends[i].key) continue;
        ::EnableWindow(::GetDlgItem(dialog, IDC_APIKEY), kBackends[i].needsKey ? TRUE : FALSE);
        ::EnableWindow(::GetDlgItem(dialog, IDC_REGION), kBackends[i].needsRegion ? TRUE : FALSE);
    }
    WriteNarrow(dialog, IDC_EMAIL, settings.email);

    char limit[32] = {};
    snprintf(limit, sizeof(limit), "%lld", settings.dailyLimitChars);
    WriteNarrow(dialog, IDC_LIMIT, limit);

    ::CheckDlgButton(dialog, IDC_PM, settings.translatePM ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dialog, IDC_ECHO, settings.echoOriginal ? BST_CHECKED : BST_UNCHECKED);

    ShowStatus(dialog);
}

void Save(HWND dialog) {
    Settings& settings = CurrentSettings();

    const LRESULT langIndex = ::SendDlgItemMessageW(dialog, IDC_LANG, CB_GETCURSEL, 0, 0);
    if (langIndex != CB_ERR) {
        const LRESULT slot =
            ::SendDlgItemMessageW(dialog, IDC_LANG, CB_GETITEMDATA, langIndex, 0);
        const auto& list = LanguageList();
        if (slot >= 0 && static_cast<size_t>(slot) < list.size()) {
            settings.sourceLang = list[static_cast<size_t>(slot)].code;
        }
    }

    const LRESULT backendIndex = ::SendDlgItemMessageW(dialog, IDC_BACKEND, CB_GETCURSEL, 0, 0);
    if (backendIndex != CB_ERR && static_cast<size_t>(backendIndex) < std::size(kBackends)) {
        settings.backend = kBackends[static_cast<size_t>(backendIndex)].key;
    }

    // The field on screen belongs to whichever service it was last shown for.
    SwitchBackend(dialog);
    settings.apiKeys = WorkingKeys();
    settings.azureRegion = ReadNarrow(dialog, IDC_REGION);
    settings.email = ReadNarrow(dialog, IDC_EMAIL);

    // An empty box means no limit, which is the same as typing 0.
    const std::string limitText = ReadNarrow(dialog, IDC_LIMIT);
    settings.dailyLimitChars = limitText.empty() ? 0 : std::strtoll(limitText.c_str(), nullptr, 10);
    if (settings.dailyLimitChars < 0) settings.dailyLimitChars = 0;
    HWND list = ::GetDlgItem(dialog, IDC_AUTOHUBS);
    const std::vector<HubRow>& rows = HubRows();
    std::vector<std::string> chosen;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (ListView_GetCheckState(list, static_cast<int>(i))) chosen.push_back(rows[i].url);
    }
    settings.autoHubs = chosen;
    settings.translatePM = ::IsDlgButtonChecked(dialog, IDC_PM) == BST_CHECKED;
    settings.echoOriginal = ::IsDlgButtonChecked(dialog, IDC_ECHO) == BST_CHECKED;

    settings.Save();
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
        case WM_INITDIALOG: {
            INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
            ::InitCommonControlsEx(&controls);
            Load(dialog);
            return TRUE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BACKEND:
                    if (HIWORD(wParam) == CBN_SELCHANGE) SwitchBackend(dialog);
                    return TRUE;

                case IDC_CLEARCACHE:
                    // Immediate, like the command it replaces. There is nothing
                    // to undo on Cancel and nothing gained by deferring it.
                    Cache::Clear();
                    ShowStatus(dialog);
                    return TRUE;

                case IDOK:
                    Save(dialog);
                    ::EndDialog(dialog, IDOK);
                    return TRUE;
                case IDCANCEL:
                    ::EndDialog(dialog, IDCANCEL);
                    return TRUE;
                default:
                    break;
            }
            break;

        default:
            break;
    }
    return FALSE;
}

}  // namespace

bool ShowSettingsDialog(HINSTANCE instance, HWND parent) {
    const INT_PTR result =
        ::DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), parent, DialogProc, 0);
    return result == IDOK;
}
