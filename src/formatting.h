#pragma once
#include "textbuffer.h"
#include "caret.h"
#include <string>

// Toggle an inline marker (e.g. ** for bold, * for italic, ` for code, ~~ for strikethrough).
// If the selection is already wrapped in the marker, remove it.
// If not, wrap it. Handles empty selection by inserting marker pair and
// placing the caret between them.
void ToggleInlineMarker(TextBuffer* buf, Selection* sel, const std::string& marker);

// Insert a link around the selection: [text](url)
// If the selection is empty, inserts [](url) with caret between [ and ].
void InsertLink(TextBuffer* buf, Selection* sel, const std::string& url);

// Check if the text at the given range is already wrapped in the marker.
bool IsWrappedIn(const std::string& text, uint32_t start, uint32_t end,
                 const std::string& marker);


// --- Block formatting ---

// Set heading level on the current line. level 0 = remove heading.
void SetHeadingLevel(TextBuffer* buf, Selection* sel, int level);

// Toggle unordered list (- prefix) on the current line.
void ToggleUnorderedList(TextBuffer* buf, Selection* sel);

// Toggle ordered list (1. prefix) on the current line.
void ToggleOrderedList(TextBuffer* buf, Selection* sel);

// Toggle blockquote (> prefix) on the current line.
void ToggleBlockquote(TextBuffer* buf, Selection* sel);

// Indent the current line one level (for use inside lists).
void IndentLine(TextBuffer* buf, Selection* sel);

// Outdent the current line one level.
void OutdentLine(TextBuffer* buf, Selection* sel);

// Renumber an ordered list run starting at the given line.
// Called after structural changes to fix numbering.
void RenumberOrderedList(TextBuffer* buf, uint32_t lineStart);
