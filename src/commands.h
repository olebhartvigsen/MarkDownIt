// The shared command table (plan 4.3, one table for every port).
//
// Every editing command the app answers is named once here: the
// command id, the name the menus show it under, the ribbon.xml
// command it rides on Windows, the mac action selector it rides on
// macOS, whether it changes the document, and the keyboard
// accelerators both ports give it.
//
// The ids come from the ledger command table
// (references/platform-parity.md) and src/ribbon.xml. The mutating
// flags come from the mac menu gate (the actionChangesTheDocument
// list in main_mac.mm before this table existed) united with the
// Windows edit-mode gates, so both ports now read one answer for
// "does this command change the document".
//
// The table is load-bearing, not documentation (ribbon-menu guide,
// acceptance E): the Windows keyboard cluster in AppWindow::OnKeyDown
// looks its accelerators up here, and the macOS menu bar
// (menu_mac.mm) reads the key equivalents of its command items from
// the same rows. The ids, ribbon command names and selectors are the
// ones the coverage checker (tools/check_command_coverage.py) and
// the parity ledger already track, so nothing outside this header
// needed to change name.
//
// Windows keys are raw virtual key codes (winuser.h values) rather
// than VK_ macros, so this header compiles on macOS and Windows
// alike; the codes are spelled out in the comments.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace commands {

enum class CommandId : uint8_t {
    // File.
    New, Open, Reload, Save, SaveAs, Close,
    // History and clipboard.
    Undo, Redo, Cut, Copy, Paste, SelectAll,
    // Inline format.
    Bold, Italic, Strike, Code, Link, ClearFormat,
    // Block format: headings and lists.
    BodyText, Heading1, Heading2, Heading3, Heading4, Heading5, Heading6,
    Bullets, Numbering, Quote, Indent, Outdent,
    // View.
    EditToggle, SourceView, Wrap, Outline,
    ZoomIn, ZoomOut, ZoomReset, ViewRecent, Share, TabBar, WidthMenu,
    WidthStandard, Width960, Width1600, WidthFull,
    // Find.
    Find, FindNext, FindPrevious, Replace, Mark,
    // Table.
    InsertTable, AddRow, AddRowAbove, RemoveRow, AddColumn,
    AddColumnLeft, RemoveColumn, SplitCell,
    AlignLeft, AlignCenter, AlignRight, RemoveTable,
    // Office.
    ImportWord, ImportPdf, ExportWord, ExportPdf,
    // App menu.
    About, AssocMd, Settings, Help,
};

// A Windows keyboard accelerator: the virtual key code plus what the
// modifiers must be. 1 means the modifier is required, 0 means it
// must be off, -1 means the key works whatever that modifier does
// (F5 keeps that shape: it reloads however it is pressed).
struct WinAccelerator {
    uint16_t vk;
    int8_t ctrl;
    int8_t shift;
};

// A mac key equivalent: the character and the extra modifiers next
// to Command. An upper case letter already implies Shift on the mac,
// so the Save As row carries "S" without the shift flag and stays
// what AppKit built; only rows whose key needs a modifier the
// character itself does not spell out carry a flag.
struct MacShortcut {
    const char* key;
    bool shift;
    bool option;
};

// One command row.
struct CommandSpec {
    CommandId id;
    // The name the menus and the ledger know the command by.
    const char* name;
    // The ribbon.xml command name, null when Windows has no button.
    const char* ribbon;
    // The ribbon command number the framework hands back in
    // UpdateProperty, or 0 when Windows carries no button. It is the Id
    // attribute of the ribbon.xml command, the number UICC turns into the
    // IDC_CMD_ symbol, so the ribbon finds the row the table already
    // holds instead of keeping a second command list.
    uint16_t ribbonId;
    // The mac action selector with its colon, null when the mac
    // carries no item (the selector comes without the colon in
    // actionCompare helpers, both spellings resolve).
    const char* selector;
    // True when the command changes the document text.
    bool mutating;
    WinAccelerator win;
    MacShortcut mac;
};

