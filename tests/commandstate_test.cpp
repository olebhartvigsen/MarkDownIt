// Command state tests: which commands can act, and where.
//
// The command table names every command once. commandstate.h answers when
// each of them is available, and three surfaces read that answer: the
// Windows ribbon, the mac menu bar and the mac format bar and toolbar.
// These tests hold the answer to the three axes the design names (the
// surface, the mode and the file), and they fail when a command is added
// to the table without a requirements row, so a new button cannot ship
// live in a place where it cannot act.
//
// Build like the other portable suites:
//   g++ -std=c++17 -Wall -Wextra -I tests -I src -I third_party/md4c
//       tests/test_main.cpp tests/commandstate_test.cpp src/commandstate.cpp
//       src/documentcontroller.cpp src/parser.cpp third_party/md4c/md4c.c ...

#include "gtest_lite.h"

#include "commandstate.h"
#include "commands.h"
#include "documentcontroller.h"
#include "parser.h"

#include <string>

namespace {

// A document with no welcome screen, opened for reading.
commands::Context Reading() {
    commands::Context ctx;
    ctx.welcome = false;
    return ctx;
}

// The same document, in edit mode.
commands::Context Editing() {
    commands::Context ctx = Reading();
    ctx.editing = true;
    return ctx;
}

// A diagram file opened in edit mode: the buffer is one synthetic fence.
commands::Context Diagram() {
    commands::Context ctx = Editing();
    ctx.diagram = true;
    ctx.inCode = true;
    // A diagram document still has a history and a find bar; only the
    // markdown commands stand down.
    ctx.canUndo = true;
    ctx.findHasQuery = true;
    return ctx;
}

// The markdown commands that must stand down inside code and in a
// diagram document: the inline marks, the block commands and Insert
// Table. Undo, the clipboard, Find and the marker do not belong here.
const commands::CommandId kMarkdownCommands[] = {
    commands::CommandId::Bold,
    commands::CommandId::Italic,
    commands::CommandId::Strike,
    commands::CommandId::Code,
    commands::CommandId::Link,
    commands::CommandId::ClearFormat,
    commands::CommandId::BodyText,
    commands::CommandId::Heading1,
    commands::CommandId::Heading2,
    commands::CommandId::Heading3,
    commands::CommandId::Heading4,
    commands::CommandId::Heading5,
    commands::CommandId::Heading6,
    commands::CommandId::Bullets,
    commands::CommandId::Numbering,
    commands::CommandId::Quote,
    commands::CommandId::Indent,
    commands::CommandId::Outdent,
    commands::CommandId::InsertTable,
};

// What still works in a diagram document: the history, the clipboard,
// the files, find, the marker and the view.
const commands::CommandId kDiagramKeeps[] = {
    commands::CommandId::Save,
    commands::CommandId::Undo,
    commands::CommandId::Cut,
    commands::CommandId::Copy,
    commands::CommandId::Paste,
    commands::CommandId::Find,
    commands::CommandId::FindNext,
    commands::CommandId::Mark,
    commands::CommandId::SourceView,
    commands::CommandId::EditToggle,
    commands::CommandId::ZoomReset,
};

// A CSV table is read, filtered and copied. Every command that writes to
// the text stands down, and so does the switch to the mode that would let it.
TEST(CommandState, ATableDocumentRefusesWhatWouldWriteToIt) {
    using commands::CommandId;
    commands::Context ctx = Reading();
    ctx.tableDocument = true;

    EXPECT_FALSE(Enabled(CommandId::EditToggle, ctx));
    EXPECT_FALSE(Enabled(CommandId::Bold, ctx));
    EXPECT_FALSE(Enabled(CommandId::Heading1, ctx));
    EXPECT_FALSE(Enabled(CommandId::InsertTable, ctx));
    EXPECT_FALSE(Enabled(CommandId::AddRowAbove, ctx));
    // A table has no headings, so there is no outline to show.
    EXPECT_FALSE(Enabled(CommandId::Outline, ctx));

    // Reading a table still works: it is what the document is for.
    EXPECT_TRUE(Enabled(CommandId::ZoomIn, ctx));
    // Not Find: its match has to be drawn in the text, and a table draws
    // cells. The column filters are the search a table has.
    EXPECT_FALSE(Enabled(CommandId::Find, ctx));
    // The source view is a view, not a write: it shows the bytes the file
    // holds, which is exactly what a reader of a table may want to check.
    EXPECT_TRUE(Enabled(CommandId::SourceView, ctx));

    commands::Context selected = ctx;
    selected.hasSelection = true;
    EXPECT_TRUE(Enabled(CommandId::Copy, selected));

    // The same context with editing off and a table in the caret is not
    // enough on its own: the kind is what refuses.
    commands::Context markdown = Reading();
    markdown.tableDocument = false;
    markdown.editing = true;
    markdown.inTable = true;
    markdown.tableAddRowAbove = true;
    EXPECT_TRUE(Enabled(CommandId::AddRowAbove, markdown));
}

}  // namespace

