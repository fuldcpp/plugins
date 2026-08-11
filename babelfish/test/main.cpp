// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
//
// Standalone harness for everything that does not need a running DC client:
// the message filter, the length splitting, the JSON reader and the cache. The
// cases here are the ones from section 9 of the spec that can be checked
// without a hub.
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "Cache.h"
#include "Incoming.h"
#include "Json.h"
#include "Languages.h"
#include "MessageFilter.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL  %s\n", what);
    }
}

void CheckEqual(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL  %s\n      got  [%s]\n      want [%s]\n", what, got.c_str(),
                    want.c_str());
    }
}

// ---------------------------------------------------------------------------

void TestShouldTranslate() {
    using MessageFilter::ShouldTranslate;

    Check(ShouldTranslate("kan du dela mappen med mig"), "plain Swedish is translated");
    Check(ShouldTranslate("jag forstar inte vad du menar"), "ASCII Swedish is translated");
    Check(ShouldTranslate("har du hela skivan pa hyllan"), "Swedish without diacritics");

    // Section 9.
    Check(!ShouldTranslate("hej"), "under four characters is left alone");
    Check(!ShouldTranslate("ok thanks"), "English is left alone");
    Check(!ShouldTranslate("what is the hub address"), "more English is left alone");
    Check(!ShouldTranslate("magnet:?xt=urn:tree:tiger:ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDEFGH"),
          "magnet links are left alone");
    Check(!ShouldTranslate("kolla magnet:?xt=urn:tree:tiger:ABCDEFG har du den"),
          "a magnet anywhere in the line is left alone");
    Check(!ShouldTranslate("/away tillbaka senare"), "slash commands are left alone");
    Check(!ShouldTranslate("+rules"), "plus commands are left alone");
    Check(!ShouldTranslate("!kick nagon"), "bang commands are left alone");
    Check(!ShouldTranslate("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDEFG"), "a bare TTH is left alone");
    Check(!ShouldTranslate("https://example.com/nagon/lang/url"), "a bare url is left alone");
    Check(!ShouldTranslate("Some.Film.2019.1080p.mkv"), "a bare filename is left alone");
    Check(!ShouldTranslate(":-) :-) !!!"), "punctuation only is left alone");
    Check(!ShouldTranslate("   "), "whitespace only is left alone");

    // A link with a real sentence around it is still worth translating.
    Check(ShouldTranslate("kolla har https://example.com den ar jattebra"),
          "a link with a sentence around it is translated");

    // Ctrl+G overrides the savings rules but not the safety ones.
    Check(ShouldTranslate("ok thanks", true), "Ctrl+G translates English anyway");
    Check(!ShouldTranslate("hej", true), "Ctrl+G still leaves a too-short word alone");
    Check(!ShouldTranslate("/away senare", true), "Ctrl+G still leaves commands alone");
    Check(!ShouldTranslate("magnet:?xt=urn:tree:tiger:ABC", true),
          "Ctrl+G still leaves magnets alone");
}

// Every case here is a real line from two days of live logs, each of which was
// sent off to be translated and came back unchanged.
void TestChatNoise() {
    using MessageFilter::IsChatNoise;
    using MessageFilter::ShouldTranslate;

    Check(IsChatNoise("haha"), "haha is noise");
    Check(IsChatNoise("HAHAHA"), "shouted laughter is noise");
    Check(IsChatNoise("hehe"), "hehe is noise");
    Check(IsChatNoise("LOL LOL LOL"), "repeated lol is noise");
    Check(IsChatNoise("LOOOOOOOOOOOOOOOOOOOOOL"), "a long lol is noise");
    Check(IsChatNoise("omfg"), "omfg is noise");
    Check(IsChatNoise("xd"), "xd is noise");
    Check(IsChatNoise("haha :)"), "laughter with punctuation is noise");

    // The rule must not swallow real messages that merely contain a laugh.
    Check(!IsChatNoise("haha vad kul du ar"), "laughter plus a sentence is not noise");
    Check(!IsChatNoise("hall"), "hall is a word, not laughter");
    Check(!IsChatNoise("hela"), "hela is a word, not laughter");
    Check(!IsChatNoise("loss"), "loss is a word, not laughter");
    Check(!IsChatNoise("hoppas"), "hoppas is a word, not laughter");

    Check(!ShouldTranslate("haha"), "noise is never translated");
    Check(!ShouldTranslate("haha", true), "not even when asked: there is nothing to translate");
    Check(ShouldTranslate("haha vad kul du ar"), "a sentence around a laugh still is");
}

