// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

// The last messages that arrived, and which hub each came from.
//
// This exists to answer a question the window tree cannot: when somebody clicks
// a line in a chat log and asks for it to be translated, which hub is that? The
// control itself says nothing, and find_hub has proved unreliable enough that
// two crashes came out of trusting it.
//
// The incoming-chat hook, on the other hand, hands over a live HubData with
// every message. Remembering the last hundred lines with their hub means a line
// the user points at can be recognised, and the answer written back through the
// host's own pointer rather than a lookup.
namespace Incoming {

struct Line {
    std::string hubUrl;
    void* hubHandle = nullptr;  // the host's own object, from the hook
    std::string nick;
    std::string text;
};

void Remember(const Line& line);

// Finds the remembered message inside a line of chat as it is displayed --
// which carries a timestamp and a nick the plugin never saw. Most recent first,
// so repeating "ok" finds the latest one. Returns false when nothing matches,
// which means the line is older than the ring or was never a chat message.
// matchPos comes back as the offset where the message begins in the displayed
// line, or npos when the match went the other way -- see LineContains.
bool Find(const std::string& displayedLine, Line& out, size_t* matchPos = nullptr);

// The matching rule on its own, so the harness can exercise it without a hub.
//
// It works in both directions. A short message sits whole inside the line as
// drawn, timestamp and nick included. A long one is word-wrapped across several
// rows, and clicking it yields one row -- a fragment of the message rather than
// the whole of it. Requiring only the first case meant long lines could not be
// translated at all.
bool LineContains(const std::string& displayedLine, const std::string& message,
                  size_t* matchPos = nullptr);

// The hub the newest remembered line came from, or empty when nothing has
// arrived yet.
//
// Somewhere to say "I cannot place that line" out loud. That branch is the one
// case where the hub is exactly what is missing, so it used to be written to
// the log and nowhere else -- and a keypress that produces no visible result is
// indistinguishable from a broken keypress. The newest arrival is not
// guaranteed to be the window being read, but it is a window the user has open,
// and any window beats silence.
std::string NewestHub();

void Clear();
size_t Count();

}  // namespace Incoming
