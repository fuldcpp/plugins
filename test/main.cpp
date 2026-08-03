// Squiggle - spell checking for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
// Fristaende test av tokenisering + stavningsmotor, utan DC-klienten.
// Kor: build\Release\squiggletest.exe
//
// Utan argument kors alla kontroller och programmet avslutas med 0 bara om
// samtliga gick igenom, sa det gar att kora fran ett skript.

#include <windows.h>

#include <cstdio>
#include <cwctype>
#include <string>
#include <thread>
#include <vector>

#include "AutoCorrect.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "Speller.h"
#include "Strings.h"
#include "Text.h"
#include "Tokenizer.h"

namespace {

int g_failures = 0;

void PrintUtf8(const std::wstring& s) {
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    std::fputs(out.c_str(), stdout);
}

void Expect(bool condition, const std::wstring& what) {
    std::fputs(condition ? "  ok    " : "  FEL   ", stdout);
    PrintUtf8(what);
    std::fputs("\n", stdout);
    if (!condition) ++g_failures;
}

// The tokens a line produces, joined with '|'. Comparing the whole set at once
// is what makes a rule change visible: a filter that starts swallowing a word it
// used to keep shows up as a diff rather than as silence.
std::wstring Tokens(const std::wstring& line) {
    std::wstring out;
    for (const Range& r : TokenizeForSpelling(line)) {
        if (!out.empty()) out += L'|';
        out += Slice(line, r);
    }
    return out;
}

void ExpectTokens(const std::wstring& line, const std::wstring& expected) {
    const std::wstring actual = Tokens(line);
    const bool ok = actual == expected;

    std::wstring what = L"\"" + line + L"\"  ->  " + (actual.empty() ? L"(inget)" : actual);
    if (!ok) what += L"    forvantat: " + (expected.empty() ? L"(inget)" : expected);
    Expect(ok, what);
}

// The tokenizer and the word folding in Speller and Nicks all lean on the CRT's
// wide character classification. If that turns out to be ASCII only, "hornet"
// splits in two and "AKE" stops matching "ake" -- silently, and only for the
// language this plugin was written for.
void CheckWideCharacterClassification() {
    std::fputs("\nTeckenklassning (aao):\n", stdout);

    Expect(std::iswalpha(static_cast<wint_t>(L'ö')) != 0, L"iswalpha('ö')");
    Expect(std::iswalpha(static_cast<wint_t>(L'Å')) != 0, L"iswalpha('Å')");
    Expect(std::iswalpha(static_cast<wint_t>(L'ä')) != 0, L"iswalpha('ä')");
    Expect(std::iswupper(static_cast<wint_t>(L'Ä')) != 0, L"iswupper('Ä')");
    Expect(std::iswlower(static_cast<wint_t>(L'ä')) != 0, L"iswlower('ä')");

    // towlower/towupper are the odd ones out: in the default "C" locale they map
    // ASCII only, which is why the folding goes through Text:: instead.
    Expect(Text::Fold(L"ÅKE Ödén") == L"åke ödén", L"Text::Fold tar aven aao");
    Expect(Text::Upper(L"räksmörgås") == L"RÄKSMÖRGÅS", L"Text::Upper tar aven aao");
    Expect(Text::Fold(L"TEH") == L"teh", L"Text::Fold tar ascii");
}

void CheckTokenizer() {
    std::fputs("\nTokenisering:\n", stdout);

    // Vanlig text.
    ExpectTokens(L"hej allihopa hur mår ni idag", L"hej|allihopa|hur|mår|ni|idag");
    ExpectTokens(L"hej, då!", L"hej|då");

    // Det som aldrig ska kontrolleras.
    ExpectTokens(L"kolla in http://example.com/nagot.zip tack", L"kolla|in|tack");
    ExpectTokens(L"www.example.se ligger nere", L"ligger|nere");
    ExpectTokens(L"magnet:?xt=urn:tree:tiger:ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", L"");
    ExpectTokens(L"C:\\Program Files\\FulDC++\\FulDC.exe", L"");
    ExpectTokens(L"skriv till mig pa nagon@example.se", L"skriv|till|mig|pa");
    ExpectTokens(L"/me testar en kommandorad med felstavvat ord", L"");
    ExpectTokens(L"filen är 4,7 GB stor", L"filen|är|stor");
    ExpectTokens(L"ok LOL BRB CU", L"ok");
    ExpectTokens(L"DarkAngel skrev nyss", L"skrev|nyss");

    // Ett kommando galler bara sin egen rad.
    ExpectTokens(L"/me hej\nhej dar", L"hej|dar");

    // Bindestreck delar, precis som forr.
    ExpectTokens(L"e-post", L"post");  // "e" ar for kort for att kontrolleras

    // Skiljetecken som halls ihop for URL:ernas skull far inte hamna inne i ett
    // ord nar runan visade sig inte vara en URL.
    ExpectTokens(L"vad?varför", L"vad|varför");
    ExpectTokens(L"hej&hå", L"hej|hå");
    ExpectTokens(L"ett+annat", L"ett|annat");

    // Apostrof mellan bokstaver ar en del av ordet, i borjan eller slutet inte.
    ExpectTokens(L"don't stop", L"don't|stop");
    ExpectTokens(L"'citat'", L"citat");

    // Langa sammansattningar ar hela poangen med plugin-programmet och far inte
    // tystas av en langdgrans.
    ExpectTokens(L"arbetsmarknadsdepartementet", L"arbetsmarknadsdepartementet");
    ExpectTokens(L"bandbreddsproblematiken", L"bandbreddsproblematiken");
}

bool HasLanguage(const Speller& speller, const std::wstring& prefix) {
    for (const std::wstring& tag : speller.ActiveLanguages()) {
        if (tag.compare(0, prefix.size(), prefix) == 0) return true;
    }
    return false;
}

// The dictionary half only runs for the languages this machine actually has, so
// a missing Swedish dictionary is reported rather than counted as a failure.
void CheckDictionary(Speller& speller) {
    std::fputs("\nOrdlistor:\n", stdout);

    if (HasLanguage(speller, L"sv")) {
        Expect(speller.IsCorrect(L"hej"), L"sv: \"hej\" ar ratt");
        Expect(speller.IsCorrect(L"bandbreddsproblem"), L"sv: sammansattning ar ratt");
        Expect(!speller.IsCorrect(L"felstavvat"), L"sv: \"felstavvat\" ar fel");
        Expect(!speller.Suggest(L"felstavvat", 3).empty(), L"sv: forslag finns");
    } else {
        std::fputs("  (hoppar over svenska - ingen sv-ordlista installerad)\n", stdout);
    }

    if (HasLanguage(speller, L"en")) {
        Expect(speller.IsCorrect(L"download"), L"en: \"download\" ar ratt");
        Expect(!speller.IsCorrect(L"downlod"), L"en: \"downlod\" ar fel");
    } else {
        std::fputs("  (hoppar over engelska - ingen en-ordlista installerad)\n", stdout);
    }

    // Sjalva poangen: ett ord racker att vara ratt i ett av spraken.
    if (HasLanguage(speller, L"sv") && HasLanguage(speller, L"en")) {
        Expect(speller.IsCorrect(L"hej") && speller.IsCorrect(L"download"),
               L"blandsprak: bada godtas samtidigt");
    }

    // Egna ord och sessionsignorering ska sla igenom oavsett ordlista, och vara
    // okansliga for versaler.
    speller.IgnoreForSession(L"Kalasbrallor");
    Expect(speller.IsCorrect(L"kalasbrallor"), L"ignorerat ord ar okansligt for versaler");

    speller.IgnoreForSession(L"Rödbetssallad");
    Expect(speller.IsCorrect(L"rödbetssallad"), L"... aven med aao i ordet");
}

}  // namespace