void TestNickMasking() {
    using MessageFilter::MaskNicks;
    using MessageFilter::UnmaskNicks;

    const std::vector<std::string> nicks = {"MidnightWanderer", "LonelyPirate", "Kaje", "mango"};

    MessageFilter::Masked m = MaskNicks("kan du dela med dig Kaje", nicks);
    CheckEqual(m.text, "kan du dela med dig {1}", "a nick becomes a token");
    Check(m.nicks.size() == 1, "one nick was taken out");

    std::string out;
    Check(UnmaskNicks(m, "can you share with me {1}", out), "the token comes back");
    CheckEqual(out, "can you share with me Kaje", "the nick is restored");

    // A name inside a longer word must be left alone.
    m = MaskNicks("Kajens fil", nicks);
    CheckEqual(m.text, "Kajens fil", "a nick inside a longer word is not touched");

    // Case does not matter for finding, but the user's spelling is what returns.
    m = MaskNicks("KAJE har den", nicks);
    CheckEqual(m.text, "{1} har den", "matching ignores case");
    Check(UnmaskNicks(m, "{1} has it", out), "restores");
    CheckEqual(out, "KAJE has it", "the spelling the user typed comes back");

    // Two people in one line, longest first so nothing is left in fragments.
    m = MaskNicks("mango och LonelyPirate delar", nicks);
    Check(m.nicks.size() == 2, "both nicks are taken out");
    Check(UnmaskNicks(m, m.text, out), "a passthrough restores");
    CheckEqual(out, "mango och LonelyPirate delar", "both come back in place");

    // A service that eats a placeholder must not produce a half-restored line.
    m = MaskNicks("hej Kaje", nicks);
    Check(!UnmaskNicks(m, "hello there", out), "a missing token is refused");
    Check(!UnmaskNicks(m, "{1} hello {1}", out), "a duplicated token is refused");

    // Nothing to do when the message mentions nobody.
    m = MaskNicks("har du filmen", nicks);
    CheckEqual(m.text, "har du filmen", "a message without nicks is unchanged");
    Check(m.nicks.empty(), "and nothing was recorded");
}

// Words the user asked to be left alone go through the nick machinery, so what
// is worth testing is the merge that feeds it and one round trip.
//
// The case that started this: "pölsa" came back from Claude as "hot dog", and
// then the correction "nej, pölsa" came back as "no, sausage". The model was not
// stuck for an equivalent -- it had found "pølse", which is Danish, and was
// confident. Nothing in a prompt fixes that; the word has to leave the message.
void TestKeepWords() {
    using MessageFilter::MaskNicks;
    using MessageFilter::MergeMaskWords;
    using MessageFilter::UnmaskNicks;

    const std::vector<std::string> nicks = {"LonelyPirate", "Kaje", "mango"};
    const std::vector<std::string> keep = {"pölsa", "kesfil"};

    std::vector<std::string> merged = MergeMaskWords(nicks, keep);
    Check(merged.size() == 5, "every word survives the merge");
    for (size_t i = 1; i < merged.size(); ++i) {
        Check(merged[i - 1].size() >= merged[i].size(), "the merge is longest first");
    }

    // A nick that is also a keep word is one word, not two placeholders.
    merged = MergeMaskWords({"Kaje", "mango"}, {"kaje"});
    Check(merged.size() == 2, "a duplicate across the two lists is dropped");

    // The round trip that matters.
    MessageFilter::Masked m = MaskNicks("i går åt jag pölsa", MergeMaskWords(nicks, keep));
    CheckEqual(m.text, "i går åt jag {1}", "a kept word is taken out");

    std::string out;
    Check(UnmaskNicks(m, "yesterday I ate {1}", out), "the token comes back");
    CheckEqual(out, "yesterday I ate pölsa", "and the word is untouched");

    // The correction that repeated the error is the same path.
    m = MaskNicks("nej, pölsa", MergeMaskWords(nicks, keep));
    Check(UnmaskNicks(m, "no, {1}", out), "the short correction restores too");
    CheckEqual(out, "no, pölsa", "the word the user insisted on survives");

    // Case is ignored when matching, and the user's own spelling returns.
    m = MaskNicks("Pölsa är gott", MergeMaskWords({}, keep));
    Check(UnmaskNicks(m, "{1} is good", out), "restores");
    CheckEqual(out, "Pölsa is good", "capitalisation is the user's, not the list's");

    // And it is still a whole-word match, so a longer word is left to the
    // translator: "pölsan" is not "pölsa".
    m = MaskNicks("pölsan var god", MergeMaskWords({}, keep));
    CheckEqual(m.text, "pölsan var god", "an inflected form is not masked");

    // A message that is nothing but a kept word must not be sent at all. It
    // masks down to "{1}", and asking a service to translate that spends a
    // request to be told what we already knew - or worse, invites it to
    // invent something in place of a placeholder it did not understand.
    using MessageFilter::AnythingToTranslate;

    m = MaskNicks("pölsa", MergeMaskWords({}, keep));
    CheckEqual(m.text, "{1}", "a message that is only a kept word masks to a token");
    Check(!AnythingToTranslate(m.text), "and there is nothing left to ask about");

    Check(!AnythingToTranslate("{1} {2}"), "two placeholders are still nothing");
    Check(!AnythingToTranslate("{1}!  ... {2}?"), "punctuation around them does not count");
    Check(!AnythingToTranslate("{1} 500 {2}"), "nor do digits");
    Check(!AnythingToTranslate(""), "nor does an empty message");

    // The digits inside a placeholder are not content, but letters outside one
    // are - including letters that are not ASCII.
    Check(AnythingToTranslate("{1} var god"), "a real word alongside a token counts");
    Check(AnythingToTranslate("ät {1}"), "so does a non-ASCII one");
    Check(AnythingToTranslate("{unclosed"), "an unclosed brace is treated as text");
}

