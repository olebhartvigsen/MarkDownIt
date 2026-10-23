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
#include "markers.h"
#include "searchreplace.h"
#include "tableedit.h"
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

    // The marker store, which holds the highlights.
    MarkerStore& markers() { return markerStore_; }

    // Hands the marked ranges to the renderer, which draws them behind
    // the text.
    void RefreshMarkerRanges();

    // Writes and reads the markers of the open document.
    void SaveMarkers();
    void LoadMarkers();

    MarkerStore markerStore_;
    std::vector<TextMatch> markerRanges_;

    // The path of the open file, empty when nothing is open.
    const std::string& filePath() const { return filePath_; }

    // Whether the text has changed since the last save.
    bool IsDirty() const { return dirty_; }

    // Marks the document as saved.
    void MarkClean();

    // Puts new text in the document, which the Word import does.
    void SetText(const std::string& text);

    // Replaces the document with imported text. The document has no
    // file of its own, and it is dirty, so Save asks for a name and
    // closing asks whether to save (office spec, Word import).
    void AdoptImportedText(const std::string& text);

    // Takes the formatting away from the selection, and leaves the
    // text as it is.
    void ClearFormat();

    // The bold and italic commands. With a selection the shared marker
    // toggle applies the guide's rules (extend a run instead of
    // nesting, split it at the selection, strip what is already
    // formatted). An empty caret toggles the pending typing format
    // instead and writes nothing to the document (text guide, fed og
    // kursiv rules and the empty caret rules).
    void ToggleBold();
    void ToggleItalic();

    // What the strip shows for the bold and italic buttons: the
    // pending typing format first, then the format under the selection
    // or the caret.
    bool FormatButtonOn(const std::string& marker) const;

    // The text markers, the highlights the user marked. A selection
    // that touches a marker takes it away, and any other selection
    // makes one.
    void ToggleMarkSelection();

    // Whether the selection overlaps any marker. A partial overlap
    // counts, and an unresolved marker counts too; both the button
    // state and the toggle's direction read this (highlighting guide
    // 4 and 6).
    bool SelectionHasMarker() const;

    // The document was saved under a new name, so the path the marker
    // sidecar keys off moves with it (highlighting guide 12.7).
    void AdoptSavedPath(const std::string& path);

    // The table commands. They do nothing when the caret is not in a
    // table, and a table never nests.
    void InsertTable(int cols, int rows);
    void AddTableRow(bool below);
    void RemoveTableRow();

    // Appends a blank row to the table the caret is in, the way Tab
    // from the last cell does.
    void AppendRowToTable();

    // Pastes a converted table at the caret in one undo step.
    void InsertPastedTable(const std::string& table,
                           uint32_t firstCellCaret);
    void AddTableColumn(bool right);
    void RemoveTableColumn();
    void SplitTableCell();
    void AlignTableColumn(int mark);
    void RemoveTable();

    // Reparses the text into the document. Called after every edit, but
    // only when the edit could have changed the structure.
    void Reparse();

    // The table the caret is in, and where in it the caret is.
    bool CaretTable(tableedit::TableLocation* out) const;

    // The source span of the table the caret is in.
    bool TableSpan(uint32_t* start, uint32_t* end) const;

    // Replaces the table's source span with new text, in one edit, so
    // one table operation is one undo step.
    void RewriteTable(uint32_t start, uint32_t end,
                      const std::string& text, uint32_t caret);

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
    // A hook that is called after every reparse, so the views that
    // borrow the document read it again.
    using DocumentChangedHandler = std::function<void()>;
    void SetDocumentChangedHandler(DocumentChangedHandler handler) {
        documentChanged_ = std::move(handler);
    }

    // The handler that runs after the caret or the selection moved. The
    // command strip reads the format state through it.
    using SelectionChangedHandler = std::function<void()>;
    void SetSelectionChangedHandler(SelectionChangedHandler handler) {
        selectionChanged_ = std::move(handler);
    }

    void SetReloadHandler(std::function<void()> handler) {
        reloadHandler_ = std::move(handler);
    }

    // Says whether the welcome screen is shown instead of a document.
    void SetWelcomeMode(bool mode) { welcomeMode_ = mode; }
    bool WelcomeMode() const { return welcomeMode_; }

    // Sets the handler that opens a file, which the welcome screen
    // calls when a card is clicked.
    void SetOpenFileHandler(std::function<void(const std::string&)> handler) {
        openFileHandler_ = std::move(handler);
    }

    // Asks for a file to be opened.
    void RequestOpenFile(const std::string& path) {
        if (openFileHandler_) openFileHandler_(path);
    }

    // Asks for the file to be reloaded.
    void RequestReload() {
        if (reloadHandler_) reloadHandler_();
    }

    // Selects the word around an offset, the way a double click does.
    void SelectWordAt(uint32_t offset);

    // The source range of the word that holds the offset, snapped to
    // drawn text. A drag after a double click moves the edge by it.
    bool WordSpanForOffset(uint32_t offset, uint32_t* outStart,
                           uint32_t* outEnd) const;

    // Selects the paragraph around an offset, the way a triple click
    // does.
    void SelectParagraphAt(uint32_t offset);

    // The source range of the paragraph that holds the offset,
    // including the break that ends it. A drag after a triple click
    // moves the edge by it.
    bool ParagraphRangeForOffset(uint32_t offset, uint32_t* outStart,
                                 uint32_t* outEnd) const;

    // The text of the document as it goes on the disk.
    //
    // A standalone SVG file is wrapped in a fence while it is shown, so
    // the text that goes back to the disk takes the fence away again.
    std::string TextForSaving() const;

    // Whether the document is a standalone SVG file.
    bool IsStandaloneSvg() const { return isSvg_; }

    // The selected text, empty when there is no selection.
    std::string SelectedText() const;

    // The text on the clipboard.
    std::string ClipboardText() const;

    // Puts text on the clipboard.
    // Puts the text on the clipboard and reports whether the write
    // went through; the cut path only deletes on success (text spec
    // C3).
    bool SetClipboardText(const std::string& text);

    // Copy and cut of the selection through the clipboard text of
    // SelectionForClipboard.
    void CopyToClipboard();
    void CutToClipboard();

    // The clipboard text of the selection: the raw bytes in the
    // source view, drawn text everywhere else, so hidden markers and
    // fence lines never leak into a copy (text spec C2).
    std::string SelectionForClipboard() const;

    // Selects the whole document.
    void SelectAll();

    // The view shows the raw Markdown, so a copy is byte for byte.
    void SetSourceView(bool on) { sourceView_ = on; }

    // Inserts text at the caret, replacing the selection when there is one.
    void InsertText(const std::string& text);

    // Pastes the clipboard's plain text at the caret, replacing the
    // selection. Inside a table cell the text is sanitized to one row
    // line, and tab separated rows become a table in one undo step.
    void PasteFromClipboard();

    // Tab and Shift+Tab. Inside a table they move between cells through
    // the shared model, and Tab from the last cell appends a row.
    // Outside a table a tab character is inserted.
    void HandleTab(bool backwards);

    // Deletes the selection, or the character before the caret.
    void DeleteBackward();

    // Deletes the selection, or the character after the caret.
    void DeleteForward();

    // Deletes the selection, or the word before the caret.
    void DeleteWordBackward();

    // Deletes the selection, or the word after the caret.
    void DeleteWordForward();

    // Inserts a Markdown soft line break at the caret. A table row does
    // not take one.
    void InsertSoftBreak();

    // Inserts a paragraph break, which the shared controller turns into
    // the right markdown.
    void InsertParagraphBreak();

    // Shows the find bar, in the find or the replace mode.
    void ShowFind(FindBarMode mode);

    // Hides the find bar.
    void HideFind();

    // The find bar, which the window puts above the document.
    FindBar& findBar() { return *findBar_; }

    // The find bar came or went. The window gives the strip its height
    // from this call and hands the focus back to the document on the
    // way out.
    void SetFindVisibilityCallback(std::function<void(bool)> callback) {
        findVisibilityChanged_ = callback;
    }

    // Re-runs the search against the live document and publishes the
    // result. `adoptCurrentMatch` re-aims the current match at the
    // caret, which an incremental search wants and a document edit
    // does not.
    void RefreshFindResults(bool adoptCurrentMatch);

    // Selects the current match and brings it into view. Navigation
    // and every refresh end here.
    void ApplyFindSelection(uint32_t offset, uint32_t length);

    // Replaces the range the find bar stands on.
    void ReplaceMatch(uint32_t offset, uint32_t length);

    // Replaces every match of the find bar, in one edit.
    void ReplaceAllMatches(const std::wstring& query,
                           const std::wstring& replacement);

    // The shared body of the bold and italic commands.
    void ToggleInlineWithPending(const std::string& marker, bool bold);

    // Puts typed text through the pending format: the first character
    // opens the markers around itself, the next ones extend the run.
    void ApplyPendingTypingFormat(const std::string& value);

    // Records a pending toggle as its own undo step.
    void RecordPendingFormatUndo(bool beforeBold, bool beforeItalic,
                                 bool beforeBoldSet, bool beforeItalicSet);

    // Restores the pending state an undo or redo entry carries.
    void RestorePendingFormat(const UndoEntry& entry, bool redo);

    // Whether the caret sits in a span of the format, which is where a
    // pending toggle starts from.
    bool CaretFormatIsActive(const std::string& marker) const;

    // Ends the pending format and the typing run.
    void ClearPendingFormat();

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
    DocumentChangedHandler documentChanged_;
    SelectionChangedHandler selectionChanged_;
    TextBuffer buffer_;
    // True while the window shows the raw source (spec C2).
    bool sourceView_ = false;
    Selection sel_;
    EditController editor_;
    UndoStack undo_;
    // The pending typing format: bold or italic toggled with an empty
    // caret, waiting for the next typed characters (text guide, fed og
    // kursiv ved et tomt caret).
    bool pendingBold_ = false;
    bool pendingItalic_ = false;
    bool pendingBoldSet_ = false;
    bool pendingItalicSet_ = false;
    // Whether the caret still sits on the suffix markers of the run
    // being typed, which lets the next characters extend that run.
    bool pendingRunActive_ = false;
    uint32_t pendingRunSuffixBytes_ = 0;
    // Whether the welcome screen is shown instead of a document.
    bool welcomeMode_ = false;
    // The handler that opens a file.
    std::function<void(const std::string&)> openFileHandler_;
    // Whether the document is a standalone SVG file.
    bool isSvg_ = false;
    // Whether the fence around the SVG was put there by the app.
    bool svgWrapped_ = false;
    // The handler that reloads the file.
    std::function<void()> reloadHandler_;
    // The find bar, which owns the search state.
    std::unique_ptr<FindBar> findBar_;
    // The window, told when the bar comes and goes.
    std::function<void(bool)> findVisibilityChanged_;
    bool dirty_ = false;
    std::unique_ptr<ContentView> contentView_;
};

}  // namespace mac
