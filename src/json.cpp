#include "json.h"

#include <cctype>
#include <cstdio>

namespace json {

namespace {

struct Parser {
    const std::string& text;
    std::size_t pos = 0;
    std::size_t line = 1;
    std::size_t col = 1;
    bool truncated = false;

    explicit Parser(const std::string& t) : text(t) {}

    char Peek() const { return pos < text.size() ? text[pos] : '\0'; }

    void Advance() {
        if (pos >= text.size()) return;
        if (text[pos] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
        pos++;
    }

    void SkipWhitespace() {
        while (pos < text.size()) {
            char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                Advance();
            } else {
                break;
            }
        }
    }

    std::string ErrorAt(const std::string& what) const {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s at line %zu, column %zu",
                      what.c_str(), line, col);
        return std::string(buf);
    }

    // Encode one Unicode code point as UTF-8 into out.
    static void AppendUtf8(std::string& out, unsigned int cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
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
        if (pos + 4 > text.size()) return false;
        unsigned int v = 0;
        for (int i = 0; i < 4; i++) {
            char c = text[pos];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned int>(c - 'A' + 10);
            else return false;
            Advance();
        }
        out = v;
        return true;
    }

    bool ParseString(std::string& out, std::string& err) {
        if (Peek() != '"') {
            err = ErrorAt("expected a string");
            return false;
        }
        Advance();
        while (true) {
            if (pos >= text.size()) {
                err = ErrorAt("unterminated string");
                return false;
            }
            char c = Peek();
            if (c == '"') {
                Advance();
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                err = ErrorAt("control character in string");
                return false;
            }
            if (c == '\\') {
                Advance();
                char e = Peek();
                switch (e) {
                    case '"': out += '"'; Advance(); break;
                    case '\\': out += '\\'; Advance(); break;
                    case '/': out += '/'; Advance(); break;
                    case 'b': out += '\b'; Advance(); break;
                    case 'f': out += '\f'; Advance(); break;
                    case 'n': out += '\n'; Advance(); break;
                    case 'r': out += '\r'; Advance(); break;
                    case 't': out += '\t'; Advance(); break;
                    case 'u': {
                        Advance();
                        unsigned int cp = 0;
                        if (!ParseHex4(cp)) {
                            err = ErrorAt("invalid \\u escape");
                            return false;
                        }
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // High surrogate: expect a low surrogate next.
                            if (Peek() == '\\' && pos + 1 < text.size() &&
                                text[pos + 1] == 'u') {
                                Advance();
                                Advance();
                                unsigned int low = 0;
                                if (!ParseHex4(low) || low < 0xDC00 ||
                                    low > 0xDFFF) {
                                    err = ErrorAt("invalid surrogate pair");
                                    return false;
                                }
                                unsigned int combined =
                                    0x10000 + ((cp - 0xD800) << 10) +
                                    (low - 0xDC00);
                                AppendUtf8(out, combined);
                            } else {
                                err = ErrorAt("unpaired surrogate");
                                return false;
                            }
                        } else {
                            AppendUtf8(out, cp);
                        }
                        break;
                    }
                    default:
                        err = ErrorAt("invalid escape");
                        return false;
                }
            } else {
                out += c;
                Advance();
            }
        }
    }

    bool ParseNumber(std::string& out, std::string& err) {
        std::size_t start = pos;
        if (Peek() == '-') Advance();
        if (Peek() == '0') {
            Advance();
        } else if (std::isdigit(static_cast<unsigned char>(Peek()))) {
            while (std::isdigit(static_cast<unsigned char>(Peek()))) Advance();
        } else {
            err = ErrorAt("invalid number");
            return false;
        }
        if (Peek() == '.') {
            Advance();
            if (!std::isdigit(static_cast<unsigned char>(Peek()))) {
                err = ErrorAt("invalid number");
                return false;
            }
            while (std::isdigit(static_cast<unsigned char>(Peek()))) Advance();
        }
        if (Peek() == 'e' || Peek() == 'E') {
            Advance();
            if (Peek() == '+' || Peek() == '-') Advance();
            if (!std::isdigit(static_cast<unsigned char>(Peek()))) {
                err = ErrorAt("invalid number");
                return false;
            }
            while (std::isdigit(static_cast<unsigned char>(Peek()))) Advance();
        }
        out = text.substr(start, pos - start);
        return true;
    }

    bool ParseLiteral(const char* lit, std::string& err) {
        std::size_t n = std::string(lit).size();
        if (text.compare(pos, n, lit) != 0) {
            err = ErrorAt(std::string("expected '") + lit + "'");
            return false;
        }
        for (std::size_t i = 0; i < n; i++) Advance();
        return true;
    }

    bool ParseValue(Value& out, std::size_t depth, std::string& err) {
        if (pos >= kMaxBytes) {
            truncated = true;
            err = "truncated: maximum size reached";
            return false;
        }
        if (depth > kMaxDepth) {
            truncated = true;
            err = ErrorAt("maximum nesting depth reached");
            return false;
        }
        SkipWhitespace();
        char c = Peek();
        if (c == '{') return ParseObject(out, depth, err);
        if (c == '[') return ParseArray(out, depth, err);
        if (c == '"') {
            out.kind = Kind::String;
            return ParseString(out.text, err);
        }
        if (c == 't') {
            if (!ParseLiteral("true", err)) return false;
            out.kind = Kind::Bool;
            out.boolValue = true;
            return true;
        }
        if (c == 'f') {
            if (!ParseLiteral("false", err)) return false;
            out.kind = Kind::Bool;
            out.boolValue = false;
            return true;
        }
        if (c == 'n') {
            if (!ParseLiteral("null", err)) return false;
            out.kind = Kind::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            out.kind = Kind::Number;
            return ParseNumber(out.text, err);
        }
        err = ErrorAt("unexpected token");
        return false;
    }

