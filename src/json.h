// Reading a JSON file into a tree
// (plan: .hermes/plans/2026-11-16_140000-json-viewer-support.md).
//
// RFC 8259 only: no comments, no trailing commas, no unquoted keys. A
// standalone .json file is read, not edited, so a strict parser is the
// honest choice; a lenient one would hide a syntax error the file
// actually has.
//
// No file API, no window and no platform header, so the parser runs in
// the portable tests and in both ports unchanged, the same discipline
// src/csv.h already follows.

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace json {

// A depth past this is rejected rather than walked with real recursion,
// so a hostile or just very deeply nested file cannot overflow the
// parser's stack.
constexpr std::size_t kMaxDepth = 64;
// Past this many bytes the parser stops and reports what it got so far,
// the same honest-cap convention csv.h uses for rows and columns.
constexpr std::size_t kMaxBytes = 8 * 1024 * 1024;  // 8 MiB

enum class Kind { Null, Bool, Number, String, Array, Object };

struct Value {
    Kind kind = Kind::Null;
    bool boolValue = false;
    // For Kind::Number, the literal digits exactly as read (no float
    // round trip). For Kind::String, the text with escapes resolved.
    std::string text;
    // Key order is the order the file held, not resorted.
    std::vector<std::pair<std::string, Value>> object;
    std::vector<Value> array;
};

struct ParseResult {
    bool ok = false;
    Value root;
    // "unexpected token at line 4, column 12", empty when ok.
    std::string error;
    // kMaxBytes or kMaxDepth was hit; root holds whatever was parsed
    // before the cap, the same partial-result convention csv::Table uses.
    bool truncated = false;
};

// Parse UTF-8 JSON text into a tree.
ParseResult Parse(const std::string& utf8Text);

// Re-serialize a parsed tree with two-space indentation, one value per
// line for scalars, and the object's original key order kept. This is
// the only output shape the viewer ever shows.
std::string PrettyPrint(const Value& root);

// Re-serialize a parsed tree with no extra whitespace: the minimal form
// used when writing a pretty-printed buffer back to disk, so a round
// trip through the viewer does not rewrite a file the user never
// touched.
std::string Compact(const Value& root);

}  // namespace json
