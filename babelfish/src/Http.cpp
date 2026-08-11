// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Http.h"

#include <cstdio>

#pragma comment(lib, "winhttp.lib")

namespace Http {
namespace {

// Section 5: 2 s to resolve and connect, 3 s to send and receive. The queue
// enforces its own overall deadline on top of this.
constexpr int kResolveTimeout = 2000;
constexpr int kConnectTimeout = 2000;
constexpr int kSendTimeout = 3000;
constexpr int kReceiveTimeout = 3000;

// Response bodies from these APIs are a few hundred bytes. The cap is there so
// a redirect to something enormous cannot grow the buffer without bound.
constexpr size_t kMaxBodyBytes = 256 * 1024;

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                        nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string DescribeError(const char* stage, DWORD code) {
    char buf[128] = {};
    snprintf(buf, sizeof(buf), "%s failed (WinHTTP %lu)", stage, static_cast<unsigned long>(code));
    return buf;
}

}  // namespace

Client::~Client() {
    Cancel();
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_) {
        ::WinHttpCloseHandle(session_);
        session_ = nullptr;
    }
}

bool Client::Cancelled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancelled_;
}

void Client::Cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled_ = true;
    // Closing the request handle from another thread is what unblocks the
    // worker: the pending WinHTTP call returns ERROR_INVALID_HANDLE instead of
    // running out its receive timeout. The worker clears request_ under the
    // same lock before closing it itself, so this cannot double-close.
    if (request_) {
        ::WinHttpCloseHandle(request_);
        request_ = nullptr;
    }
}

HINTERNET Client::EnsureSession() {
    if (session_) return session_;

    session_ = ::WinHttpOpen(L"FulDCBabelfish/1.0",
                             WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_) {
        // Automatic proxy discovery is not available before Windows 8.1; fall
        // back to the system configuration rather than giving up on the request.
        session_ = ::WinHttpOpen(L"FulDCBabelfish/1.0",
                                 WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session_) {
        ::WinHttpSetTimeouts(session_, kResolveTimeout, kConnectTimeout, kSendTimeout,
                             kReceiveTimeout);
    }
    return session_;
}

std::string Client::UrlEncode(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (unsigned char c : text) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                                c == '.' || c == '~';
        if (unreserved) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

Response Client::Request(const wchar_t* verb, const std::string& url,
                         const std::vector<std::string>& headers, const std::string& body) {
    Response response;

    const std::wstring wideUrl = Widen(url);
    URL_COMPONENTS parts = {};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!::WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts)) {
        response.error = "malformed url";
        return response;
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_) {
            response.error = "cancelled";
            return response;
        }
        if (!EnsureSession()) {
            response.error = DescribeError("WinHttpOpen", ::GetLastError());
            return response;
        }

        connection = ::WinHttpConnect(session_, host.c_str(), parts.nPort, 0);
        if (!connection) {
            response.error = DescribeError("WinHttpConnect", ::GetLastError());
            return response;
        }

        const DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        request = ::WinHttpOpenRequest(connection, verb, path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request) {
            response.error = DescribeError("WinHttpOpenRequest", ::GetLastError());
            ::WinHttpCloseHandle(connection);
            return response;
        }

        ::WinHttpSetTimeouts(request, kResolveTimeout, kConnectTimeout, kSendTimeout,
                             kReceiveTimeout);
        request_ = request;
    }

    // Everything below runs with the lock released so Cancel() can get in.
    auto finish = [&](Response&& result) -> Response {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (request_ == request) {
                ::WinHttpCloseHandle(request);
                request_ = nullptr;
            }
            // If request_ is no longer ours, Cancel() already closed it.
        }
        ::WinHttpCloseHandle(connection);
        return std::move(result);
    };

    std::wstring headerBlock;
    for (const std::string& line : headers) {
        headerBlock += Widen(line);
        headerBlock += L"\r\n";
    }

    const bool sent = ::WinHttpSendRequest(
        request,
        headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
        headerBlock.empty() ? 0 : static_cast<DWORD>(-1),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent) {
        response.error = DescribeError("WinHttpSendRequest", ::GetLastError());
        return finish(std::move(response));
    }

    if (!::WinHttpReceiveResponse(request, nullptr)) {
        response.error = DescribeError("WinHttpReceiveResponse", ::GetLastError());
        return finish(std::move(response));
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    ::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                          WINHTTP_NO_HEADER_INDEX);
    response.status = status;

    for (;;) {
        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(request, &available)) {
            response.error = DescribeError("WinHttpQueryDataAvailable", ::GetLastError());
            return finish(std::move(response));
        }
        if (available == 0) break;

        if (response.body.size() + available > kMaxBodyBytes) {
            available = static_cast<DWORD>(kMaxBodyBytes - response.body.size());
            if (available == 0) break;
        }

        const size_t offset = response.body.size();
        response.body.resize(offset + available);
        DWORD read = 0;
        if (!::WinHttpReadData(request, response.body.data() + offset, available, &read)) {
            response.error = DescribeError("WinHttpReadData", ::GetLastError());
            return finish(std::move(response));
        }
        response.body.resize(offset + read);
        if (read == 0) break;
    }

    response.ok = true;
    return finish(std::move(response));
}

}  // namespace Http
