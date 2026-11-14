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
// This wave carries the skeleton, family 4 (find and replace, the
// text changing half only), family 2 (headings and lists:
// SetHeading, ToggleBullets, ToggleNumbering, ToggleQuote, Indent and
// Outdent) and family 3, the inline formatting and undo plumbing:
// the bold, italic, strikethrough, code and link toggles, Clear
// Formatting, the pending typing format of an empty caret, and the
// format state query the chrome of both ports reads.
//
// The controller is deliberately thin about everything that is not
// text: it does not know a window, a renderer or a dialog. A command
// body ends by notifying the port through the document changed
// handler, and the port does its own re-parse, redraw and menu
// sync, exactly what the command twins used to do by hand. A command
// that only moved the notion of which format is active (a pending
// toggle with no selection) ends with the selection changed handler
// instead, because nothing about the text changed and the port only
// needs to refresh its buttons.
//
// Family 3 resolved the map's divergence 1 on the way in: the inline
// toggles answer the SHARED formatting semantics of formatting.cpp
// (per marker run, what the mac build did), not the Windows shortcut
// that stripped every run of the type through
// RemoveAllFormattingInSelection. The strip-all engine survives as
// exactly what ClearFormat is: ClearInlineFormatting, one body on
// both ports now. The transcript corpus in
// tests/transcripts/inline_formatting pins the resolution.

#pragma once

#include "navigation.h"
#include <cstdint>
#include <functional>
#include <string>

#include "caret.h"
#include "documentfile.h"
#include "dom.h"
#include "editcontroller.h"
#include "textbuffer.h"
#include "undostack.h"

// Active formatting state at the caret position or over the
// selection. This moved in from the Windows app.h: the ribbon reads
// it through the shell that forwards to the controller query, and
// the mac strip reads the same answer, so the pressed state of the
// toggle buttons can no longer drift between the ports.
struct FormatState {
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool strike = false;
    int  headingLevel = 0;
    bool inBullets = false;
    bool inNumbering = false;
    bool inQuote = false;
};

// The pending typing format: bold or italic toggled with an empty
// caret, waiting for the next typed characters (text guide, fed og
// kursiv ved et tomt caret, rules 1 to 5). Both ports carried this
// state once each with different spellings; the aliases in the
// ports now bind to these fields.
struct PendingFormatState {
    bool bold = false;
    bool italic = false;
    bool boldSet = false;
    bool italicSet = false;
    // Whether the caret still sits on the suffix markers of the run
    // being typed, which lets the next characters extend that run.
    bool runActive = false;
    uint32_t runSuffixBytes = 0;
};

class DocumentController {
public:
    DocumentController();

    // The owned state. The ports alias these so every address in
    // their code keeps pointing at the same objects.
    TextBuffer& buffer() { return buffer_; }
    Selection& selection() { return sel_; }
    EditController& editor() { return editor_; }
    UndoStack& undo() { return undo_; }
    // The parsed document. Both ports parse the buffer text into it
    // through their reparse paths (the alias they hold), and the
    // controller reads it back for the format state query and the
    // clear format engine.
    Document& document() { return document_; }
    // The pending typing format (see PendingFormatState above).
    PendingFormatState& pending() { return pending_; }

    // The edit-mode gate. The ports own the mode flag (their views
    // and bars read it too); they keep the controller in step, and
    // every command body checks it here before it touches text. A
    // mutating command in read mode is a no-op, never a mode
    // switch, the rule both ports already enforced (word processor
    // guide, read-only documents).
    void SetEditing(bool on) { editing_ = on; }
    bool IsEditing() const { return editing_; }

    // Family 6: the file side. LoadFile takes the bytes of a file (or
    // imported text with an empty path), decodes them (BOM, line ends,
    // the .mmd and .svg fence), swaps the buffer, clears undo and puts
    // the caret at the top. The port re-parses, lays out and watches
    // the file. BytesForSaving answers what goes to disk. The dialogs,
    // the recent list and the file watcher stay native.
    void LoadFile(const std::string& raw, const std::string& pathUtf8);
    std::string BytesForSaving() const;
    // The text has no file behind it any more (an import, a new
    // document): no BOM, no fence, LF line ends.
    void ForgetFile() { fileFormat_ = FileFormat{}; }
    const FileFormat& fileFormat() const { return fileFormat_; }

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

    // -------------------------------------------------------------------
    // Family 3: inline formatting and the undo plumbing around it.
    // -------------------------------------------------------------------

