#pragma once

// Outline model: the heading navigation projection of the document DOM.
// Portable and headless (no windows.h) so it builds and tests under g++.
// The pane keeps no second document copy; it borrows the Document after
// each reparse and derives its item list.

#include <vector>
#include <string>
#include <cstdint>
#include "dom.h"

// One outline entry, in document order.
struct OutlineItem {
    int              level = 0;   // heading level 1..6
    uint32_t         offset = 0;  // source byte offset of the heading text
    std::u32string   text;        // simplified plain text of the heading
};

// Collect all heading nodes in document order. Simplification: inline
// spans of any kind contribute their plain UTF-32 text only; images
// contribute nothing. Returns an empty vector when no headings exist.
std::vector<OutlineItem> CollectHeadings(const Document& doc);

// Plain text of one item (helper exposed for tests).
std::u32string ConcatenateInlineText(const OutlineItem& item);
