// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Text.h"

#include <windows.h>

namespace Text {

std::wstring Fold(std::wstring s) {
    if (!s.empty()) ::CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

std::wstring Upper(std::wstring s) {
    if (!s.empty()) ::CharUpperBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

}  // namespace Text
