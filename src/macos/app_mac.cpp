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
#include "clipboard.h"
#include "clipboard_mac.h"
#include "content_view_mac.h"
#include "findbar_mac.h"
#include "inputfilter.h"
#include "navigation.h"
#include "parser.h"
#include "platform/seams.h"
#include "platformservices.h"
#include "tableedit.h"
#include "textdrag.h"
#include "textenc.h"

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

// The buffer, the selection, the editor and the undo stack belong to
// the shared document controller now; the aliases bind to it here.
// The controller wires the editor to the buffer, the selection and
// the undo stack in its own constructor.
App::App() : buffer_(controller_.buffer()),
             document_(controller_.document()),
             sel_(controller_.selection()),
             editor_(controller_.editor()),
             undo_(controller_.undo()),
             pendingBold_(controller_.pending().bold),
             pendingItalic_(controller_.pending().italic),
             pendingBoldSet_(controller_.pending().boldSet),
             pendingItalicSet_(controller_.pending().italicSet),
             pendingRunActive_(controller_.pending().runActive),
             pendingRunSuffixBytes_(controller_.pending().runSuffixBytes) {
    // The document changed handler of the controller runs the mac
    // reparse path: a controller command that changed the text lands
    // here, so OnTextChanged no longer needs a second, manual run
    // after every command.
    controller_.SetDocumentChangedHandler([this] {
        OnTextChanged();
    });
    // A pending format toggle with an empty caret changes no text: the
    // mac shell only refreshes through its selection path.
    controller_.SetSelectionChangedHandler([this] {
        OnSelectionChanged();
    });
    findBar_ = std::make_unique<FindBar>();

    // The bar asks the app where the caret is, and tells it what to do
    // when the search changes or a match is picked.
    findBar_->model().SetListener({
        .caretOffset = [this] {
            return sel_.active.offset;
        },
        .documentSize = [this] {
            // The live document size, so the bar can debounce its
            // searches on a large document (find spec, large
            // documents).
            return buffer_.Text().size();
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
}  // namespace

bool App::OpenFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream contents;
    contents << file.rdbuf();
    std::string text = contents.str();

    // A standalone SVG file is a picture, so the content is wrapped in
    // a fence, and the normal pipeline reads it as one.
    // The extension is compared in lower case, so a file saved as
    // DIAGRAM.SVG is a picture too and not a page of one long line.
    std::string tail = path.size() >= 4 ? path.substr(path.size() - 4)
                                        : std::string();
    for (char& c : tail) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    isSvg_ = tail == ".svg";
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
    // Reading first: the document waits in read mode until the Edit
    // toggle, the same default the Windows window opens files with.
    SetEditing(false);
    return true;
}

std::string App::TextForSaving() const {
    if (svgWrapped_) return UnwrapLangFence("svg", buffer_.Text());
    return buffer_.Text();
}

void App::SetEditing(bool on) {
    editing_ = on;
    // The document controller gates its own command bodies: keep its
    // edit flag in step, so a family command in read mode is a no-op
    // there too.
    controller_.SetEditing(on);
    // The layout follows the mode: reading draws the blank line next
    // to a list as a thin strip, editing keeps its full line, so the
    // document view re-measures.
    if (contentView_) contentView_->SetEditing(on);
    // Find works in every mode, but Replace only where the document
    // can change. Re-gate whenever the mode flips, so a bar left open
    // across the toggle greys the buttons (find spec 4 and 60), the
    // way the Windows window re-gates on SetEdit.
    if (findBar_) {
        findBar_->model().SetReplaceEnabled(on);
        if (findBar_->IsVisible()) findBar_->SyncControls();
    }
    // The command strip and the menu read the format and mode state
    // through the selection handler.
    if (selectionChanged_) selectionChanged_();
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
    // A new document is for writing: the mode comes back on, the way
    // the Windows window enters edit mode for a new document.
    SetEditing(true);
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
    findBar_->model().SetReplaceEnabled(Editing());
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
    if (!editing_) return;
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
    if (!editing_) return;
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
    if (!editing_) return;
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
    if (!editing_) return;
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
    if (!editing_) return;
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
    if (!editing_) return;
    // The splice itself is shared with the portable suite, so the
    // combined Tab command is covered by a test (spec X2).
    std::string row;
    uint32_t insertPos = 0;
    uint32_t caretAfter = 0;
    if (!TableAppendRow(document_, buffer_.Text(), sel_.active.offset, &row,
                        &insertPos, &caretAfter)) {
        return;
    }
    editor_.ReplaceTextRange(insertPos, 0, row, EditType::Insert);
    editor_.BreakUndoCoalesce();
    sel_.Collapse({caretAfter});
    OnTextChanged();
}

// Control characters never reach the buffer: typing drops everything
// below 0x20 except tab (the Windows rule), a paste also keeps the line
// breaks its own normalization just made.
static std::string DropControlChars(const std::string& text,
                                    bool keepNewlines) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 && c != '\t' && !(keepNewlines && c == '\n')) {
            continue;
        }
        out += c;
    }
    return out;
}

