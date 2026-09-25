#pragma once

#include <string>
#include <cstdint>
#include "dom.h"

// Parse UTF-8 markdown text into out.
// Returns true on success, false on parse error.
bool ParseMarkdown(const std::string& utf8, Document& out);

// Incremental reparse: reparses only the blocks affected by an edit.
// Parameters:
//   utf8       - the full new text
//   oldDoc     - the previous Document (used to identify unchanged blocks)
//   editOffset - byte offset where the edit starts
//   oldLen     - number of bytes removed
//   newLen     - number of bytes inserted
//   out        - output: updated Document
// Returns true on success.
bool ParseMarkdownIncremental(const std::string& utf8,
                               const Document& oldDoc,
                               uint32_t editOffset,
                               uint32_t oldLen,
                               uint32_t newLen,
                               Document& out);

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
