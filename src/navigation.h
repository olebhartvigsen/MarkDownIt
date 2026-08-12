#pragma once
#include "layoutcache.h"
#include "textbuffer.h"
#include <cstdint>

// Move left by one grapheme cluster.
uint32_t MoveLeft(const TextBuffer& buf, uint32_t offset);

// Move right by one grapheme cluster.
uint32_t MoveRight(const TextBuffer& buf, uint32_t offset);

// Move left by one word (Ctrl+Left).
uint32_t MoveWordLeft(const TextBuffer& buf, uint32_t offset);

// Move right by one word (Ctrl+Right).
uint32_t MoveWordRight(const TextBuffer& buf, uint32_t offset);

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