int main(int argc, char** argv) {
    ::SetConsoleOutputCP(CP_UTF8);

    Speller speller;
    if (!speller.Init({L"sv-SE", L"en-US", L"en-GB"})) {
        std::fputs("Ingen stavningsmotor tillganglig.\n", stdout);
        return 1;
    }

    // "squiggletest thread" reproduces the bug that shipped twice: the checkers
    // are COM objects created on the thread the host loads the plugin on, but
    // every lookup comes from the GUI thread. Without the rebind, every call
    // fails and the failure looks exactly like "misspelled, no suggestions".
    //
    // The answers are compared against a speller created on this thread rather
    // than against a fixed list of Swedish words: what is being tested is that
    // the thread the checkers were built on makes no difference, and a machine
    // without a Swedish dictionary would otherwise fail the test for a reason
    // that has nothing to do with threads.
    if (argc > 1 && std::string(argv[1]) == "thread") {
        Speller crossThread;
        std::thread loader([&crossThread] {
            crossThread.Init({L"sv-SE", L"en-US", L"en-GB"});
        });
        loader.join();

        std::fputs("Motorer skapade pa en annan trad, jamfort med den har traden:\n", stdout);

        for (const wchar_t* w : {L"jag", L"felstava", L"hej", L"datorn", L"download",
                                 L"felstavvat", L"downlod"}) {
            const bool expected = speller.IsCorrect(w);
            const bool actual = crossThread.IsCorrect(w);
            Expect(actual == expected,
                   std::wstring(w) + L" -> " + (actual ? L"ratt" : L"fel") +
                       (actual == expected ? L"" : L"    (samma trad sa tvartom)"));
        }

        const std::vector<std::wstring> mine = speller.Suggest(L"felstavvat", 3);
        const std::vector<std::wstring> theirs = crossThread.Suggest(L"felstavvat", 3);
        Expect(mine == theirs, L"samma forslag for \"felstavvat\" fran bada tradarna");
        if (mine.empty()) {
            std::fputs("  (obs: inget sprak har forslag for ordet, sa forslagsdelen "
                       "sager mindre an vanligt)\n", stdout);
        }

        crossThread.Shutdown();
        speller.Shutdown();

        std::printf("\n%s\n", g_failures == 0 ? "RESULTAT: godkant" : "RESULTAT: UNDERKANT");
        return g_failures == 0 ? 0 : 1;
    }

    // "squiggletest langs" prints how each installed language will be labelled.
    if (argc > 1 && std::string(argv[1]) == "langs") {
        const std::vector<std::wstring> installed = Speller::InstalledLanguages();
        if (installed.empty()) {
            std::fputs("Inga sprak alls - kontrollera att COM gar att initiera harifran.\n", stdout);
            return 1;
        }

        for (const wchar_t* forced : {L"sv", L"en"}) {
            Strings::SetLanguageOverride(forced);
            std::fputs("--- ", stdout);
            PrintUtf8(forced);
            std::fputs(" ---\n", stdout);

            for (const std::wstring& tag : installed) {
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
        // "squiggletest dialog en" forces English so the layout can be checked in
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
    std::fputs("\n", stdout);

    CheckWideCharacterClassification();
    CheckTokenizer();
    CheckDictionary(speller);

    speller.Shutdown();

    std::printf("\n%s\n", g_failures == 0 ? "RESULTAT: godkant"
                                          : "RESULTAT: UNDERKANT");
    if (g_failures != 0) std::printf("%d kontroll(er) misslyckades\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
