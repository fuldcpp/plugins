// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Incoming.h"

#include <deque>
#include <mutex>

namespace Incoming {
namespace {

// A hundred lines is a few minutes of a busy hub and a whole evening of a quiet
// one. Beyond that the user has scrolled somewhere they are unlikely to ask
// about, and the alternative -- keeping everything -- is a leak with a nicer
// name.
constexpr size_t kMaxLines = 100;

// Shorter than this and a message would match almost any line it appears in,
// which is how the wrong hub would end up answering.
constexpr size_t kMinMatchLength = 4;

// A wrapped row has to carry more than that before it is taken as evidence,
// because it is being matched against every message in the ring rather than
// against one.
constexpr size_t kMinFragmentLength = 12;

// The part of a displayed line after the timestamp and the nick, which is the
// part that can be looked for inside a message.
std::string Payload(const std::string& displayedLine) {
    size_t at = 0;
    const size_t close = displayedLine.find('>');
    if (close != std::string::npos && close + 1 < displayedLine.size()) at = close + 1;
    while (at < displayedLine.size() && displayedLine[at] == ' ') ++at;
    return displayedLine.substr(at);
}

std::mutex g_mutex;
std::deque<Line> g_lines;

}  // namespace

bool LineContains(const std::string& displayedLine, const std::string& message,
                  size_t* matchPos) {
    if (matchPos) *matchPos = std::string::npos;
    if (message.size() < kMinMatchLength) return false;

    const size_t at = displayedLine.find(message);
    if (at != std::string::npos) {
        if (matchPos) *matchPos = at;
        return true;
    }

    // The other direction: one wrapped row out of a longer message.
    const std::string payload = Payload(displayedLine);
    if (payload.size() < kMinFragmentLength) return false;
    return message.find(payload) != std::string::npos;
}

void Remember(const Line& line) {
    if (line.text.empty()) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_lines.push_back(line);
    while (g_lines.size() > kMaxLines) g_lines.pop_front();
}

bool Find(const std::string& displayedLine, Line& out, size_t* matchPos) {
    std::lock_guard<std::mutex> lock(g_mutex);

    // Newest first: somebody who has said "ok thanks" twice means the one they
    // just clicked, which is almost always the later one.
    for (auto it = g_lines.rbegin(); it != g_lines.rend(); ++it) {
        size_t at = std::string::npos;
        if (LineContains(displayedLine, it->text, &at)) {
            out = *it;
            if (matchPos) *matchPos = at;
            return true;
        }
    }
    return false;
}

void Clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_lines.clear();
}

std::string NewestHub() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_lines.empty() ? std::string() : g_lines.back().hubUrl;
}

size_t Count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_lines.size();
}

}  // namespace Incoming