    bool ParseObject(Value& out, std::size_t depth, std::string& err) {
        out.kind = Kind::Object;
        Advance();  // '{'
        SkipWhitespace();
        if (Peek() == '}') {
            Advance();
            return true;
        }
        while (true) {
            SkipWhitespace();
            std::string key;
            if (Peek() != '"') {
                err = ErrorAt("expected a string key");
                return false;
            }
            if (!ParseString(key, err)) return false;
            SkipWhitespace();
            if (Peek() != ':') {
                err = ErrorAt("expected ':'");
                return false;
            }
            Advance();
            Value v;
            if (!ParseValue(v, depth + 1, err)) return false;
            out.object.emplace_back(std::move(key), std::move(v));
            SkipWhitespace();
            if (Peek() == ',') {
                Advance();
                SkipWhitespace();
                if (Peek() == '}') {
                    err = ErrorAt("trailing comma is not allowed");
                    return false;
                }
                continue;
            }
            if (Peek() == '}') {
                Advance();
                return true;
            }
            err = ErrorAt("expected ',' or '}'");
            return false;
        }
    }

    bool ParseArray(Value& out, std::size_t depth, std::string& err) {
        out.kind = Kind::Array;
        Advance();  // '['
        SkipWhitespace();
        if (Peek() == ']') {
            Advance();
            return true;
        }
        while (true) {
            Value v;
            if (!ParseValue(v, depth + 1, err)) return false;
            out.array.push_back(std::move(v));
            SkipWhitespace();
            if (Peek() == ',') {
                Advance();
                SkipWhitespace();
                if (Peek() == ']') {
                    err = ErrorAt("trailing comma is not allowed");
                    return false;
                }
                continue;
            }
            if (Peek() == ']') {
                Advance();
                return true;
            }
            err = ErrorAt("expected ',' or ']'");
            return false;
        }
    }
};

void EscapeString(const std::string& text, std::string& out) {
    out += '"';
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void PrettyPrintValue(const Value& v, int depth, std::string& out) {
    const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');
    const std::string padIn(static_cast<std::size_t>(depth + 1) * 2, ' ');
    switch (v.kind) {
        case Kind::Null:
            out += "null";
            break;
        case Kind::Bool:
            out += v.boolValue ? "true" : "false";
            break;
        case Kind::Number:
            out += v.text;
            break;
        case Kind::String: {
            EscapeString(v.text, out);
            break;
        }
        case Kind::Array: {
            if (v.array.empty()) {
                out += "[]";
                break;
            }
            out += "[\n";
            for (std::size_t i = 0; i < v.array.size(); i++) {
                out += padIn;
                PrettyPrintValue(v.array[i], depth + 1, out);
                if (i + 1 < v.array.size()) out += ',';
                out += '\n';
            }
            out += pad;
            out += ']';
            break;
        }
        case Kind::Object: {
            if (v.object.empty()) {
                out += "{}";
                break;
            }
            out += "{\n";
            for (std::size_t i = 0; i < v.object.size(); i++) {
                out += padIn;
                out += '"';
                out += v.object[i].first;
                out += "\": ";
                PrettyPrintValue(v.object[i].second, depth + 1, out);
                if (i + 1 < v.object.size()) out += ',';
                out += '\n';
            }
            out += pad;
            out += '}';
            break;
        }
    }
}

}  // namespace

void CompactValue(const Value& v, std::string& out) {
    switch (v.kind) {
        case Kind::Null:
            out += "null";
            break;
        case Kind::Bool:
            out += v.boolValue ? "true" : "false";
            break;
        case Kind::Number:
            out += v.text;
            break;
        case Kind::String:
            EscapeString(v.text, out);
            break;
        case Kind::Array: {
            out += '[';
            for (std::size_t i = 0; i < v.array.size(); i++) {
                if (i) out += ',';
                CompactValue(v.array[i], out);
            }
            out += ']';
            break;
        }
        case Kind::Object: {
            out += '{';
            for (std::size_t i = 0; i < v.object.size(); i++) {
                if (i) out += ',';
                EscapeString(v.object[i].first, out);
                out += ':';
                CompactValue(v.object[i].second, out);
            }
            out += '}';
            break;
        }
    }
}

ParseResult Parse(const std::string& utf8Text) {
    ParseResult result;
    if (utf8Text.size() > kMaxBytes) {
        result.truncated = true;
        result.error = "truncated: maximum size reached";
        return result;
    }
    Parser p(utf8Text);
    std::string err;
    if (!p.ParseValue(result.root, 0, err)) {
        result.error = err;
        result.truncated = p.truncated;
        return result;
    }
    p.SkipWhitespace();
    if (p.pos != utf8Text.size()) {
        result.error = p.ErrorAt("unexpected trailing data");
        return result;
    }
    result.ok = true;
    return result;
}

std::string PrettyPrint(const Value& root) {
    std::string out;
    PrettyPrintValue(root, 0, out);
    out += '\n';
    return out;
}

std::string Compact(const Value& root) {
    std::string out;
    CompactValue(root, out);
    return out;
}

}  // namespace json