void App::PasteFromClipboard() {
    if (!editing_) return;
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

    // Markdown syntax in a pasted block stays literal text: the paste
    // is escaped the way the Windows build escapes it, so a paste can
    // never construct hidden syntax. The raw text is kept for the
    // spreadsheet detector, which needs the tabs and quotes intact.
    const std::string raw = text;
    text = EscapeMarkdown(raw);

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
        if (TabSeparatedToMarkdownTable(raw, &table, &cellCaret)) {
            InsertPastedTable(table, cellCaret);
            return;
        }
    }

    // No control characters survive into the buffer; tab is content
    // and the normalized line breaks were just made on purpose.
    text = DropControlChars(text, true);
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
    if (!editing_) return;
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

// The formatting goes away from the selection: the whole body lives
// in the document controller now (family 3), the shared strip-all
// engine for the inline markers and the block resets in one undo
// trail. The mac body this replaces cleared the block prefix with a
// blind splice that recorded no undo entry and moved the inline
// clears through stale offsets; one engine on both ports instead.
// The controller's completion notification runs OnTextChanged, so
// the shell below only forwards.
void App::ClearFormat() {
    controller_.ClearFormat();
}

// The two marker commands carry the pending typing format: the
// bodies live in the document controller (family 3), the pending
// state included. What the controller changed runs back through
// the handlers the App constructor wires: the selection case ends
// in OnTextChanged, the empty caret case in OnSelectionChanged.
void App::ToggleBold() {
    controller_.ToggleBold();
}

void App::ToggleItalic() {
    controller_.ToggleItalic();
}

// The pending format reaches the document only when text does. The
// body lives in the document controller now (family 3): the first
// typed character opens the markers around itself; the next ones
// replace the run's closing markers with themselves plus the
// markers, so the run extends instead of nesting (text guide, fed
// og kursiv ved et tomt caret rules 1 and 2).
void App::ApplyPendingTypingFormat(const std::string& value) {
    controller_.ApplyPendingTypingFormat(value);
}

void App::RestorePendingFormat(const UndoEntry& entry, bool redo) {
    controller_.RestorePendingFormat(entry, redo);
}

// What the strip and the menu validation read for the format
// buttons: the controller's format state query, one answer for both
// ports (family 3). The pending typing format shows pressed first,
// because the query already folds it into the empty caret answer.
bool App::FormatButtonOn(const std::string& marker) const {
    const FormatState fs = controller_.GetFormatState();
    if (marker == "**") return fs.bold;
    if (marker == "*")  return fs.italic;
    if (marker == "~~") return fs.strike;
    if (marker == "`")  return fs.code;
    return false;
}

