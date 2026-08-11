// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Json {
namespace {

// Recursive-descent over a UTF-8 buffer. Depth is capped because these
// documents come off the network and a few thousand '[' would otherwise be
// enough to walk the stack off the end.
constexpr int kMaxDepth = 32;

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool ParseDocument(Value& out) {
        SkipSpace();
        if (!ParseValue(out, 0)) return false;
        SkipSpace();
        return pos_ >= text_.size();
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    bool AtEnd() const { return pos_ >= text_.size(); }
    char Peek() const { return AtEnd() ? '\0' : text_[pos_]; }

    void SkipSpace() {
        while (!AtEnd()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool Literal(const char* word) {
        const size_t n = strlen(word);
        if (text_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    // Appends one code point to a UTF-8 string.
    static void AppendUtf8(std::string& out, unsigned int cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool ParseHex4(unsigned int& out) {
        if (pos_ + 4 > text_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_ + i];
            out <<= 4;
            if (c >= '0' && c <= '9') {
                out |= static_cast<unsigned int>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                out |= static_cast<unsigned int>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                out |= static_cast<unsigned int>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        pos_ += 4;
        return true;
    }

    bool ParseString(std::string& out) {
        if (Peek() != '"') return false;
        ++pos_;

        while (!AtEnd()) {
            const char c = text_[pos_++];
            if (c == '"') return true;

            if (c != '\\') {
                out += c;
                continue;
            }

            if (AtEnd()) return false;
            const char esc = text_[pos_++];
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned int cp = 0;
                    if (!ParseHex4(cp)) return false;
                    // A character outside the BMP arrives as a surrogate pair;
                    // both halves have to be recombined before encoding, or the
                    // result is two invalid three-byte sequences.
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < text_.size() &&
                        text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        const size_t save = pos_;
                        pos_ += 2;
                        unsigned int low = 0;
                        if (ParseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            pos_ = save;
                        }
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > kMaxDepth) return false;
        SkipSpace();
        if (AtEnd()) return false;

        switch (Peek()) {
            case '{': {
                ++pos_;
                out.type = Value::Type::Object;
                SkipSpace();
                if (Peek() == '}') { ++pos_; return true; }
                for (;;) {
                    SkipSpace();
                    std::string key;
                    if (!ParseString(key)) return false;
                    SkipSpace();
                    if (Peek() != ':') return false;
                    ++pos_;

                    Value child;
                    if (!ParseValue(child, depth + 1)) return false;
                    out.object.emplace_back(std::move(key), std::move(child));

                    SkipSpace();
                    if (Peek() == ',') { ++pos_; continue; }
                    if (Peek() == '}') { ++pos_; return true; }
                    return false;
                }
            }

            case '[': {
                ++pos_;
                out.type = Value::Type::Array;
                SkipSpace();
                if (Peek() == ']') { ++pos_; return true; }
                for (;;) {
                    Value child;
                    if (!ParseValue(child, depth + 1)) return false;
                    out.array.push_back(std::move(child));

                    SkipSpace();
                    if (Peek() == ',') { ++pos_; continue; }
                    if (Peek() == ']') { ++pos_; return true; }
                    return false;
                }
            }

            case '"':
                out.type = Value::Type::String;
                return ParseString(out.string);

            case 't':
                if (!Literal("true")) return false;
                out.type = Value::Type::Bool;
                out.boolean = true;
                return true;

            case 'f':
                if (!Literal("false")) return false;
                out.type = Value::Type::Bool;
                out.boolean = false;
                return true;

            case 'n':
                if (!Literal("null")) return false;
                out.type = Value::Type::Null;
                return true;

            default: {
                const size_t start = pos_;
                if (Peek() == '-' || Peek() == '+') ++pos_;
                bool digits = false;
                while (!AtEnd()) {
                    const char c = text_[pos_];
                    if ((c >= '0' && c <= '9')) {
                        digits = true;
                        ++pos_;
                    } else if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                        ++pos_;
                    } else {
                        break;
                    }
                }
                if (!digits) return false;
                out.type = Value::Type::Number;
                out.number = std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr);
                return true;
            }
        }
    }
};

}  // namespace

const Value* Value::Find(const char* key) const {
    if (type != Type::Object || !key) return nullptr;
    for (const auto& entry : object) {
        if (entry.first == key) return &entry.second;
    }
    return nullptr;
}

const Value* Value::At(size_t index) const {
    if (type != Type::Array || index >= array.size()) return nullptr;
    return &array[index];
}

std::string Value::GetString(const char* key) const {
    const Value* v = Find(key);
    return (v && v->IsString()) ? v->string : std::string();
}

std::string Value::GetString(const char* key1, const char* key2) const {
    const Value* v = Find(key1);
    return v ? v->GetString(key2) : std::string();
}

double Value::GetNumber(const char* key, double fallback) const {
    const Value* v = Find(key);
    return (v && v->IsNumber()) ? v->number : fallback;
}

bool Parse(const std::string& text, Value& out) {
    out = Value{};
    Parser parser(text);
    return parser.ParseDocument(out);
}

std::string Escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8] = {};
                    // Control characters are illegal raw inside a JSON string.
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // Everything else, including UTF-8 continuation bytes, goes
                    // through untouched; the body is sent as UTF-8.
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

}  // namespace Json
