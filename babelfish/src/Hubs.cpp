// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Hubs.h"

#include <algorithm>
#include <mutex>

namespace Hubs {
namespace {

// A vector rather than a set: there are a handful of hubs, and keeping them in
// the order they turned up makes the settings dialog stable between openings.
std::mutex g_mutex;
std::vector<std::string> g_connected;
Probe g_probe = nullptr;

// The hooks run on whichever thread the client feels like -- the outgoing-chat
// hook demonstrably does not run on the GUI thread -- and the dialog reads this
// from the GUI thread, so the lock is not decoration.
std::vector<std::string>::iterator Find(const std::string& url) {
    return std::find(g_connected.begin(), g_connected.end(), url);
}

}  // namespace

void Online(const std::string& url) {
    if (url.empty()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (Find(url) == g_connected.end()) g_connected.push_back(url);
}

void Offline(const std::string& url) {
    if (url.empty()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = Find(url);
    if (it != g_connected.end()) g_connected.erase(it);
}

void Seen(const std::string& url) {
    // A message from a hub is proof it is connected, whether or not the online
    // hook was ever seen for it. This is what covers hubs that were already up
    // when the plugin was installed.
    Online(url);
}

std::vector<std::string> Connected() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_connected;
}

bool IsConnected(const std::string& url) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return Find(url) != g_connected.end();
}

void SetProbe(Probe probe) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_probe = probe;
}

bool LooksConnected(const std::string& url) {
    Probe probe = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        probe = g_probe;
    }
    if (probe && probe(url)) return true;
    return IsConnected(url);
}

void Clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_connected.clear();
}

}  // namespace Hubs
