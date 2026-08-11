// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "MessageFilter.h"

#include "Languages.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace MessageFilter {
namespace {

// Section 6: a false negative here costs one request, a false positive sends
// the user's Swedish out untranslated with no explanation. So the list stays
// short and made of words that are rare or absent in the Nordic and German
// spelling a DC user would type without diacritics. "for" is deliberately
// missing: a Swede typing without diacritics writes it for "for".
constexpr const char* kEnglishStopWords[] = {
    "the",  "and",   "you",    "are",   "is",     "it",      "what",  "why",
    "how",  "when",  "who",    "thanks","thank",  "thx",     "please","have",
    "has",  "had",   "with",   "ok",    "okay",   "hello",   "hey",   "bye",
    "sorry","welcome","yes",   "nice",  "good",   "know",    "need",  "want",
    "here", "there", "this",   "that",  "they",   "them",    "your",  "just",
    "get",  "got",   "see",    "look",  "about",  "would",   "could", "should",
    // Added after two days of live use, where "i turn it off" and "You're
    // welcome!" were both sent off to be translated. None of these is a word in
    // Swedish, Norwegian, Danish or German, which is what keeps them safe.
    "turn", "off",   "again",  "still", "very",   "really",  "maybe", "sure",
    "right","wrong", "better", "same",  "anyway", "away",
};

// Two hits is the threshold from section 6. One is far too easy to reach by
// accident: "ok" alone appears in every language.
constexpr int kEnglishHitsRequired = 2;

bool IsAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string Trim(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && IsAsciiSpace(s[begin])) ++begin;
    while (end > begin && IsAsciiSpace(s[end - 1])) --end;
    return s.substr(begin, end - begin);
}

