// The macOS application object.
//
// The document, the text buffer, the edit controller and the undo stack
// are the shared core, used exactly as the Windows build uses them. Only
// the window and the drawing are new.

#include "app_mac.h"
#include "formatting.h"

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

namespace {

// The find scope: a range takes part in a search only when it is
// drawn, so the hidden markers are never searched. It is the same
// rule the Windows window applies.
bool FindScopeIsRendered(uint32_t start, uint32_t length, void* context) {
    const auto* renderer = static_cast<const MacRenderer*>(context);
    return renderer != nullptr && renderer->RangeIsRendered(start, length);
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
            // An incremental search re-aims the current match at the
            // caret on every keystroke.
            RefreshFindResults(true);
        },
        .onSelectionChanged = [this](uint32_t offset, uint32_t length) {
            // The selection follows the match, and the highlight index
            // is re-published, so the current tint never lags a
            // navigation (guide 6).
            ApplyFindSelection(offset, length);
            if (contentView_) {
                FindReplaceState& state = findBar_->model().state();
                contentView_->SetSearchMatches(&state.Matches(),
                                               state.CurrentIndex());
            }
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
    // The highlights of the document come with it.
    LoadMarkers();
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
    // A new document has no markers: the layer goes with the old
    // document's identity (highlighting guide 37).
    LoadMarkers();
    ClearPendingFormat();
}

bool App::ReloadFile() {
    if (filePath_.empty()) {
        return false;
    }
    return OpenFile(filePath_);
}

void App::ShowFind(FindBarMode mode) {
    // Without a document there is nothing to search; the Windows
    // window returns the same way.
    if (WelcomeMode()) return;

    // A second search command while the bar is up only returns focus
    // to the field and keeps the query; the Windows bar behaves the
    // same (find spec 2.1).
    if (findBar_->IsVisible()) {
        findBar_->Show(mode);
        findBar_->FocusSearchField();
        return;
    }

    // The field starts with the selected text when it holds nothing
    // yet, so the last search survives a reopen (find spec 54).
    if (findBar_->model().GetSearchText().empty()) {
        const std::wstring query = textenc::Utf8ToWide(SelectedText());
        findBar_->model().SetSearchText(query);
    }
    findBar_->model().SetReplaceEnabled(true);
    findBar_->Show(mode);
    // The strip has its height now, so the window lays the document
    // out under it before the first match is scrolled into view.
    if (findVisibilityChanged_) findVisibilityChanged_(true);
    RefreshFindResults(true);
}

// Closing the bar says nothing about the document, the selection or
// the clipboard; only the marks go away (guide 2.4).
void App::HideFind() {
    findBar_->Hide();
    if (contentView_) {
        contentView_->SetSearchMatches(nullptr, -1);
        contentView_->Invalidate();
    }
    if (findVisibilityChanged_) findVisibilityChanged_(false);
}

// The current match becomes the document selection and is scrolled
// into view, so the reader sees what Replace will hit (guide 7).
void App::ApplyFindSelection(uint32_t offset, uint32_t length) {
    if (length == 0) return;

    SetSelection(offset, offset + length);
    if (contentView_) {
        contentView_->ScrollToOffset(offset);
    }
}

void App::RefreshFindResults(bool adoptCurrentMatch) {
    FindReplaceState& state = findBar_->model().state();
    // The search covers the drawn text, never the hidden markers; the
    // same scope the Windows window passes.
    state.Refresh(buffer_.Text(), &FindScopeIsRendered,
                  contentView_
                      ? const_cast<MacRenderer*>(&contentView_->renderer())
                      : nullptr);
    if (adoptCurrentMatch && state.HasQuery()) {
        state.AdoptMatchAt(sel_.active.offset);
    }
    findBar_->model().SyncFromState();
    findBar_->SyncControls();

    if (contentView_) {
        contentView_->SetSearchMatches(&state.Matches(), state.CurrentIndex());
    }

    // The current match becomes the selection and scrolls into view,
    // so the reader sees where the search landed.
    TextMatch current{};
    if (state.CurrentMatch(&current)) {
        ApplyFindSelection(current.start, current.length);
    }
}

void App::ReplaceMatch(uint32_t offset, uint32_t length) {
    if (length == 0) return;

    // The field holding nothing means the match goes away, which is
    // what replacing it with an empty string is.
    const std::wstring replacement = findBar_->model().GetReplaceText();
    std::string text = textenc::WideToUtf8(replacement);

    // A replacement inside a table cell goes through the same
    // sanitizing a paste does, or a pipe or a row break could turn
    // the cell into table syntax (spec rule 10 and AC-10).
    if (IsOffsetInTable(document_, offset)) {
        text = SanitizePasteForTableCell(text);
    }

    SetSelection(offset, offset + length);
    editor_.ReplaceTextRange(offset, length, text, EditType::Other);
    // The replacement is one undo step of its own; the next keystroke
    // must not merge into it.
    editor_.BreakUndoCoalesce();
    OnTextChanged();
    RefreshFindResults(false);
    // The next occurrence of the query becomes the current match; when
    // none is left, the inserted text stays selected (spec AC-12).
    if (!findBar_->model().SelectMatchAt(sel_.active.offset)) {
        SetSelection(offset, offset + static_cast<uint32_t>(text.size()));
    }
}

void App::ReplaceAllMatches(const std::wstring& query,
                            const std::wstring& replacement) {
    if (query.empty()) return;

    const std::string text = textenc::WideToUtf8(replacement);
    FindReplaceState& state = findBar_->model().state();
    std::string replaced;
    const size_t count = state.BuildReplacedDocument(buffer_.Text(), &replaced);
    if (count == 0) {
        findBar_->model().SetStatusText(L"No matches");
        findBar_->SyncControls();
        return;
    }

    // The whole sweep is one undo entry, so a single undo brings the
    // document back (guide 17).
    editor_.ReplaceTextRange(0, static_cast<uint32_t>(buffer_.Length()),
                              replaced, EditType::Other);
    editor_.BreakUndoCoalesce();
    sel_.Collapse(CaretPos{0});
    OnTextChanged();
    RefreshFindResults(false);

    // The count is what tells the user the sweep happened (guide 15).
    std::wstring message = std::to_wstring(count);
    message += L" replacements made.";
    findBar_->model().SetStatusText(message);
    findBar_->SyncControls();
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
    // A document that is replaced wholesale starts without a pending
    // typing format.
    ClearPendingFormat();
}

void App::AdoptImportedText(const std::string& text) {
    SetText(text);
    // The imported markdown is not the file that was open before, so
    // the document loses its path and its name. Without this a Save
    // would write the imported text over the previous document.
    filePath_.clear();
    isSvg_ = false;
    svgWrapped_ = false;
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

// Removes the whole table the caret is in, in one edit, so the table
// leaves in a single undo step. The caret lands on the block that
// followed the table.
void App::RemoveTable() {
    uint32_t start = 0;
    uint32_t end = 0;
    if (!TableSpan(&start, &end)) return;

    const std::string& text = buffer_.Text();
    uint32_t following = start;
    uint32_t look = end;
    while (look < text.size() && text[look] == '\n') ++look;
    if (look < text.size()) {
        for (const Node& node : document_.nodes) {
            if (node.block != BlockKind::Table && node.srcOffset == look) {
                following = look;
                break;
            }
        }
    }
    const uint32_t removed = end - start;
    const uint32_t caret = following > removed ? following - removed : 0;
    RewriteTable(start, end, "", caret);
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

void App::HandleTab(bool backwards) {
    // Markdown tables keep each cell on one source line, so inside a
    // table Tab moves between cells through the shared table model
    // instead of inserting indentation. The selection's anchor side
    // decides, the same rule the Windows build uses.
    if (IsOffsetInTable(document_, sel_.active.offset) ||
        (!sel_.Empty() && IsOffsetInTable(document_, sel_.Start()))) {
        uint32_t tableOffset = 0;
        if (MoveTableCell(document_, sel_.active.offset, backwards,
                          &tableOffset)) {
            SetCaret(tableOffset);
            return;
        }
        if (!backwards) {
            // Tab from the last cell appends a blank row and puts the
            // caret in its first cell.
            AppendRowToTable();
        }
        // Shift+Tab from the first cell stays where it is.
        return;
    }

    // Inside a code block Tab writes the indentation the block needs
    // and Shift+Tab strips one level of it; elsewhere the line is
    // indented or outdented, the split the Windows build uses (spec
    // rules 6 and 7 and rules 934-948).
    const std::string& source = buffer_.Text();
    bool inCodeBlock = false;
    for (const Node& node : document_.nodes) {
        if ((node.block == BlockKind::CodeBlock ||
             node.block == BlockKind::MermaidFlowchart ||
             node.block == BlockKind::MermaidPie ||
             node.block == BlockKind::MermaidSequence) &&
            sel_.active.offset >= node.srcOffset &&
            sel_.active.offset <= node.srcOffset + node.srcLength) {
            inCodeBlock = true;
            break;
        }
    }
    if (inCodeBlock) {
        if (!backwards) {
            InsertText("    ");
            return;
        }
        const size_t newline = sel_.active.offset == 0
            ? std::string::npos
            : source.rfind('\n', sel_.active.offset - 1);
        const uint32_t lineStart = newline == std::string::npos
            ? 0
            : static_cast<uint32_t>(newline + 1);
        uint32_t spaces = 0;
        while (lineStart + spaces < source.size() && spaces < 4 &&
               source[lineStart + spaces] == ' ') {
            ++spaces;
        }
        if (spaces == 4) {
            editor_.ReplaceTextRange(lineStart, 4, "", EditType::Other);
        }
        editor_.BreakUndoCoalesce();
        OnTextChanged();
        return;
    }

    // Outside a code block the line follows its list level.
    if (backwards) {
        OutdentLine(&buffer_, &sel_, &undo_);
    } else {
        IndentLine(&buffer_, &sel_, &undo_);
    }
    editor_.BreakUndoCoalesce();
    OnTextChanged();
}

// Appends a blank row with the table's column count and puts the caret
// in its first cell, so a table grows by keyboard alone. One undo step
// covers the row and the caret.
void App::AppendRowToTable() {
    const std::string& text = buffer_.Text();
    for (size_t ti = 0; ti < document_.nodes.size(); ++ti) {
        const Node& node = document_.nodes[ti];
        if (node.block != BlockKind::Table) continue;
        const uint32_t start = node.srcOffset;
        const uint32_t end = start + node.srcLength;
        if (sel_.active.offset < start || sel_.active.offset > end) continue;

        const int cols = static_cast<int>(TableColumnCount(document_, ti));
        if (cols <= 0) return;

        std::string row = TableBlankRow(cols);
        const uint32_t insertPos = end;
        // A table that runs into the next block needs its own line.
        const bool neededBreak = insertPos > 0 && text[insertPos - 1] != '\n';
        if (neededBreak) row.insert(0, "\n");

        editor_.ReplaceTextRange(insertPos, 0, row, EditType::Insert);
        editor_.BreakUndoCoalesce();
        sel_.Collapse({insertPos + (neededBreak ? 2u : 1u)});
        OnTextChanged();
        return;
    }
}

void App::PasteFromClipboard() {
    std::string text = ClipboardText();
    if (text.empty()) return;

    // Only the clipboard's plain text is pasted, and its line endings
    // are normalized to the document's own, the way the Windows build
    // normalizes CRLF and CR.
    std::string normalized;
    normalized.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            normalized.push_back('\n');
        } else {
            normalized.push_back(text[i]);
        }
    }
    text = normalized;

    const bool inTable = IsOffsetInTable(document_, sel_.active.offset) ||
        (!sel_.Empty() && IsOffsetInTable(document_, sel_.Start()));

    // A paste into a cell keeps the row intact: the shared rule strips
    // pipes and row breaks, the cell's structural validation.
    if (inTable) text = SanitizePasteForTableCell(text);

    // Tab separated rows (a spreadsheet block) become a Markdown table
    // in one paste.
    if (!inTable) {
        std::string table;
        uint32_t cellCaret = 0;
        if (TabSeparatedToMarkdownTable(text, &table, &cellCaret)) {
            InsertPastedTable(table, cellCaret);
            return;
        }
    }

    if (text.empty()) return;

    // The paste is its own undo step: it must not merge into the typing
    // run before it, and typing afterwards must not merge back.
    editor_.BreakUndoCoalesce();
    editor_.InsertText(text);
    editor_.BreakUndoCoalesce();
    OnTextChanged();
}

// A pasted block of tab separated rows becomes a table at the caret,
// with the plumbing the Windows build uses: a blank separator line in
// front where the block would otherwise merge into the block before
// it, and one undo step for the separators, the table and the caret.
void App::InsertPastedTable(const std::string& table,
                            uint32_t firstCellCaret) {
    // A table never nests, so a pasted table does nothing inside one.
    if (tableedit::ContainingTable(document_, sel_.active.offset)) return;

    uint32_t insertPos = sel_.active.offset;
    uint32_t replaceLen = 0;
    if (!sel_.Empty()) {
        insertPos = sel_.Start();
        replaceLen = sel_.Length();
    }

    const std::string& text = buffer_.Text();
    std::string insertText;
    insertText.append(TableSeparatorNewlinesBefore(text, insertPos), '\n');
    const uint32_t prefixLen = static_cast<uint32_t>(insertText.size());
    insertText += table;
    if (insertPos + replaceLen < text.size() &&
        text[insertPos + replaceLen] != '\n') {
        insertText += "\n";
    }

    editor_.ReplaceTextRange(insertPos, replaceLen, insertText,
                              EditType::Insert);
    editor_.BreakUndoCoalesce();
    sel_.Collapse({insertPos + prefixLen + firstCellCaret});
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
    if (documentChanged_) documentChanged_();
}

// The formatting goes away from the selection. The block markers are
// taken out first, because they move the text around, and the inline
// markers go last, by the shared module, so the two builds clear the
// same way.
void App::ClearFormat() {
    if (sel_.Empty()) return;

    uint32_t start = sel_.Start();
    uint32_t end = sel_.End();

    // Only the first line of the selection is cleared. A selection
    // over many lines keeps its block formatting, because taking it
    // away would change the shape of the document.
    const uint32_t lineStart = buffer_.LineStart(start);
    const std::string prefix = buffer_.Text().substr(lineStart, start - lineStart);

    // The heading markers.
    uint32_t removeCount = 0;
    uint32_t removeTo = lineStart;
    {
        size_t hashes = 0;
        while (hashes < prefix.size() && prefix[hashes] == '#') ++hashes;
        if (hashes > 0 && hashes < prefix.size() && prefix[hashes] == ' ') {
            removeCount = static_cast<uint32_t>(hashes) + 1;
            removeTo = lineStart + removeCount;
        }
    }

    // The quote and the list markers. They come after the heading
    // markers, because a quote of a heading is written that way.
    if (removeCount == 0) {
        size_t spaces = 0;
        while (spaces < prefix.size() && prefix[spaces] == ' ') ++spaces;
        if (spaces < prefix.size()) {
            const char ch = prefix[spaces];
            const bool isQuote = ch == '>';
            const bool isList = ch == '-' || ch == '*' || ch == '+';
            if (isQuote || isList) {
                removeCount = static_cast<uint32_t>(spaces) + 1;
                removeTo = lineStart + removeCount;
                if (removeTo < buffer_.Text().size() &&
                    buffer_.Text()[removeTo] == ' ') {
                    ++removeTo;
                    ++removeCount;
                }
            }
        }
    }

    if (removeCount > 0) {
        buffer_.Splice(lineStart, removeCount, "");
        const uint32_t shift = removeCount;
        start = (start > shift) ? start - shift : 0;
        end = (end > shift) ? end - shift : 0;
        sel_.anchor = {start};
        sel_.active = {end};
    }

    // The inline markers go last, because the block change moved the
    // text around.
    ClearInlineFormatting(document_, &buffer_, &sel_, &undo_);
}

// The two marker commands carry the pending typing format.
void App::ToggleBold() { ToggleInlineWithPending("**", true); }

void App::ToggleItalic() { ToggleInlineWithPending("*", false); }

// With a selection the shared marker toggle applies the guide's rules.
// An empty caret toggles what the NEXT characters get and changes
// nothing in the document (text guide, fed og kursiv ved et tomt
// caret rules 1 and 2).
void App::ToggleInlineWithPending(const std::string& marker, bool bold) {
    if (!sel_.Empty()) {
        ClearPendingFormat();
        ToggleInlineMarker(&buffer_, &sel_, marker, &undo_);
        editor_.BreakUndoCoalesce();
        OnTextChanged();
        return;
    }

    const bool beforeBold = pendingBold_;
    const bool beforeItalic = pendingItalic_;
    const bool beforeBoldSet = pendingBoldSet_;
    const bool beforeItalicSet = pendingItalicSet_;

    bool& value = bold ? pendingBold_ : pendingItalic_;
    bool& set = bold ? pendingBoldSet_ : pendingItalicSet_;
    // The pending state starts from the format the caret sits in when
    // nothing is pending yet, the way the Windows build reads it.
    const bool current = set ? value : CaretFormatIsActive(marker);
    value = !current;
    set = true;
    pendingRunActive_ = false;
    pendingRunSuffixBytes_ = 0;
    RecordPendingFormatUndo(beforeBold, beforeItalic, beforeBoldSet,
                            beforeItalicSet);
    editor_.BreakUndoCoalesce();
    OnSelectionChanged();
}

// The pending format reaches the document only when text does. The
// first typed character opens the markers around itself; the next ones
// replace the run's closing markers with themselves plus the markers,
// so the run extends instead of nesting (text guide, fed og kursiv ved
// et tomt caret rules 1 and 2).
void App::ApplyPendingTypingFormat(const std::string& value) {
    std::string prefix;
    std::string suffix;
    if (pendingBoldSet_ && pendingBold_) {
        prefix += "**";
        suffix = "**" + suffix;
    }
    if (pendingItalicSet_ && pendingItalic_) {
        prefix += "*";
        suffix = "*" + suffix;
    }

    if (suffix.empty()) {
        editor_.InsertText(value);
        pendingRunActive_ = false;
        pendingRunSuffixBytes_ = 0;
        return;
    }

    const std::string& source = buffer_.Text();
    const uint32_t caret = sel_.active.offset;
    const uint32_t suffixBytes = static_cast<uint32_t>(suffix.size());
    const bool extend = pendingRunActive_ && sel_.Empty() &&
        pendingRunSuffixBytes_ == suffixBytes && caret >= suffixBytes &&
        source.compare(caret - suffixBytes, suffixBytes, suffix) == 0;

    if (extend) {
        editor_.ReplaceTextRange(caret - suffixBytes, suffixBytes,
                                 value + suffix, EditType::Insert);
    } else {
        editor_.InsertText(prefix + value + suffix);
    }
    pendingRunActive_ = true;
    pendingRunSuffixBytes_ = suffixBytes;
}

// One undo step for a pending toggle, so Cmd+Z brings the old pending
// state back before it touches any text (text guide, fed og kursiv ved
// et tomt caret rule 5).
void App::RecordPendingFormatUndo(bool beforeBold, bool beforeItalic,
                                  bool beforeBoldSet, bool beforeItalicSet) {
    UndoEntry entry{};
    entry.offset = sel_.active.offset;
    entry.selBefore = sel_;
    entry.selAfter = sel_;
    entry.timestamp = 0;
    entry.type = EditType::PendingFormat;
    entry.hasPendingFormat = true;
    entry.pendingBold = beforeBold;
    entry.pendingItalic = beforeItalic;
    entry.pendingBoldSet = beforeBoldSet;
    entry.pendingItalicSet = beforeItalicSet;
    entry.afterPendingBold = pendingBold_;
    entry.afterPendingItalic = pendingItalic_;
    entry.afterPendingBoldSet = pendingBoldSet_;
    entry.afterPendingItalicSet = pendingItalicSet_;
    undo_.Push(entry);
}

void App::RestorePendingFormat(const UndoEntry& entry, bool redo) {
    if (!entry.hasPendingFormat) return;
    if (redo) {
        pendingBold_ = entry.afterPendingBold;
        pendingItalic_ = entry.afterPendingItalic;
        pendingBoldSet_ = entry.afterPendingBoldSet;
        pendingItalicSet_ = entry.afterPendingItalicSet;
    } else {
        pendingBold_ = entry.pendingBold;
        pendingItalic_ = entry.pendingItalic;
        pendingBoldSet_ = entry.pendingBoldSet;
        pendingItalicSet_ = entry.pendingItalicSet;
    }
    pendingRunActive_ = false;
    pendingRunSuffixBytes_ = 0;
}

// Whether the caret sits inside a span of the format, which is where
// the pending toggle starts from (text guide, fed og kursiv ved et
// tomt caret rule 3).
bool App::CaretFormatIsActive(const std::string& marker) const {
    const bool wantBold = marker == "**";
    const uint32_t caret = sel_.active.offset;
    for (const auto& node : document_.nodes) {
        const uint32_t blockStart = node.srcOffset;
        const uint32_t blockEnd = blockStart + node.srcLength;
        if (caret < blockStart || caret > blockEnd) continue;
        for (const auto& child : node.children) {
            const uint32_t start = child.srcOffset;
            const uint32_t end = start + child.srcLength;
            if (start <= caret && caret <= end) {
                if (wantBold && child.strong) return true;
                if (!wantBold && child.em) return true;
            }
        }
    }
    return false;
}

// What the strip reads for the bold and italic buttons. The pending
// typing format shows pressed first; with a selection the wrapped
// check answers for the rest.
bool App::FormatButtonOn(const std::string& marker) const {
    const bool bold = marker == "**";
    if (bold && pendingBoldSet_) return pendingBold_;
    if (!bold && pendingItalicSet_) return pendingItalic_;
    if (sel_.Empty()) return CaretFormatIsActive(marker);
    return IsWrappedIn(buffer_.Text(), sel_.Start(),
                       sel_.Start() + sel_.Length(), marker);
}

// A caret move ends the pending format: the next characters start
// fresh wherever the caret lands (text guide, fed og kursiv ved et
// tomt caret rule 3). Every click and caret move arrives through
// SetCaret or SetSelection.
void App::ClearPendingFormat() {
    pendingBoldSet_ = false;
    pendingItalicSet_ = false;
    pendingRunActive_ = false;
    pendingRunSuffixBytes_ = 0;
}

// Whether the selection overlaps any marker. A partial overlap counts,
// and an unresolved marker counts too: the button shows the state the
// next click will change (highlighting guide 4 and 6).
bool App::SelectionHasMarker() const {
    if (sel_.Empty()) return false;

    const uint32_t start = sel_.Start();
    const uint32_t end = start + sel_.Length();
    for (const TextMarker& marker : markerStore_.Markers()) {
        if (marker.start < end && start < marker.end) {
            return true;
        }
    }
    return false;
}

// A selection that touches a marker takes it away, and any other
// selection makes one. One button, one shortcut, the way the Windows
// build has it.
void App::ToggleMarkSelection() {
    if (sel_.Empty()) return;

    const uint32_t start = sel_.Start();
    const uint32_t end = start + sel_.Length();

    if (SelectionHasMarker()) {
        if (markerStore_.RemoveIntersecting(start, end) == 0) return;
    } else {
        // The mark covers drawn text only: hidden Markdown syntax is
        // trimmed off both ends before anything is stored, so a stored
        // marker never starts or ends on bytes the renderer does not
        // draw (highlighting guide 10).
        uint32_t lo = 0;
        uint32_t hi = 0;
        if (!contentView_ ||
            !contentView_->renderer().ClipToRendered(
                start, static_cast<uint32_t>(sel_.Length()), &lo, &hi)) {
            return;  // the selection covers no drawn text
        }
        if (!markerStore_.Add(buffer_.Text(), lo, hi)) return;
    }

    SaveMarkers();
    RefreshMarkerRanges();
    if (contentView_) contentView_->Invalidate();
}

// Hands the marked ranges to the renderer, which draws them behind
// the text.
void App::RefreshMarkerRanges() {
    markerRanges_.clear();
    for (const TextMarker& marker : markerStore_.Markers()) {
        if (!marker.resolved || marker.end <= marker.start) continue;
        markerRanges_.push_back(
            {marker.start, static_cast<uint32_t>(marker.end - marker.start)});
    }
    if (contentView_) contentView_->SetMarkers(&markerRanges_);
}

// Writes the markers to the sidecar file next to the document.
void App::SaveMarkers() {
    if (filePath_.empty()) return;
    markerStore_.Save(filePath_, buffer_.Text());
}

// The document was saved under a new name. The marker sidecar keys off
// the path, so it follows the document, and the marks on screen are
// cloned onto the new identity (highlighting guide 12.7 and 37).
void App::AdoptSavedPath(const std::string& path) {
    filePath_ = path;
    SaveMarkers();
}

// Reads the markers of the document, and re-anchors them.
void App::LoadMarkers() {
    markerRanges_.clear();
    markerStore_.Clear();
    if (filePath_.empty()) {
        if (contentView_) contentView_->SetMarkers(&markerRanges_);
        return;
    }
    markerStore_.LoadFor(filePath_, buffer_.Text());
    markerStore_.Resolve(buffer_.Text());
    RefreshMarkerRanges();
}

void App::OnTextChanged() {
    dirty_ = true;
    Reparse();
    // The markers are anchored to the text, so they move with it.
    markerStore_.Resolve(buffer_.Text());
    RefreshMarkerRanges();

    // The find bar stands on document offsets, so an edit refreshes
    // the results rather than letting a stale offset survive (guide
    // 31).
    if (findBar_->IsVisible()) {
        RefreshFindResults(false);
    }
}

void App::OnSelectionChanged() {
    if (contentView_) contentView_->Invalidate();
    if (selectionChanged_) selectionChanged_();
}

void App::SetCaret(uint32_t offset) {
    const uint32_t length = buffer_.Length();
    if (offset > length) offset = length;
    sel_.Collapse(CaretPos{offset});
    ClearPendingFormat();
    editor_.BreakUndoCoalesce();
    OnSelectionChanged();
}

void App::SetSelection(uint32_t start, uint32_t end) {
    const uint32_t length = buffer_.Length();
    if (start > length) start = length;
    if (end > length) end = length;
    sel_.anchor = CaretPos{start};
    sel_.active = CaretPos{end};
    ClearPendingFormat();
    editor_.BreakUndoCoalesce();
    OnSelectionChanged();
}

bool App::WordSpanForOffset(uint32_t offset, uint32_t* outStart,
                            uint32_t* outEnd) const {
    if (buffer_.Text().empty()) return false;

    // The shared word span is the boundary the word-wise caret moves
    // and the word deletion use, so a double click and a word move can
    // never disagree about where a word ends.
    uint32_t start = 0;
    uint32_t end = 0;
    WordSpanAt(buffer_, offset, &start, &end);
    if (end <= start) return false;

    // The ends snap to drawn text, so a mark never lands on a hidden
    // marker.
    if (contentView_) {
        start = contentView_->NormalizeToRenderedCaret(start);
        end = contentView_->NormalizeToRenderedCaret(end);
    }
    if (end < start) {
        const uint32_t swap = start;
        start = end;
        end = swap;
    }
    if (end <= start) return false;
    if (outStart) *outStart = start;
    if (outEnd) *outEnd = end;
    return true;
}

void App::SelectWordAt(uint32_t offset) {
    uint32_t start = 0;
    uint32_t end = 0;
    if (!WordSpanForOffset(offset, &start, &end)) return;
    // On an empty line the word span is the whitespace run around the
    // break; selecting it would grab the neighbouring line's text, so
    // the caret simply lands on the line (spec rule 5).
    const std::string& text = buffer_.Text();
    bool blank = true;
    for (uint32_t at = start; at < end && at < text.size(); ++at) {
        const char c = text[at];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            blank = false;
            break;
        }
    }
    if (blank) {
        SetCaret(offset);
        return;
    }
    SetSelection(start, end);
}

