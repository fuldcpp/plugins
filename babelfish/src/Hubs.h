// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>
#include <vector>

// Which hubs the client is on.
//
// DCHub can add, find and remove a hub but cannot list them, so the settings
// dialog would have nothing to offer if it only had that interface. The hub
// hooks fill the gap: dcpp.hubs.onOnline and onOffline announce every change,
// and every other hook the plugin already has hands over a HubData with a url
// in it. Between them that is a complete enough picture to tick a box against.
//
// The one blind spot is a hub that was already connected when the plugin
// loaded, which announces nothing. Those appear as soon as anything happens in
// them, and a hub already in the automatic list is always listed regardless.
namespace Hubs {

void Online(const std::string& url);
void Offline(const std::string& url);

// Any hub the plugin has laid eyes on, from any hook. Cheap enough to call on
// every message.
void Seen(const std::string& url);

// Currently online, in the order they were first seen.
std::vector<std::string> Connected();

bool IsConnected(const std::string& url);

// Asks the host directly whether a url is a hub it is on.
//
// The hooks only announce changes, so a hub that was already up when the plugin
// loaded is invisible to them -- which showed as "not connected" next to a hub
// the client was plainly connected to. find_hub answers for the hubs the client
// actually has, and that is a better answer than a tally of what was witnessed.
// Set from dllmain, which is the only place that may touch the plugin API.
using Probe = bool (*)(const std::string& url);
void SetProbe(Probe probe);

// The probe's answer where there is one, the witnessed tally otherwise.
bool LooksConnected(const std::string& url);

void Clear();

}  // namespace Hubs
