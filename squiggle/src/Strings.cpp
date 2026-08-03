// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Strings.h"

#include <windows.h>

namespace Strings {
namespace {

constexpr size_t kCount = static_cast<size_t>(Str::Count);

const wchar_t* const kSwedish[kCount] = {
    /* DialogTitle           */ L"Squiggle – inställningar",
    /* GroupLanguages        */ L"Språk",
    /* LangHintNone          */ L"Windows har ingen stavningskontroll installerad alls.",
    /* LangHintAdd           */ L"Saknas ett språk? Lägg till det under Inställningar – Tid och språk.",
    /* GroupUnderline        */ L"Våglinje",
    /* LabelColour           */ L"Färg:",
    /* ButtonChangeColour    */ L"Ändra…",
    /* LabelThickness        */ L"Tjocklek:",
    /* ThicknessFormat       */ L"%d px",
    /* PreviewSample         */ L"felstavvat ord",
    /* PreviewMisspelled     */ L"felstavvat",
    /* GroupPersonal         */ L"Egna ord (ett per rad)",
    /* GroupAutoCorrect      */ L"Autokorrigering",
    /* CheckAutoCorrect      */ L"Rätta feltryckningar automatiskt",
    /* AutoCorrectHint       */ L"Avstängd som standard. Ctrl+Z ångrar en rättelse och stänger av den för ordet.",
    /* LabelInterface        */ L"Gränssnitt:",
    /* InterfaceAuto         */ L"Automatiskt",
    /* ButtonOpenFolder      */ L"Öppna plugin-mappen",
    /* ButtonOk              */ L"OK",
    /* ButtonCancel          */ L"Avbryt",
    /* StatusCounts          */ L"Kända nick: %d     Autokorrigeringsregler: %d",
    /* StatusNoLanguage      */ L"\nVarning: inget språk är aktivt, inget kontrolleras.",
    /* MenuNoSuggestions     */ L"(inga förslag)",
    /* MenuAddToDictionary   */ L"Lägg till i ordlistan",
    /* MenuIgnoreSession     */ L"Ignorera den här sessionen",
    /* AutoCorrectFileHeader */
        L"# Autokorrigering för Squiggle\n"
        L"#\n"
        L"# En regel per rad, formatet är:  fel=rätt\n"
        L"# Rader som börjar med # ignoreras.\n"
        L"#\n"
        L"# Ordet byts ut när du skrivit klart det, alltså när du trycker\n"
        L"# mellanslag, punkt, komma eller enter. Stora och små bokstäver spelar\n"
        L"# ingen roll: skriver du \"Teh\" blir det \"The\".\n"
        L"#\n"
        L"# Lägg till dina egna vanligaste feltryckningar här under.\n",
};

const wchar_t* const kEnglish[kCount] = {
    /* DialogTitle           */ L"Squiggle – settings",
    /* GroupLanguages        */ L"Languages",
    /* LangHintNone          */ L"Windows has no spell checker installed at all.",
    /* LangHintAdd           */ L"Missing a language? Add it under Settings – Time & language.",
    /* GroupUnderline        */ L"Underline",
    /* LabelColour           */ L"Colour:",
    /* ButtonChangeColour    */ L"Change…",
    /* LabelThickness        */ L"Thickness:",
    /* ThicknessFormat       */ L"%d px",
    /* PreviewSample         */ L"mispeled word",
    /* PreviewMisspelled     */ L"mispeled",
    /* GroupPersonal         */ L"Personal words (one per line)",
    /* GroupAutoCorrect      */ L"Autocorrect",
    /* CheckAutoCorrect      */ L"Correct typos automatically",
    /* AutoCorrectHint       */ L"Off by default. Ctrl+Z undoes a correction and disables it for that word.",
    /* LabelInterface        */ L"Interface:",
    /* InterfaceAuto         */ L"Automatic",
    /* ButtonOpenFolder      */ L"Open plugin folder",
    /* ButtonOk              */ L"OK",
    /* ButtonCancel          */ L"Cancel",
    /* StatusCounts          */ L"Known nicknames: %d     Autocorrect rules: %d",
    /* StatusNoLanguage      */ L"\nWarning: no language is active, nothing is being checked.",
    /* MenuNoSuggestions     */ L"(no suggestions)",
    /* MenuAddToDictionary   */ L"Add to dictionary",
    /* MenuIgnoreSession     */ L"Ignore for this session",
    /* AutoCorrectFileHeader */
        L"# Autocorrect rules for Squiggle\n"
        L"#\n"
        L"# One rule per line, the format is:  wrong=right\n"
        L"# Lines starting with # are ignored.\n"
        L"#\n"
        L"# The word is replaced once you finish typing it, that is when you press\n"
        L"# space, full stop, comma or enter. Case does not matter: type \"Teh\"\n"
        L"# and you get \"The\".\n"
        L"#\n"
        L"# Add your own most frequent typos below.\n",
};

Lang g_lang = Lang::English;
Lang g_detected = Lang::English;
bool g_overridden = false;

Lang FromWindows() {
    const LANGID id = ::GetUserDefaultUILanguage();
    return (PRIMARYLANGID(id) == LANG_SWEDISH) ? Lang::Swedish : Lang::English;
}

}  // namespace

void DetectLanguage(const char* hostLanguageTag) {
    if (hostLanguageTag && (hostLanguageTag[0] == 's' || hostLanguageTag[0] == 'S') &&
        (hostLanguageTag[1] == 'v' || hostLanguageTag[1] == 'V')) {
        g_detected = Lang::Swedish;
    } else if (hostLanguageTag && hostLanguageTag[0] != '\0') {
        g_detected = Lang::English;
    } else {
        g_detected = FromWindows();
    }

    if (!g_overridden) g_lang = g_detected;
}

void SetLanguageOverride(const std::wstring& tag) {
    if (tag == L"sv") {
        g_lang = Lang::Swedish;
        g_overridden = true;
    } else if (tag == L"en") {
        g_lang = Lang::English;
        g_overridden = true;
    } else {
        g_overridden = false;
        g_lang = g_detected;
    }
}

Lang Current() {
    return g_lang;
}

const wchar_t* T(Str id) {
    const size_t index = static_cast<size_t>(id);
    if (index >= kCount) return L"";
    return (g_lang == Lang::Swedish) ? kSwedish[index] : kEnglish[index];
}

}  // namespace Strings
