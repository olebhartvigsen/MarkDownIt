#pragma once
#include "textbuffer.h"
#include "caret.h"
#include "dom.h"
#include <string>

class EditController {
public:
    EditController() : buf_(nullptr), sel_(nullptr) {}
    EditController(TextBuffer* buf, Selection* sel)
        : buf_(buf), sel_(sel) {}

    void InsertText(const std::string& utf8);
    void DeleteBackward();
    void DeleteForward();
    void DeleteSelection();

    // Insert a paragraph break, context-aware.
    // The Document is needed to determine the current block type.
    void InsertParagraphBreak(const Document& doc);

private:
    TextBuffer* buf_;
    Selection*  sel_;
};

// Find the previous grapheme boundary before the given offset.
// Returns 0 if already at the start.
uint32_t PrevGraphemeBoundary(const std::string& s, uint32_t offset);

// Find the next grapheme boundary after the given offset.
// Returns s.size() if already at the end.
uint32_t NextGraphemeBoundary(const std::string& s, uint32_t offset);
