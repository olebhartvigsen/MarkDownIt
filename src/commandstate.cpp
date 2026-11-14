// The command state: one answer for the ribbon, the menu bar, the format
// bar and the toolbar (see commandstate.h for the three axes).

#include "commandstate.h"

#include "documentcontroller.h"
#include "navigation.h"

namespace commands {

namespace {

// A row helper keeps the table below one line per command.
constexpr Requirements Doc(bool editing = false) {
    Requirements r;
    r.needsDocument = true;
    r.needsEditing = editing;
    return r;
}

constexpr Requirements Markdown(bool selection = false) {
    Requirements r = Doc(true);
    r.needsMarkdown = true;
    r.needsSelection = selection;
    return r;
}

constexpr Requirements Table(TableNeed need) {
    Requirements r = Doc(true);
    r.table = need;
    return r;
}

constexpr Requirements Always() { return Requirements{}; }

constexpr Requirements Zoom(bool in) {
    Requirements r = Doc();
    r.zoomIn = in;
    r.zoomOut = !in;
    return r;
}

}  // namespace

const Requirements& RequirementsOf(CommandId id) {
    static const Requirements kUnknown = [] {
        Requirements r;
        r.known = false;
        return r;
    }();
    static const Requirements kAlways = Always();
    static const Requirements kDoc = Doc();
    static const Requirements kEditing = Doc(true);
    static const Requirements kCopy = [] {
        Requirements r = Doc();
        r.needsSelection = true;
        return r;
    }();
    static const Requirements kCut = [] {
        Requirements r = Doc(true);
        r.needsSelection = true;
        return r;
    }();
    static const Requirements kUndo = [] {
        Requirements r = Doc(true);
        r.needsUndo = true;
        return r;
    }();
    static const Requirements kRedo = [] {
        Requirements r = Doc(true);
        r.needsRedo = true;
        return r;
    }();
    static const Requirements kInsertTable = [] {
        Requirements r = Doc(true);
        r.needsMarkdown = true;
        r.outsideTable = true;
        return r;
    }();
    static const Requirements kFindStep = [] {
        Requirements r = Doc();
        r.needsQuery = true;
        return r;
    }();
    static const Requirements kMark = [] {
        Requirements r = Doc();
        r.needsSelection = true;
        return r;
    }();
    static const Requirements kOutline = [] {
        Requirements r = Doc();
        r.needsHeadings = true;
        return r;
    }();
    static const Requirements kZoomIn = Zoom(true);
    static const Requirements kZoomOut = Zoom(false);
    static const Requirements kBold = Markdown();
    static const Requirements kClearFormat = Markdown(true);
    // One entry per TableNeed, in declaration order, so the table rows
    // below can name the need and return the requirements behind it.
    static const Requirements kTables[] = {
        Table(TableNeed::None),
        Table(TableNeed::Any),
        Table(TableNeed::AddRowBelow),
        Table(TableNeed::AddRowAbove),
        Table(TableNeed::RemoveRow),
        Table(TableNeed::AddColumnRight),
        Table(TableNeed::AddColumnLeft),
        Table(TableNeed::RemoveColumn),
        Table(TableNeed::SplitCell),
    };

    switch (id) {
    // File. New, Open and the two imports bring a document into being,
    // so they answer on the welcome screen; the rest need one.
    case CommandId::New:
    case CommandId::Open:
    case CommandId::ImportWord:
    case CommandId::ImportPdf:
        return kAlways;
    case CommandId::Reload:
    case CommandId::SaveAs:
    case CommandId::Close:
    case CommandId::Share:
    case CommandId::ExportWord:
    case CommandId::ExportPdf:
        return kDoc;
    case CommandId::Save:
        return kEditing;

    // History and clipboard. Undo and redo need history on top of the
    // mode; copy needs a selection in either mode, cut needs both.
    case CommandId::Undo:
        return kUndo;
    case CommandId::Redo:
        return kRedo;
    case CommandId::Cut:
        return kCut;
    case CommandId::Copy:
        return kCopy;
    case CommandId::Paste:
        return kEditing;
    case CommandId::SelectAll:
        return kDoc;

    // Inline format: markdown, edit mode, and a selection for the two
    // commands that have nothing to act on without one.
    case CommandId::Bold:
    case CommandId::Italic:
    case CommandId::Strike:
    case CommandId::Code:
    case CommandId::Link:
        return kBold;
    case CommandId::ClearFormat:
        return kClearFormat;

    // Blocks: headings, lists, quote, indent.
    case CommandId::BodyText:
    case CommandId::Heading1:
    case CommandId::Heading2:
    case CommandId::Heading3:
    case CommandId::Heading4:
    case CommandId::Heading5:
    case CommandId::Heading6:
    case CommandId::Bullets:
    case CommandId::Numbering:
    case CommandId::Quote:
    case CommandId::Indent:
    case CommandId::Outdent:
        return kBold;

    // View. The edit switch and the source view need a document; the
    // outline needs headings in it; the zoom answers from its range.
    case CommandId::EditToggle:
    case CommandId::SourceView:
    case CommandId::Wrap:
    case CommandId::WidthMenu:
    case CommandId::WidthStandard:
    case CommandId::Width960:
    case CommandId::Width1600:
    case CommandId::WidthFull:
    case CommandId::ZoomReset:
        return kDoc;
    case CommandId::Outline:
        return kOutline;
    case CommandId::ZoomIn:
        return kZoomIn;
    case CommandId::ZoomOut:
        return kZoomOut;
    case CommandId::ViewRecent:
    case CommandId::TabBar:
        return kAlways;

    // Find. The two steps need a query; replace also needs edit mode
    // and the other two need a document. The marker works in both
    // modes, a selection is enough (highlighting guide 4).
    case CommandId::Find:
        return kDoc;
    case CommandId::FindNext:
    case CommandId::FindPrevious:
        return kFindStep;
    case CommandId::Replace:
        // The bar is where a query is typed, so Replace opens it in
        // edit mode whatever the query says. The two buttons inside the
        // bar follow the matches, and the bar greys its replace half
        // while the page is read (find guide 4).
        return kEditing;
    case CommandId::Mark:
        return kMark;

    // Tables. Insert Table acts outside one; the rest act inside the
    // table the caret is in, narrowed by what that table can take.
    case CommandId::InsertTable:
        return kInsertTable;
    case CommandId::AddRow:
        return kTables[static_cast<size_t>(TableNeed::AddRowBelow)];
    case CommandId::AddRowAbove:
        return kTables[static_cast<size_t>(TableNeed::AddRowAbove)];
    case CommandId::RemoveRow:
        return kTables[static_cast<size_t>(TableNeed::RemoveRow)];
    case CommandId::AddColumn:
        return kTables[static_cast<size_t>(TableNeed::AddColumnRight)];
    case CommandId::AddColumnLeft:
        return kTables[static_cast<size_t>(TableNeed::AddColumnLeft)];
    case CommandId::RemoveColumn:
        return kTables[static_cast<size_t>(TableNeed::RemoveColumn)];
    case CommandId::SplitCell:
        return kTables[static_cast<size_t>(TableNeed::SplitCell)];
    case CommandId::AlignLeft:
    case CommandId::AlignCenter:
    case CommandId::AlignRight:
    case CommandId::RemoveTable:
        return kTables[static_cast<size_t>(TableNeed::Any)];

    // App menu: always, they are about the app and not the document.
    case CommandId::About:
    case CommandId::AssocMd:
    case CommandId::Settings:
    case CommandId::Help:
        return kAlways;
    }
    return kUnknown;
}

bool Enabled(CommandId id, const Context& ctx) {
    const Requirements& req = RequirementsOf(id);
    if (!req.known) return false;
    if (req.needsDocument && ctx.welcome) return false;
    if (req.needsEditing && (ctx.welcome || !ctx.editing)) return false;
    if (req.needsMarkdown && (ctx.diagram || ctx.inCode)) return false;
    if (req.needsSelection && !ctx.hasSelection) return false;
    if (req.needsQuery && !ctx.findHasQuery) return false;
    if (req.needsUndo && !ctx.canUndo) return false;
    if (req.needsRedo && !ctx.canRedo) return false;
    if (req.needsHeadings && !ctx.hasHeadings) return false;
    if (req.outsideTable && ctx.inTable) return false;
    if (req.zoomIn && !ctx.canZoomIn) return false;
    if (req.zoomOut && !ctx.canZoomOut) return false;
    switch (req.table) {
    case TableNeed::None:
        break;
    case TableNeed::Any:
        if (!ctx.inTable) return false;
        break;
    case TableNeed::AddRowBelow:
        if (!ctx.inTable || !ctx.tableAddRowBelow) return false;
        break;
    case TableNeed::AddRowAbove:
        if (!ctx.inTable || !ctx.tableAddRowAbove) return false;
        break;
    case TableNeed::RemoveRow:
        if (!ctx.inTable || !ctx.tableRemoveRow) return false;
        break;
    case TableNeed::AddColumnRight:
        if (!ctx.inTable || !ctx.tableAddColumnRight) return false;
        break;
    case TableNeed::AddColumnLeft:
        if (!ctx.inTable || !ctx.tableAddColumnLeft) return false;
        break;
    case TableNeed::RemoveColumn:
        if (!ctx.inTable || !ctx.tableRemoveColumn) return false;
        break;
    case TableNeed::SplitCell:
        if (!ctx.inTable || !ctx.tableSplitCell) return false;
        break;
    }
    return true;
}

Context FromDocument(DocumentController& controller) {
    Context ctx;
    ctx.welcome = false;
    ctx.editing = controller.IsEditing();
    ctx.diagram = controller.InDiagramDocument();
    ctx.inCode = controller.CaretInCodeBlock();
    ctx.hasSelection = !controller.selection().Empty();
    ctx.canUndo = controller.undo().CanUndo();
    ctx.canRedo = controller.undo().CanRedo();
    // The outline needs one heading, not the list: a scan that stops at
    // the first one costs a walk of the node list and no allocation, and
    // the ribbon asks this for every button it redraws.
    ctx.hasHeadings = false;
    for (const Node& node : controller.document().nodes) {
        if (node.block == BlockKind::Heading) {
            ctx.hasHeadings = true;
            break;
        }
    }
    ctx.inTable = controller.CaretInTable();
    if (ctx.inTable) {
        const TableCapabilities caps = controller.CaretTableCapabilities();
        ctx.tableAddRowBelow = caps.addRowBelow;
        ctx.tableAddRowAbove = caps.addRowAbove;
        ctx.tableRemoveRow = caps.removeRow;
        ctx.tableAddColumnRight = caps.addColumnRight;
        ctx.tableAddColumnLeft = caps.addColumnLeft;
        ctx.tableRemoveColumn = caps.removeColumn;
        ctx.tableSplitCell = caps.splitCell;
    }
    return ctx;
}

}  // namespace commands
