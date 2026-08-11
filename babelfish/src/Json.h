// FulDC++ Babelfish - chat translation for Direct Connect clients
// Copyright (C) 2026 kaje
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version. See the LICENSE file for details.
#pragma once

#include <string>
#include <utility>
#include <vector>

// A small JSON reader, enough for the five translation APIs this plugin talks
// to. Written by hand rather than vendored because the alternative is a 900 kB
// single header for what amounts to "walk two levels down and read a string".
//
// The point of parsing at all rather than searching for substrings is escapes:
// every one of these services will happily hand back \" and \uXXXX in the
// translated text, and a substring match cuts them in half.
namespace Json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    bool IsNull() const { return type == Type::Null; }
    bool IsString() const { return type == Type::String; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    // Member lookup. Returns null for a missing key or a non-object, so chains
    // like Find("a")->Find("b") need one null check at the end rather than one
    // per level -- see the Get* helpers below.
    const Value* Find(const char* key) const;

    // Element lookup, null when out of range or not an array.
    const Value* At(size_t index) const;

    // Reads through a chain of keys, tolerating anything missing on the way.
    // GetString("responseData", "translatedText") is the whole idiom.
    std::string GetString(const char* key) const;
    std::string GetString(const char* key1, const char* key2) const;
    double GetNumber(const char* key, double fallback = 0) const;
};

// Parses a complete document. Returns false on malformed input; out is then
// left in an unspecified but safe state.
bool Parse(const std::string& text, Value& out);

// Escapes a UTF-8 string for embedding in a JSON string literal. Does not add
// the surrounding quotes.
std::string Escape(const std::string& text);

}  // namespace Json
