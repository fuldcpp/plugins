// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ITranslator.h"

// Section 4:
//
//   hook (GUI thread) -> queue -> ONE worker thread -> PostMessage -> hidden
//   window (GUI thread) -> send
//
// Exactly one worker. Translating two messages in parallel to save latency puts
// them on the hub in the wrong order as soon as somebody types quickly, and
// order matters more than 300 ms.
namespace Queue {

// A queued message. Section 4: strings only, never pointers. A HubDataPtr or
// UserDataPtr can be dead by the time the translation comes back 800 ms later
// because the hub disconnected, so the destination is looked up again at send
// time from the url and cid stored here.
struct Job {
    uint64_t id = 0;

    std::string hubUrl;
    std::string cid;    // empty for main chat
    std::string nick;   // display only; the lookup goes through cid
    bool isPm = false;

    // The host's own object, exactly as the hook handed it over.
    //
    // Section 4 says to keep strings and never pointers, because a hub can drop
    // while a translation is in flight. That reasoning is sound and the url is
    // still carried for it -- but find_hub has turned out to return objects
    // that pass every check and still do not work: sendHubMessage silently
    // sends nothing through one. The host's own pointer does work; it is what
    // the local echo and every /tr reply go through.
    //
    // So this is kept as a preference, guarded rather than trusted: it is only
    // used if it still reads as a valid hub whose url matches the one recorded
    // above, which a freed and reused block will not. If that check fails the
    // code falls back to find_hub, and if that is no good either the job is
    // dropped and logged.
    void* hubHandle = nullptr;
    void* userHandle = nullptr;

    std::string text;          // what to translate, with any "/me " removed
    bool thirdPerson = false;  // put back through the send call's own flag

    std::string sourceLang;

    // Outgoing chat always goes into English -- that is the whole design. A
    // line somebody else wrote goes the other way, into the language the user
    // reads, so the target cannot be a constant any more.
    std::string targetLang = "en";

    // Not sent anywhere: shown in the window the line came from, under the
    // original. Translating incoming chat is about understanding it, and
    // nobody else needs to see the result.
    bool incoming = false;

    TranslatorConfig config;

    // Nicks from the hub the message is going to, snapshotted on the GUI thread
    // where the user list can be read. Longest first.
    std::vector<std::string> nicks;
};

// Where a finished job goes. Implemented in dllmain.cpp because it is the only
// place that touches the plugin API; every call happens on the GUI thread.
struct Sink {
    virtual ~Sink() = default;

    // Sends the message. translated is false when the original is going out
    // unchanged because something failed -- section 5 forbids swallowing a
    // message, so this is always called.
    virtual void Deliver(const Job& job, const std::string& text, bool translated) = 0;

    // Writes a line into the chat window the job came from. Used for the local
    // echo and for telling the user the quota is gone, which section 7.6
    // requires be visible rather than a silent downgrade.
    virtual void Notice(const Job& job, const std::string& message) = 0;

    virtual void Log(const std::string& message) = 0;
};

// Starts the worker thread. Safe to call once; later calls do nothing.
void Start(Sink* sink);

// Section 2. Sets the stop flag, aborts any request in flight so the worker
// does not sit out its receive timeout, wakes the worker and joins it. Missing
// the join is what crashes the client on exit when the DLL unloads underneath a
// call still in progress.
void Stop();

// Queues a job and returns its id. Zero means the queue is not running.
uint64_t Push(Job job);

// Called on the GUI thread for each id the worker posts back.
void DeliverResult(uint64_t jobId);

// How many jobs are waiting or in flight. For /tr status.
size_t Pending();

// The service that last answered a real request, and when, as "Claude 22:31".
// Empty until one has. A cache hit does not count -- it never reached anybody.
//
// This exists because "which service am I actually using" is not answerable
// from the configured name alone, and the honest answer matters when the answer
// costs money.
std::string LastService();

}  // namespace Queue