// Binding helpers keep the rows one line each.
inline constexpr WinAccelerator NoWin{0, 0, 0};  // vk 0: no key
inline constexpr MacShortcut NoMac{nullptr, false, false};
inline constexpr WinAccelerator Ctrl(uint16_t vk) { return {vk, 1, 0}; }
inline constexpr WinAccelerator CtrlShift(uint16_t vk) { return {vk, 1, 1}; }
inline constexpr WinAccelerator AnyMods(uint16_t vk) { return {vk, -1, -1}; }
inline constexpr MacShortcut Key(const char* key) { return {key, false, false}; }
inline constexpr MacShortcut ShiftKey(const char* key) {
    return {key, true, false};
}
inline constexpr MacShortcut OptKey(const char* key) {
    return {key, false, true};
}

// ---------------------------------------------------------------------------
// The table. Order follows the menus: file, history, inline format,
// blocks, view, find, table, office, app.
// ---------------------------------------------------------------------------
inline constexpr CommandSpec kCommands[] = {
    // File. New and Open answer from the early accelerators in
    // OnKeyDown, so their Windows keys stay there this wave (the look
    // of the mac menu comes from these rows).
    {CommandId::New, "New", "cmdNew", 10105, "newDocument:", false, NoWin, Key("n")},
    {CommandId::Open, "Open", "cmdOpen", 10100, "openDocument:", false, NoWin, Key("o")},
    {CommandId::Reload, "Reload", "cmdReload", 10101, "reloadDocument:", false,
     AnyMods(0x74 /* VK_F5 */), Key("r")},
    {CommandId::Save, "Save", "cmdSave", 10103, "saveDocument:", false,
     Ctrl(0x53 /* S */), Key("s")},
    {CommandId::SaveAs, "Save As", "cmdSaveAs", 10104, "saveDocumentAs:", false,
     CtrlShift(0x53 /* S */), Key("S")},
    {CommandId::Close, "Close", nullptr, 0, "closeDocument:", false,
     NoWin, Key("w")},

    // History and clipboard. Undo and redo change the document; cut
    // and paste do, copy and select all do not.
    {CommandId::Undo, "Undo", "cmdUndo", 10700, "undo:", true,
     Ctrl(0x5A /* Z */), Key("z")},
    {CommandId::Redo, "Redo", "cmdRedo", 10701, "redo:", true,
     CtrlShift(0x5A /* Z */), Key("Z")},
    {CommandId::Cut, "Cut", nullptr, 0, "cut:", true,
     Ctrl(0x58 /* X */), Key("x")},
    {CommandId::Copy, "Copy", nullptr, 0, "copy:", false,
     Ctrl(0x43 /* C */), Key("c")},
    {CommandId::Paste, "Paste", nullptr, 0, "paste:", true,
     Ctrl(0x56 /* V */), Key("v")},
    {CommandId::SelectAll, "Select All", nullptr, 0, "selectAll:", false,
     Ctrl(0x41 /* A */), Key("a")},

    // Inline format.
    {CommandId::Bold, "Bold", "cmdBold", 10300, "toggleBold:", true,
     Ctrl(0x42 /* B */), Key("b")},
    {CommandId::Italic, "Italic", "cmdItalic", 10301, "toggleItalic:", true,
     Ctrl(0x49 /* I */), Key("i")},
    {CommandId::Strike, "Strikethrough", "cmdStrike", 10302, "toggleStrike:", true,
     CtrlShift(0x58 /* X */), NoMac},
    {CommandId::Code, "Code", "cmdCode", 10303, "toggleCode:", true,
     Ctrl(0xC0 /* VK_OEM_3, backtick */), NoMac},
    {CommandId::Link, "Link", "cmdLink", 10304, "toggleLink:", true,
     Ctrl(0x4B /* K */), Key("k")},
    {CommandId::ClearFormat, "Clear Formatting", "cmdClearFormat", 10305,
     "clearFormat:", true, NoWin, NoMac},

    // Blocks: headings and lists. Every level is a command of its own.
    // Windows carries levels 1 to 3 on the ribbon and reaches 4 to 6
    // through accelerators; the mac menu and format bar offer all six.
    {CommandId::BodyText, "Body Text", nullptr, 0, "setBodyText:", true,
     NoWin, NoMac},
    {CommandId::Heading1, "Heading 1", "cmdHeading1", 10400, "setHeading1:", true,
     Ctrl(0x31 /* 1 */), Key("1")},
    {CommandId::Heading2, "Heading 2", "cmdHeading2", 10401, "setHeading2:", true,
     Ctrl(0x32 /* 2 */), Key("2")},
    {CommandId::Heading3, "Heading 3", "cmdHeading3", 10402, "setHeading3:", true,
     Ctrl(0x33 /* 3 */), Key("3")},
    {CommandId::Heading4, "Heading 4", nullptr, 0, "setHeading4:", true,
     Ctrl(0x34 /* 4 */), NoMac},
    {CommandId::Heading5, "Heading 5", nullptr, 0, "setHeading5:", true,
     Ctrl(0x35 /* 5 */), NoMac},
    {CommandId::Heading6, "Heading 6", nullptr, 0, "setHeading6:", true,
     Ctrl(0x36 /* 6 */), NoMac},
    // The lists keep the Windows keys with Command in place of Control,
    // so a reader who knows one window knows the other; quote follows
    // the period key its Windows accelerator names.
    {CommandId::Bullets, "Bulleted List", "cmdBullets", 10403, "toggleBullets:",
     true, CtrlShift(0x38 /* 8 */), ShiftKey("8")},
    {CommandId::Numbering, "Numbered List", "cmdNumbering", 10404,
     "toggleNumbering:", true, CtrlShift(0x37 /* 7 */), ShiftKey("7")},
    {CommandId::Quote, "Quote", "cmdQuote", 10405, "toggleQuote:", true,
     CtrlShift(0xBE /* VK_OEM_PERIOD */), ShiftKey(".")},
    {CommandId::Indent, "Indent", "cmdIndent", 10406, "indentBlock:", true,
     NoWin, NoMac},
    {CommandId::Outdent, "Outdent", "cmdOutdent", 10407, "outdentBlock:", true,
     NoWin, NoMac},

    // View.
    {CommandId::EditToggle, "Edit Mode", "cmdEditToggle", 10500, "editMode:",
     false, NoWin, NoMac},
    {CommandId::SourceView, "Source View", "cmdViewSource", 10702,
     "toggleSourceView:", false, NoWin, NoMac},
    {CommandId::Wrap, "Wrap Text", "cmdWrap", 10200, "toggleWrap:", false,
     NoWin, NoMac},
    {CommandId::Outline, "Outline", "cmdOutline", 10606, "toggleOutline:", false,
     NoWin, Key("O")},
    {CommandId::ZoomIn, "Zoom In", "cmdZoomIn", 10201, "zoomIn:", false,
     NoWin, Key("+")},
    {CommandId::ZoomOut, "Zoom Out", "cmdZoomOut", 10202, "zoomOut:", false,
     NoWin, Key("-")},
    {CommandId::ZoomReset, "Actual Size", nullptr, 0, "zoomReset:", false,
     NoWin, Key("0")},
    {CommandId::ViewRecent, "Recent Documents", "cmdRecent", 10106,
     "showRecents:",
     false, NoWin, NoMac},
    {CommandId::Share, "Share", nullptr, 0, "shareDocument:", false,
     NoWin, NoMac},
    {CommandId::TabBar, "Show Tab Bar", nullptr, 0, "toggleTabBar:", false,
     NoWin, NoMac},
    {CommandId::WidthMenu, "Reading Width", "cmdWidthMenu", 10605, nullptr, false,
     NoWin, NoMac},
    {CommandId::WidthStandard, "Standard", "cmdWidthStandard", 10601, nullptr,
     false, NoWin, NoMac},
    {CommandId::Width960, "960 px", "cmdWidth960", 10602, nullptr, false,
     NoWin, NoMac},
    {CommandId::Width1600, "1600 px", "cmdWidth1600", 10603, nullptr, false,
     NoWin, NoMac},
    {CommandId::WidthFull, "Full Width", "cmdWidthFull", 10604, nullptr, false,
     NoWin, NoMac},

    // Find. Windows answers Ctrl+F and Ctrl+H from the early
    // accelerators in OnKeyDown; the mac keys come from here. Replace
    // opens with Cmd+Option+F, because Cmd+H belongs to Hide (spec
    // AC-11); Find Previous needs the explicit Shift next to its
    // upper case G.
    {CommandId::Find, "Find", "cmdFind", 10703, "findDocument:", false,
     NoWin, Key("f")},
    {CommandId::FindNext, "Find Next", nullptr, 0, "findNext:", false,
     NoWin, Key("g")},
    {CommandId::FindPrevious, "Find Previous", nullptr, 0, "findPrevious:",
     false, NoWin, ShiftKey("G")},
    {CommandId::Replace, "Replace", "cmdReplace", 10704, "replaceInDocument:",
     false, NoWin, OptKey("f")},
    {CommandId::Mark, "Highlight", "cmdMark", 10900, "toggleMark:", false,
     NoWin, ShiftKey("h")},

    // Table. They act on the table the caret is in and do nothing
    // outside one, so they are all mutating.
    {CommandId::InsertTable, "Insert Table", "cmdInsertTable", 10210,
     "insertTable:", true, NoWin, NoMac},
    {CommandId::AddRow, "Add Row Below", "cmdAddRow", 10212, "addRowBelow:", true,
     NoWin, NoMac},
    {CommandId::AddRowAbove, "Add Row Above", "cmdAddRowAbove", 10217,
     "addRowAbove:", true, NoWin, NoMac},
    {CommandId::RemoveRow, "Remove Row", "cmdRemoveRow", 10213, "removeRow:", true,
     NoWin, NoMac},
    {CommandId::AddColumn, "Add Column Right", "cmdAddColumn", 10214,
     "addColumnRight:", true, NoWin, NoMac},
    {CommandId::AddColumnLeft, "Add Column Left", "cmdAddColumnLeft", 10218,
     "addColumnLeft:", true, NoWin, NoMac},
    {CommandId::RemoveColumn, "Remove Column", "cmdRemoveColumn", 10215,
     "removeColumn:", true, NoWin, NoMac},
    {CommandId::SplitCell, "Split Cell", "cmdSplitCell", 10223, "splitCell:",
     true, NoWin, NoMac},
    {CommandId::AlignLeft, "Align Left", "cmdAlignLeft", 10220, "alignLeft:",
     true, NoWin, NoMac},
    {CommandId::AlignCenter, "Align Center", "cmdAlignCenter", 10221,
     "alignCenter:", true, NoWin, NoMac},
    {CommandId::AlignRight, "Align Right", "cmdAlignRight", 10222, "alignRight:",
     true, NoWin, NoMac},
    {CommandId::RemoveTable, "Delete Table", "cmdRemoveTable", 10216,
     "removeTable:", true, NoWin, NoMac},

    // Office.
    {CommandId::ImportWord, "Import from Word", "cmdImportDocx", 10800,
     "importWord:", false, NoWin, NoMac},
    {CommandId::ImportPdf, "Import from PDF", nullptr, 0, "importPdf:",
     false, NoWin, NoMac},
    {CommandId::ExportWord, "Export as Word", "cmdExportDocx", 10801,
     "exportWord:", false, NoWin, NoMac},
    {CommandId::ExportPdf, "Export as PDF", "cmdExportPdf", 10802, "exportPdf:",
     false, NoWin, NoMac},

    // App menu.
    {CommandId::About, "About", "cmdAbout", 10102, "orderFrontStandardAboutPanel:",
     false, NoWin, NoMac},
    {CommandId::AssocMd, "Set as Default Reader", "cmdAssocMd", 10600,
     "setAsDefaultReader:", false, NoWin, NoMac},
    {CommandId::Settings, "Settings", nullptr, 0, "openSettings:", false,
     NoWin, Key(",")},
    {CommandId::Help, "Help", nullptr, 0, "showHelp:", false, NoWin, NoMac},
};