bool App::ParagraphRangeForOffset(uint32_t offset, uint32_t* outStart,
                                  uint32_t* outEnd) const {
    const std::string& text = buffer_.Text();
    if (text.empty()) return false;

    // The paragraph is the block the offset sits in: its drawn text and
    // the break that ends it. A list item keeps its marker, the next
    // item's prefix and the closing newline out of the selection, the
    // same rule the Windows selection follows.
    const Node* node = nullptr;
    for (const Node& n : document_.nodes) {
        if (offset >= n.srcOffset && offset < n.srcOffset + n.srcLength) {
            node = &n;
        }
    }
    if (!node) {
        for (const Node& n : document_.nodes) {
            if (offset >= n.srcOffset && offset <= n.srcOffset + n.srcLength) {
                node = &n;
            }
        }
    }

    uint32_t start = 0;
    uint32_t end = 0;
    if (node && node->block == BlockKind::List) {
        start = node->contentOffset;
        end = node->srcOffset + node->srcLength;
    } else if (node) {
        start = node->contentOffset;
        end = node->contentOffset + node->contentLength;
        // The paragraph break belongs to the selection, so the next
        // typed character replaces the whole paragraph.
        while (end < text.size() &&
               (text[end] == '\n' || text[end] == '\r')) {
            ++end;
        }
    } else {
        size_t s = offset < text.size() ? offset : text.size();
        size_t e = s;
        while (s > 0 && text[s - 1] != '\n') --s;
        while (e < text.size() && text[e] != '\n') ++e;
        start = static_cast<uint32_t>(s);
        end = static_cast<uint32_t>(e);
    }
    if (end <= start) return false;
    if (outStart) *outStart = start;
    if (outEnd) *outEnd = end;
    return true;
}

