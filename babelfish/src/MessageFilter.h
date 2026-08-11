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

// Decides what is worth sending to a translation service. Everything here works
// on UTF-8 and has no dependency on the plugin API, so it can be exercised from
// the standalone test harness.
namespace MessageFilter {

// The gate in front of every request. False means "send this exactly as the
// user wrote it".
//
// userAsked is true for Ctrl+G and "/tr once", where the user made a deliberate
// request. The rules that exist to save quota -- the English heuristic and the
// minimum length -- are then skipped, because guessing against an explicit
// instruction is worse than spending one request. The rules that exist to avoid
// breaking things, such as commands and magnet links, always apply.
bool ShouldTranslate(const std::string& text, bool userAsked = false);

// True for a "/me ..." message. The prefix is stripped before translation and
// put back before sending, so the client still renders it as an action.
bool IsThirdPerson(const std::string& text);

// Returns the text after "/me ", or the text unchanged when it is not an
// action message.
std::string StripThirdPerson(const std::string& text);

// Section 6, "Engelskkontroll": no bytes outside ASCII, and at least two hits
// in a small stop-word list. This is the single largest quota saving there is,
// because a lot of DC chat is already in English.
bool LooksEnglish(const std::string& text);

// False when masking has left nothing a service could translate: placeholders,
// punctuation and digits only.
//
// "polsa" on its own, with polsa on the keep list, becomes "{1}". Sending that
// spends a request to be told what we already knew, and invites a service to
// return something imaginative in place of a placeholder it did not understand.
bool AnythingToTranslate(const std::string& maskedText);

// Section 6, "Prefixparsning". Matches a leading "xx: " where xx is a language
// the backends accept, and hands back the code plus the remaining text. The
// prefix overrides the *source* language only; the target is always English.
// Returns false for "hej: du dar" and "http: nej", which are ordinary text.
bool ParseLangPrefix(const std::string& text, std::string& langOut, std::string& restOut);

// Breaks text into pieces of at most maxBytes UTF-8 bytes, preferring sentence
// ends, then word boundaries, and finally a character boundary. Concatenating
// the pieces reproduces the input byte for byte, which is what lets the
// translated pieces be joined back together without losing spacing.
std::vector<std::string> SplitForLimit(const std::string& text, size_t maxBytes);

// True when the whole message is laughter or an interjection: "haha", "LOL",
// "omfg", "LOOOOOOOL". There is nothing to translate in any language, the
// service returns them unchanged, and against a paid backend each one costs
// real money. Two days of live logs were mostly this.
bool IsChatNoise(const std::string& text);

// A message with its nicks taken out, ready to be translated.
struct Masked {
    std::string text;                 // what to send to the service
    std::vector<std::string> nicks;   // nicks[i] was replaced by the token {i+1}
};

// Replaces every known nick with a numbered token, longest nick first so a name
// inside another name cannot leave a fragment behind. Matching is
// case-insensitive but bounded to whole words, so "Kajen" keeps its ending and
// only the nick "Kaje" standing alone is taken out.
//
// A translation service has no way of knowing that "mango" is a person. Handing
// it "{1} har filen" instead keeps the name out of its reach entirely.
Masked MaskNicks(const std::string& text, const std::vector<std::string>& nicks);

// Puts the nicks back. Returns false when a token did not survive translation,
// in which case the caller must not use the result: a half-restored message is
// missing somebody's name, which is worse than one that was never translated.
bool UnmaskNicks(const Masked& masked, const std::string& translated, std::string& out);

// Everything that must survive a translation intact, longest first.
//
// Nicks and the user's own keep list go through the same masking, so they are
// merged here rather than at each call site. Order is the whole point: a short
// word that sits inside a longer one would mask half of it and leave a
// fragment, so length decides. Duplicates are dropped without regard to case --
// a nick that is also a keep word must not consume two placeholders for one
// occurrence in the text.
std::vector<std::string> MergeMaskWords(const std::vector<std::string>& nicks,
                                        const std::vector<std::string>& keep);

// Sanity check on what a service hands back, before it is sent anywhere.
//
// MyMemory's memory is contributed by the public and contains whole documents
// filed under single words. False means the result is not a translation of this
// source and the original should go out instead.
bool PlausibleTranslation(const std::string& source, const std::string& translation);

// Splits a piece into leading whitespace, the part worth translating, and
// trailing whitespace. The two whitespace runs are put back around the
// translation so line breaks survive a round trip.
void TrimParts(const std::string& piece, std::string& lead, std::string& core,
               std::string& tail);

// Normalised cache key input: trimmed and lowercased ASCII. Kept here so the
// cache and the tests agree on what counts as the same message.
std::string Normalise(const std::string& text);

}  // namespace MessageFilter