// A second key for a command that already carries one. The Windows
// redo pair keeps both shapes, and the hidden mac zoom item shares
// the ZoomIn selector with the plain equals key (zoom guide,
// keyboard safety).
struct CommandAlias {
    CommandId id;
    WinAccelerator win;
    MacShortcut mac;
};

inline constexpr CommandAlias kAliases[] = {
    // Ctrl+Y redoes whatever Shift does (the ladder this replaces
    // tested Ctrl alone on that key).
    {CommandId::Redo, WinAccelerator{0x59 /* Y */, 1, -1}, NoMac},
    {CommandId::ZoomIn, NoWin, Key("=")},
};

// ---------------------------------------------------------------------------
// Lookups.
// ---------------------------------------------------------------------------

inline const CommandSpec* Find(CommandId id) {
    for (const CommandSpec& spec : kCommands) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

// Resolves a mac selector with or without its trailing colon, so
// both the menu wiring (sel_getName carries the colon) and string
// comparisons against the ledger spelling work.
inline const CommandSpec* FindBySelector(const char* selector) {
    if (!selector) return nullptr;
    const size_t len = std::strlen(selector);
    const bool colon = len > 0 && selector[len - 1] == ':';
    for (const CommandSpec& spec : kCommands) {
        if (!spec.selector) continue;
        if (std::strcmp(spec.selector, selector) == 0) return &spec;
        if (!colon) {
            // Compare against the row without its colon.
            const size_t rlen = std::strlen(spec.selector);
            if (rlen == len + 1 && spec.selector[rlen - 1] == ':' &&
                std::strncmp(spec.selector, selector, len) == 0) {
                return &spec;
            }
        }
    }
    return nullptr;
}

// Resolves the number the ribbon framework hands back in UpdateProperty
// to the row that carries it, so the ribbon button and the mac menu item
// ask the same command state about the same command. Null for the
// framework's own commands (tabs, groups, the QAT), which carry no row.
inline const CommandSpec* FindByRibbonId(uint32_t ribbonId) {
    if (ribbonId == 0) return nullptr;
    for (const CommandSpec& spec : kCommands) {
        if (spec.ribbonId == ribbonId) return &spec;
    }
    return nullptr;
}

inline const CommandSpec* FindByRibbon(const char* ribbon) {
    if (!ribbon) return nullptr;
    for (const CommandSpec& spec : kCommands) {
        if (spec.ribbon && std::strcmp(spec.ribbon, ribbon) == 0) {
            return &spec;
        }
    }
    return nullptr;
}

// Whether the mac action changes the document: the menu gate of
// main_mac.mm reads this instead of a selector list of its own.
inline bool MutatingSelector(const char* selector) {
    const CommandSpec* spec = FindBySelector(selector);
    return spec && spec->mutating;
}

inline bool WinMatches(const WinAccelerator& acc, uint32_t vk, bool ctrl,
                       bool shift) {
    if (acc.vk == 0 || acc.vk != vk) return false;
    if (acc.ctrl >= 0 && acc.ctrl != (ctrl ? 1 : 0)) return false;
    if (acc.shift >= 0 && acc.shift != (shift ? 1 : 0)) return false;
    return true;
}

// The command behind a key press, or null when the table carries no
// binding for it. This is the whole dispatch contract the key
// handler needs: the rows above say which modifiers a key requires,
// and a key whose modifiers differ from every row answers nothing,
// exactly what the case ladder it replaced did.
inline const CommandSpec* MatchWinAccelerator(uint32_t vk, bool ctrl,
                                              bool shift) {
    for (const CommandSpec& spec : kCommands) {
        if (WinMatches(spec.win, vk, ctrl, shift)) return &spec;
    }
    for (const CommandAlias& alias : kAliases) {
        if (WinMatches(alias.win, vk, ctrl, shift)) return Find(alias.id);
    }
    return nullptr;
}

// The level behind a heading command, level 1 to 6; false for any
// other command.
inline bool HeadingLevel(CommandId id, int* level) {
    if (id < CommandId::Heading1 || id > CommandId::Heading6) return false;
    *level = static_cast<int>(id) - static_cast<int>(CommandId::Heading1) + 1;
    return true;
}

// The alias key a command answers to on the mac besides the one in
// its row, or null when it has none.
inline const char* MacAliasKey(CommandId id) {
    for (const CommandAlias& alias : kAliases) {
        if (alias.id == id && alias.mac.key) return alias.mac.key;
    }
    return nullptr;
}

}  // namespace commands
