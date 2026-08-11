// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <windows.h>
#include <winhttp.h>

#include <mutex>
#include <string>
#include <vector>

// A blocking WinHTTP client sized for one request at a time, which is all the
// single worker thread ever needs.
//
// The reason this is a class rather than a free function is Cancel(): the
// client can be shut down while a request is in flight, and the worker thread
// has to come back out of WinHttpReceiveResponse promptly or the join in
// pluginExit sits there for the length of the timeout while the host waits to
// unload the DLL.
namespace Http {

struct Response {
    bool ok = false;         // transport succeeded; says nothing about status
    DWORD status = 0;        // HTTP status code
    std::string body;        // response body, as received (UTF-8 in practice)
    std::string error;       // human-readable transport failure, empty when ok
};

class Client {
public:
    Client() = default;
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // verb is L"GET" or L"POST". headers are full "Name: value" lines. body is
    // sent as-is and should be empty for GET.
    Response Request(const wchar_t* verb, const std::string& url,
                     const std::vector<std::string>& headers, const std::string& body);

    // Aborts any request in flight and makes every later Request fail
    // immediately. Safe to call from another thread; that is the entire point.
    void Cancel();

    bool Cancelled() const;

    // Percent-encodes a string for use in a query parameter.
    static std::string UrlEncode(const std::string& text);

private:
    mutable std::mutex mutex_;
    HINTERNET session_ = nullptr;   // created lazily, reused across requests
    HINTERNET request_ = nullptr;   // non-null only while a call is in flight
    bool cancelled_ = false;

    HINTERNET EnsureSession();
};

}  // namespace Http
