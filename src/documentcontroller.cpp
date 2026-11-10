// The document controller bodies (plan 4.1, extraction batch 4).
//
// The command bodies below are the movers from the two ports, kept
// byte for byte in what they do: the splice and the undo entries
// come from the shared formatting functions exactly as before, and
// only the shell around them moved. The undo stamping is the one
// deliberate change: every entry the controller builds goes through
// the platform clock (GetMonotonicMs, defined at the top of this
// file), so both ports and every test target stamp alike.
//
// The inline family (formatting.cpp) unified on the shared per-run
// marker semantics here, the resolution the map directed for its
// divergence 1: the old Windows toggle stripped every run of the
// type, the shared body extends, unwraps and merges runs instead,
// and the strip-all engine lives on as the ClearFormat body. The
// test corpus in tests/transcripts/inline_formatting pins what that
// means, and runs on both platform jobs.

#include "documentcontroller.h"

#include <algorithm>
#include <chrono>

#include "formatting.h"
#include "navigation.h"
#include "tableedit.h"
#include "platformservices.h"

// The undo clock of the shared code. It lives in the controller
// itself, not in the app halves, because the controller compiles into
// the test targets on both runners, and the app translation units do
// not: the link of the skeleton wave needed the symbol from exactly
// here. The steady clock is monotonic on every platform, so the 800 ms
// coalescing rule of the undo stack reads the same clock everywhere
// (undo stack guide, platform services).
namespace platform {

uint64_t GetMonotonicMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace platform

DocumentController::DocumentController()
    : editor_(&buffer_, &sel_) {
    // The edit controller and the undo stack work as one unit: every
    // typed or spliced change lands in the stack through the editor.
    editor_.SetUndoStack(&undo_);
}

// Family 2: headings and lists. One body now, where the Windows
// wrappers (app.cpp) and the mac twins (main_mac.mm) each carried a
// copy. The gate first: a mutating command is a no-op in read mode,
// never a mode switch. The undo entry comes from the formatting
// function itself, the coalescing break keeps the command its own
// undo step, and the port re-parses and redraws through the handler.
void DocumentController::SetHeading(int level) {
    if (!editing_) return;
    SetHeadingLevel(&buffer_, &sel_, level, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::ToggleBullets() {
    if (!editing_) return;
    ToggleUnorderedList(&buffer_, &sel_, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::ToggleNumbering() {
    if (!editing_) return;
    ToggleOrderedList(&buffer_, &sel_, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::ToggleQuote() {
    if (!editing_) return;
    ToggleBlockquote(&buffer_, &sel_, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::Indent() {
    if (!editing_) return;
    IndentLine(&buffer_, &sel_, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::Outdent() {
    if (!editing_) return;
    OutdentLine(&buffer_, &sel_, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

// ---------------------------------------------------------------------------
// Family 3: inline formatting and the undo plumbing around it.
// ---------------------------------------------------------------------------

// The format state query, moved in from the Windows body whole: the
// caret path accepts a span that merely touches the caret position
// (closed interval, plus the spans that end right before or start
// right after it), the selection path trims whitespace and takes
// every overlapping span, table cells answer through their own u16
// mapping, and the pending typing format overrides the empty-caret
// answer when it is set. The mac strip and the Windows ribbon read
// this one answer now.
FormatState DocumentController::GetFormatState() const {
    FormatState fs;
    const auto& text = buffer_.Text();
    if (text.empty()) return fs;

    uint32_t caret = sel_.active.offset;
    if (caret > text.size()) caret = static_cast<uint32_t>(text.size());

    // Helper lambda: check table cell inline formatting.
    // For a table node, iterate all rows/cells and check if the caret
    // or selection overlaps any CellInlineSpan (converting u16 indices
    // to source byte offsets via u16ToSrc). Also scan the raw source
    // text for markdown markers as a fallback.
    auto checkTableCells = [&](const Node& node, uint32_t chkStart, uint32_t chkEnd) {
        // Path 1: CellInlineSpan via u16ToSrc mapping.
        for (const auto& row : node.rows) {
            for (const auto& cell : row.cells) {
                if (cell.u16ToSrc.empty() || cell.inlineSpans.empty()) continue;
                uint32_t cellSrcStart = cell.srcOffset;
                uint32_t cellSrcEnd = cell.u16ToSrc.back() + 1;
                if (chkEnd <= cellSrcStart || chkStart >= cellSrcEnd) continue;

                for (const auto& span : cell.inlineSpans) {
                    if (span.u16Start >= cell.u16ToSrc.size()) continue;
                    uint32_t spanSrcStart = cell.u16ToSrc[span.u16Start];
                    uint32_t spanSrcEnd;
                    if (span.u16End < cell.u16ToSrc.size())
                        spanSrcEnd = cell.u16ToSrc[span.u16End];
                    else
                        spanSrcEnd = cellSrcEnd;

                    // Check overlap with [chkStart, chkEnd).
                    if (spanSrcEnd <= chkStart || spanSrcStart >= chkEnd) continue;
                    if (span.bold)   fs.bold = true;
                    if (span.italic) fs.italic = true;
                    if (span.code)   fs.code = true;
                    if (span.strike) fs.strike = true;
                }
            }
        }

        // Path 2: raw source text marker scan (fallback).
        // Scan the source text around chkStart for markdown markers.
        // This catches formatting that u16ToSrc mapping might miss.
        // Find cell boundaries (pipe chars or newlines).
        uint32_t pos = (chkStart + chkEnd) / 2; // use midpoint
        if (pos >= text.size()) return;
        uint32_t left = pos;
        while (left > 0 && text[left - 1] != '|' && text[left - 1] != '\n')
            left--;
        uint32_t right = pos;
        while (right < text.size() && text[right] != '|' && text[right] != '\n')
            right++;
        // For each marker type, scan left for opening, right for closing.
        auto findFmt = [&](char ch, uint32_t mlen) -> bool {
            // Scan left from `pos` for `mlen` consecutive `ch` chars.
            for (uint32_t i = pos; i >= left + mlen; i--) {
                bool match = true;
                for (uint32_t k = 0; k < mlen; k++)
                    if (text[i - 1 - k] != ch) { match = false; break; }
                if (match) {
                    // Check that these aren't part of a longer run (e.g.
                    // *** is both bold+italic, not just bold).
                    // Also check the char before the run to distinguish
                    // ** (bold) from * (italic).
                    if (mlen == 1 && i >= left + 2 && text[i - 2] == ch)
                        continue; // part of a longer ** run, skip for single-*
                    // Found opening. Scan right for closing of same length.
                    for (uint32_t j = pos; j + mlen <= right; j++) {
                        bool match2 = true;
                        for (uint32_t k = 0; k < mlen; k++)
                            if (text[j + k] != ch) { match2 = false; break; }
                        if (match2) return true;
                    }
                    return false;
                }
                if (i == 0) break;
            }
            return false;
        };
        if (findFmt('*', 2)) fs.bold = true;
        if (findFmt('~', 2)) fs.strike = true;
        if (findFmt('`', 1)) fs.code = true;
        if (!fs.bold && findFmt('*', 1)) fs.italic = true;
    };

    // Collapsed caret: check any inline span whose [cs, ce) range
    // contains the caret position, using CLOSED intervals (cs <= P
    // AND P <= ce). This detects formatting at both the start and end
    // of a span, matching how Word and VS Code behave. Also detect
    // spans that end right before or start right after the caret
    // (adjacent), so the button stays active at boundaries.
    if (sel_.Empty()) {
        uint32_t lo = (caret > 0) ? caret - 1 : 0;
        uint32_t hi = (caret < text.size()) ? caret + 1 : caret;

        for (const auto& node : document_.nodes) {
            uint32_t bs = node.srcOffset;
            uint32_t be = bs + node.srcLength;
            if (caret < bs || caret > be) continue;

            if (node.block == BlockKind::Heading) {
                fs.headingLevel = node.level;
            } else if (node.block == BlockKind::List) {
                if (node.ordered) fs.inNumbering = true;
                else fs.inBullets = true;
            } else if (node.block == BlockKind::BlockQuote) {
                fs.inQuote = true;
            }

            // Regular inline spans.
            for (const auto& child : node.children) {
                uint32_t cs = child.srcOffset;
                uint32_t ce = cs + child.srcLength;
                bool hit = (cs <= caret && caret <= ce);
                if (!hit) {
                    if (ce == lo) hit = true;      // span ends right before caret
                    if (cs == hi) hit = true;      // span starts right after caret
                }
                if (hit) {
                    if (child.strong) fs.bold = true;
                    if (child.em)     fs.italic = true;
                    if (child.code)   fs.code = true;
                    if (child.strike) fs.strike = true;
                }
            }

            // Table cell spans.
            if (node.block == BlockKind::Table) {
                checkTableCells(node, caret, caret + 1);
            }
        }
        if (pending_.boldSet) fs.bold = pending_.bold;
        if (pending_.italicSet) fs.italic = pending_.italic;
        return fs;
    }

    // Non-empty selection: trim whitespace, then check ALL blocks and
    // ALL inline blocks that overlap the trimmed range. Do NOT stop
    // after the first block, since the selection may span paragraphs.
    uint32_t start = sel_.Start();
    uint32_t end = start + sel_.Length();
    if (end > text.size()) end = static_cast<uint32_t>(text.size());

    while (start < end &&
           (text[start] == ' ' || text[start] == '\t' ||
            text[start] == '\n' || text[start] == '\r'))
        start++;
    while (end > start &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' ||
            text[end - 1] == '\n' || text[end - 1] == '\r'))
        end--;

    if (start >= end) {
        start = sel_.Start();
        end = start + 1;
        if (end > text.size()) end = static_cast<uint32_t>(text.size());
    }

    for (const auto& node : document_.nodes) {
        uint32_t bs = node.srcOffset;
        uint32_t be = bs + node.srcLength;
        if (end <= bs || start >= be) continue;

        if (node.block == BlockKind::Heading) {
            fs.headingLevel = node.level;
        } else if (node.block == BlockKind::List) {
            if (node.ordered) fs.inNumbering = true;
            else fs.inBullets = true;
        } else if (node.block == BlockKind::BlockQuote) {
            fs.inQuote = true;
        }

        for (const auto& child : node.children) {
            uint32_t cs = child.srcOffset;
            uint32_t ce = cs + child.srcLength;
            if (cs >= end || ce <= start) continue;
            if (child.strong) fs.bold = true;
            if (child.em)     fs.italic = true;
            if (child.code)   fs.code = true;
            if (child.strike) fs.strike = true;
        }

        // Table cell spans.
        if (node.block == BlockKind::Table) {
            checkTableCells(node, start, end);
        }
    }
    return fs;
}

// The shared body of the bold and italic toggles (moved in from
// App::ToggleInlineWithPending on the mac side; the Windows twins
// had the same shape around their own state). With a selection the
// SHARED marker toggle applies the guide's per-run rules. An empty
// caret toggles what the NEXT characters get and changes nothing in
// the document (text guide, fed og kursiv ved et tomt caret rules 1
// and 2): the pending toggle is its own undo step, and undo brings
// the old pending state back before it touches any text (rule 5).
void DocumentController::ToggleInlineWithPending(const std::string& marker,
                                                 bool bold) {
    if (!editing_) return;
    if (!sel_.Empty()) {
        ClearPendingFormat();
        ToggleInlineMarker(&buffer_, &sel_, marker, &undo_);
        editor_.BreakUndoCoalesce();
        NotifyDocumentChanged();
        return;
    }

    const bool beforeBold = pending_.bold;
    const bool beforeItalic = pending_.italic;
    const bool beforeBoldSet = pending_.boldSet;
    const bool beforeItalicSet = pending_.italicSet;

    bool& value = bold ? pending_.bold : pending_.italic;
    bool& set = bold ? pending_.boldSet : pending_.italicSet;
    // The pending state starts from the format the caret sits in
    // when nothing is pending yet, read through the moved query (so
    // a caret next to or inside a span counts, the same answer the
    // ribbon buttons show).
    const FormatState fs = GetFormatState();
    const bool current =
        set ? value : (bold ? fs.bold : fs.italic);
    value = !current;
    set = true;
    pending_.runActive = false;
    pending_.runSuffixBytes = 0;
    RecordPendingFormatUndo(sel_, beforeBold, beforeItalic, beforeBoldSet,
                            beforeItalicSet, pending_.bold, pending_.italic,
                            pending_.boldSet, pending_.italicSet);
    editor_.BreakUndoCoalesce();
    NotifySelectionChanged();
}

void DocumentController::ToggleBold() {
    if (!editing_) return;
    ToggleInlineWithPending("**", true);
}

void DocumentController::ToggleItalic() {
    if (!editing_) return;
    ToggleInlineWithPending("*", false);
}

// Strikethrough carries no pending format on either port, so it
// answers the shared marker toggle directly: the exactly wrapped
// selection unwraps, the empty caret inserts the bare pair with the
// caret between them, and a partial overlap extends the run (the
// resolution of the map's divergence 1 applied to this marker).
void DocumentController::ToggleStrike() {
    if (!editing_) return;
    ToggleInlineMarker(&buffer_, &sel_, "~~", &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

// Code keeps the whole line branch the Windows twin carried: a
// selection that spans one or more entire lines toggles a fenced
// code block through the shared ToggleCodeBlock, and any other
// selection answers the inline backtick toggle. Both branches are
// shared formatting functions, so the mac menu item gains the
// fenced shape under it rather than a new body.
void DocumentController::ToggleCode() {
    if (!editing_) return;
    if (!sel_.Empty()) {
        const std::string& text = buffer_.Text();
        uint32_t start = sel_.Start();
        uint32_t end = start + sel_.Length();
        // Check if the selection starts at the beginning of a line and
        // ends at the end of a line (or at a newline character).
        bool startsAtLineStart = (start == 0 || text[start - 1] == '\n');
        bool endsAtLineEnd = (end >= text.size() || text[end] == '\n');
        // Also handle selections that end right before the newline
        // (e.g. the last visible character, not including the newline).
        if (end > 0 && end <= text.size() && end < text.size() && text[end] != '\n') {
            // Check if the rest of the line is only whitespace
            uint32_t check = end;
            while (check < text.size() && text[check] != '\n') {
                if (text[check] != ' ' && text[check] != '\t') break;
                check++;
            }
            if (check >= text.size() || text[check] == '\n')
                endsAtLineEnd = true;
        }
        if (startsAtLineStart && endsAtLineEnd) {
            ToggleCodeBlock(&buffer_, &sel_, &undo_);
            editor_.BreakUndoCoalesce();
            NotifyDocumentChanged();
            return;
        }
    }
    // Default: inline code (single backticks).
    ToggleInlineMarker(&buffer_, &sel_, "`", &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

// The link command body. The dialog itself stays with the port
// (the Win32 builder and the mac alert); this takes the resolved
// destination. The base editor specification requires a non-empty
// visible selection for link formatting, so an empty caret or an
// empty destination changes nothing.
void DocumentController::ToggleInlineLink(const std::string& url) {
    if (!editing_) return;
    if (sel_.Empty()) return;
    if (url.empty()) return;
    InsertLink(&buffer_, &sel_, url, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

// The inline markers go away first, through the shared strip-all
// engine, then the block formatting of the first line follows per
// the block state the query reads (this is the Windows twin's body,
// which already ran the shared module and kept every undo entry:
// the mac twin cleared the block prefix with a blind splice that
// recorded nothing, and read the inline engine through stale bit
// offsets; one body now, one order, one undo trail).
void DocumentController::ClearFormat() {
    if (!editing_) return;
    if (sel_.Empty()) return;  // a selection is needed to clear anything

    ClearInlineFormatting(document_, &buffer_, &sel_, &undo_);

    // Remove BLOCK-level formatting: headings, lists, blockquotes.
    // The query re-reads the parse the command runs under, the same
    // re-read the twin performed after the inline change.
    FormatState fs = GetFormatState();
    if (fs.headingLevel > 0) {
        SetHeadingLevel(&buffer_, &sel_, 0, &undo_);
    }
    if (fs.inBullets)  ToggleUnorderedList(&buffer_, &sel_, &undo_);
    if (fs.inNumbering) ToggleOrderedList(&buffer_, &sel_, &undo_);
    if (fs.inQuote)    ToggleBlockquote(&buffer_, &sel_, &undo_);

    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

// The pending format reaches the document only when text does. The
// first typed character opens the markers around itself; the next
// ones replace the run's closing markers with themselves plus the
// markers, so the run extends instead of nesting (text guide, fed
// og kursiv ved et tomt caret rules 1 and 2). The two port bodies
// were this same code twice; the empty input guard keeps a no-op
// input from recording a no-op splice.
void DocumentController::ApplyPendingTypingFormat(const std::string& value) {
    std::string prefix;
    std::string suffix;
    if (pending_.boldSet && pending_.bold) {
        prefix += "**";
        suffix = "**" + suffix;
    }
    if (pending_.italicSet && pending_.italic) {
        prefix += "*";
        suffix = "*" + suffix;
    }

    if (value.empty()) {
        pending_.runActive = false;
        pending_.runSuffixBytes = 0;
        return;
    }
    if (suffix.empty()) {
        editor_.InsertText(value);
        pending_.runActive = false;
        pending_.runSuffixBytes = 0;
        return;
    }

    const std::string& source = buffer_.Text();
    const uint32_t caret = sel_.active.offset;
    const uint32_t suffixBytes = static_cast<uint32_t>(suffix.size());
    const bool canExtend = pending_.runActive && sel_.Empty() &&
        pending_.runSuffixBytes == suffixBytes &&
        caret >= suffixBytes &&
        source.compare(caret - suffixBytes, suffixBytes, suffix) == 0;

    if (canExtend) {
        editor_.ReplaceTextRange(caret - suffixBytes, suffixBytes,
                                 value + suffix, EditType::Insert);
    } else {
        editor_.InsertText(prefix + value + suffix);
    }
    pending_.runActive = true;
    pending_.runSuffixBytes = suffixBytes;
}

// Ends the pending format and the typing run: the next characters
// start fresh wherever the caret lands (text guide, fed og kursiv
// ved et tomt caret rule 3). Every click and caret move of the ports
// arrives through their selection paths, which call this.
void DocumentController::ClearPendingFormat() {
    pending_.boldSet = false;
    pending_.italicSet = false;
    pending_.runActive = false;
    pending_.runSuffixBytes = 0;
}

// Restores the pending state an undo or redo entry carries. The port
// undo paths call this right after the editor applied the entry, so
// the pending toggles travel with the undo history (the empty caret
// rules, rule 5).
void DocumentController::RestorePendingFormat(const UndoEntry& entry,
                                              bool redo) {
    if (!entry.hasPendingFormat) return;
    if (redo) {
        pending_.bold = entry.afterPendingBold;
        pending_.italic = entry.afterPendingItalic;
        pending_.boldSet = entry.afterPendingBoldSet;
        pending_.italicSet = entry.afterPendingItalicSet;
    } else {
        pending_.bold = entry.pendingBold;
        pending_.italic = entry.pendingItalic;
        pending_.boldSet = entry.pendingBoldSet;
        pending_.italicSet = entry.pendingItalicSet;
    }
    pending_.runActive = false;
    pending_.runSuffixBytes = 0;
}

void DocumentController::ReplaceMatch(uint32_t offset, uint32_t length,
                                      const std::string& replacement) {
    if (!editing_ || length == 0) return;
    std::string text = replacement;
    if (IsOffsetInTable(document_, offset)) {
        text = SanitizePasteForTableCell(text);
    }
    // The match is the selection the undo entry returns to.
    sel_.anchor = CaretPos{offset};
    sel_.active = CaretPos{offset + length};
    editor_.ReplaceTextRange(offset, length, text, EditType::Other);
    // The replacement is one undo step of its own; the next keystroke
    // must not merge into it.
    editor_.BreakUndoCoalesce();
    // The editor leaves a caret after the text; keep the text selected.
    sel_.anchor = CaretPos{offset};
    ClearPendingFormat();
    NotifyDocumentChanged();
}

void DocumentController::ReplaceAllText(const std::string& replaced) {
    if (!editing_) return;
    const std::string original = buffer_.Text();
    const Selection before = sel_;
    buffer_.Splice(0, static_cast<uint32_t>(original.size()), replaced);
    sel_.Collapse(CaretPos{0});
    UndoEntry entry{};
    entry.offset = 0;
    entry.removed = original;
    entry.inserted = replaced;
    entry.selBefore = before;
    entry.selAfter = sel_;
    entry.timestamp = platform::GetMonotonicMs();
    entry.type = EditType::Other;
    undo_.Push(entry);
    editor_.BreakUndoCoalesce();
    ClearPendingFormat();
    NotifyDocumentChanged();
}

// ---------------------------------------------------------------------
// Family 5: tables.
// ---------------------------------------------------------------------

bool DocumentController::CaretInTable() const {
    if (IsOffsetInTable(document_, sel_.active.offset)) return true;
    return !sel_.Empty() && IsOffsetInTable(document_, sel_.Start());
}

TableCapabilities DocumentController::CaretTableCapabilities() const {
    // Resolve the caret the same way the commands do, so an enabled
    // button always matches a command that runs.
    const Node* table =
        tableedit::ContainingTable(document_, sel_.active.offset);
    if (!table) return TableCapabilities();
    tableedit::TableLocation loc;
    if (!tableedit::Locate(buffer_.Text(), table, sel_.active.offset, &loc)) {
        return TableCapabilities();
    }
    return TableCapabilitiesFor(loc.rowIndex, loc.numCols);
}

void DocumentController::RewriteTableSpan(uint32_t start, uint32_t length,
                                          const std::string& text,
                                          uint32_t caret) {
    SpliceWithUndo(start, length, text);
    editor_.BreakUndoCoalesce();
    sel_.Collapse(CaretPos{caret});
    NotifyDocumentChanged();
}

bool DocumentController::AddTableRow(bool below) {
    if (!editing_) return false;
    const Node* table =
        tableedit::ContainingTable(document_, sel_.active.offset);
    if (!table) return false;
    tableedit::TableLocation loc;
    if (!tableedit::Locate(buffer_.Text(), table, sel_.active.offset, &loc)) {
        return false;
    }
    const tableedit::TableEdit edit =
        tableedit::AddRow(buffer_.Text(), loc, below);
    if (!edit.ok) return false;
    RewriteTableSpan(edit.start, edit.length, edit.text, edit.caret);
    return true;
}

bool DocumentController::RemoveTableRow() {
    if (!editing_) return false;
    const Node* table =
        tableedit::ContainingTable(document_, sel_.active.offset);
    if (!table) return false;
    tableedit::TableLocation loc;
    if (!tableedit::Locate(buffer_.Text(), table, sel_.active.offset, &loc)) {
        return false;
    }
    const tableedit::TableEdit edit =
        tableedit::RemoveRow(buffer_.Text(), loc);
    if (!edit.ok) return false;
    RewriteTableSpan(edit.start, edit.length, edit.text, edit.caret);
    return true;
}

bool DocumentController::AddTableColumn(bool right) {
    if (!editing_) return false;
    const Node* table =
        tableedit::ContainingTable(document_, sel_.active.offset);
    if (!table) return false;
    const std::string& text = buffer_.Text();
    tableedit::TableLocation loc;
    if (!tableedit::Locate(text, table, sel_.active.offset, &loc)) return false;
    const uint32_t start = table->srcOffset;
    const uint32_t end = start + table->srcLength;
    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableInsertColumn(text, start, end, loc.columnIndex, right,
                           sel_.active.offset, &rebuilt, &caret)) {
        return false;
    }
    // The whole span goes in as one splice: one Ctrl+Z removes the column.
    RewriteTableSpan(start, end - start, rebuilt, caret);
    return true;
}

bool DocumentController::RemoveTableColumn() {
    if (!editing_) return false;
    const Node* table =
        tableedit::ContainingTable(document_, sel_.active.offset);
    if (!table) return false;
    const std::string& text = buffer_.Text();
    tableedit::TableLocation loc;
    if (!tableedit::Locate(text, table, sel_.active.offset, &loc)) return false;
    // The last column stays.
    if (!TableCapabilitiesFor(loc.rowIndex, loc.numCols).removeColumn) {
        return false;
    }
    const uint32_t start = table->srcOffset;
    const uint32_t end = start + table->srcLength;
    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableRemoveColumn(text, start, end, loc.columnIndex,
                           sel_.active.offset, &rebuilt, &caret)) {
        return false;
    }
    RewriteTableSpan(start, end - start, rebuilt, caret);
    return true;
}

bool DocumentController::SetTableColumnAlign(TableAlignMark mark) {
    if (!editing_) return false;
    const Node* table =
        tableedit::ContainingTable(document_, sel_.active.offset);
    if (!table) return false;
    const std::string& text = buffer_.Text();
    tableedit::TableLocation loc;
    if (!tableedit::Locate(text, table, sel_.active.offset, &loc)) return false;
    const uint32_t start = table->srcOffset;
    const uint32_t end = start + table->srcLength;
    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableSetColumnAlign(text, start, end, loc.columnIndex, mark,
                             sel_.active.offset, &rebuilt, &caret)) {
        return false;
    }
    RewriteTableSpan(start, end - start, rebuilt, caret);
    return true;
}

bool DocumentController::SplitTableCell() {
    if (!editing_) return false;
    const Node* table =
        tableedit::ContainingTable(document_, sel_.active.offset);
    if (!table) return false;
    const std::string& text = buffer_.Text();
    tableedit::TableLocation loc;
    if (!tableedit::Locate(text, table, sel_.active.offset, &loc)) return false;
    // The header and delimiter rows carry the table's shape.
    if (!TableCapabilitiesFor(loc.rowIndex, loc.numCols).splitCell) {
        return false;
    }
    const uint32_t start = table->srcOffset;
    const uint32_t end = start + table->srcLength;
    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableSplitCell(text, start, end, loc.columnIndex, loc.rowIndex, 2,
                        sel_.active.offset, &rebuilt, &caret)) {
        return false;
    }
    RewriteTableSpan(start, end - start, rebuilt, caret);
    return true;
}

bool DocumentController::RemoveTable() {
    if (!editing_) return false;
    const Node* table =
        tableedit::ContainingTable(document_, sel_.active.offset);
    if (!table) return false;
    const std::string& text = buffer_.Text();
    const uint32_t start = table->srcOffset;
    const uint32_t end = start + table->srcLength;
    const uint32_t removed = end - start;

    // The block after the table starts at the first non-newline byte
    // past the table, when a block begins there. Its offset shifts left
    // by the removed length. With no such block the caret stays where
    // the table stood; both ports once subtracted the length from that
    // offset as well, which put the caret in the wrong place whenever
    // the table was not at the very start of the text.
    const uint32_t size = static_cast<uint32_t>(text.size());
    uint32_t look = end;
    while (look < size && text[look] == '\n') ++look;
    uint32_t caret = start;
    if (look < size) {
        for (const Node& node : document_.nodes) {
            if (node.block != BlockKind::Table && node.srcOffset == look) {
                caret = look - removed;
                break;
            }
        }
    }
    const uint32_t newSize = size - removed;
    if (caret > newSize) caret = newSize;
    RewriteTableSpan(start, removed, "", caret);
    return true;
}

bool DocumentController::InsertTableBlock(const std::string& table,
                                          uint32_t firstCellCaret) {
    uint32_t insertPos = sel_.active.offset;
    uint32_t replaceLen = 0;
    if (!sel_.Empty()) {
        insertPos = sel_.Start();
        replaceLen = sel_.Length();
        sel_.Collapse(CaretPos{insertPos});
    }
    // One splice, one undo step: the separator lines, the table and the
    // trailing break are one structural insert.
    const std::string& text = buffer_.Text();
    std::string insertText;
    // A table is a block: without a blank line before it, the block
    // before (the last item of a list, say) absorbs its rows.
    insertText.append(TableSeparatorNewlinesBefore(text, insertPos), '\n');
    const uint32_t prefixLen = static_cast<uint32_t>(insertText.size());
    insertText += table;
    if (insertPos + replaceLen < text.size() &&
        text[insertPos + replaceLen] != '\n') {
        insertText += "\n";
    }
    RewriteTableSpan(insertPos, replaceLen, insertText,
                     insertPos + prefixLen + firstCellCaret);
    return true;
}

bool DocumentController::InsertTable(int cols, int rows) {
    if (!editing_) return false;
    // A table never nests: not from the menu, the picker or a paste.
    if (CaretInTable()) return false;
    // The caret goes to the first header cell, past its leading pipe.
    return InsertTableBlock(tableedit::BuildGrid(cols, rows), 1);
}

bool DocumentController::InsertPastedTable(const std::string& table,
                                           uint32_t firstCellCaret) {
    if (!editing_) return false;
    if (CaretInTable()) return false;
    // A structural paste is not part of a pending typing run.
    ClearPendingFormat();
    return InsertTableBlock(table, firstCellCaret);
}

bool DocumentController::AppendRowToTable() {
    if (!editing_) return false;
    std::string row;
    uint32_t insertPos = 0;
    uint32_t caretAfter = 0;
    if (!TableAppendRow(document_, buffer_.Text(), sel_.active.offset, &row,
                        &insertPos, &caretAfter)) {
        return false;
    }
    RewriteTableSpan(insertPos, 0, row, caretAfter);
    return true;
}

void DocumentController::SpliceWithUndo(uint32_t offset, uint32_t length,
                                        const std::string& replacement) {
    std::string removed = buffer_.Text().substr(
        std::min(offset, static_cast<uint32_t>(buffer_.Text().size())),
        std::min(length, static_cast<uint32_t>(
            buffer_.Text().size() - std::min(offset,
                static_cast<uint32_t>(buffer_.Text().size())))));
    Selection selBefore = sel_;
    buffer_.Splice(offset, length, replacement);
    Selection selAfter = sel_;
    UndoEntry entry{};
    entry.offset = offset;
    entry.removed = removed;
    entry.inserted = replacement;
    entry.selBefore = selBefore;
    entry.selAfter = selAfter;
    entry.timestamp = platform::GetMonotonicMs();
    entry.type = EditType::Insert;
    undo_.Push(entry);
}

void DocumentController::RecordPendingFormatUndo(
    const Selection& sel, bool beforeBold, bool beforeItalic,
    bool beforeBoldSet, bool beforeItalicSet, bool afterBold,
    bool afterItalic, bool afterBoldSet, bool afterItalicSet) {
    UndoEntry entry{};
    entry.offset = sel.active.offset;
    entry.selBefore = sel;
    entry.selAfter = sel;
    entry.timestamp = platform::GetMonotonicMs();
    entry.type = EditType::PendingFormat;
    entry.hasPendingFormat = true;
    entry.pendingBold = beforeBold;
    entry.pendingItalic = beforeItalic;
    entry.pendingBoldSet = beforeBoldSet;
    entry.pendingItalicSet = beforeItalicSet;
    entry.afterPendingBold = afterBold;
    entry.afterPendingItalic = afterItalic;
    entry.afterPendingBoldSet = afterBoldSet;
    entry.afterPendingItalicSet = afterItalicSet;
    undo_.Push(entry);
    // The after state rides in the same entry because the public
    // stack API cannot append a redo side: the current state is
    // deterministic from the before state and this toggle, so
    // ApplyUndo/ApplyRedo recomputes the opposite state for this
    // entry.
}

void DocumentController::LoadFile(const std::string& raw,
                                  const std::string& pathUtf8) {
    const std::string text = DecodeFileText(raw, pathUtf8, &fileFormat_);
    buffer_.SetText(text);
    undo_.Clear();
    sel_.Collapse(CaretPos{0});
    pending_ = PendingFormatState{};
}

std::string DocumentController::BytesForSaving() const {
    return EncodeFileText(buffer_.Text(), fileFormat_);
}