    // What the toggle buttons and the menu validation read: the
    // bold, italic, code, strike (and block) state at the caret or
    // over the selection, with the pending typing format on top
    // when it is set. One query for both ports, moved in from the
    // Windows body.
    FormatState GetFormatState() const;

    // The inline toggles. With a non-empty selection they apply the
    // shared per-run marker semantics of ToggleInlineMarker
    // (formatting.cpp), the resolution of divergence 1: a selection
    // that only partially overlaps a run extends it, an exactly
    // wrapped selection unwraps, and a multi run selection merges
    // into one wrap around what the runs covered. With an empty
    // caret, bold and italic toggle the PENDING typing format
    // instead and write nothing to the document (the empty caret
    // rules of the text guide); strikethrough and code insert the
    // bare marker pair with the caret between, the empty selection
    // branch of ToggleInlineMarker.
    void ToggleBold();
    void ToggleItalic();
    void ToggleStrike();
    // Code keeps the Windows whole line branch: a selection that
    // spans one or more entire lines toggles a fenced code block
    // (the shared ToggleCodeBlock) rather than inline backticks.
    void ToggleCode();
    // The link command. The url prompt stays with the port (the
    // Win32 dialog builder, the mac alert); the shell passes the
    // resolved destination here. The link needs a non-empty
    // selection, and the insert itself is the shared InsertLink.
    void ToggleInlineLink(const std::string& url);
    // Takes EVERY inline formatting marker away from the selection,
    // through the shared strip-all engine (ClearInlineFormatting),
    // plus the block formatting of the first line. This is the
    // home of the strip-all behavior the family's toggles gave up
    // when they unified on the per-run semantics.
    void ClearFormat();

    // Puts typed text through the pending format: the first typed
    // character opens the markers around itself, the next ones
    // extend the run by replacing its closing markers, so a
    // continuous input stays one span instead of nesting markers
    // (text guide, rules 1 and 2). Moved in from both ports, whose
    // bodies were the same modulo spelled state.
    void ApplyPendingTypingFormat(const std::string& value);
    // Ends the pending format and the typing run. The port calls it
    // from every caret move and every path that replaces the
    // selection (word processor guide A-H, the canonical caret).
    void ClearPendingFormat();
    // Restores the pending state an undo or redo entry carries, so
    // Cmd+Z brings the old pending toggles back before it touches
    // any text (the empty caret rules, rule 5).
    void RestorePendingFormat(const UndoEntry& entry, bool redo);

    // -------------------------------------------------------------------
    // Family 4: find and replace. The bar, the match list and the
    // scope query stay with the ports; what changes the text is here.
    // -------------------------------------------------------------------

    // Replaces one match. Inside a table cell the replacement goes
    // through the same sanitizing a paste does, so a pipe or a row
    // break cannot turn the cell into table syntax (table spec rule
    // 10). This resolved the map's divergence 2: the Windows path
    // spliced raw text. The edit is one undo step of its own, and
    // the inserted text is left selected, so the port either moves
    // on to the next match or leaves the text selected when none is
    // left (find spec AC-12). Read mode is a no-op.
    void ReplaceMatch(uint32_t offset, uint32_t length,
                      const std::string& replacement);

    // Swaps the whole document for the text the find state built
    // for Replace All. One undo entry for the sweep, and the caret
    // goes to the top of the document, the Windows answer. The port
    // has already ruled out the zero match case. Read mode is a
    // no-op.
    void ReplaceAllText(const std::string& replaced);

    // -------------------------------------------------------------------
    // Family 5: tables. The pickers (the Win32 grid, the mac popover)
    // and the ribbon or menu wiring stay with the ports; every command
    // that changes the table text is here. The algorithms were shared
    // already (tableedit.cpp, navigation.cpp); what moves is the glue
    // both ports wrote twice. Each command is one undo step and does
    // nothing in read mode. They return whether text changed.
    // -------------------------------------------------------------------

    // True when the caret, or the anchor side of a selection, sits in
    // a table. This is the one rule for "in a table": Tab, paste,
    // insert and the ribbon all ask it.
    // Whether the caret sits in a fenced or indented code block, or in a
    // diagram: the place where markdown formatting has no meaning. The
    // command gates read this, so a format command inside a fence stands
    // down instead of injecting markers into source nobody rendered.
    bool CaretInCodeBlock() const;

