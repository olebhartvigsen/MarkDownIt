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
    void ReplaceTextRange(uint32_t offset, uint32_t length,
                          const std::string& replacement,
                          EditType type = EditType::Other,
                          const Selection* undoSelectionBefore = nullptr);
    // Return true when the source buffer changed. Passing the parsed document
    // lets paragraph-boundary deletion remove one complete paragraph separator.
    bool DeleteBackward(const Document* doc = nullptr);
    bool DeleteForward(const Document* doc = nullptr);
    void DeleteWordBackward();
    void DeleteWordForward();
    void DeleteSelection();

    // Insert a paragraph break, context-aware.
    bool InsertParagraphBreak(const Document& doc);

    // Insert one Markdown soft line break without creating a new paragraph.
    bool InsertSoftBreak(const Document& doc);

    // Undo/redo: apply inverse or re-apply an entry.
    bool Undo(UndoEntry* undone = nullptr);
    bool Redo(UndoEntry* redone = nullptr);

    // Break undo coalescing (call on caret movement).
    void BreakUndoCoalesce() { if (undo_) undo_->BreakCoalesce(); }

private:
    TextBuffer* buf_;
    Selection*  sel_;
    UndoStack*  undo_;

    void RecordAndApply(uint32_t offset, uint32_t length,
                        const std::string& replacement,
                        EditType type,
                        const Selection* undoSelectionBefore = nullptr);
};

// Find the previous grapheme boundary before the given offset.
// Returns 0 if already at the start.
uint32_t PrevGraphemeBoundary(const std::string& s, uint32_t offset);

// Find the next grapheme boundary after the given offset.
// Returns s.size() if already at the end.
uint32_t NextGraphemeBoundary(const std::string& s, uint32_t offset);

// Return true only at a valid UTF-8 grapheme-cluster boundary.
bool IsGraphemeBoundary(const std::string& s, uint32_t offset);
