// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
// Fristaende test av tokenisering + stavningsmotor, utan DC-klienten.
// Kor: build\Release\squiggletest.exe

#include <windows.h>

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "AutoCorrect.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "Speller.h"
#include "Strings.h"
#include "Tokenizer.h"

namespace {

void PrintUtf8(const std::wstring& s) {
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    std::fputs(out.c_str(), stdout);
}

void Check(Speller& speller, const std::wstring& line) {
    std::fputs("  \"", stdout);
    PrintUtf8(line);
    std::fputs("\"\n", stdout);

    const std::vector<Range> words = TokenizeForSpelling(line);
    bool any = false;
    for (const Range& r : words) {
        const std::wstring w = Slice(line, r);
        if (speller.IsCorrect(w)) continue;

        any = true;
        std::fputs("      FEL: ", stdout);
        PrintUtf8(w);

        const std::vector<std::wstring> sug = speller.Suggest(w, 3);
        if (!sug.empty()) {
            std::fputs("  ->  ", stdout);
            for (size_t i = 0; i < sug.size(); ++i) {
                if (i) std::fputs(", ", stdout);
                PrintUtf8(sug[i]);
            }
        }
        std::fputs("\n", stdout);
    }
    if (!any) std::fputs("      (rent)\n", stdout);
}

}  // namespace

int main(int argc, char** argv) {
    ::SetConsoleOutputCP(CP_UTF8);

    Speller speller;
    if (!speller.Init({L"sv-SE", L"en-US", L"en-GB"})) {
        std::fputs("Ingen stavningsmotor tillganglig.\n", stdout);
        return 1;
    }

    // "stavtest dialog" opens the settings dialog on its own, so the resource
    // template and control wiring can be checked without a DC client.
    // "squiggletest thread" reproduces the bug that shipped twice: the checkers
    // are COM objects created on the thread the host loads the plugin on, but
    // every lookup comes from the GUI thread. Without the rebind, every call
    // fails and the failure looks exactly like "misspelled, no suggestions".
    if (argc > 1 && std::string(argv[1]) == "thread") {
        Speller crossThread;
        std::thread loader([&crossThread] {
            crossThread.Init({L"sv-SE", L"en-US", L"en-GB"});
        });
        loader.join();

        std::printf("Motorer skapade pa en annan trad. Kontrollerar harifran:\n");
        int wrong = 0;
        for (const wchar_t* w : {L"jag", L"felstava", L"hej", L"datorn"}) {
            const bool ok = crossThread.IsCorrect(w);
            std::fputs(ok ? "   OK   " : "   FEL  ", stdout);
            PrintUtf8(w);
            std::fputs("\n", stdout);
            if (!ok) ++wrong;
        }

        const std::vector<std::wstring> sug = crossThread.Suggest(L"felstavvat", 3);
        std::printf("Forslag for 'felstavvat': %zu\n", sug.size());

        std::printf(wrong == 0 && !sug.empty() ? "\nRESULTAT: godkant\n"
                                               : "\nRESULTAT: UNDERKANT\n");
        crossThread.Shutdown();
        return wrong == 0 && !sug.empty() ? 0 : 1;
    }

    // "stavtest langs" prints how each installed language will be labelled.
    if (argc > 1 && std::string(argv[1]) == "langs") {
        for (const wchar_t* forced : {L"sv", L"en"}) {
            Strings::SetLanguageOverride(forced);
            std::fputs("--- ", stdout);
            PrintUtf8(forced);
            std::fputs(" ---\n", stdout);

            for (const std::wstring& tag : Speller::InstalledLanguages()) {
                const LCTYPE type = (Strings::Current() == Strings::Lang::English)
                                        ? LOCALE_SENGLISHDISPLAYNAME
                                        : LOCALE_SLOCALIZEDDISPLAYNAME;
                wchar_t buf[256] = {};
                const int n = ::GetLocaleInfoEx(tag.c_str(), type, buf, 256);
                std::fputs("   ", stdout);
                PrintUtf8(tag);
                std::fputs("  ->  ", stdout);
                PrintUtf8(n > 1 ? buf : tag.c_str());
                std::fputs("\n", stdout);
            }
        }
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "dialog") {
        // "stavtest dialog en" forces English so the layout can be checked in
        // the language that has the longer strings.
        if (argc > 2) {
            const std::string tag(argv[2]);
            Strings::SetLanguageOverride(std::wstring(tag.begin(), tag.end()));
            CurrentSettings().uiLanguage = std::wstring(tag.begin(), tag.end());
        }

        AutoCorrect::SetPath(L"autocorrect-test.txt");
        AutoCorrect::Load();
        speller.SetPersonalPath(L"personal-words-test.txt");
        speller.LoadPersonal();

        const bool ok = ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, speller);
        std::printf("Dialogen stangdes med %s\n", ok ? "OK" : "Avbryt");
        return 0;
    }

    std::fputs("Aktiva sprak:", stdout);
    for (const std::wstring& tag : speller.ActiveLanguages()) {
        std::fputs(" ", stdout);
        PrintUtf8(tag);
    }
    std::fputs("\n\n", stdout);

    const std::wstring cases[] = {
        // Ska vara rena
        L"hej allihopa hur mår ni idag",
        L"jag laddar ner filen från hubben nu",
        L"det där är ett bandbreddsproblem",
        L"kolla in länken http://example.com/nagot.zip tack",
        L"magnet:?xt=urn:tree:tiger:ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
        L"DarkAngel skrev nyss i chatten",
        L"/me testar en kommandorad med felstavvat ord",
        L"ok LOL BRB CU",
        L"filen är 4,7 GB stor och tar 20 min",
        L"C:\\Program Files\\FulDC++\\FulDC.exe",
        // Ska ge traff
        L"detta är felstavvat och konstitt",
        L"jag har ett stavnigskontroll problem",
        // Blandsprak - visar luckan tills engelskan ar pa plats
        L"jag ska download the file sen",
    };

    for (const std::wstring& c : cases) Check(speller, c);

    speller.Shutdown();
    return 0;
}
