// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>

// User-visible text in Swedish and English.
//
// One binary rather than two builds: two copies of the source drift apart the
// first time a bug is fixed in only one of them. The dialog layout stays in the
// resource file and every caption is written at WM_INITDIALOG, so there is only
// ever one set of control positions to maintain.
namespace Strings {

enum class Lang { Swedish, English };

enum class Str {
    DialogTitle,
    GroupLanguages,
    LangHintNone,
    LangHintAdd,
    GroupUnderline,
    LabelColour,
    ButtonChangeColour,
    LabelThickness,
    ThicknessFormat,
    PreviewSample,
    PreviewMisspelled,
    GroupPersonal,
    GroupAutoCorrect,
    CheckAutoCorrect,
    AutoCorrectHint,
    LabelInterface,
    InterfaceAuto,
    ButtonOpenFolder,
    ButtonOk,
    ButtonCancel,
    StatusCounts,
    StatusNoLanguage,
    MenuNoSuggestions,
    MenuAddToDictionary,
    MenuIgnoreSession,
    SaveFailedTitle,
    SaveFailedFormat,
    AutoCorrectFileHeader,
    Count
};

// Picks the language from the host's IETF tag (DCConfig::get_language), falling
// back to the Windows UI language when the host does not say. Anything that is
// not Swedish gets English.
void DetectLanguage(const char* hostLanguageTag);

// "sv" or "en" force a language; an empty string returns to auto-detection.
void SetLanguageOverride(const std::wstring& tag);

Lang Current();

const wchar_t* T(Str id);

}  // namespace Strings