TEST(CommandState, EveryCommandCarriesRequirements) {
    // The table grew a command: give it a row in RequirementsOf instead
    // of letting it fall through to the unknown answer (which greys it
    // everywhere, silently).
    int rows = 0;
    for (const commands::CommandSpec& spec : commands::kCommands) {
        const commands::Requirements& req = commands::RequirementsOf(spec.id);
        EXPECT_TRUE(req.known);
        ++rows;
    }
    EXPECT_TRUE(rows > 60);
}

TEST(CommandState, MutatingCommandsNeedEditMode) {
    // The invariant behind the old mac selector list and the old ribbon
    // gates: a command that changes the document never acts in view
    // mode, and never on the welcome screen.
    for (const commands::CommandSpec& spec : commands::kCommands) {
        if (!spec.mutating) continue;
        const commands::Requirements& req = commands::RequirementsOf(spec.id);
        EXPECT_TRUE(req.known);
        EXPECT_TRUE(req.needsEditing);
        EXPECT_TRUE(req.needsDocument);
    }
}

TEST(CommandState, WelcomeScreenHoldsNoDocument) {
    commands::Context welcome;  // welcome = true, nothing else
    EXPECT_FALSE(Enabled(commands::CommandId::Save, welcome));
    EXPECT_FALSE(Enabled(commands::CommandId::Reload, welcome));
    EXPECT_FALSE(Enabled(commands::CommandId::Bold, welcome));
    EXPECT_FALSE(Enabled(commands::CommandId::Find, welcome));
    EXPECT_FALSE(Enabled(commands::CommandId::ZoomIn, welcome));
    EXPECT_FALSE(Enabled(commands::CommandId::EditToggle, welcome));
    EXPECT_FALSE(Enabled(commands::CommandId::Copy, welcome));
    // The screen itself is a place to go, and the app commands stay.
    EXPECT_TRUE(Enabled(commands::CommandId::ViewRecent, welcome));
    EXPECT_TRUE(Enabled(commands::CommandId::New, welcome));
    EXPECT_TRUE(Enabled(commands::CommandId::Open, welcome));
    EXPECT_TRUE(Enabled(commands::CommandId::ImportWord, welcome));
    EXPECT_TRUE(Enabled(commands::CommandId::Settings, welcome));
    EXPECT_TRUE(Enabled(commands::CommandId::Help, welcome));
}

TEST(CommandState, ViewModeKeepsReadingAndCopying) {
    commands::Context ctx = Reading();
    ctx.hasSelection = true;
    ctx.canUndo = true;
    // Reading: find, copy, mark, zoom, the view switches, export.
    EXPECT_TRUE(Enabled(commands::CommandId::Copy, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::Find, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::Mark, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::ZoomIn, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::SourceView, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::ExportPdf, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::SaveAs, ctx));
    // Writing: every mutation stands down, save included, because in
    // view mode nothing can have changed.
    EXPECT_FALSE(Enabled(commands::CommandId::Save, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Paste, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Cut, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Bold, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Undo, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Replace, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::InsertTable, ctx));
}

