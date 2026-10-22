#pragma once

// The macOS application object.
//
// The Windows app has one App class that owns the document, the editor and
// the window. The mac app has the same shape, but the window is an
// NSWindow and the drawing happens in a content view.
//
// The shared core is reused as it is: the Document comes from the parser,
// the editing goes through the edit controller, and the undo stack is the
// same one the Windows build uses.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "caret.h"
#include "dom.h"
#include "editcontroller.h"
#include "textbuffer.h"
#include "undostack.h"

namespace mac {

class ContentView;

// The application. Owns the document, the editing state and the window.
class App {
public:
    App();
    ~App();

    // Opens a file. Returns false when the file cannot be read, which the
    // caller reports to the user.
    bool OpenFile(const std::string& path);

    // Starts an empty document. The file path is cleared, so the app
    // asks for a name when the document is saved.
    void NewDocument();

    // The document as it stands, parsed from the current text.
    const Document& GetDocument() const { return document_; }

    // The text buffer, which the content view edits through.
    TextBuffer& buffer() { return buffer_; }

    // The edit controller, which owns the caret and the selection.
    EditController& editor() { return editor_; }

    // The selection, which the mouse and the keyboard both drive.
    Selection& selection() { return sel_; }
    const Selection& selection() const { return sel_; }

    // The undo stack.
    UndoStack& undo() { return undo_; }

    // The path of the open file, empty when nothing is open.
    const std::string& filePath() const { return filePath_; }

    // Whether the text has changed since the last save.
    bool IsDirty() const { return dirty_; }

    // Marks the document as saved.
    void MarkClean();

    // Reparses the text into the document. Called after every edit, but
    // only when the edit could have changed the structure.
    void Reparse();

    // The content view, which the window hosts.
    ContentView* contentView() const { return contentView_.get(); }

    // Called by the content view when the user edits the text.
    void OnTextChanged();

    // Called by the content view when the caret or the selection moved.
    void OnSelectionChanged();

    // Sets the caret, moving the selection with it.
    void SetCaret(uint32_t offset);

    // Selects a range of the text.
    void SetSelection(uint32_t start, uint32_t end);

    // Whether the selection covers any text.
    bool HasSelection() const { return !sel_.Empty(); }

    // The selected text, empty when there is no selection.
    std::string SelectedText() const;

    // The text on the clipboard.
    std::string ClipboardText() const;

    // Puts text on the clipboard.
    void SetClipboardText(const std::string& text);

    // Inserts text at the caret, replacing the selection when there is one.
    void InsertText(const std::string& text);

    // Deletes the selection, or the character before the caret.
    void DeleteBackward();

    // Deletes the selection, or the character after the caret.
    void DeleteForward();

    // Inserts a paragraph break, which the shared controller turns into
    // the right markdown.
    void InsertParagraphBreak();

    // Undoes the last edit.
    void Undo();

    // Redoes the last undone edit.
    void Redo();

    // Takes ownership of the content view, which the window creates.
    void SetContentView(ContentView* view);

    // The source text, which is what a source view shows.
    const std::string& sourceText() const { return buffer_.Text(); }

private:
    std::string filePath_;
    Document document_;
    TextBuffer buffer_;
    Selection sel_;
    EditController editor_;
    UndoStack undo_;
    bool dirty_ = false;
    std::unique_ptr<ContentView> contentView_;
};

}  // namespace mac
