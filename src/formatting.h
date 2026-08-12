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