TEST(CommandState, EditModeArmsTheFormatCommands) {
    commands::Context ctx = Editing();
    for (commands::CommandId id : kMarkdownCommands) {
        if (id == commands::CommandId::ClearFormat) continue;
        EXPECT_TRUE(Enabled(id, ctx));
    }
    EXPECT_TRUE(Enabled(commands::CommandId::Save, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::Paste, ctx));
    // A selection is a precondition for these two, not the mode.
    EXPECT_FALSE(Enabled(commands::CommandId::ClearFormat, ctx));
    ctx.hasSelection = true;
    EXPECT_TRUE(Enabled(commands::CommandId::ClearFormat, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::Cut, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::Copy, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::Mark, ctx));
}

TEST(CommandState, CopyAndCutNeedASelection) {
    commands::Context ctx = Editing();
    EXPECT_FALSE(Enabled(commands::CommandId::Copy, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Cut, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Mark, ctx));
    ctx.hasSelection = true;
    EXPECT_TRUE(Enabled(commands::CommandId::Copy, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::Cut, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::Mark, ctx));
}

TEST(CommandState, UndoFollowsTheHistory) {
    commands::Context ctx = Editing();
    EXPECT_FALSE(Enabled(commands::CommandId::Undo, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Redo, ctx));
    ctx.canUndo = true;
    EXPECT_TRUE(Enabled(commands::CommandId::Undo, ctx));
    ctx.canRedo = true;
    EXPECT_TRUE(Enabled(commands::CommandId::Redo, ctx));
}

TEST(CommandState, DiagramDocumentTakesTheMarkdownCommands) {
    // A standalone .mmd or .svg file is one synthetic fence around bare
    // diagram source: the buffer is text, but markdown formatting in it
    // would corrupt the picture the fence draws.
    commands::Context ctx = Diagram();
    ctx.hasSelection = true;
    for (commands::CommandId id : kMarkdownCommands) {
        EXPECT_FALSE(Enabled(id, ctx));
    }
    for (commands::CommandId id : kDiagramKeeps) {
        EXPECT_TRUE(Enabled(id, ctx));
    }
    // Editing the source text still works: the clipboard, the history
    // and replace are text operations, not markdown ones.
    EXPECT_TRUE(Enabled(commands::CommandId::Replace, ctx));
}

TEST(CommandState, CodeBlockTakesTheMarkdownCommands) {
    // The same rule inside a markdown document: a caret in a fence has
    // no markdown around it to change.
    commands::Context ctx = Editing();
    ctx.inCode = true;
    EXPECT_FALSE(Enabled(commands::CommandId::Bold, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Heading1, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Bullets, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Quote, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Indent, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::InsertTable, ctx));
    // Where each "not in code" gate must keep the other axes intact.
    ctx.inCode = false;
    EXPECT_TRUE(Enabled(commands::CommandId::Bold, ctx));
}

TEST(CommandState, TableCommandsFollowTheCaret) {
    commands::Context ctx = Editing();
    // Outside a table only Insert Table acts, and inside one it stops.
    EXPECT_TRUE(Enabled(commands::CommandId::InsertTable, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::AddRow, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::RemoveTable, ctx));
    ctx.inTable = true;
    EXPECT_FALSE(Enabled(commands::CommandId::InsertTable, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::AddRow, ctx));
    // A header row can take a row below it but cannot lose one, and the
    // last column cannot be removed.
    ctx.tableAddRowBelow = true;
    EXPECT_TRUE(Enabled(commands::CommandId::AddRow, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::AddRowAbove, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::RemoveRow, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::RemoveColumn, ctx));
    // Alignment and Delete Table act anywhere in the table.
    EXPECT_TRUE(Enabled(commands::CommandId::AlignLeft, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::AlignCenter, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::RemoveTable, ctx));
}

TEST(CommandState, FindStepsNeedAQuery) {
    commands::Context ctx = Reading();
    EXPECT_FALSE(Enabled(commands::CommandId::FindNext, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::FindPrevious, ctx));
    // Replace opens the bar where the query is typed, so it does not
    // wait for one; it waits for edit mode, which is where it can act.
    EXPECT_FALSE(Enabled(commands::CommandId::Replace, ctx));
    ctx.editing = true;
    EXPECT_TRUE(Enabled(commands::CommandId::Replace, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::FindNext, ctx));
    ctx.findHasQuery = true;
    EXPECT_TRUE(Enabled(commands::CommandId::FindNext, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::FindPrevious, ctx));
}