// A caret move ends the pending format: the next characters start
// fresh wherever the caret lands (text guide, fed og kursiv ved et
// tomt caret rule 3). Every click and caret move arrives through
// SetCaret or SetSelection, which call this shell.
void App::ClearPendingFormat() {
    controller_.ClearPendingFormat();
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
        // The blanks come off before the clip. The clip widens a range to
        // the drawn spans it touches, so a leading or trailing space would
        // pull it out to a hidden delimiter: that is how a code span's
        // backticks ended up inside a mark, with the wash spilling into the
        // chip's padding.
        uint32_t from = start;
        uint32_t to = end;
        while (from < to && IsBlankByte(buffer_.Text()[from])) ++from;
        while (to > from && IsBlankByte(buffer_.Text()[to - 1])) --to;
        if (from >= to) return;

        uint32_t lo = 0;
        uint32_t hi = 0;
        if (!contentView_ ||
            !contentView_->renderer().ClipToRendered(
                from, to - from, &lo, &hi)) {
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
    // Inside a table cell a double click takes the whole cell (plan
    // 19 tier 1); word granularity is for prose only, the same
    // split the Windows build makes.
    if (!sourceView_) {
        uint32_t cellStart = 0;
        uint32_t cellEnd = 0;
        if (CellTextRangeAtOffset(document_, buffer_.Text(), offset,
                                  &cellStart, &cellEnd)) {
            SetSelection(cellStart, cellEnd);
            // The next Cmd+A continues at the row, so a double
            // click followed by Cmd+A walks the same steps a bare
            // Cmd+A run does (the Windows pre-seed).
            selectAllTier_ = 1;
            selectAllCaret_ = cellEnd;
            return;
        }
    }
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
        // The item's own text, past the bullet and any blockquote
        // marker, through the shared rule so a triple click never
        // selects the bullet (highlighting spec 5.1).
        start = ListItemContentStart(*node, text);
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
    // Inside a table cell the paragraph equivalent is the row (plan
    // 19): the block is the whole table, which only the Cmd+A
    // tiers expand to.
    if (!sourceView_) {
        uint32_t rowStart = 0;
        uint32_t rowEnd = 0;
        if (RowTextRangeAtOffset(document_, buffer_.Text(), offset,
                                 &rowStart, &rowEnd)) {
            SetSelection(rowStart, rowEnd);
            return;
        }
    }
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
    if (!editing_) return;
    if (!HasSelection()) return;
    // The text only leaves the document once the clipboard holds it;
    // a failed write must not lose anything (text spec C3).
    if (SetClipboardText(SelectionForClipboard())) DeleteBackward();
}

void App::SelectAll() {
    // Progressive selection tiers when the caret sits in a table
    // cell (plan 19, spec S8): cell, then row, then whole table,
    // then the whole document. The run escalates only while the
    // caret has not moved between presses; any other action that
    // moves the caret resets the run.
    if (sel_.active.offset != selectAllCaret_) selectAllTier_ = 0;

    // The source view shows the Markdown text, not rendered cells,
    // so there is nothing to escalate there: one press takes the
    // whole buffer.
    if (!sourceView_) {
        const TableSelectionTier step = NextTableSelectionTier(
            document_, buffer_.Text(), sel_.active.offset,
            selectAllTier_);
        selectAllTier_ = step.tier;
        SetSelection(step.start, step.end);
        selectAllCaret_ = step.end;
        return;
    }
    selectAllTier_ = 0;
    SetSelection(0, static_cast<uint32_t>(buffer_.Length()));
    selectAllCaret_ = sel_.active.offset;
}

// Counts the UTF-16 code units of a UTF-8 string, the unit an input
// method counts in.
static uint32_t Utf16Length(const std::string& text) {
    uint32_t units = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t codePoint = 0;
        size_t step = 1;
        if (c < 0x80) {
            codePoint = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            codePoint = ((c & 0x1F) << 6) |
                        (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            step = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            codePoint = ((c & 0x0F) << 12) |
                        ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                        (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            step = 3;
        } else if (i + 3 < text.size()) {
            codePoint = ((c & 0x07) << 18) |
                        ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                        ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                        (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            step = 4;
        } else {
            break;
        }
        units += codePoint > 0xFFFF ? 2u : 1u;
        i += step;
    }
    return units;
}

void App::SetMarkedText(const std::string& utf8) {
    if (!editing_) return;
    if (!editor_.HasComposition()) editor_.BeginComposition();
    editor_.SetCompositionText(utf8);
}

void App::UnmarkText() {
    // The input method let the composition go without committing it:
    // the text it covered comes back and the history stays untouched.
    if (editor_.HasComposition()) editor_.CancelComposition();
}

bool App::HasMarkedText() const {
    return editor_.HasComposition();
}

void App::MarkedRangeUtf16(uint32_t* location, uint32_t* length) const {
    uint32_t start = 0;
    uint32_t len = 0;
    editor_.CompositionRange(&start, &len);
    const std::string& text = buffer_.Text();
    *location = Utf16Length(text.substr(0, start));
    *length = Utf16Length(text.substr(start, len));
}

void App::SelectionRangeUtf16(uint32_t* location, uint32_t* length) const {
    const std::string& text = buffer_.Text();
    *location = Utf16Length(text.substr(0, sel_.Start()));
    *length = Utf16Length(text.substr(sel_.Start(), sel_.Length()));
}

void App::InsertText(const std::string& text) {
    if (!editing_) return;
    if (text.empty()) return;

    // Control characters never reach the buffer through typing: the
    // Windows build drops everything below 0x20 except tab, and the
    // same rule holds here.
    const std::string filtered = DropControlChars(text, false);
    if (filtered.empty()) return;

    // A typed character that would otherwise be markdown syntax is
    // escaped, so the text stays plain text. This is what the Windows
    // application does as well.
    const std::string escaped =
        EscapeForInsert(buffer_, sel_.active.offset, filtered,
                        IsOffsetInTable(document_, sel_.active.offset));
    if (editor_.HasComposition()) {
        // The committed string of an input method arrives here while
        // the marked text still sits in the buffer: it replaces that
        // text and closes the composition with one undo entry, instead
        // of being inserted a second time.
        editor_.SetCompositionText(escaped);
        editor_.CommitComposition();
    } else {
        ApplyPendingTypingFormat(escaped);
    }

    // A character that finishes a markdown pattern turns the text around
    // it, the way a space after a dash makes a list.
    if (!escaped.empty()) {
        CheckAutoformat(&buffer_, &sel_, escaped[0]);
    }
    OnTextChanged();
}

void App::DeleteBackward() {
    if (!editing_) return;
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
    if (!editing_) return;
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
    if (!editing_) return;
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
    if (!editing_) return;
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
    if (!editing_) return;
    // A table row does not take a soft break; a refused edit must not
    // mark the document dirty.
    if (editor_.InsertSoftBreak(document_)) OnTextChanged();
}

void App::InsertParagraphBreak() {
    if (!editing_) return;
    // The shared controller refuses the break the table model has no
    // place for; a refused edit is not a change.
    if (editor_.InsertParagraphBreak(document_)) OnTextChanged();
}

void App::Undo() {
    if (!editing_) return;
    UndoEntry entry{};
    if (editor_.Undo(&entry)) {
        RestorePendingFormat(entry, false);
        OnTextChanged();
        OnSelectionChanged();
    }
}

void App::Redo() {
    if (!editing_) return;
    UndoEntry entry{};
    if (editor_.Redo(&entry)) {
        RestorePendingFormat(entry, true);
        OnTextChanged();
        OnSelectionChanged();
    }
}

void App::SetContentView(std::unique_ptr<ContentView> view) {
    contentView_ = std::move(view);
}

}  // namespace platform

namespace platform {

namespace {

// One code point to UTF-8, the same encoding the Windows app writes
// when it builds a heading's text for anchor matching.
void AppendCodePointUtf8(std::string* out, char32_t cp) {
    if (cp <= 0x7F) {
        out->push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}  // namespace

std::string App::LinkAtOffset(uint32_t offset) const {
    // The parser records the drawn link text and its byte range, not
    // the Markdown delimiters around it, so a hit on the drawn text is
    // a hit on the link (rule 6: the hidden markers never take a
    // caret, and never take the activation either).
    for (const Node& node : document_.nodes) {
        for (const InlineBlock& ib : node.children) {
            if (ib.kind != InlineKind::Link) continue;
            const uint32_t end = ib.srcOffset + ib.srcLength;
            if (offset >= ib.srcOffset && offset < end) return ib.url;
            if (offset == end && contentView_ &&
                !contentView_->renderer().OffsetIsRendered(offset)) {
                return ib.url;
            }
        }
        if (node.block != BlockKind::Table) continue;
        for (const TableRow& row : node.rows) {
            for (const TableCell& cell : row.cells) {
                for (const TableLink& link : cell.links) {
                    const uint32_t end = link.srcOffset + link.srcLength;
                    if (offset >= link.srcOffset && offset < end) {
                        return link.url;
                    }
                    if (offset == end && contentView_ &&
                        !contentView_->renderer().OffsetIsRendered(offset)) {
                        return link.url;
                    }
                }
            }
        }
    }
    return std::string();
}

bool App::HeadingOffsetForAnchor(const std::string& url,
                                 uint32_t* out) const {
    if (url.empty() || url[0] != '#') return false;
    const std::string anchor = SlugifyAnchor(url.substr(1));
    for (const Node& node : document_.nodes) {
        if (node.block != BlockKind::Heading) continue;
        std::string headingText;
        for (const InlineBlock& ib : node.children) {
            for (char32_t cp : ib.text) {
                AppendCodePointUtf8(&headingText, cp);
            }
        }
        if (SlugifyAnchor(headingText) == anchor) {
            if (out) *out = node.contentOffset;
            return true;
        }
    }
    return false;
}

bool App::OpenExternalUrl(const std::string& url) const {
    if (url.empty()) return false;
    // Control characters mark a crafted destination and are refused
    // before anything opens (rule 3).
    for (char c : url) {
        if (static_cast<unsigned char>(c) < 0x20) return false;
    }
    std::string lower;
    lower.reserve(url.size());
    for (char c : url) {
        lower.push_back((c >= 'A' && c <= 'Z') ?
                        static_cast<char>(c - 'A' + 'a') : c);
    }
    const bool allowed = lower.rfind("http://", 0) == 0 ||
                         lower.rfind("https://", 0) == 0 ||
                         lower.rfind("mailto:", 0) == 0;
    if (!allowed) return false;
    return platform::OpenExternalUrl(url);
}

bool App::MoveTableRow(bool up, uint32_t* out) {
    if (!out) return false;
    const uint32_t caret = sel_.active.offset;
    const Node* table = nullptr;
    for (const Node& node : document_.nodes) {
        if (node.block != BlockKind::Table) continue;
        if (caret >= node.srcOffset &&
            caret < node.srcOffset + node.srcLength) {
            table = &node;
            break;
        }
    }
    if (!table) return false;
    return TableVerticalMove(buffer_.Text(), table->srcOffset,
                             table->srcOffset + table->srcLength,
                             caret, up ? -1 : 1, out);
}

bool App::MoveSelectedText(uint32_t drop) {
    if (!editing_) return false;
    if (sel_.Empty()) return false;

    // The drop runs through the canonical position model: the point
    // becomes a source offset, and that offset is normalized to the
    // rendered caret the same way every other position is (drag and
    // drop 5).
    if (contentView_) {
        const uint32_t normalized =
            contentView_->renderer().NormalizeToRenderedCaret(drop);
        if (normalized == UINT32_MAX) return false;
        drop = normalized;
    }

    // Table guard: a drop inside a cell may not carry table syntax,
    // pipes or line breaks would corrupt the row, and a drop on the
    // delimiter row is refused outright (the same rule the Windows
    // drop follows).
    const std::string& text = buffer_.Text();
    TableCellRef dropRef;
    const bool dropInTable =
        TableCellAtOffset(document_, text, drop, &dropRef);
    if (dropInTable && dropRef.separatorRow) return false;

    std::string moved;
    uint32_t newStart = 0;
    if (!MoveTextRange(text, sel_.Start(), sel_.Length(), drop,
                       &moved, &newStart)) {
        return false;
    }

    uint32_t segmentLen = sel_.Length();
    if (dropInTable) {
        std::string segment = moved.substr(newStart, segmentLen);
        if (segment.find('|') != std::string::npos) return false;
        segment = SanitizePasteForTableCell(segment);
        if (segment.size() != segmentLen) {
            moved.replace(newStart, segmentLen, segment);
            segmentLen = static_cast<uint32_t>(segment.size());
        }
    }

    const Selection before = sel_;
    const std::string original = text;
    buffer_.Splice(0, static_cast<uint32_t>(original.size()), moved);
    Selection after;
    after.anchor = CaretPos{newStart};
    after.active = CaretPos{newStart + segmentLen};
    sel_ = after;

    // One undo entry for the whole move (drag and drop 4): the entry
    // replaces the buffer, so undo restores the text and the original
    // selection together.
    UndoEntry entry{};
    entry.offset = 0;
    entry.removed = original;
    entry.inserted = moved;
    entry.selBefore = before;
    entry.selAfter = after;
    entry.timestamp = 0;
    entry.type = EditType::Other;
    undo_.Push(entry);
    editor_.BreakUndoCoalesce();
    OnTextChanged();
    OnSelectionChanged();
    return true;
}

}  // namespace platform
