// The macOS application object.
//
// The document, the text buffer, the edit controller and the undo stack
// are the shared core, used exactly as the Windows build uses them. Only
// the window and the drawing are new.

#include "app_mac.h"

#include <fstream>
#include <sstream>

#include "autoformat.h"
#include "clipboard_mac.h"
#include "content_view_mac.h"
#include "findbar_mac.h"
#include "inputfilter.h"
#include "navigation.h"
#include "parser.h"
#include "tableedit.h"
#include "textenc.h"

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
    findBar_ = std::make_unique<FindBar>();

    // The bar asks the app where the caret is, and tells it what to do
    // when the search changes or a match is picked.
    findBar_->model().SetListener({
        .caretOffset = [this] {
            return sel_.active.offset;
        },
        .onSearchChanged = [this] {
            RefreshFindResults();
        },
        .onSelectionChanged = [this](uint32_t offset, uint32_t length) {
            SetSelection(offset, offset + length);
            if (contentView_) contentView_->Invalidate();
        },
        .onReplaceCurrent = [this](uint32_t offset, uint32_t length) {
            ReplaceMatch(offset, length);
        },
        .onReplaceAll = [this](const std::wstring& query,
                               const std::wstring& replacement,
                               const FindStateOptions&) {
            ReplaceAllMatches(query, replacement);
        },
        .onClose = [this] {
            HideFind();
        },
    });
}

App::~App() = default;

