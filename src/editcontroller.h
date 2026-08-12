#pragma once
#include "textbuffer.h"
#include "caret.h"
#include "dom.h"
#include "undostack.h"
#include <string>

class EditController {
public:
    EditController() : buf_(nullptr), sel_(nullptr), undo_(nullptr) {}
    EditController(TextBuffer* buf, Selection* sel)
        : buf_(buf), sel_(sel), undo_(nullptr) {}

    void SetUndoStack(UndoStack* u) { undo_ = u; }

    void InsertText(const std::string& utf8);
    void DeleteBackward();
    void DeleteForward();
    void DeleteSelection();

    // Insert a paragraph break, context-aware.
    void InsertParagraphBreak(const Document& doc);

    // Undo/redo: apply inverse or re-apply an entry.
    void Undo();
    void Redo();

    // Break undo coalescing (call on caret movement).
    void BreakUndoCoalesce() { if (undo_) undo_->BreakCoalesce(); }

private:
    TextBuffer* buf_;
    Selection*  sel_;
    UndoStack*  undo_;

    void RecordAndApply(uint32_t offset, uint32_t length,
                       const std::string& replacement,
                       EditType type);
};

// Find the previous grapheme boundary before the given offset.
// Returns 0 if already at the start.
uint32_t PrevGraphemeBoundary(const std::string& s, uint32_t offset);

// Find the next grapheme boundary after the given offset.
// Returns s.size() if already at the end.
uint32_t NextGraphemeBoundary(const std::string& s, uint32_t offset);
