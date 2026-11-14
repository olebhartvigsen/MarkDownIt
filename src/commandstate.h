// Whether a command can act right now (command state, one answer for both
// ports).
//
// The command table (commands.h) says what every command IS: its name, its
// ribbon button, its mac selector, whether it changes the document. This
// header says when every command is AVAILABLE. The two surfaces that show
// commands, the Windows ribbon and the mac menu bar, format bar and
// toolbar, ask this one function, so a button and its menu item cannot
// disagree, and a command that cannot act is greyed instead of silently
// doing nothing (ribbon-menu guide: disable, never hide).
//
// Three axes decide the answer:
//
//   1. The surface. The welcome screen holds no document, so nothing that
//      edits, saves, finds or zooms has anything to act on.
//   2. The mode. In view mode every command that changes the document
//      stands down; reading, selecting, copying, finding, marking and
//      zooming keep working (text spec: state and commands).
//   3. The file. A standalone .mmd or .svg file opens as one synthetic
//      fence around bare diagram source, and a markdown document may hold
//      a fence or a diagram of its own. Markdown formatting has no
//      meaning inside code, so those commands stand down there too, and
//      the caret decides which of the two cases applies.
//
// The requirements are data, not prose: RequirementsOf names every command
// of the table, and tests/commandstate_test.cpp fails when a row is
// missing or when a mutating command forgets the edit-mode gate.

#pragma once

#include <cstdint>

#include "commands.h"

class DocumentController;

namespace commands {

// What a table action needs before it can act on the document.
enum class TableNeed : uint8_t {
    None,             // not a table command
    Any,              // any table the caret is in
    AddRowBelow,
    AddRowAbove,
    RemoveRow,
    AddColumnRight,
    AddColumnLeft,
    RemoveColumn,
    SplitCell,
};

// The window facts a command answers from. The document half comes from
// the shared controller (FromDocument below); the window half (welcome,
// find, zoom) belongs to the shell that owns it.
struct Context {
    // The welcome screen is on screen: there is no document.
    bool welcome = true;
    bool editing = false;
    // The document came from a standalone .mmd or .svg file.
    bool diagram = false;
    // The caret sits in a code-like block: a fence, an indented block or
    // a diagram inside a markdown document.
    bool inCode = false;
    bool hasSelection = false;
    bool canUndo = false;
    bool canRedo = false;
    // The caret's table context and what the table can take there.
    bool inTable = false;
    bool tableAddRowBelow = false;
    bool tableAddRowAbove = false;
    bool tableRemoveRow = false;
    bool tableAddColumnRight = false;
    bool tableAddColumnLeft = false;
    bool tableRemoveColumn = false;
    bool tableSplitCell = false;
    bool hasHeadings = false;
    // The find bar holds a query, so the two steps through the matches
    // have somewhere to go.
    bool findHasQuery = false;
    // Both default to true: a shell that has not filled them in shows a
    // working button rather than a lens that can never move.
    bool canZoomIn = true;
    bool canZoomOut = true;
};

// What a command needs. Every field is a gate the context must satisfy.
struct Requirements {
    // False when the table grew a command this header has not classified
    // yet; the test fails on it rather than shipping a live button.
    bool known = true;
    bool needsDocument = false;
    bool needsEditing = false;
    // Markdown formatting: refused in a diagram document and inside code.
    bool needsMarkdown = false;
    bool needsSelection = false;
    bool needsQuery = false;
    bool needsUndo = false;
    bool needsRedo = false;
    bool needsHeadings = false;
    // Insert Table acts OUTSIDE a table: a table may not hold a table.
    bool outsideTable = false;
    TableNeed table = TableNeed::None;
    // The zoom commands answer from the zoom range, so the button at the
    // end of the range stands down (zoom guide).
    bool zoomIn = false;
    bool zoomOut = false;
};

// The requirements of one command. Total over the command table, with
// known = false for anything it has not seen.
const Requirements& RequirementsOf(CommandId id);

// Whether the command can act in this context.
bool Enabled(CommandId id, const Context& ctx);

// Fills the document half of the context from the shared controller:
// the mode, the file kind, the caret's code and table context, the
// selection, the history, the headings. The shell fills the rest.
Context FromDocument(DocumentController& controller);

}  // namespace commands
