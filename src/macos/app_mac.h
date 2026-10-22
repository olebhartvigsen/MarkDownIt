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
#include <functional>
#include <functional>
#include <string>
#include <vector>

#include "caret.h"
#include "dom.h"
#include "editcontroller.h"
#include "findbar_model.h"
#include "textbuffer.h"
#include "undostack.h"

namespace mac {

class ContentView;

// The find bar. The bar is an AppKit view, so only the implementation
// file knows the whole class.
class FindBar;

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

    // Reads the open file again and throws the changes away. Returns
    // false when there is no file open or the file cannot be read.
    bool ReloadFile();

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

    // Sets the handler that reloads the file. The window owns it,
    // because the window asks the user before the changes are thrown
    // away.
    void SetReloadHandler(std::function<void()> handler) {
        reloadHandler_ = std::move(handler);
    }

    // Asks for the file to be reloaded.
    void RequestReload() {
        if (reloadHandler_) reloadHandler_();
    }

    // Selects the word around an offset, the way a double click does.
    void SelectWordAt(uint32_t offset);

    // Selects the paragraph around an offset, the way a triple click
    // does.
    void SelectParagraphAt(uint32_t offset);

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

    // Shows the find bar, in the find or the replace mode.
    void ShowFind(FindBarMode mode);

    // Hides the find bar.
    void HideFind();

    // The find bar, which the window puts above the document.
    FindBar& findBar() { return *findBar_; }

    // Re-runs the search against the live document and publishes the
    // result.
    void RefreshFindResults();

    // Replaces the range the find bar stands on.
    void ReplaceMatch(uint32_t offset, uint32_t length);

    // Replaces every match of the find bar, in one edit.
    void ReplaceAllMatches(const std::wstring& query,
                           const std::wstring& replacement);

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
    // The handler that reloads the file.
    std::function<void()> reloadHandler_;
    // The find bar, which owns the search state.
    std::unique_ptr<FindBar> findBar_;
    bool dirty_ = false;
    std::unique_ptr<ContentView> contentView_;
};

}  // namespace mac