void App::SelectParagraphAt(uint32_t offset) {
    uint32_t start = 0;
    uint32_t end = 0;
    if (!ParagraphRangeForOffset(offset, &start, &end)) return;
    SetSelection(start, end);
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

bool App::SetClipboardText(const std::string& text) {
    return TheClipboard().SetText(text);
}

std::string App::SelectionForClipboard() const {
    const std::string& source = buffer_.Text();
    const uint32_t start = std::min<uint32_t>(
        sel_.Start(), static_cast<uint32_t>(source.size()));
    const uint32_t end = std::min<uint32_t>(
        start + sel_.Length(), static_cast<uint32_t>(source.size()));
    if (start >= end) return std::string();

    // The source view shows the raw Markdown, so the clipboard gets it
    // byte for byte, the same rule the Windows selection follows.
    if (sourceView_) return source.substr(start, end - start);

    // Everywhere else the clipboard carries the drawn text: markdown
    // delimiters and fence markers are not on screen and must not
    // leak into it (text spec C2).
    std::string visible;
    for (uint32_t at = start; at < end;) {
        const uint32_t next = NextGraphemeBoundary(source, at);
        if (next <= at) break;
        const bool rendered =
            contentView_ && contentView_->renderer().OffsetIsRendered(at);
        const bool lineBreak = source[at] == '\n' || source[at] == '\r';
        if (rendered || lineBreak) visible.append(source, at, next - at);
        at = next;
    }
    if (visible.empty() &&
        (!contentView_ || contentView_->renderer().blocks().empty())) {
        // A document whose layout is not ready yet still copies its
        // bytes rather than copying nothing.
        visible = source.substr(start, end - start);
    }
    return visible;
}

void App::CopyToClipboard() {
    if (!HasSelection()) return;
    SetClipboardText(SelectionForClipboard());
}

void App::CutToClipboard() {
    if (!HasSelection()) return;
    // The text only leaves the document once the clipboard holds it;
    // a failed write must not lose anything (text spec C3).
    if (SetClipboardText(SelectionForClipboard())) DeleteBackward();
}

void App::SelectAll() {
    SetSelection(0, static_cast<uint32_t>(buffer_.Length()));
}

void App::InsertText(const std::string& text) {
    if (text.empty()) return;

    // A typed character that would otherwise be markdown syntax is
    // escaped, so the text stays plain text. This is what the Windows
    // application does as well.
    const std::string escaped =
        EscapeForInsert(buffer_, sel_.active.offset, text,
                        IsOffsetInTable(document_, sel_.active.offset));
    ApplyPendingTypingFormat(escaped);

    // A character that finishes a markdown pattern turns the text around
    // it, the way a space after a dash makes a list.
    if (!escaped.empty()) {
        CheckAutoformat(&buffer_, &sel_, escaped[0]);
    }
    OnTextChanged();
}

void App::DeleteBackward() {
    // A delete resets the typing run but keeps the pending format: the
    // next characters still get it, without an empty format object
    // left behind (text guide, fed og kursiv ved et tomt caret rule
    // 4).
    pendingRunActive_ = false;
    pendingRunSuffixBytes_ = 0;
    if (!sel_.Empty()) {
        editor_.DeleteSelection();
        OnTextChanged();
        return;
    }

    // Inside a cell the boundary rule is the cell's own: the pipe that
    // opens the cell is never consumed. A refused edit leaves the
    // document clean.
    const bool changed = IsOffsetInTable(document_, sel_.active.offset)
        ? editor_.DeleteBackwardInCell(document_)
        : editor_.DeleteBackward(&document_);
    if (changed) OnTextChanged();
}

void App::DeleteForward() {
    // A delete resets the typing run but keeps the pending format: the
    // next characters still get it, without an empty format object
    // left behind (text guide, fed og kursiv ved et tomt caret rule
    // 4).
    pendingRunActive_ = false;
    pendingRunSuffixBytes_ = 0;
    if (!sel_.Empty()) {
        editor_.DeleteSelection();
        OnTextChanged();
        return;
    }

    // Inside a cell the closing pipe is never consumed.
    const bool changed = IsOffsetInTable(document_, sel_.active.offset)
        ? editor_.DeleteForwardInCell(document_)
        : editor_.DeleteForward(&document_);
    if (changed) OnTextChanged();
}

void App::DeleteWordBackward() {
    // A delete resets the typing run but keeps the pending format: the
    // next characters still get it, without an empty format object
    // left behind (text guide, fed og kursiv ved et tomt caret rule
    // 4).
    pendingRunActive_ = false;
    pendingRunSuffixBytes_ = 0;
    if (!sel_.Empty()) {
        editor_.DeleteSelection();
        OnTextChanged();
        return;
    }

    // A word deletion that ate nothing is not a change.
    const size_t lengthBefore = buffer_.Length();
    editor_.DeleteWordBackward();
    if (buffer_.Length() != lengthBefore) OnTextChanged();
}

void App::DeleteWordForward() {
    // A delete resets the typing run but keeps the pending format: the
    // next characters still get it, without an empty format object
    // left behind (text guide, fed og kursiv ved et tomt caret rule
    // 4).
    pendingRunActive_ = false;
    pendingRunSuffixBytes_ = 0;
    if (!sel_.Empty()) {
        editor_.DeleteSelection();
        OnTextChanged();
        return;
    }

    const size_t lengthBefore = buffer_.Length();
    editor_.DeleteWordForward();
    if (buffer_.Length() != lengthBefore) OnTextChanged();
}

void App::InsertSoftBreak() {
    // A table row does not take a soft break; a refused edit must not
    // mark the document dirty.
    if (editor_.InsertSoftBreak(document_)) OnTextChanged();
}

void App::InsertParagraphBreak() {
    // The shared controller refuses the break the table model has no
    // place for; a refused edit is not a change.
    if (editor_.InsertParagraphBreak(document_)) OnTextChanged();
}

void App::Undo() {
    UndoEntry entry{};
    if (editor_.Undo(&entry)) {
        RestorePendingFormat(entry, false);
        OnTextChanged();
        OnSelectionChanged();
    }
}

void App::Redo() {
    UndoEntry entry{};
    if (editor_.Redo(&entry)) {
        RestorePendingFormat(entry, true);
        OnTextChanged();
        OnSelectionChanged();
    }
}

void App::SetContentView(ContentView* view) {
    contentView_.reset(view);
}

}  // namespace mac