void TestIncomingMatching() {
    using Incoming::LineContains;

    // What the hook hands over is the bare message; what the user clicks on is
    // the line as the client drew it, with a timestamp and a nick around it.
    Check(LineContains("[22:04:07] <mango> jag kan stava till hashpipa",
                       "jag kan stava till hashpipa"),
          "a displayed line contains its message");
    Check(LineContains("<[Telia]_Thomas> fasen va sugen haggis jag blev nu",
                       "fasen va sugen haggis jag blev nu"),
          "a bracketed nick does not get in the way");

    Check(!LineContains("[22:04:07] <mango> nagot helt annat", "jag kan stava till hashpipa"),
          "a different line does not match");

    // Short messages would match almost anything, and matching the wrong line
    // means answering in the wrong hub.
    Check(!LineContains("[22:04:07] <mango> ok", "ok"), "a two-letter message is not matched");
    Check(!LineContains("[22:04:07] <mango> hej", "hej"), "nor a three-letter one");
    Check(LineContains("[22:04:07] <mango> okej", "okej"), "four characters is enough");

    // A long message is word-wrapped, so clicking it yields one row: a fragment
    // of the message rather than the whole of it. Requiring only the first
    // direction meant long lines could not be translated at all.
    const std::string longMessage =
        "jag kan stava till hashpipa jag med, men inte min grej riktigt tyvarr";
    Check(LineContains("<mango> jag kan stava till hashpipa jag med, men inte", longMessage),
          "a wrapped row matches the message it came from");
    Check(!LineContains("<mango> nagot", longMessage), "a short row is not evidence enough");

    // The offset is what tells the caller which nick introduced the message.
    //
    // Derived rather than written out: it was 18, and renaming a nick in this
    // file by one character broke it. The number was never the point - where
    // the message starts is.
    const std::string line = "[22:04:07] <mango> okej dh";
    size_t at = 0;
    Check(LineContains(line, "okej dh", &at), "matching reports where");
    Check(at == line.find("okej dh"), "and the offset points at the message");
}