TEST(CommandState, ZoomStopsAtTheRange) {
    commands::Context ctx = Reading();
    ctx.canZoomIn = false;
    ctx.canZoomOut = true;
    EXPECT_FALSE(Enabled(commands::CommandId::ZoomIn, ctx));
    EXPECT_TRUE(Enabled(commands::CommandId::ZoomOut, ctx));
    ctx.canZoomIn = true;
    ctx.canZoomOut = false;
    EXPECT_TRUE(Enabled(commands::CommandId::ZoomIn, ctx));
    EXPECT_FALSE(Enabled(commands::CommandId::ZoomOut, ctx));
    // Actual Size is always on offer with a document.
    EXPECT_TRUE(Enabled(commands::CommandId::ZoomReset, ctx));
}

TEST(CommandState, OutlineNeedsHeadings) {
    commands::Context ctx = Reading();
    EXPECT_FALSE(Enabled(commands::CommandId::Outline, ctx));
    ctx.hasHeadings = true;
    EXPECT_TRUE(Enabled(commands::CommandId::Outline, ctx));
}

TEST(CommandState, FromDocumentReadsTheController) {
    // A markdown document with a fence: outside it the markdown commands
    // stand, inside it they stand down.
    DocumentController controller;
    controller.LoadFile("text\n\n```\ncode here\n```\n\nmore text\n", "a.md");
    ParseMarkdown(controller.buffer().Text(), controller.document());
    controller.SetEditing(true);

    commands::Context ctx = commands::FromDocument(controller);
    EXPECT_FALSE(ctx.welcome);
    EXPECT_TRUE(ctx.editing);
    EXPECT_FALSE(ctx.diagram);
    EXPECT_FALSE(ctx.hasSelection);
    EXPECT_FALSE(ctx.inTable);
    EXPECT_FALSE(ctx.hasHeadings);
    EXPECT_FALSE(ctx.inCode);
    EXPECT_TRUE(Enabled(commands::CommandId::Bold, ctx));

    // The caret moves into the fence.
    controller.selection().active.offset = 8;
    ctx = commands::FromDocument(controller);
    EXPECT_TRUE(ctx.inCode);
    EXPECT_FALSE(Enabled(commands::CommandId::Bold, ctx));

    // A heading gives the outline something to list.
    DocumentController headed;
    headed.LoadFile("# Title\n\ntext\n", "b.md");
    ParseMarkdown(headed.buffer().Text(), headed.document());
    headed.SetEditing(true);
    const commands::Context hctx = commands::FromDocument(headed);
    EXPECT_TRUE(hctx.hasHeadings);
    EXPECT_TRUE(Enabled(commands::CommandId::Outline, hctx));

    // A standalone .mmd file arrives wrapped in its own fence, so the
    // whole buffer is a diagram whatever the caret does.
    DocumentController diagram;
    diagram.LoadFile("flowchart TD\n    A --> B\n", "c.mmd");
    ParseMarkdown(diagram.buffer().Text(), diagram.document());
    diagram.SetEditing(true);
    const commands::Context dctx = commands::FromDocument(diagram);
    EXPECT_TRUE(dctx.diagram);
    EXPECT_FALSE(Enabled(commands::CommandId::Bold, dctx));
    EXPECT_FALSE(Enabled(commands::CommandId::Heading1, dctx));

    // A standalone .svg file is the same shape.
    DocumentController svg;
    svg.LoadFile("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>\n", "d.svg");
    ParseMarkdown(svg.buffer().Text(), svg.document());
    svg.SetEditing(true);
    const commands::Context sctx = commands::FromDocument(svg);
    EXPECT_TRUE(sctx.diagram);
    EXPECT_FALSE(Enabled(commands::CommandId::Bullets, sctx));
}
