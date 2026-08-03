// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "TextFile.h"

#include <windows.h>

#include <vector>

namespace TextFile {
namespace {

const char kBom[] = "\xEF\xBB\xBF";

}  // namespace

std::wstring Read(const std::wstring& path) {
    if (path.empty()) return {};

    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER size = {};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > (32 << 20)) {
        ::CloseHandle(file);
        return {};
    }

    std::vector<char> bytes(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ::ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    ::CloseHandle(file);
    if (!ok || read == 0) return {};

    const char* data = bytes.data();
    int count = static_cast<int>(read);
    if (count >= 3 && std::memcmp(data, kBom, 3) == 0) {
        data += 3;
        count -= 3;
    }
    if (count <= 0) return {};

    const int wide = ::MultiByteToWideChar(CP_UTF8, 0, data, count, nullptr, 0);
    if (wide <= 0) return {};

    std::wstring out(static_cast<size_t>(wide), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, data, count, out.data(), wide);
    return out;
}

bool Write(const std::wstring& path, const std::wstring& text) {
    if (path.empty()) return false;

    std::string utf8;
    if (!text.empty()) {
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                            nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            utf8.resize(static_cast<size_t>(n));
            ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                  utf8.data(), n, nullptr, nullptr);
        }
    }

    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    bool ok = ::WriteFile(file, kBom, 3, &written, nullptr) != FALSE;
    if (ok && !utf8.empty()) {
        ok = ::WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) != FALSE;
    }

    ::CloseHandle(file);
    return ok;
}

}  // namespace TextFile