    // Whether this document came from a standalone .mmd or .svg file. Its
    // buffer is one synthetic fence around bare diagram source, so the
    // markdown commands never apply, however the caret is placed.
    bool InDiagramDocument() const {
        return fileFormat_.isMmd || fileFormat_.isSvg;
    }

    bool CaretInTable() const;

    // What the table commands can do at the caret. The ribbon greys
    // out a command from it, and the commands call it too, so a live
    // button never does nothing (table spec, capability rules).
    TableCapabilities CaretTableCapabilities() const;

    bool AddTableRow(bool below);
    bool RemoveTableRow();
    bool AddTableColumn(bool right);
    bool RemoveTableColumn();
    bool SetTableColumnAlign(TableAlignMark mark);
    // Refused on the header and delimiter rows (table spec 22).
    bool SplitTableCell();

    // Removes the whole table in one splice. The caret goes to the
    // block that followed it, or to where the table stood.
    bool RemoveTable();

    // A new table of the given size at the caret, replacing a
    // selection. A table never nests, and it always gets a blank
    // separator line in front, or the block before would absorb it.
    // The caret lands in the first header cell.
    bool InsertTable(int cols, int rows);

    // A converted spreadsheet block as a table at the caret, with the
    // same line handling. firstCellCaret is the offset of the first
    // cell to land in, counted from the start of the table text.
    bool InsertPastedTable(const std::string& table, uint32_t firstCellCaret);

    // Tab from the last cell: a blank row with the table's column
    // count, and the caret in its first cell.
    bool AppendRowToTable();

    // Splices the buffer and records the undo entry in one step, so
    // the table, find and link paths of both ports share one entry
    // shape. The stamp comes from the platform clock, so both ports
    // stamp the same monotonic ms.
    void SpliceWithUndo(uint32_t offset, uint32_t length,
                        const std::string& replacement);

    // What a command body calls when it has changed the document:
    // the port re-parses, re-renders and re-syncs its chrome. The
    // handler runs inside the command, so the call order stays the
    // one the old command twins kept.
    using DocumentChangedHandler = std::function<void()>;
    void SetDocumentChangedHandler(DocumentChangedHandler handler) {
        documentChanged_ = std::move(handler);
    }

    // What a command body calls when only the caret notion changed:
    // a pending toggle at an empty caret, or a query that depends
    // on the selection. The port refreshes its buttons and menus;
    // nothing about the document text changed.
    using SelectionChangedHandler = std::function<void()>;
    void SetSelectionChangedHandler(SelectionChangedHandler handler) {
        selectionChanged_ = std::move(handler);
    }

private:
    // Declaration order matters: the edit controller binds to the
    // buffer and the selection by address in its constructor.
    TextBuffer buffer_;
    Selection sel_;
    EditController editor_;
    UndoStack undo_;
    // The parsed document, refreshed by the ports' reparse paths
    // (their alias of document()). It is declared here because it
    // depends on nothing; no binding order applies to it.
    Document document_;
    // The pending typing format state.
    PendingFormatState pending_;
    bool editing_ = false;
    FileFormat fileFormat_;
    DocumentChangedHandler documentChanged_;
    SelectionChangedHandler selectionChanged_;

    // The shared body of the bold and italic toggles: a pending
    // toggle at an empty caret, the shared marker toggle over a
    // selection (moved in from App::ToggleInlineWithPending).
    void ToggleInlineWithPending(const std::string& marker, bool bold);

    // One table operation: the span goes in as one splice, the undo
    // coalescing breaks so a following keystroke starts a new step, and
    // the caret moves to where the operation says.
    void RewriteTableSpan(uint32_t start, uint32_t length,
                          const std::string& text, uint32_t caret);
    // The shared insert of a table block at the caret (grid insert and
    // paste): separator lines, trailing break, one splice.
    bool InsertTableBlock(const std::string& table, uint32_t firstCellCaret);

    // One undo step for a pending toggle, recorded by the toggle
    // body above (text guide, bold and italic at an empty caret,
    // rule 5). It carried the port-side callers of the pre
    // controller waves; those shells are gone now.
    void RecordPendingFormatUndo(const Selection& sel, bool beforeBold,
                                 bool beforeItalic, bool beforeBoldSet,
                                 bool beforeItalicSet, bool afterBold,
                                 bool afterItalic, bool afterBoldSet,
                                 bool afterItalicSet);

    void NotifyDocumentChanged() {
        if (documentChanged_) documentChanged_();
    }
    void NotifySelectionChanged() {
        if (selectionChanged_) selectionChanged_();
    }
};