namespace {

// Whether the text already has a fence of the given language.
bool HasLangFence(const std::string& text, const std::string& lang) {
    size_t i = 0;
    while (i < text.size()) {
        size_t eol = text.find('\n', i);
        std::string line = text.substr(
            i, (eol == std::string::npos ? text.size() : eol) - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t b = 0;
        while (b < line.size() && b < 3 && line[b] == ' ') b++;
        if (line.compare(b, 3, "```") == 0) {
            std::string info = line.substr(b + 3);
            while (!info.empty() && (info.back() == ' ' ||
                                     info.back() == '\t' || info.back() == '\r')) {
                info.pop_back();
            }
            if (info == lang) return true;
        }
        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    return false;
}

// Puts a fence around the text.
std::string WrapLangFence(const std::string& lang, const std::string& text) {
    std::string out = "```" + lang + "\n";
    out += text;
    if (out.empty() || out.back() != '\n') out += '\n';
    if (text.empty()) out += '\n';
    out += "```\n";
    return out;
}

// Takes the fence away again.
//
// At most one fence at the top and the one at the end go, so the
// content that has fences of its own stays as it was.
std::string UnwrapLangFence(const std::string& lang, const std::string& text) {
    const std::string open = "```" + lang + "\n";
    if (text.compare(0, open.size(), open) != 0) return text;

    std::string body = text.substr(open.size());
    const size_t close = body.rfind("```");
    if (close == std::string::npos) return text;
    body = body.substr(0, close);
    if (!body.empty() && body.back() == '\n') body.pop_back();
    return body;
}

// Whether the path ends in the given extension.
bool EndsWith(const std::string& path, const std::string& extension) {
    if (path.size() < extension.size()) return false;
    return path.compare(path.size() - extension.size(), extension.size(),
                        extension) == 0;
}

}  // namespace

bool App::OpenFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream contents;
    contents << file.rdbuf();
    std::string text = contents.str();

    // A standalone SVG file is a picture, so the content is wrapped in
    // a fence, and the normal pipeline reads it as one.
    isSvg_ = EndsWith(path, ".svg");
    svgWrapped_ = false;
    if (isSvg_ && !HasLangFence(text, "svg")) {
        text = WrapLangFence("svg", text);
        svgWrapped_ = true;
    }

    filePath_ = path;
    buffer_.SetText(text);
    buffer_.ClearDirty();
    dirty_ = false;
    sel_.Collapse(CaretPos{0});
    Reparse();
    return true;
}

std::string App::TextForSaving() const {
    if (svgWrapped_) return UnwrapLangFence("svg", buffer_.Text());
    return buffer_.Text();
}

void App::NewDocument() {
    filePath_.clear();
    isSvg_ = false;
    svgWrapped_ = false;
    buffer_.SetText(std::string());
    buffer_.ClearDirty();
    dirty_ = false;
    sel_.Collapse(CaretPos{0});
    Reparse();
}

bool App::ReloadFile() {
    if (filePath_.empty()) {
        return false;
    }
    return OpenFile(filePath_);
}

void App::ShowFind(FindBarMode mode) {
    // The field starts with the selected text, the way the Windows bar
    // does it, so the user can widen the query.
    const std::wstring query = textenc::Utf8ToWide(SelectedText());
    findBar_->model().SetSearchText(query);
    findBar_->model().SetReplaceEnabled(true);
    findBar_->Show(mode);
    RefreshFindResults();
}

void App::HideFind() {
    findBar_->Hide();
    if (contentView_) contentView_->Invalidate();
}

void App::RefreshFindResults() {
    FindReplaceState& state = findBar_->model().state();
    // The search runs over the whole buffer. A match never spans a line
    // break, so the search cannot join two blocks.
    state.Refresh(buffer_.Text(), nullptr, nullptr);
    if (state.HasQuery()) {
        state.AdoptMatchAt(sel_.active.offset);
    }
    findBar_->model().SyncFromState();

    if (contentView_) {
        contentView_->SetSearchMatches(&state.Matches(), state.CurrentIndex());
    }
}

void App::ReplaceMatch(uint32_t offset, uint32_t length) {
    if (length == 0) return;

    const std::wstring replacement = findBar_->model().GetReplaceText();
    const std::string text = textenc::WideToUtf8(replacement);
    if (text.empty()) return;

    SetSelection(offset, offset + length);
    editor_.ReplaceTextRange(offset, length, text, EditType::Insert);
    OnTextChanged();
    RefreshFindResults();
}

void App::ReplaceAllMatches(const std::wstring& query,
                            const std::wstring& replacement) {
    if (query.empty()) return;

    const std::string text = textenc::WideToUtf8(replacement);
    FindReplaceState& state = findBar_->model().state();
    std::string replaced;
    const size_t count = state.BuildReplacedDocument(buffer_.Text(), &replaced);
    if (count == 0) {
        return;
    }

    editor_.ReplaceTextRange(0, static_cast<uint32_t>(buffer_.Length()),
                              replaced, EditType::Insert);
    OnTextChanged();
    RefreshFindResults();
}

void App::MarkClean() {
    buffer_.ClearDirty();
    dirty_ = false;
}

void App::SetText(const std::string& text) {
    buffer_.SetText(text);
    sel_.Collapse(CaretPos{0});
    dirty_ = true;
    Reparse();
}

// The table the caret is in, and where in it the caret is.
bool App::CaretTable(tableedit::TableLocation* out) const {
    const Node* table =
        tableedit::ContainingTable(document_, sel_.active.offset);
    if (!table) return false;
    return tableedit::Locate(buffer_.Text(), table, sel_.active.offset, out);
}

// The source span of the table the caret is in. The span is empty when
// the caret is not in a table.
bool App::TableSpan(uint32_t* start, uint32_t* end) const {
    const Node* table =
        tableedit::ContainingTable(document_, sel_.active.offset);
    if (!table) return false;
    *start = table->srcOffset;
    *end = table->srcOffset + table->srcLength;
    return true;
}

// Replaces the table's source span with new text. The whole span goes
// in one edit, so one table operation is one undo step.
void App::RewriteTable(uint32_t start, uint32_t end,
                       const std::string& text, uint32_t caret) {
    editor_.ReplaceTextRange(start, end - start, text, EditType::Insert);
    editor_.BreakUndoCoalesce();
    sel_.Collapse({caret});
    OnTextChanged();
}

// Adds a row above or below the caret's row.
void App::AddTableRow(bool below) {
    tableedit::TableLocation loc;
    if (!CaretTable(&loc)) return;
    const tableedit::TableEdit edit =
        tableedit::AddRow(buffer_.Text(), loc, below);
    if (!edit.ok) return;
    RewriteTable(edit.start, edit.start + edit.length, edit.text, edit.caret);
}

// Removes the caret's row.
void App::RemoveTableRow() {
    tableedit::TableLocation loc;
    if (!CaretTable(&loc)) return;
    const tableedit::TableEdit edit =
        tableedit::RemoveRow(buffer_.Text(), loc);
    if (!edit.ok) return;
    RewriteTable(edit.start, edit.start + edit.length, edit.text, edit.caret);
}

// Adds a column to the left or the right of the caret's column.
void App::AddTableColumn(bool right) {
    tableedit::TableLocation loc;
    if (!CaretTable(&loc)) return;

    uint32_t start = 0;
    uint32_t end = 0;
    if (!TableSpan(&start, &end)) return;

    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableInsertColumn(buffer_.Text(), start, end, loc.columnIndex, right,
                           sel_.active.offset, &rebuilt, &caret)) {
        return;
    }
    RewriteTable(start, end, rebuilt, caret);
}

// Removes the caret's column.
void App::RemoveTableColumn() {
    tableedit::TableLocation loc;
    if (!CaretTable(&loc)) return;
    if (!TableCapabilitiesFor(loc.rowIndex, loc.numCols).removeColumn) return;

    uint32_t start = 0;
    uint32_t end = 0;
    if (!TableSpan(&start, &end)) return;

    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableRemoveColumn(buffer_.Text(), start, end, loc.columnIndex,
                           sel_.active.offset, &rebuilt, &caret)) {
        return;
    }
    RewriteTable(start, end, rebuilt, caret);
}

