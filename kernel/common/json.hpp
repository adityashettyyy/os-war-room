// Minimal dependency-free JSON: a recursive-descent parser (json::Value) and a streaming Writer.
#pragma once
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace owr::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;
    std::map<std::string, Value> o;

    bool isNull() const { return type == Type::Null; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }
    bool has(const std::string& k) const { return type == Type::Object && o.count(k) > 0; }
    size_t size() const { return type == Type::Array ? a.size() : type == Type::Object ? o.size() : 0; }

    const Value& operator[](const std::string& k) const {
        static const Value null;
        auto it = o.find(k);
        return (type == Type::Object && it != o.end()) ? it->second : null;
    }
    const Value& operator[](size_t i) const {
        static const Value null;
        return (type == Type::Array && i < a.size()) ? a[i] : null;
    }
    double num(double def = 0) const { return type == Type::Number ? n : (type == Type::Bool ? (b ? 1 : 0) : def); }
    int integer(int def = 0) const { return type == Type::Number ? (int)n : (type == Type::Bool ? (b ? 1 : 0) : def); }
    bool boolean(bool def = false) const { return type == Type::Bool ? b : (type == Type::Number ? n != 0 : def); }
    std::string str(const std::string& def = "") const { return type == Type::String ? s : def; }
};

class Parser {
    const std::string& t;
    size_t i = 0;
    [[noreturn]] void fail(const char* m) const {
        throw std::runtime_error(std::string("JSON parse error at offset ") + std::to_string(i) + ": " + m);
    }
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i; }
    bool eat(char c) { ws(); if (i < t.size() && t[i] == c) { ++i; return true; } return false; }
    void expect(char c) { if (!eat(c)) fail("unexpected character"); }
    std::string string() {
        expect('"');
        std::string r;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c != '\\') { r += c; continue; }
            if (i >= t.size()) fail("bad escape");
            char e = t[i++];
            switch (e) {
                case 'n': r += '\n'; break;
                case 't': r += '\t'; break;
                case 'r': r += '\r'; break;
                case 'b': r += '\b'; break;
                case 'f': r += '\f'; break;
                case 'u': {
                    if (i + 4 > t.size()) fail("bad \\u escape");
                    unsigned cp = (unsigned)std::stoul(t.substr(i, 4), nullptr, 16);
                    i += 4;
                    if (cp < 0x80) r += (char)cp;
                    else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3F)); }
                    else { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: r += e;
            }
        }
        if (i >= t.size()) fail("unterminated string");
        ++i;
        return r;
    }
public:
    explicit Parser(const std::string& text) : t(text) {}
    Value value(int depth = 0) {
        if (depth > 64) fail("nesting too deep");
        ws();
        if (i >= t.size()) fail("unexpected end");
        Value v;
        char c = t[i];
        if (c == '{') {
            ++i; v.type = Value::Type::Object;
            if (eat('}')) return v;
            do { ws(); std::string k = string(); expect(':'); v.o[k] = value(depth + 1); } while (eat(','));
            expect('}');
        } else if (c == '[') {
            ++i; v.type = Value::Type::Array;
            if (eat(']')) return v;
            do { v.a.push_back(value(depth + 1)); } while (eat(','));
            expect(']');
        } else if (c == '"') {
            v.type = Value::Type::String; v.s = string();
        } else if (t.compare(i, 4, "true") == 0) { v.type = Value::Type::Bool; v.b = true; i += 4; }
        else if (t.compare(i, 5, "false") == 0) { v.type = Value::Type::Bool; v.b = false; i += 5; }
        else if (t.compare(i, 4, "null") == 0) { i += 4; }
        else {
            size_t used = 0;
            try { v.n = std::stod(t.substr(i), &used); } catch (...) { fail("invalid value"); }
            v.type = Value::Type::Number; i += used;
        }
        return v;
    }
    Value parseAll() { Value v = value(); ws(); if (i != t.size()) fail("trailing characters"); return v; }
};

inline Value parse(const std::string& text) { return Parser(text).parseAll(); }

// Streaming writer: w.obj().k("a").v(1).k("b").arr().v(1).v(2).end().end();
class Writer {
    std::string o;
    std::vector<char> st;
    bool comma = false;
    void pre() { if (comma) o += ','; comma = false; }
    void quote(const std::string& s) {
        o += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\r': o += "\\r"; break;
                case '\t': o += "\\t"; break;
                default:
                    if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                    else o += (char)c;
            }
        }
        o += '"';
    }
public:
    Writer& obj() { pre(); o += '{'; st.push_back('}'); return *this; }
    Writer& arr() { pre(); o += '['; st.push_back(']'); return *this; }
    Writer& end() { o += st.back(); st.pop_back(); comma = true; return *this; }
    Writer& k(const std::string& key) { pre(); quote(key); o += ':'; return *this; }
    Writer& v(const std::string& s) { pre(); quote(s); comma = true; return *this; }
    Writer& v(const char* s) { return v(std::string(s)); }
    template <class T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    Writer& v(T x) {
        pre();
        if constexpr (std::is_same_v<T, bool>) o += x ? "true" : "false";
        else if constexpr (std::is_floating_point_v<T>) {
            char b[48];
            snprintf(b, sizeof b, "%.4f", (double)x);
            std::string s = b;
            if (s == "nan" || s == "-nan" || s == "inf" || s == "-inf") s = "0";
            o += s;
        } else o += std::to_string(x);
        comma = true;
        return *this;
    }
    Writer& null() { pre(); o += "null"; comma = true; return *this; }
    Writer& raw(const std::string& j) { pre(); o += j; comma = true; return *this; }
    Writer& ints(const std::vector<int>& xs) { arr(); for (int x : xs) v(x); return end(); }
    Writer& strs(const std::vector<std::string>& xs) { arr(); for (auto& x : xs) v(x); return end(); }
    std::string str() const { return o; }
};

}  // namespace owr::json
