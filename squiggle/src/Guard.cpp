// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Guard.h"

#include <atomic>
#include <exception>

namespace Guard {
namespace {

using Logger = void (*)(const std::string&);

// Written once at startup and read from the GUI thread afterwards, so an atomic
// rather than a plain pointer.
std::atomic<Logger> g_logger{nullptr};

// One slot per callback in the plugin, with a few to spare.
//
// Deliberately not a mutex: this runs inside the failure it is reporting, and a
// lock taken here could be one the failing thread already holds. Waiting on it
// would throw std::system_error out of a noexcept function, which is the exact
// shape of crash this file exists to prevent.
constexpr size_t kMaxSites = 8;
std::atomic<const char*> g_seen[kMaxSites];

// True the first time a given callback reports. Sites are string literals, so
// comparing the addresses is enough.
bool FirstTime(const char* where) {
    for (std::atomic<const char*>& slot : g_seen) {
        const char* seen = slot.load(std::memory_order_relaxed);
        if (seen == nullptr) {
            const char* expected = nullptr;
            if (slot.compare_exchange_strong(expected, where, std::memory_order_relaxed)) {
                return true;
            }
            // Another thread claimed the slot between the read and the write.
            // It may well have written this same site, so it still has to be
            // compared.
            seen = expected;
        }
        if (seen == where) return false;
    }
    return true;  // more sites than slots: say something rather than nothing
}

}  // namespace

void SetLogger(Logger logger) {
    g_logger.store(logger, std::memory_order_relaxed);
}

void Note(const char* where, const char* what) noexcept {
    const Logger logger = g_logger.load(std::memory_order_relaxed);
    if (!logger || !where) return;
    if (!FirstTime(where)) return;

    try {
        std::string line = "internt fel i ";
        line += where;
        if (what && *what) {
            line += ": ";
            line += what;
        }
        line += ". Atgarden hoppades over, klienten lever vidare.";
        logger(line);
    } catch (...) {
        // A log line is not worth a second failure on the way out of the first.
    }
}

void NoteCaught(const char* where) noexcept {
    try {
        throw;  // hands us back whatever the calling catch block is holding
    } catch (const std::exception& error) {
        Note(where, error.what());
    } catch (...) {
        Note(where, nullptr);
    }
}

}  // namespace Guard
