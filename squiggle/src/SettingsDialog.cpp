// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "SettingsDialog.h"

#include "AutoCorrect.h"
#include "Nicks.h"
#include "Settings.h"
#include "Speller.h"
#include "Strings.h"
#include "TextFile.h"

#include "../res/resource.h"

#include <commctrl.h>
#include <commdlg.h>  // WIN32_LEAN_AND_MEAN leaves out the common dialogs
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

struct DialogState {
    Speller* speller = nullptr;
    Settings working;
    std::vector<std::wstring> available;
};

using Strings::Str;
using Strings::T;

// Writes every caption from the string table, so the resource only has to carry
// the layout.
void ApplyLanguage(HWND dlg) {
    ::SetWindowTextW(dlg, T(Str::DialogTitle));

    const struct {
        int id;
        Str text;
    } captions[] = {
        {IDC_GRP_LANG, Str::GroupLanguages},
        {IDC_GRP_UNDERLINE, Str::GroupUnderline},
        {IDC_LBL_COLOUR, Str::LabelColour},
        {IDC_COLOUR, Str::ButtonChangeColour},
        {IDC_LBL_THICKNESS, Str::LabelThickness},
        {IDC_GRP_PERSONAL, Str::GroupPersonal},
        {IDC_GRP_AUTO, Str::GroupAutoCorrect},
        {IDC_AUTOCORRECT, Str::CheckAutoCorrect},
        {IDC_AUTOHINT, Str::AutoCorrectHint},
        {IDC_LBL_UILANG, Str::LabelInterface},
        {IDC_OPENFOLDER, Str::ButtonOpenFolder},
        {IDOK, Str::ButtonOk},
        {IDCANCEL, Str::ButtonCancel},
    };

    for (const auto& c : captions) ::SetDlgItemTextW(dlg, c.id, T(c.text));
}

std::wstring TextOf(HWND control) {
    const int len = ::GetWindowTextLengthW(control);
    if (len <= 0) return {};
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    const int got = ::GetWindowTextW(control, s.data(), len + 1);
    s.resize(got < 0 ? 0 : static_cast<size_t>(got));
    return s;
}

bool IsEnabled(const DialogState& state, const std::wstring& tag) {
    return std::find(state.working.languages.begin(), state.working.languages.end(), tag) !=
           state.working.languages.end();
}

// "sv-SE" on its own tells you very little; Windows already knows it as
// "svenska (Sverige)", which is also what distinguishes it from "svenska
// (Finland)". The tag is kept in a second column because that is what the
// settings actually store.
std::wstring DisplayNameFor(const std::wstring& tag) {
    // Follow the plugin's own language rather than the Windows one, so an
    // English interface does not list its languages in Swedish.
    const LCTYPE type = (Strings::Current() == Strings::Lang::English)
                            ? LOCALE_SENGLISHDISPLAYNAME
                            : LOCALE_SLOCALIZEDDISPLAYNAME;

    wchar_t buf[LOCALE_NAME_MAX_LENGTH * 2] = {};
    const int n = ::GetLocaleInfoEx(tag.c_str(), type, buf, static_cast<int>(std::size(buf)));
    if (n <= 1) return tag;  // unknown or uninstalled tag: show it raw
    return buf;
}

void InitLanguageList(HWND dlg) {
    HWND list = ::GetDlgItem(dlg, IDC_LANGLIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);

    RECT rc = {};
    ::GetClientRect(list, &rc);
    const int total = rc.right - rc.left - ::GetSystemMetrics(SM_CXVSCROLL);
    const int tagWidth = 90;

    LVCOLUMNW col = {};
    col.mask = LVCF_WIDTH;
    col.cx = total - tagWidth;
    ListView_InsertColumn(list, 0, &col);
    col.cx = tagWidth;
    ListView_InsertColumn(list, 1, &col);
}

// Rebuilt whenever the interface language changes, because the display names
// change with it. Check states come from working.languages, so the caller must
// have read them back first.
void PopulateLanguages(HWND dlg, const DialogState& state) {
    HWND list = ::GetDlgItem(dlg, IDC_LANGLIST);
    ListView_DeleteAllItems(list);

    int row = 0;
    for (const std::wstring& tag : state.available) {
        const std::wstring name = DisplayNameFor(tag);

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(name.c_str());
        ListView_InsertItem(list, &item);
        ListView_SetItemText(list, row, 1, const_cast<wchar_t*>(tag.c_str()));
        ListView_SetCheckState(list, row, IsEnabled(state, tag) ? TRUE : FALSE);
        ++row;
    }

    ::SetDlgItemTextW(dlg, IDC_LANGHINT,
                      T(state.available.empty() ? Str::LangHintNone : Str::LangHintAdd));
}

