#pragma once
#include "layoutcache.h"
#include "textbuffer.h"
#include <cstdint>

// Move left by one grapheme cluster, skipping hidden markdown markers.
// layoutCache gives access to u16ToSrc mapping to know which source
// offsets are rendered vs. hidden markers. May be null (raw move).
uint32_t MoveLeft(const TextBuffer& buf, uint32_t offset,
                  const LayoutCache* layoutCache = nullptr);

// Move right by one grapheme cluster, skipping hidden markdown markers.
uint32_t MoveRight(const TextBuffer& buf, uint32_t offset,
                   const LayoutCache* layoutCache = nullptr);

// Move left by one word (Ctrl+Left).
uint32_t MoveWordLeft(const TextBuffer& buf, uint32_t offset);

// Move right by one word (Ctrl+Right).
uint32_t MoveWordRight(const TextBuffer& buf, uint32_t offset);

// Return the contiguous Unicode word/space/punctuation span containing offset.
void WordSpanAt(const TextBuffer& buf, uint32_t offset,
                uint32_t* outStart, uint32_t* outEnd);
bool IsOffsetInTable(const Document& doc, uint32_t offset);

// Move to the adjacent Markdown table cell. Returns false outside a table
// or at the requested document edge.
bool MoveTableCell(const Document& doc, uint32_t offset, bool backwards,
                   uint32_t* destination);

// Move up or down by one rendered line.
// direction: -1 for up, +1 for down.
// desiredX: in/out. On first vertical move, set to caret x.
//          On subsequent moves, keep the same desiredX.
// Returns the new offset.
uint32_t MoveVertical(const LayoutCache& lc, uint32_t offset,
                      int direction, float* desiredX,
                      float scrollY, float lineHeight);

// Move to the start of the visual line containing offset.
uint32_t MoveLineStart(const LayoutCache& lc, uint32_t offset);

// Move to the end of the visual line containing offset.
uint32_t MoveLineEnd(const LayoutCache& lc, uint32_t offset);