bool StartsWith(const std::string& s, const char* prefix) {
    const size_t n = std::strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

bool ContainsNoCase(const std::string& haystack, const char* needle) {
    const size_t n = std::strlen(needle);
    if (n == 0 || haystack.size() < n) return false;
    for (size_t i = 0; i + n <= haystack.size(); ++i) {
        size_t j = 0;
        while (j < n && std::tolower(static_cast<unsigned char>(haystack[i + j])) ==
                            std::tolower(static_cast<unsigned char>(needle[j]))) {
            ++j;
        }
        if (j == n) return true;
    }
    return false;
}

// Splits on ASCII whitespace. Good enough: every rule below only needs to know
// roughly how many words there are and what they look like.
std::vector<std::string> Words(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (IsAsciiSpace(c)) {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

// A word reduced to its ASCII letters and lowercased, so "thanks!" and
// "(thanks)" both match the stop-word list.
std::string LetterCore(const std::string& word) {
    std::string out;
    for (unsigned char c : word) {
        if (std::isalpha(c)) out += static_cast<char>(std::tolower(c));
    }
    return out;
}

// "You're welcome!" went out for translation because LetterCore turned the
// first word into "youre", which is in no list. Splitting on the apostrophe
// first gives "you" and "re", and the sentence is recognised as English.
std::vector<std::string> LetterParts(const std::string& word) {
    std::vector<std::string> out;
    std::string current;
    for (unsigned char c : word) {
        if (std::isalpha(c)) {
            current += static_cast<char>(std::tolower(c));
        } else if (!current.empty()) {
            out.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

// Universal chat noise, in any language.
constexpr const char* kInterjections[] = {
    "lol", "lmao", "rofl", "omg", "omfg", "wtf", "xd", "hmm", "hmmm", "mm",
    "mmm", "mhm", "meh", "pff", "aha", "oj",
};

// Laughter is spelled by repeating one or two letters, so it is recognised by
// its alphabet rather than by a list: "haha", "hehehe", "ahah", "LOOOOOL",
// "xddd". Anything three letters or longer built only from one of these pairs
// is somebody laughing, in every language this plugin will ever see.
bool IsLaughter(const std::string& letters) {
    if (letters.size() < 3) return false;

    static const char* const kPairs[] = {"ha", "he", "hi", "ho", "lo", "xd"};
    for (const char* pair : kPairs) {
        bool onlyThese = true;
        bool sawFirst = false;
        bool sawSecond = false;
        for (char c : letters) {
            if (c == pair[0]) {
                sawFirst = true;
            } else if (c == pair[1]) {
                sawSecond = true;
            } else {
                onlyThese = false;
                break;
            }
        }
        // Both letters have to appear, or "hhh" and "ooo" would qualify -- and
        // so would any three-letter run of a single letter.
        if (onlyThese && sawFirst && sawSecond) return true;
    }
    return false;
}

bool IsUrlLike(const std::string& word) {
    return StartsWith(word, "http://") || StartsWith(word, "https://") ||
           StartsWith(word, "www.") || StartsWith(word, "magnet:") ||
           StartsWith(word, "dchub://") || StartsWith(word, "adc://") ||
           StartsWith(word, "adcs://") || StartsWith(word, "nmdcs://");
}

// A TTH is base32: 39 characters from A-Z and 2-7.
bool IsTth(const std::string& word) {
    if (word.size() != 39) return false;
    for (char c : word) {
        const bool base32 = (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7');
        if (!base32) return false;
    }
    return true;
}

// "something.mkv" but not "hej." or "3.5". Requires letters before the dot and
// a plausible extension after it.
bool LooksLikeFilename(const std::string& word) {
    const size_t dot = word.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= word.size()) return false;

    const std::string ext = word.substr(dot + 1);
    if (ext.size() < 2 || ext.size() > 4) return false;
    for (unsigned char c : ext) {
        if (!std::isalnum(c)) return false;
    }

    // A sentence like "kul. jo" is not a filename; a real one has no spaces and
    // usually carries a separator or digit somewhere in the stem.
    const std::string stem = word.substr(0, dot);
    bool hasLetter = false;
    for (unsigned char c : stem) {
        if (std::isalpha(c)) hasLetter = true;
    }
    return hasLetter;
}

// True when the message contains something that could be a word in some
// language. Used to drop messages made only of emoji, digits and punctuation.
bool HasLetters(const std::string& text) {
    int letters = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (std::isalpha(c)) ++letters;
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            // Two-byte sequences from 0xC3 up are Latin letters with marks,
            // Greek and Cyrillic. 0xC2 is the punctuation and symbol block.
            if (c >= 0xC3) ++letters;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 0xE2 covers arrows, dingbats and general punctuation; 0xE3 and up
            // are CJK, Hangul and similar, which are letters.
            if (c >= 0xE3) ++letters;
            i += 3;
        } else {
            // Four-byte sequences at this point are overwhelmingly emoji.
            i += 4;
        }
        if (letters >= 2) return true;
    }
    return letters >= 2;
}

}  // namespace

std::string Normalise(const std::string& text) {
    std::string trimmed = Trim(text);
    for (char& c : trimmed) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x80) c = static_cast<char>(std::tolower(u));
    }
    return trimmed;
}

bool IsThirdPerson(const std::string& text) {
    const std::string trimmed = Trim(text);
    return StartsWith(trimmed, "/me ") || trimmed == "/me";
}

std::string StripThirdPerson(const std::string& text) {
    if (!IsThirdPerson(text)) return text;
    const std::string trimmed = Trim(text);
    if (trimmed.size() <= 4) return {};
    return trimmed.substr(4);
}

bool LooksEnglish(const std::string& text) {
    for (unsigned char c : text) {
        if (c >= 0x80) return false;
    }

    int hits = 0;
    for (const std::string& word : Words(text)) {
        for (const std::string& part : LetterParts(word)) {
            if (part.empty()) continue;
            for (const char* stop : kEnglishStopWords) {
                if (part == stop) {
                    ++hits;
                    break;
                }
            }
            if (hits >= kEnglishHitsRequired) return true;
        }
    }
    return false;
}

bool ParseLangPrefix(const std::string& text, std::string& langOut, std::string& restOut) {
    if (text.size() < 3) return false;

    const unsigned char a = static_cast<unsigned char>(text[0]);
    const unsigned char b = static_cast<unsigned char>(text[1]);
    if (!std::isalpha(a) || !std::isalpha(b) || text[2] != ':') return false;

    std::string code;
    code += static_cast<char>(std::tolower(a));
    code += static_cast<char>(std::tolower(b));
    if (!Languages::IsKnown(code)) return false;

    // "de:ich" and "de: ich" both work; anything else after the colon does not
    // start a prefix. This is what keeps "http: nej" out, along with the code
    // check above.
    size_t rest = 3;
    while (rest < text.size() && IsAsciiSpace(text[rest])) ++rest;

    const std::string remainder = text.substr(rest);
    if (Trim(remainder).empty()) return false;

    langOut = code;
    restOut = remainder;
    return true;
}

bool IsChatNoise(const std::string& text) {
    const std::vector<std::string> words = Words(Trim(text));
    if (words.empty()) return false;

    for (const std::string& word : words) {
        const std::string letters = LetterCore(word);

        // Punctuation and emoji between the laughs do not make it a sentence.
        if (letters.empty()) continue;

        if (IsLaughter(letters)) continue;

        bool listed = false;
        for (const char* known : kInterjections) {
            if (letters == known) listed = true;
        }
        if (listed) continue;

        return false;  // a real word: the message is not just noise
    }
    return true;
}

bool ShouldTranslate(const std::string& text, bool userAsked) {
    const std::string trimmed = Trim(text);
    if (trimmed.empty()) return false;

    // A magnet link is the one thing that must never be touched: mangling it
    // makes the file unfindable, and it can sit anywhere in the line.
    if (ContainsNoCase(trimmed, "magnet:?xt=urn:tree:tiger:")) return false;
    if (ContainsNoCase(trimmed, "magnet:?xt=")) return false;

    // Client and hub commands. "/me" is the exception; it is a message with a
    // prefix rather than a command.
    const char first = trimmed[0];
    if (first == '/' || first == '+' || first == '!') {
        if (!IsThirdPerson(trimmed)) return false;
    }

    const std::string body = IsThirdPerson(trimmed) ? Trim(StripThirdPerson(trimmed)) : trimmed;
    if (body.empty()) return false;

    // Section 6: under four characters after trimming. This one applies even
    // when the user asked, which is a change of mind and a warranted one.
    //
    // Ctrl+G on "hej" came back from MyMemory as a complete volunteer-management
    // e-mail template, HTML tags and printf placeholders included, and the
    // plugin sent all of it to the hub. A three-letter word is a near-guaranteed
    // hit on somebody's junk contribution in a shared translation memory, and
    // there is nothing worth gaining on the other side of that bet.
    if (body.size() < 4) return false;

    if (!HasLetters(body)) return false;

    // Applies even to an explicit Ctrl+G. There is no translation of "haha" to
    // ask for, and against a paid backend the request costs money to be told so.
    if (IsChatNoise(body)) return false;

    // A line that is mostly a link, a hash or a filename. Counting what is left
    // over is more robust than pattern-matching the whole line, because people
    // paste a link and add three words of comment.
    const std::vector<std::string> words = Words(body);
    size_t opaque = 0;
    size_t opaqueChars = 0;
    for (const std::string& word : words) {
        if (IsUrlLike(word) || IsTth(word) || LooksLikeFilename(word)) {
            ++opaque;
            opaqueChars += word.size();
        }
    }
    if (opaque > 0) {
        const size_t remainingWords = words.size() - opaque;
        const size_t remainingChars = body.size() > opaqueChars ? body.size() - opaqueChars : 0;
        // Fewer than two real words, or almost nothing left once the opaque
        // tokens are removed, means there is nothing to translate.
        if (remainingWords < 2 || remainingChars < 8) return false;
    }

    if (!userAsked && LooksEnglish(body)) return false;

    return true;
}

namespace {

// Braces are what translation tooling conventionally uses for placeholders, and
// they survive better than a bare word, which a service may well decide to
// translate. The number keeps the mapping unambiguous when a message mentions
// several people.
std::string Token(size_t index) {
    return "{" + std::to_string(index + 1) + "}";
}

// A nick only counts when it stands alone. Without this, "Kaje" would be taken
// out of "Kajen" and the sentence would come back mangled in a new way.
bool WholeWordAt(const std::string& text, size_t at, size_t length) {
    const auto letter = [](unsigned char c) {
        // Anything non-ASCII is treated as a letter: a nick that ends where an
        // "a-with-ring" begins is still inside a word.
        return std::isalnum(c) != 0 || c >= 0x80;
    };
    if (at > 0 && letter(static_cast<unsigned char>(text[at - 1]))) return false;
    const size_t after = at + length;
    if (after < text.size() && letter(static_cast<unsigned char>(text[after]))) return false;
    return true;
}

size_t FindNoCase(const std::string& haystack, const std::string& needle, size_t from) {
    if (needle.empty() || needle.size() > haystack.size()) return std::string::npos;
    for (size_t i = from; i + needle.size() <= haystack.size(); ++i) {
        size_t j = 0;
        while (j < needle.size() &&
               std::tolower(static_cast<unsigned char>(haystack[i + j])) ==
                   std::tolower(static_cast<unsigned char>(needle[j]))) {
            ++j;
        }
        if (j == needle.size()) return i;
    }
    return std::string::npos;
}

}  // namespace

bool AnythingToTranslate(const std::string& maskedText) {
    for (size_t i = 0; i < maskedText.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(maskedText[i]);

        // Skip a whole placeholder rather than its characters: the digits
        // inside one are not content, and neither are the braces.
        if (c == '{') {
            const size_t close = maskedText.find('}', i);
            if (close != std::string::npos) {
                i = close;
                continue;
            }
        }

        if (std::isalpha(c) != 0 || c >= 0x80) return true;
    }
    return false;
}

std::vector<std::string> MergeMaskWords(const std::vector<std::string>& nicks,
                                        const std::vector<std::string>& keep) {
    // Keep words go in first, so that where a keep word and a nick are the same
    // length the user's own choice is the one in the list.
    std::vector<std::string> out;
    out.reserve(keep.size() + nicks.size());

    const auto alreadyThere = [&out](const std::string& word) {
        for (const std::string& have : out) {
            if (have.size() == word.size() && FindNoCase(have, word, 0) == 0) return true;
        }
        return false;
    };

    for (const std::string& word : keep) {
        if (!word.empty() && !alreadyThere(word)) out.push_back(word);
    }
    for (const std::string& nick : nicks) {
        if (!nick.empty() && !alreadyThere(nick)) out.push_back(nick);
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    return out;
}

Masked MaskNicks(const std::string& text, const std::vector<std::string>& nicks) {
    Masked out;
    out.text = text;

    for (const std::string& nick : nicks) {
        if (nick.size() < 3) continue;

        size_t at = 0;
        bool used = false;
        while ((at = FindNoCase(out.text, nick, at)) != std::string::npos) {
            if (!WholeWordAt(out.text, at, nick.size())) {
                at += 1;
                continue;
            }
            if (!used) {
                out.nicks.push_back(nick);
                used = true;
            }
            // The nick as the user spelled it goes back, not as the list has it,
            // so their capitalisation survives. It is recorded on first use.
            const std::string token = Token(out.nicks.size() - 1);
            out.nicks.back() = out.text.substr(at, nick.size());
            out.text.replace(at, nick.size(), token);
            at += token.size();
        }
    }
    return out;
}

bool UnmaskNicks(const Masked& masked, const std::string& translated, std::string& out) {
    out = translated;
    for (size_t i = 0; i < masked.nicks.size(); ++i) {
        const std::string token = Token(i);
        const size_t at = out.find(token);
        if (at == std::string::npos) return false;

        out.replace(at, token.size(), masked.nicks[i]);

        // One token per nick. A service that duplicated it would otherwise
        // leave a stray "{1}" in the message.
        if (out.find(token) != std::string::npos) return false;
    }
    return true;
}

bool PlausibleTranslation(const std::string& source, const std::string& translation) {
    const std::string result = Trim(translation);
    if (result.empty()) return false;

    // Markup never belongs in a chat line, and its presence means the memory
    // returned a document rather than a phrase.
    if (!ContainsNoCase(source, "<") && (ContainsNoCase(result, "<br") ||
                                         ContainsNoCase(result, "</"))) {
        return false;
    }

    // Translating between these languages changes length by a little. It does
    // not multiply it. The constant leaves room for a genuinely terse source.
    const size_t ceiling = source.size() * 4 + 40;
    if (result.size() > ceiling) return false;

    // A translation is attacker-influenceable: MyMemory's memory is a public,
    // editable corpus, and the result is about to be sent as the user. The first
    // non-space character of a chat line decides how the host reads it -- '/' and
    // '!' and '+' begin client commands, '$' is NMDC protocol -- so a result that
    // starts with one, when the user's own line did not, is refused and the
    // original goes out in its place rather than a command they never typed.
    const char lead = result[0];
    if (lead == '/' || lead == '!' || lead == '+' || lead == '$') {
        const std::string trimmedSource = Trim(source);
        if (trimmedSource.empty() || trimmedSource[0] != lead) return false;
    }

    // Control characters have no place in a single chat line. An embedded newline
    // is the one that matters -- it would split one message into two on the wire --
    // but none of them belong here, so any rejects the whole translation.
    for (unsigned char c : result) {
        if (c < 0x20) return false;
    }

    return true;
}

std::vector<std::string> SplitForLimit(const std::string& text, size_t maxBytes) {
    std::vector<std::string> pieces;
    if (maxBytes == 0 || text.size() <= maxBytes) {
        if (!text.empty()) pieces.push_back(text);
        return pieces;
    }

    size_t start = 0;
    while (start < text.size()) {
        if (text.size() - start <= maxBytes) {
            pieces.push_back(text.substr(start));
            break;
        }

        const size_t limit = start + maxBytes;

        // Best: a sentence end inside the window. Cut after the punctuation and
        // the space that follows it, so the next piece starts on a word.
        size_t cut = std::string::npos;
        for (size_t i = limit; i > start; --i) {
            const char c = text[i - 1];
            if ((c == '.' || c == '!' || c == '?' || c == '\n') && i > start + maxBytes / 4) {
                cut = i;
                break;
            }
        }

        // Next best: a space.
        if (cut == std::string::npos) {
            for (size_t i = limit; i > start; --i) {
                if (IsAsciiSpace(text[i - 1])) {
                    cut = i;
                    break;
                }
            }
        }

        // Last resort: a character boundary, so a multi-byte character is never
        // cut in half and turned into two invalid bytes.
        if (cut == std::string::npos || cut <= start) {
            cut = limit;
            while (cut > start && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
            if (cut <= start) cut = limit;  // pathological input; take the hit
        }

        pieces.push_back(text.substr(start, cut - start));
        start = cut;
    }

    return pieces;
}

void TrimParts(const std::string& piece, std::string& lead, std::string& core,
               std::string& tail) {
    size_t begin = 0;
    size_t end = piece.size();
    while (begin < end && IsAsciiSpace(piece[begin])) ++begin;
    while (end > begin && IsAsciiSpace(piece[end - 1])) --end;

    lead = piece.substr(0, begin);
    core = piece.substr(begin, end - begin);
    tail = piece.substr(end);
}

}  // namespace MessageFilter