void FillLanguages(HWND dlg, DialogState& state) {
    InitLanguageList(dlg);

    state.available = Speller::InstalledLanguages();

    // Languages that were configured but are no longer installed still belong in
    // the list, otherwise saving would silently drop them.
    for (const std::wstring& tag : state.working.languages) {
        if (std::find(state.available.begin(), state.available.end(), tag) == state.available.end()) {
            state.available.push_back(tag);
        }
    }

    PopulateLanguages(dlg, state);
}

void ReadLanguages(HWND dlg, DialogState& state) {
    HWND list = ::GetDlgItem(dlg, IDC_LANGLIST);
    state.working.languages.clear();

    for (size_t i = 0; i < state.available.size(); ++i) {
        if (ListView_GetCheckState(list, static_cast<int>(i))) {
            state.working.languages.push_back(state.available[i]);
        }
    }
}

void FillThickness(HWND dlg, const DialogState& state) {
    HWND combo = ::GetDlgItem(dlg, IDC_THICKNESS);
    for (int i = 1; i <= 4; ++i) {
        wchar_t label[16] = {};
        ::wsprintfW(label, T(Str::ThicknessFormat), i);
        ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(state.working.thickness - 1), 0);
}

// Auto / Svenska / English. The two language names stay in their own language:
// somebody looking for English should not have to recognise "Engelska" first.
void FillUiLanguage(HWND dlg, const DialogState& state) {
    HWND combo = ::GetDlgItem(dlg, IDC_UILANG);
    ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(T(Str::InterfaceAuto)));
    ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Svenska"));
    ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));

    int sel = 0;
    if (state.working.uiLanguage == L"sv") sel = 1;
    else if (state.working.uiLanguage == L"en") sel = 2;
    ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
}

std::wstring ReadUiLanguage(HWND dlg) {
    const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_UILANG, CB_GETCURSEL, 0, 0);
    if (sel == 1) return L"sv";
    if (sel == 2) return L"en";
    return {};
}

// Draws the sample word with the squiggle exactly as the chat box will, so the
// colour and thickness choices can be judged without closing the dialog.
void DrawPreview(const DRAWITEMSTRUCT* dis, const DialogState& state) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    ::FillRect(hdc, &rc, ::GetSysColorBrush(COLOR_WINDOW));

    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, ::GetSysColor(COLOR_WINDOWTEXT));

    RECT text = rc;
    text.left += 6;
    ::DrawTextW(hdc, T(Str::PreviewSample), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Underline only the misspelled part of the sample.
    const wchar_t* misspelled = T(Str::PreviewMisspelled);
    SIZE word = {};
    ::GetTextExtentPoint32W(hdc, misspelled, static_cast<int>(wcslen(misspelled)), &word);

    TEXTMETRICW tm = {};
    ::GetTextMetricsW(hdc, &tm);
    const int centre = (rc.top + rc.bottom) / 2;
    const int baseline = centre + tm.tmHeight / 2 - 1;

    HPEN pen = ::CreatePen(PS_SOLID, state.working.thickness, state.working.colour);
    HGDIOBJ old = ::SelectObject(hdc, pen);

    ::MoveToEx(hdc, text.left, baseline, nullptr);
    bool up = true;
    for (int x = text.left; x < text.left + word.cx; x += 2) {
        ::LineTo(hdc, std::min(x + 2, static_cast<int>(text.left + word.cx)), up ? baseline - 2 : baseline);
        up = !up;
    }

    ::SelectObject(hdc, old);
    ::DeleteObject(pen);
}

void PickColour(HWND dlg, DialogState& state) {
    static COLORREF custom[16] = {};

    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = dlg;
    cc.rgbResult = state.working.colour;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (::ChooseColorW(&cc)) {
        state.working.colour = cc.rgbResult;
        ::InvalidateRect(::GetDlgItem(dlg, IDC_PREVIEW), nullptr, TRUE);
    }
}

void UpdateStatus(HWND dlg, const DialogState& state) {
    wchar_t counts[160] = {};
    ::wsprintfW(counts, T(Str::StatusCounts), static_cast<int>(Nicks::Count()),
                static_cast<int>(AutoCorrect::Count()));

    std::wstring text = counts;
    if (state.speller && !state.speller->Ready()) text += T(Str::StatusNoLanguage);
    ::SetDlgItemTextW(dlg, IDC_STATUS, text.c_str());
}

