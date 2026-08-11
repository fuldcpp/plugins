// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>

// Nothing may be thrown out of a procedure the host or Windows calls into.
//
// There is no C++ frame of ours above one: a subclass procedure is called from
// inside user32's message dispatch, a message hook from inside SendMessage, and
// the plugin's own entry points from the client's plugin manager. An exception
// that reaches those frames does not unwind to anything that can handle it, so
// it ends the client rather than the operation.
//
// This is not hypothetical. The sister plugin held a std::mutex across
// CreateWindowExW, which sends WM_NCCREATE synchronously through the plugin's
// own WH_CALLWNDPROC hook, which called back into the same function on the same
// thread: a recursive lock on a non-recursive mutex, a std::system_error out
// through user32, and FulDC++ died about once a second on its first live run.
//
// Squiggle has never been seen to do anything of the sort. The point is not the
// bug we know about but the one we do not: behind these guards, a fault anywhere
// in the plugin costs the keystroke it happened on and writes a line in the log,
// instead of taking the client down with it.
namespace Guard {

// Where the lines go: Squiggle's own Log(), installed at startup. Until then
// there is nowhere to write and notes are dropped.
void SetLogger(void (*logger)(const std::string&));

// Logs one line for an exception caught at a callback boundary.
//
// `where` names the callback and has to be a string literal: it is remembered by
// address, so a fault that repeats on every keystroke is reported the first time
// and then kept quiet rather than filling the client's log.
void Note(const char* where, const char* what) noexcept;

// The same, for a catch block that did not name the exception. Must be called
// from inside one -- it rethrows to find out what it is holding.
void NoteCaught(const char* where) noexcept;

// Runs body, and turns anything it throws into a note and `fallback`. For the
// callbacks whose failure value is a plain "not handled".
template <typename Result, typename Body>
Result Guarded(const char* where, Result fallback, Body&& body) {
    try {
        return body();
    } catch (...) {
        NoteCaught(where);
    }
    return fallback;
}

}  // namespace Guard