void TestIncomingRing() {
    Incoming::Clear();

    Incoming::Line first;
    first.hubUrl = "adcs://one.example:1234";
    first.text = "eerste bericht hier";
    Incoming::Remember(first);

    Incoming::Line second;
    second.hubUrl = "adcs://two.example:1234";
    second.text = "tweede bericht hier";
    Incoming::Remember(second);

    Incoming::Line found;
    Check(Incoming::Find("[01:02:03] <piet> tweede bericht hier", found), "the later line matches");
    CheckEqual(found.hubUrl, "adcs://two.example:1234", "and carries its own hub");

    Check(Incoming::Find("[01:02:03] <jan> eerste bericht hier", found), "the earlier one too");
    CheckEqual(found.hubUrl, "adcs://one.example:1234", "with the hub it arrived on");

    // The same words twice: the most recent is what somebody just clicked.
    Incoming::Line again;
    again.hubUrl = "adcs://three.example:1234";
    again.text = "eerste bericht hier";
    Incoming::Remember(again);
    Check(Incoming::Find("<jan> eerste bericht hier", found), "a repeat matches");
    CheckEqual(found.hubUrl, "adcs://three.example:1234", "the newest one wins");

    Check(!Incoming::Find("[01:02:03] * somebody joined", found), "an unremembered line does not");
    Incoming::Clear();
    Check(Incoming::Count() == 0, "clearing empties the ring");
}

void TestPlausibility() {
    using MessageFilter::PlausibleTranslation;

    Check(PlausibleTranslation("en rod stuga", "a red cottage"), "a normal translation passes");
    Check(PlausibleTranslation("hej", "hello"), "a short honest translation passes");
    Check(PlausibleTranslation("tack", "thank you very much indeed"),
          "reasonable expansion passes");

    // The one that actually happened: "hej" came back as an e-mail template.
    Check(!PlausibleTranslation(
              "hej",
              "Hello,<br><br>A new application for the volunteer assignment \"%s\" has been "
              "created in our system and is ready to be processed.<br>%s<br><br>Sincerely,"
              "<br><br>Engagement Helsingborg"),
          "a translation-memory document is rejected");

    Check(!PlausibleTranslation("hej", "hello<br>there"), "markup is rejected");
    Check(!PlausibleTranslation("hej da", "an extremely long piece of prose that has nothing "
                                          "whatever to do with the two words it claims to "
                                          "translate at all"),
          "a wildly overlong result is rejected");
    Check(!PlausibleTranslation("hej", ""), "an empty result is rejected");

    // A poisoned memory must not turn into a client command or a protocol line.
    Check(!PlausibleTranslation("hej allihopa", "/clear"),
          "a result that becomes a client command is rejected");
    Check(!PlausibleTranslation("hej", "!kick someone"),
          "a result starting with a command sigil is rejected");
    Check(!PlausibleTranslation("hej", "$Something|"),
          "a result starting with the NMDC protocol char is rejected");
    Check(!PlausibleTranslation("hej", "line one\nline two"),
          "a result with an embedded newline is rejected");
    Check(!PlausibleTranslation("hej", std::string("bell\x07here")),
          "a result with a control character is rejected");
    // The sigils are only suspicious when the user's own line did not use them.
    Check(PlausibleTranslation("/me nagot", "/me something"),
          "a leading sigil the source also had is allowed");
    Check(PlausibleTranslation("hej", "hello, world!"),
          "a trailing exclamation mark is fine");
}

void TestThirdPerson() {
    Check(MessageFilter::IsThirdPerson("/me dricker kaffe"), "/me is recognised");
    Check(!MessageFilter::IsThirdPerson("/mem nagot"), "/mem is not /me");
    CheckEqual(MessageFilter::StripThirdPerson("/me dricker kaffe"), "dricker kaffe",
               "/me prefix is stripped");
    Check(MessageFilter::ShouldTranslate("/me dricker kaffe pa balkongen"),
          "/me content is translated");
}

void TestEnglishHeuristic() {
    Check(MessageFilter::LooksEnglish("ok thanks"), "ok thanks reads as English");
    Check(MessageFilter::LooksEnglish("what is the address"), "a sentence reads as English");
    Check(!MessageFilter::LooksEnglish("tack sa mycket"), "Swedish does not read as English");
    Check(!MessageFilter::LooksEnglish("tack \xc3\xa5\xc3\xa4\xc3\xb6"),
          "non-ASCII is never English");
    Check(!MessageFilter::LooksEnglish("thanks"), "one stop word is not enough");
    Check(!MessageFilter::LooksEnglish("was ist das"),
          "German 'was' alone does not read as English");

    // Both of these were translated in live use and should not have been.
    Check(MessageFilter::LooksEnglish("You're welcome!"),
          "an apostrophe no longer hides a stop word");
    Check(MessageFilter::LooksEnglish("i turn it off"), "short English is recognised");
}