// The install directory is a GUID folder buried in %LOCALAPPDATA%, so the word
// lists and the bundled readme are effectively unreachable without this.
void OpenPluginFolder(HWND dlg, const DialogState& state) {
    if (!state.speller) return;

    const std::wstring& file = state.speller->PersonalPath();
    const size_t slash = file.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;

    const std::wstring folder = file.substr(0, slash);
    ::ShellExecuteW(dlg, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Switching the interface language redraws the dialog in place, so the choice
// can be judged rather than guessed at.
void Relocalize(HWND dlg, DialogState& state) {
    ApplyLanguage(dlg);

    // The tick boxes are the source of truth for which languages are on, so read
    // them back before the list is rebuilt with new display names.
    ReadLanguages(dlg, state);
    PopulateLanguages(dlg, state);

    const int thickness = state.working.thickness;
    ::SendDlgItemMessageW(dlg, IDC_THICKNESS, CB_RESETCONTENT, 0, 0);
    FillThickness(dlg, state);
    ::SendDlgItemMessageW(dlg, IDC_THICKNESS, CB_SETCURSEL, static_cast<WPARAM>(thickness - 1), 0);

    // Only the "Automatic" entry is translated; the selection is preserved.
    const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_UILANG, CB_GETCURSEL, 0, 0);
    ::SendDlgItemMessageW(dlg, IDC_UILANG, CB_RESETCONTENT, 0, 0);
    FillUiLanguage(dlg, state);
    if (sel != CB_ERR) ::SendDlgItemMessageW(dlg, IDC_UILANG, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);

    UpdateStatus(dlg, state);
    ::InvalidateRect(::GetDlgItem(dlg, IDC_PREVIEW), nullptr, TRUE);
}

INT_PTR CALLBACK DialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(::GetWindowLongPtrW(dlg, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<DialogState*>(lParam);
            ::SetWindowLongPtrW(dlg, GWLP_USERDATA, lParam);

            ApplyLanguage(dlg);
            FillLanguages(dlg, *state);
            FillThickness(dlg, *state);
            FillUiLanguage(dlg, *state);
            ::CheckDlgButton(dlg, IDC_AUTOCORRECT,
                             state->working.autoCorrect ? BST_CHECKED : BST_UNCHECKED);
            ::SetDlgItemTextW(dlg, IDC_PERSONAL, state->speller->PersonalAsText().c_str());
            ::SetDlgItemTextW(dlg, IDC_CORRECTIONS, AutoCorrect::AsText().c_str());
            UpdateStatus(dlg, *state);
            return TRUE;
        }

        case WM_DRAWITEM: {
            if (wParam == IDC_PREVIEW && state) {
                DrawPreview(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam), *state);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            if (!state) break;

            switch (LOWORD(wParam)) {
                case IDC_COLOUR:
                    PickColour(dlg, *state);
                    return TRUE;

                case IDC_OPENFOLDER:
                    OpenPluginFolder(dlg, *state);
                    return TRUE;

                case IDC_UILANG:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        state->working.uiLanguage = ReadUiLanguage(dlg);
                        Strings::SetLanguageOverride(state->working.uiLanguage);
                        Relocalize(dlg, *state);
                    }
                    return TRUE;

                case IDC_THICKNESS:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_THICKNESS, CB_GETCURSEL, 0, 0);
                        if (sel != CB_ERR) state->working.thickness = static_cast<int>(sel) + 1;
                        ::InvalidateRect(::GetDlgItem(dlg, IDC_PREVIEW), nullptr, TRUE);
                    }
                    return TRUE;

                case IDOK: {
                    ReadLanguages(dlg, *state);
                    state->working.autoCorrect =
                        ::IsDlgButtonChecked(dlg, IDC_AUTOCORRECT) == BST_CHECKED;

                    const unsigned failedBefore = TextFile::FailureCount();
                    state->speller->SavePersonalText(TextOf(::GetDlgItem(dlg, IDC_PERSONAL)));
                    AutoCorrect::SaveText(TextOf(::GetDlgItem(dlg, IDC_CORRECTIONS)));

                    // The dialog stays open when the words could not be written.
                    // Closing it would be the last moment they existed anywhere,
                    // and the user can still copy them out of the box or press
                    // Cancel. Silently discarding hand-typed words is the one
                    // outcome worth refusing.
                    if (TextFile::FailureCount() != failedBefore) {
                        wchar_t body[1024] = {};
                        ::wsprintfW(body, T(Str::SaveFailedFormat),
                                    TextFile::WriteFailure().c_str());
                        ::MessageBoxW(dlg, body, T(Str::SaveFailedTitle),
                                      MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    ::EndDialog(dlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    ::EndDialog(dlg, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }

    return FALSE;
}

}  // namespace

bool ShowSettingsDialog(HINSTANCE instance, HWND parent, Speller& speller) {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    ::InitCommonControlsEx(&icc);

    DialogState state;
    state.speller = &speller;
    state.working = CurrentSettings();

    // The dialog switches language live, so cancelling has to put it back.
    const std::wstring languageBefore = CurrentSettings().uiLanguage;

    const INT_PTR result = ::DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), parent,
                                             DialogProc, reinterpret_cast<LPARAM>(&state));
    if (result != IDOK) {
        Strings::SetLanguageOverride(languageBefore);
        return false;
    }

    CurrentSettings() = state.working;
    CurrentSettings().Save();
    Strings::SetLanguageOverride(CurrentSettings().uiLanguage);
    return true;
}
