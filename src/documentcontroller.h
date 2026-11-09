// The document controller: the shared owner of the edit surface
// (plan 4.1, extraction batch 4).
//
// Both apps used to carry the same editing state twice, once inside
// AppWindow on Windows and once inside App on macOS, with command
// bodies written twice, one per port. The controller owns that state
// once: the text buffer, the selection, the edit controller and the
// undo stack live here, the ports hold aliases of them, and the
// command bodies move in one at a time, family by family
// (references/batch4-controller-extraction.md).
//
// This wave carries the skeleton and family 2, headings and lists:
// SetHeading, ToggleBullets, ToggleNumbering, ToggleQuote, Indent
// and Outdent, the twins that used to live in app.cpp and
// main_mac.mm.
//
// The controller is deliberately thin about everything that is not
// text: it does not know a window, a renderer or a dialog. A command
// body ends by notifying the port through the document changed
// handler, and the port does its own re-parse, redraw and menu
// sync, exactly what the command twins used to do by hand.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "caret.h"
#include "editcontroller.h"
#include "textbuffer.h"
#include "undostack.h"

class DocumentController {
public:
    DocumentController();

    // The owned state. The ports alias these so every address in
    // their code keeps pointing at the same objects.
    TextBuffer& buffer() { return buffer_; }
    Selection& selection() { return sel_; }
    EditController& editor() { return editor_; }
    UndoStack& undo() { return undo_; }

    // The edit-mode gate. The ports own the mode flag (their views
    // and bars read it too); they keep the controller in step, and
    // every command body checks it here before it touches text. A
    // mutating command in read mode is a no-op, never a mode
    // switch, the rule both ports already enforced (word processor
    // guide, read-only documents).
    void SetEditing(bool on) { editing_ = on; }
    bool IsEditing() const { return editing_; }

    // Family 2: headings and lists. SetHeading takes the resolved
    // level: 0 means paragraph. The Windows ribbon answers a toggle
    // (pressing the heading's own button reverts to a paragraph), so
    // the Windows shell resolves the level before the call; the mac
    // menu items apply a level outright. The bodies are the movers
    // from the old per-port twins: the splice and the undo entry
    // come from the shared formatting functions, and the undo
    // coalescing break keeps one command from merging into a typing
    // run (undo stack, one entry per command).
    void SetHeading(int level);
    void ToggleBullets();
    void ToggleNumbering();
    void ToggleQuote();
    void Indent();
    void Outdent();

    // Splices the buffer and records the undo entry in one step, so
    // the table, find and link paths of both ports share one entry
    // shape. The stamp comes from the platform clock, so both ports
    // stamp the same monotonic ms.
    void SpliceWithUndo(uint32_t offset, uint32_t length,
                        const std::string& replacement);

    // One undo step for a pending format toggle, so undo brings the
    // old pending state back before it touches any text (text guide,
    // bold and italic at an empty caret, rule 5). The port passes the
    // selection and the before state from its arguments plus its own
    // current pending state as the after state.
    void RecordPendingFormatUndo(const Selection& sel, bool beforeBold,
                                bool beforeItalic, bool beforeBoldSet,
                                bool beforeItalicSet, bool afterBold,
                                bool afterItalic, bool afterBoldSet,
                                bool afterItalicSet);

    // What a command body calls when it has changed the document:
    // the port re-parses, re-renders and re-syncs its chrome. The
    // handler runs inside the command, so the call order stays the
    // one the old command twins kept.
    using DocumentChangedHandler = std::function<void()>;
    void SetDocumentChangedHandler(DocumentChangedHandler handler) {
        documentChanged_ = std::move(handler);
    }

private:
    // Declaration order matters: the edit controller binds to the
    // buffer and the selection by address in its constructor.
    TextBuffer buffer_;
    Selection sel_;
    EditController editor_;
    UndoStack undo_;
    bool editing_ = false;
    DocumentChangedHandler documentChanged_;

    void NotifyDocumentChanged() {
        if (documentChanged_) documentChanged_();
    }
};