void TestLangPrefix() {
    std::string lang;
    std::string rest;

    Check(MessageFilter::ParseLangPrefix("de: ich verstehe nicht", lang, rest),
          "a known code is a prefix");
    CheckEqual(lang, "de", "prefix language");
    CheckEqual(rest, "ich verstehe nicht", "prefix remainder");

    Check(MessageFilter::ParseLangPrefix("fr:bonjour", lang, rest), "no space after the colon");
    CheckEqual(rest, "bonjour", "remainder without a space");

    Check(!MessageFilter::ParseLangPrefix("hej: du dar", lang, rest), "hej is not a language");
    Check(!MessageFilter::ParseLangPrefix("http: nej", lang, rest), "http is not a language");
    Check(!MessageFilter::ParseLangPrefix("de:", lang, rest), "a prefix with nothing after it");
    Check(!MessageFilter::ParseLangPrefix("kolla de: har", lang, rest),
          "a prefix has to be at the start");
}

void TestSplitting() {
    // Section 9: a message over 500 bytes is split and joined back together.
    std::string sentence = "det har ar en ganska lang mening som fortsatter ett tag till. ";
    std::string long_text;
    while (long_text.size() < 1400) long_text += sentence;

    const std::vector<std::string> pieces = MessageFilter::SplitForLimit(long_text, 500);
    Check(pieces.size() >= 3, "a long message is split into several pieces");

    std::string rejoined;
    bool withinLimit = true;
    for (const std::string& piece : pieces) {
        if (piece.size() > 500) withinLimit = false;
        rejoined += piece;
    }
    Check(withinLimit, "every piece fits the limit");
    CheckEqual(rejoined, long_text, "the pieces rejoin into the original");

    // A short message is one piece.
    const std::vector<std::string> one = MessageFilter::SplitForLimit("kort text", 500);
    Check(one.size() == 1, "a short message is not split");

    // Multi-byte characters are never cut in half.
    std::string wide;
    while (wide.size() < 900) wide += "\xc3\xa5\xc3\xa4\xc3\xb6";  // aao, two bytes each
    const std::vector<std::string> wideParts = MessageFilter::SplitForLimit(wide, 100);
    bool boundariesOk = true;
    for (const std::string& piece : wideParts) {
        if (!piece.empty() && (static_cast<unsigned char>(piece[0]) & 0xC0) == 0x80) {
            boundariesOk = false;
        }
    }
    Check(boundariesOk, "pieces start on a character boundary");
}

void TestTrimParts() {
    std::string lead;
    std::string core;
    std::string tail;

    MessageFilter::TrimParts("\n  hej pa dig  \n", lead, core, tail);
    CheckEqual(lead, "\n  ", "leading whitespace is kept");
    CheckEqual(core, "hej pa dig", "the core is trimmed");
    CheckEqual(tail, "  \n", "trailing whitespace is kept");

    // Section 7.1: line breaks in a multi-line message survive the round trip.
    const std::string multi = "forsta raden\nandra raden\ntredje raden";
    const std::vector<std::string> pieces = MessageFilter::SplitForLimit(multi, 500);
    Check(pieces.size() == 1, "a multi-line message goes in one request");
    CheckEqual(pieces[0], multi, "line breaks are preserved");
}

void TestLanguages() {
    // Section 9: a German Windows locale gives "de" on its own.
    CheckEqual(Languages::FromLocaleName("de-DE"), "de", "de-DE maps to de");
    CheckEqual(Languages::FromLocaleName("sv-SE"), "sv", "sv-SE maps to sv");
    CheckEqual(Languages::FromLocaleName("nb-NO"), "no", "nb-NO maps to no");
    CheckEqual(Languages::FromLocaleName("en-GB"), "en", "en-GB maps to en");
    CheckEqual(Languages::FromLocaleName("xx-YY"), "", "an unknown locale maps to nothing");
    Check(Languages::IsKnown("sv"), "sv is known");
    Check(!Languages::IsKnown("zz"), "zz is not known");
}

