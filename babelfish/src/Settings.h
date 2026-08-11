// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <map>
#include <string>
#include <vector>

// A thin wrapper over the host's config interface. Everything is stored as a
// string, the same way Squiggle does it, because the config API returns an
// empty value for a setting that was never written and an empty string is the
// only unambiguous "not configured" marker across every type here.
//
// Section 7.4: every key is optional. A clean install with nothing configured
// must translate, which is why the defaults below are the ones that need no
// account and no key.
struct Settings {
    // "mymemory" | "azure" | "deepl" | "google" | "claude"
    std::string backend = "mymemory";

    // Source language as a bare two-letter code. Filled in once on first run
    // from GetUserDefaultLocaleName(); the target is always English and is not
    // configurable.
    std::string sourceLang;

    std::string email;        // raises the MyMemory allowance to 50 000 chars/day
    std::string azureRegion;  // required for Azure multi-service keys

    // One key per service, not one key.
    //
    // It was a single field, on the reasoning that five fields in a dialog is
    // worse than one. That is true right up until somebody tries two services:
    // pasting a DeepL key overwrote the Claude one, and switching back gave a
    // 401 with nothing to explain it. Comparing services is exactly what people
    // do when they first get the plugin working.
    std::map<std::string, std::string> apiKeys;

    std::string ApiKeyFor(const std::string& service) const;
    void SetApiKeyFor(const std::string& service, const std::string& key);

    std::vector<std::string> autoHubs;  // hub urls translating without Ctrl+G

    // Whether automatic mode extends to private messages. Ctrl+G works in a
    // private message either way -- an explicit request is an explicit request.
    //
    // This was off while the private-message path was unproven, since it reads
    // UserData::hubHint and ::cid at offsets 8 and 16 and this client's structs
    // are known not to match the header everywhere. It has since been exercised
    // against a live client and works, so it follows the spec again: the
    // leading pointer fields are where the header says, and only the far end of
    // the struct diverges.
    bool translatePM = true;
    bool echoOriginal = true;

    // Rolling daily character count, so /tr status can report how much of a
    // free allowance is gone. Reset when the date changes.
    std::string quotaDate;      // "YYYY-MM-DD" in local time
    long long quotaChars = 0;

    // A hard stop, in characters per day. Zero switches it off.
    //
    // MyMemory enforces its own allowance; a paid service does not - it simply
    // bills. With automatic mode on several hubs there is nothing between a
    // runaway loop and the invoice, so the plugin carries its own brake. The
    // default is generous enough never to interrupt an ordinary evening and
    // small enough to catch something going wrong.
    long long dailyLimitChars = 20000;

    // Words the services must not touch, masked out of a message before it is
    // sent and put back afterwards -- the same path nicks take.
    //
    // Nobody can fill this in ahead of time: you find out that "polsa" comes
    // back as "hot dog" by watching it happen. So it is meant to be added to
    // after the fact, one word at a time, as each one betrays itself.
    std::vector<std::string> keepWords;

    // False when the word is too short to mask. Under three characters a word
    // matches inside too much else, and the placeholder machinery skips it
    // silently, which would look like the setting simply did nothing.
    bool AddKeepWord(const std::string& word);
    bool RemoveKeepWord(const std::string& word);

    static void Bind(void* config, const char* guid);

    void Load();
    void Save() const;

    bool IsAutoHub(const std::string& hubUrl) const;
    void SetAutoHub(const std::string& hubUrl, bool on);

    // Adds to today's counter, rolling the date over when needed. Returns the
    // new total.
    long long AddQuotaChars(size_t count);

    // The language the host runs its own interface in, as an IETF tag such as
    // "sv-SE", or empty when the host will not say. What the plugin's dialog
    // should speak: it is a panel inside somebody else's window, and matching
    // Windows instead of the client gets it wrong on any machine where the two
    // differ -- which is most of the machines a DC client runs on.
    static std::string HostLanguage();

    // Empty while everything is fine; otherwise the path that could not be
    // written, with the Windows error code.
    //
    // The plugin's files live beside its own DLL, wherever the host chose to
    // put that. Dropped into a folder under Program Files -- which is where an
    // AirDC++ install normally sits, and this client is often run portable from
    // one -- that folder is read-only to anyone not elevated, and every save
    // does nothing at all. The symptom is settings that reset on every restart
    // with nothing to explain it, which is the second time this plugin would
    // have lost settings silently. So the failure is remembered and reported.
    static std::string WriteFailure();

    // Directory the plugin's own files live in, with a trailing backslash.
    // Derived from the DLL's own module handle rather than the host's
    // get_install_path -- see the comment on the definition.
    static std::wstring DataDirectory();

    // Where the plugin's own files are kept: %LOCALAPPDATA%\Babelfish\.
    //
    // Deliberately not the plugin's install directory, which the client empties
    // on every update along with everything the plugin had written there. A
    // keep list is built one word at a time over months; losing it to an
    // update would make the feature not worth having.
    static std::wstring SettingsDirectory();

    // The same path as UTF-8, for the log and for /tr probe.
    static std::string SettingsDirectoryUtf8();
    static void SetModule(void* instance);
};

Settings& CurrentSettings();