// Splits the caret's cell into two.
void App::SplitTableCell() {
    tableedit::TableLocation loc;
    if (!CaretTable(&loc)) return;

    uint32_t start = 0;
    uint32_t end = 0;
    if (!TableSpan(&start, &end)) return;

    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableSplitCell(buffer_.Text(), start, end, loc.columnIndex,
                        loc.rowIndex, 2, sel_.active.offset,
                        &rebuilt, &caret)) {
        return;
    }
    RewriteTable(start, end, rebuilt, caret);
}

// Sets the alignment of the caret's column.
void App::AlignTableColumn(int mark) {
    tableedit::TableLocation loc;
    if (!CaretTable(&loc)) return;

    uint32_t start = 0;
    uint32_t end = 0;
    if (!TableSpan(&start, &end)) return;

    TableAlignMark alignment = TableAlignMark::None;
    if (mark == 1) alignment = TableAlignMark::Left;
    else if (mark == 2) alignment = TableAlignMark::Center;
    else if (mark == 3) alignment = TableAlignMark::Right;

    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableSetColumnAlign(buffer_.Text(), start, end, loc.columnIndex,
                              alignment, sel_.active.offset,
                              &rebuilt, &caret)) {
        return;
    }
    RewriteTable(start, end, rebuilt, caret);
}

// Inserts a new table at the caret.
void App::InsertTable(int cols, int rows) {
    // A table never nests, so the command does nothing inside one.
    if (tableedit::ContainingTable(document_, sel_.active.offset)) return;

    uint32_t insertPos = sel_.active.offset;
    uint32_t replaceLen = 0;
    if (!sel_.Empty()) {
        insertPos = sel_.Start();
        replaceLen = sel_.Length();
    }

    const std::string& text = buffer_.Text();
    std::string insertText;
    // A table needs a blank separator line in front of it, or the
    // previous block absorbs it.
    insertText.append(TableSeparatorNewlinesBefore(text, insertPos), '\n');
    const uint32_t prefixLen = static_cast<uint32_t>(insertText.size());
    insertText += tableedit::BuildGrid(cols, rows);
    if (insertPos + replaceLen < text.size() &&
        text[insertPos + replaceLen] != '\n') {
        insertText += "\n";
    }

    editor_.ReplaceTextRange(insertPos, replaceLen, insertText,
                              EditType::Insert);
    editor_.BreakUndoCoalesce();
    // The caret goes to the first header cell's content, not past the
    // whole table.
    sel_.Collapse({insertPos + prefixLen + 1});
    OnTextChanged();
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

namespace {

// The characters that make up a word.
//
// A word is a run of letters, digits and the marks that go with them.
// Everything else breaks a word in two.
bool IsWordChar(unsigned char c) {
    if (c >= 0x80) return true;  // Any letter outside ASCII.
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    if (c >= '0' && c <= '9') return true;
    return c == '_' || c == '-';
}

}  // namespace

void App::SelectWordAt(uint32_t offset) {
    const std::string& text = buffer_.Text();
    if (text.empty()) return;

    size_t start = offset < text.size() ? offset : text.size();

    // The click may have landed on the space after a word, and then the
    // word before it is the one that counts.
    if (start > 0 && start < text.size() &&
        !IsWordChar(static_cast<unsigned char>(text[start]))) {
        size_t before = start;
        while (before > 0 &&
               !IsWordChar(static_cast<unsigned char>(text[before - 1]))) {
            --before;
        }
        if (before < start) start = before;
    }

    while (start > 0 && IsWordChar(static_cast<unsigned char>(text[start - 1])))
        --start;
    size_t end = start;
    while (end < text.size() && IsWordChar(static_cast<unsigned char>(text[end])))
        ++end;

    SetSelection(static_cast<uint32_t>(start), static_cast<uint32_t>(end));
}

void App::SelectParagraphAt(uint32_t offset) {
    const std::string& text = buffer_.Text();
    if (text.empty()) return;

    size_t start = offset < text.size() ? offset : text.size();
    size_t end = start;

    while (start > 0 && text[start - 1] != '\n') --start;
    while (end < text.size() && text[end] != '\n') ++end;

    SetSelection(static_cast<uint32_t>(start), static_cast<uint32_t>(end));
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

    // A typed character that would otherwise be markdown syntax is
    // escaped, so the text stays plain text. This is what the Windows
    // application does as well.
    const std::string escaped =
        EscapeForInsert(buffer_, sel_.active.offset, text,
                        IsOffsetInTable(document_, sel_.active.offset));
    editor_.InsertText(escaped);

    // A character that finishes a markdown pattern turns the text around
    // it, the way a space after a dash makes a list.
    if (!escaped.empty()) {
        CheckAutoformat(&buffer_, &sel_, escaped[0]);
    }
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