void TestJson() {
    Json::Value root;

    // A MyMemory response, with the escapes those services really send.
    const std::string body =
        "{\"responseData\":{\"translatedText\":\"He said \\\"hi\\\" and left\\nthen came back\","
        "\"match\":0.85},\"responseStatus\":200,\"responseDetails\":\"\"}";
    Check(Json::Parse(body, root), "a MyMemory response parses");
    CheckEqual(root.GetString("responseData", "translatedText"),
               "He said \"hi\" and left\nthen came back", "escapes are decoded");
    Check(root.GetNumber("responseStatus", 0) == 200, "responseStatus is read");

    const Json::Value* data = root.Find("responseData");
    Check(data && data->GetNumber("match", 0) > 0.84, "match is read");

    // \u escapes, including a surrogate pair.
    Json::Value unicode;
    Check(Json::Parse("{\"t\":\"h\\u00e4r \\ud83d\\ude00\"}", unicode), "unicode escapes parse");
    CheckEqual(unicode.GetString("t"), "h\xc3\xa4r \xf0\x9f\x98\x80",
               "\\u and surrogate pairs become UTF-8");

    // Azure hands back arrays at the top level.
    Json::Value azure;
    Check(Json::Parse("[{\"translations\":[{\"text\":\"hello\",\"to\":\"en\"}]}]", azure),
          "an Azure response parses");
    const Json::Value* first = azure.At(0);
    const Json::Value* translations = first ? first->Find("translations") : nullptr;
    const Json::Value* translation = translations ? translations->At(0) : nullptr;
    Check(translation != nullptr, "the Azure translation is reachable");
    if (translation) CheckEqual(translation->GetString("text"), "hello", "Azure text is read");

    // Malformed input must not be mistaken for success.
    Json::Value broken;
    Check(!Json::Parse("{\"a\":", broken), "a truncated document fails");
    Check(!Json::Parse("not json at all", broken), "garbage fails");

    CheckEqual(Json::Escape("say \"hi\"\n"), "say \\\"hi\\\"\\n", "escaping round-trips");
}

void TestCache() {
    wchar_t temp[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, temp);
    const std::wstring path = std::wstring(temp) + L"fuldctranslate-test-cache.txt";
    ::DeleteFileW(path.c_str());

    Cache::Load(path);

    std::string out;
    Check(!Cache::Lookup("mymemory", "tack sa mycket", "sv", out), "an empty cache misses");

    Cache::Store("mymemory", "tack sa mycket", "sv", "thanks a lot");
    Check(Cache::Lookup("mymemory", "tack sa mycket", "sv", out), "a stored phrase hits");
    CheckEqual(out, "thanks a lot", "the stored translation comes back");

    // The key is normalised, so case and surrounding spaces do not matter.
    Check(Cache::Lookup("mymemory", "  Tack Sa Mycket  ", "sv", out), "the key is normalised");

    // A different source language is a different entry.
    Check(!Cache::Lookup("mymemory", "tack sa mycket", "de", out),
          "the source language is part of the key");

    // So is the service: switching engines must not serve the old one's answers.
    Check(!Cache::Lookup("claude", "tack sa mycket", "sv", out),
          "the backend is part of the key");
    Cache::Store("claude", "tack sa mycket", "sv", "thank you so much");
    Check(Cache::Lookup("claude", "tack sa mycket", "sv", out), "the new backend stores its own");
    CheckEqual(out, "thank you so much", "each backend keeps its own answer");
    Check(Cache::Lookup("mymemory", "tack sa mycket", "sv", out), "the old one is still there");
    CheckEqual(out, "thanks a lot", "switching back finds the original answer");

    // Line breaks survive being written to disk and read back.
    Cache::Store("mymemory", "rad ett\nrad tva", "sv", "line one\nline two");
    Cache::Save();
    Cache::Load(path);
    Check(Cache::Lookup("mymemory", "rad ett\nrad tva", "sv", out),
          "the cache survives a reload");
    CheckEqual(out, "line one\nline two", "line breaks survive the file format");

    ::DeleteFileW(path.c_str());
}

}  // namespace

int main() {
    TestShouldTranslate();
    TestChatNoise();
    TestNickMasking();
    TestKeepWords();
    TestIncomingMatching();
    TestIncomingRing();
    TestPlausibility();
    TestThirdPerson();
    TestEnglishHeuristic();
    TestLangPrefix();
    TestSplitting();
    TestTrimParts();
    TestLanguages();
    TestJson();
    TestCache();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
