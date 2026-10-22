// The macOS application object.
//
// The document, the text buffer, the edit controller and the undo stack
// are the shared core, used exactly as the Windows build uses them. Only
// the window and the drawing are new.

#include "app_mac.h"

#include <fstream>
#include <sstream>

#include "clipboard_mac.h"
#include "content_view_mac.h"
#include "parser.h"

namespace mac {

namespace {

// The clipboard the app uses. There is one pasteboard on macOS, so one
// instance serves the whole app.
Clipboard& TheClipboard() {
    static Clipboard clipboard;
    return clipboard;
}

}  // namespace

App::App() : editor_(&buffer_, &sel_) {
    editor_.SetUndoStack(&undo_);
}

App::~App() = default;

bool App::OpenFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string text = contents.str();

    filePath_ = path;
    buffer_.SetText(text);
    buffer_.ClearDirty();
    dirty_ = false;
    sel_.Collapse(CaretPos{0});
    Reparse();
    return true;
}

void App::NewDocument() {
    filePath_.clear();
    buffer_.SetText(std::string());
    buffer_.ClearDirty();
    dirty_ = false;
    sel_.Collapse(CaretPos{0});
    Reparse();
}

void App::MarkClean() {
    buffer_.ClearDirty();
    dirty_ = false;
}

void App::Reparse() {
    // The parse is the shared one. A parse failure leaves the previous
    // document in place, which is what the Windows build does too. The
    // user still sees their text.
    if (!ParseMarkdown(buffer_.Text(), document_)) {
        return;
    }
    if (contentView_) contentView_->OnDocumentChanged();
}

void App::OnTextChanged() {
    dirty_ = true;
    Reparse();
}

void App::OnSelectionChanged() {
    if (contentView_) contentView_->Invalidate();
}

void App::SetCaret(uint32_t offset) {
    const uint32_t length = buffer_.Length();
    if (offset > length) offset = length;
    sel_.Collapse(CaretPos{offset});
    editor_.BreakUndoCoalesce();
    OnSelectionChanged();
}

void App::SetSelection(uint32_t start, uint32_t end) {
    const uint32_t length = buffer_.Length();
    if (start > length) start = length;
    if (end > length) end = length;
    sel_.anchor = CaretPos{start};
    sel_.active = CaretPos{end};
    editor_.BreakUndoCoalesce();
    OnSelectionChanged();
}

std::string App::SelectedText() const {
    if (sel_.Empty()) return std::string();
    const std::string& text = buffer_.Text();
    const uint32_t start = sel_.Start();
    const uint32_t end = sel_.End();
    if (start >= text.size() || end > text.size() || start >= end) {
        return std::string();
    }
    return text.substr(start, end - start);
}

std::string App::ClipboardText() const {
    return TheClipboard().GetText();
}

void App::SetClipboardText(const std::string& text) {
    TheClipboard().SetText(text);
}

void App::InsertText(const std::string& text) {
    if (text.empty()) return;
    editor_.InsertText(text);
    OnTextChanged();
}

void App::DeleteBackward() {
    if (sel_.Empty()) {
        editor_.DeleteBackward(&document_);
    } else {
        editor_.DeleteSelection();
    }
    OnTextChanged();
}

void App::DeleteForward() {
    if (sel_.Empty()) {
        editor_.DeleteForward(&document_);
    } else {
        editor_.DeleteSelection();
    }
    OnTextChanged();
}

void App::InsertParagraphBreak() {
    editor_.InsertParagraphBreak(document_);
    OnTextChanged();
}

void App::Undo() {
    if (editor_.Undo()) {
        OnTextChanged();
    }
}

void App::Redo() {
    if (editor_.Redo()) {
        OnTextChanged();
    }
}

void App::SetContentView(ContentView* view) {
    contentView_.reset(view);
}

}  // namespace mac
