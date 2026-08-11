// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Marshal.h"

#include <atomic>
#include <mutex>

namespace Marshal {
namespace {

constexpr wchar_t kClassName[] = L"FulDCBabelfishMarshal";
constexpr UINT kMsgResult = WM_APP + 1;
constexpr UINT kMsgDestroy = WM_APP + 2;
constexpr UINT kDestroyTimeoutMs = 3000;

std::mutex g_mutex;
HINSTANCE g_instance = nullptr;
Handler g_handler = nullptr;
ATOM g_class = 0;
DWORD g_guiThread = 0;

// True while CreateWindowExW is running. See EnsureWindow.
bool g_creating = false;

// EnsureWindow is called from a message hook, so it runs on essentially every
// message the GUI thread receives. If creation genuinely cannot succeed --
// which in practice means the process is out of handles -- retrying that often
// makes a bad situation worse, so it gives up after a few attempts.
constexpr int kMaxCreateAttempts = 5;
int g_createAttempts = 0;

// Read without the lock by Post, which runs on the worker thread on every
// completed job and must not contend with anything.
std::atomic<HWND> g_window{nullptr};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Nothing may be thrown out of a window procedure: it unwinds through
    // user32, which ends the process rather than the call.
    try {
        if (msg == kMsgResult) {
            Handler handler = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                handler = g_handler;
            }
            if (handler) handler(static_cast<uint64_t>(wParam));
            return 0;
        }

        if (msg == kMsgDestroy) {
            ::DestroyWindow(hwnd);
            return 0;
        }

        if (msg == WM_NCDESTROY) {
            g_window.store(nullptr, std::memory_order_release);
        }
    } catch (...) {
        // Losing one result is survivable; taking the client down is not.
    }

    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

void SetModule(HINSTANCE instance) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_instance = instance;
}

void SetHandler(Handler handler) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_handler = handler;
}

bool Ready() {
    return g_window.load(std::memory_order_acquire) != nullptr;
}

void SetGuiThread(DWORD threadId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_guiThread = threadId;
}

bool EnsureWindow() {
    if (Ready()) return true;

    HINSTANCE instance = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_window.load(std::memory_order_acquire)) return true;

        // A message-only window is served by the message loop of the thread
        // that created it. Created on a thread that never pumps, it would
        // swallow every result silently, which is far harder to diagnose than
        // not being created.
        if (g_guiThread != 0 && ::GetCurrentThreadId() != g_guiThread) return false;

        // CreateWindowExW below sends WM_NCCREATE and WM_CREATE synchronously,
        // and those travel through the WH_CALLWNDPROC hook ChatInput installs
        // on this very thread -- whose first act is to call back in here. The
        // window is not stored yet at that point, so without this guard the
        // nested call would try to take a mutex this thread already holds, and
        // MSVC answers that with a thrown std::system_error. Out of a window
        // procedure that is not an error, it is a dead client. It killed
        // FulDC++ once per second before it was found.
        if (g_creating) return false;
        if (g_createAttempts >= kMaxCreateAttempts) return false;
        ++g_createAttempts;

        if (g_class == 0) {
            // RegisterClassExW sends no messages, so it is safe under the lock.
            WNDCLASSEXW cls = {};
            cls.cbSize = sizeof(cls);
            cls.lpfnWndProc = WindowProc;
            cls.hInstance = g_instance;
            cls.lpszClassName = kClassName;
            g_class = ::RegisterClassExW(&cls);
            if (g_class == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        }

        g_creating = true;
        instance = g_instance;
    }

    HWND hwnd = ::CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  instance, nullptr);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_creating = false;
    if (!hwnd) return false;

    g_createAttempts = 0;
    g_window.store(hwnd, std::memory_order_release);
    return true;
}

bool Post(uint64_t jobId) {
    HWND hwnd = g_window.load(std::memory_order_acquire);
    if (!hwnd) return false;
    return ::PostMessageW(hwnd, kMsgResult, static_cast<WPARAM>(jobId), 0) != FALSE;
}

void Destroy() {
    HWND hwnd = g_window.load(std::memory_order_acquire);
    if (hwnd) {
        if (::GetWindowThreadProcessId(hwnd, nullptr) == ::GetCurrentThreadId()) {
            ::DestroyWindow(hwnd);
        } else {
            DWORD_PTR unused = 0;
            // Blocking, so the window is gone before the DLL unloads. The
            // timeout is there because a wedged GUI thread must not turn a
            // client shutdown into a hang.
            ::SendMessageTimeoutW(hwnd, kMsgDestroy, 0, 0, SMTO_ABORTIFHUNG,
                                  kDestroyTimeoutMs, &unused);
        }
        g_window.store(nullptr, std::memory_order_release);
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_handler = nullptr;
    g_createAttempts = 0;
    if (g_class != 0) {
        ::UnregisterClassW(kClassName, g_instance);
        g_class = 0;
    }
}

}  // namespace Marshal
