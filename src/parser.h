#pragma once

#include <string>
#include <cstdint>
#include "dom.h"
// ReparseRange lives with the text buffer that produces it: the buffer knows
// which bytes a splice touched, and the parser consumes that range. Keeping
// one definition keeps the two in step.
#include "textbuffer.h"

// Parse UTF-8 markdown text into out.
// Returns true on success, false on parse error.
bool ParseMarkdown(const std::string& utf8, Document& out);

// Incremental reparse in place: `doc` holds the previous parse of the old
// text and is updated to the parse of `utf8`, touching only the blocks the
// change reached. Returns true when the document was updated incrementally.
// Returns false - leaving `doc` untouched - when the change cannot be applied
// safely; the caller must then run a full ParseMarkdown.
//
// The incremental path stands down when the document uses constructs whose
// meaning is global (link reference definitions), when the change reaches the
// title's first H1, or when the document's recorded source size does not match
// the old text the range describes. Those are correctness boundaries, not
// performance ones.
bool ParseMarkdownIncremental(const std::string& utf8, Document& doc,
                              const ReparseRange& range);

// The same, for a single splice described as (offset, removed, inserted).
bool ParseMarkdownIncremental(const std::string& utf8, Document& doc,
                              uint32_t editOffset, uint32_t oldLen,
                              uint32_t newLen);

// Out-of-place convenience: copies oldDoc, applies the change, and falls back
// to a full parse when the incremental path stands down. `out` always holds a
// correct parse of `utf8`. When `incremental` is non-null it reports whether
// the fast path produced the result.
bool ParseMarkdownIncremental(const std::string& utf8,
                               const Document& oldDoc,
                               uint32_t editOffset,
                               uint32_t oldLen,
                               uint32_t newLen,
                               Document& out,
                               bool* incremental = nullptr);

// Measure parse time in milliseconds (for diagnostics).
double MeasureParseMs(const std::string& utf8);

// Serialize a Document to a comparable string for testing.
std::string DocumentToString(const Document& doc);

// Structural validation of a parsed document (table guidelines §60):
// every table must have rows and cells, a uniform column count across
// all rows, and cell source spans inside the table's own source range.
// Returns true when all invariants hold; otherwise false with one
// message per violation appended to errors (errors may be null).
// Nested tables are unrepresentable in this model (a TableCell holds
// text and inline spans only, never block children), so the checker
// guards the invariants the model can express and any future model
// change must keep them true.
bool ValidateDocument(const Document& doc, std::vector<std::string>* errors);
