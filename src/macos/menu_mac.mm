// The macOS menu bar.
//
// The commands the Windows build puts in its ribbon live here as a
// standard macOS menu bar: File, Edit, View, Format and Table.
//
// The items carry no target of their own. AppKit walks the responder
// chain, which ends at the app delegate, and calls the command there.
// The responder chain is also what enables the items, so an item is grey
// exactly when nothing carries its command out. A hand made target that
// does not implement the selectors is what left every item grey.

#include "menu_mac.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

// Builds a submenu with the given items.
//
// The items are given as title, selector and key equivalent triples.
NSMenu* BuildSubmenu(const char* title,
                     const std::vector<std::pair<const char*, SEL>>& items,
                     const std::vector<const char*>& shortcuts) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:title]];
    for (size_t i = 0; i < items.size(); ++i) {
        NSString* itemTitle = [NSString stringWithUTF8String:items[i].first];
        NSString* shortcut = @"";
        if (i < shortcuts.size() && shortcuts[i]) {
            shortcut = [NSString stringWithUTF8String:shortcuts[i]];
        }
        [menu addItemWithTitle:itemTitle action:items[i].second
                keyEquivalent:shortcut];
    }
    return menu;
}

namespace mac {

namespace {

// Adds a top level menu to the bar.
//
// The title goes on the item itself, because an item without a title
// does not name its menu in the menu bar.
void AddTopLevel(NSMenu* mainMenu, const char* title, NSMenu* submenu) {
    NSMenuItem* item = [[NSMenuItem alloc]
        initWithTitle:[NSString stringWithUTF8String:title]
               action:nil
        keyEquivalent:@""];
    item.submenu = submenu;
    [mainMenu addItem:item];
}

}  // namespace

// Builds the main menu bar.
//
// The commands reach the app delegate through the responder chain, so
// no item is wired to a target here.
NSMenu* BuildMainMenu() {
    NSMenu* mainMenu = [[NSMenu alloc] init];

    // The application menu, the one that carries the name of the app.
    {
        NSMenu* appMenu = [[NSMenu alloc] init];
        [appMenu addItemWithTitle:@"About MarkDownIt"
                           action:@selector(orderFrontStandardAboutPanel:)
                    keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Settings..."
                           action:@selector(openSettings:)
                    keyEquivalent:@","];
        [appMenu addItemWithTitle:@"Set as Default Reader..."
                           action:@selector(setAsDefaultReader:)
                    keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Hide MarkDownIt"
                           action:@selector(hide:)
                    keyEquivalent:@"h"];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Quit MarkDownIt"
                           action:@selector(terminate:)
                    keyEquivalent:@"q"];
        AddTopLevel(mainMenu, "MarkDownIt", appMenu);
    }

    // File.
    {
        NSMenu* fileMenu = BuildSubmenu(
            "File",
            {{"New", @selector(newDocument:)},
             {"Open...", @selector(openDocument:)},
             {"Reload", @selector(reloadDocument:)},
             {"Close", @selector(closeDocument:)},
             {"Save", @selector(saveDocument:)},
             {"Save As...", @selector(saveDocumentAs:)}},
            {"n", "o", "", "w", "s", "S"});

        // The office commands sit in their own menu, the way the other
        // editors have them.
        NSMenu* officeMenu = BuildSubmenu(
            "Office",
            {{"Import from Word...", @selector(importWord:)},
             {"Import from PDF...", @selector(importPdf:)},
             {"Export as Word...", @selector(exportWord:)},
             {"Export as PDF...", @selector(exportPdf:)}},
            {"", "", "", ""});
        NSMenuItem* officeItem =
            [[NSMenuItem alloc] initWithTitle:@"Office"
                                       action:nil
                                keyEquivalent:@""];
        officeItem.submenu = officeMenu;
        [fileMenu addItem:officeItem];
        AddTopLevel(mainMenu, "File", fileMenu);
    }

    // Edit.
    {
        NSMenu* editMenu = BuildSubmenu(
            "Edit",
            {{"Undo", @selector(undo:)},
             {"Redo", @selector(redo:)},
             {"Cut", @selector(cut:)},
             {"Copy", @selector(copy:)},
             {"Paste", @selector(paste:)},
             {"Select All", @selector(selectAll:)},
             {"Find...", @selector(findDocument:)},
             {"Replace...", @selector(replaceInDocument:)}},
            {"z", "Z", "x", "c", "v", "a", "f", ""});
        // The standard selectors travel the responder chain, so a
        // focused text field handles its own undo and clipboard before
        // the document does (spec AC-01). Replace opens with
        // Cmd+Option+F, because Cmd+H belongs to Hide (spec AC-11).
        [[editMenu itemWithTitle:@"Replace..."] setKeyEquivalent:@"f"];
        [[editMenu itemWithTitle:@"Replace..."]
            setKeyEquivalentModifierMask:NSEventModifierFlagCommand |
                                        NSEventModifierFlagOption];
        AddTopLevel(mainMenu, "Edit", editMenu);
    }

    // View.
    {
        NSMenu* viewMenu = BuildSubmenu(
            "View",
            {{"Zoom In", @selector(zoomIn:)},
             {"Zoom Out", @selector(zoomOut:)},
             {"Actual Size", @selector(zoomReset:)},
             {"Source View", @selector(toggleSourceView:)},
             {"Outline", @selector(toggleOutline:)},
             {"Wrap Text", @selector(toggleWrap:)},
             {"Wider Content", @selector(cycleContentWidth:)}},
            {"+", "-", "0", "", "", "", ""});
        AddTopLevel(mainMenu, "View", viewMenu);
    }

    // Format.
    {
        NSMenu* formatMenu = BuildSubmenu(
            "Format",
            {{"Bold", @selector(toggleBold:)},
             {"Italic", @selector(toggleItalic:)},
             {"Strikethrough", @selector(toggleStrike:)},
             {"Code", @selector(toggleCode:)},
             {"Link", @selector(toggleLink:)},
             {"Highlight", @selector(toggleMark:)},
             {"Clear Formatting", @selector(clearFormat:)}},
            {"b", "i", "", "", "k", "h", ""});
        // Marking carries the Windows shortcut with Command on the
        // mac: Ctrl+Shift+H becomes Cmd+Shift+H (highlighting guide
        // 20).
        [[formatMenu itemWithTitle:@"Highlight"]
            setKeyEquivalentModifierMask:NSEventModifierFlagCommand |
                                        NSEventModifierFlagShift];
        // The block commands live in their own menu, the way the Format
        // menu of the other editors has them.
        NSMenu* blockMenu = BuildSubmenu(
            "Blocks",
            {{"Heading 1", @selector(setHeading1:)},
             {"Heading 2", @selector(setHeading2:)},
             {"Heading 3", @selector(setHeading3:)},
             {"Bulleted List", @selector(toggleBullets:)},
             {"Numbered List", @selector(toggleNumbering:)},
             {"Quote", @selector(toggleQuote:)},
             {"Indent", @selector(indentBlock:)},
             {"Outdent", @selector(outdentBlock:)}},
            {"1", "2", "3", "", "", "", "", ""});
        NSMenuItem* blockItem =
            [[NSMenuItem alloc] initWithTitle:@"Blocks"
                                       action:nil
                                keyEquivalent:@""];
        blockItem.submenu = blockMenu;
        [formatMenu addItem:blockItem];
        AddTopLevel(mainMenu, "Format", formatMenu);
    }

    // Table. The commands act on the table the caret is in, and they
    // do nothing when the caret is not in one.
    {
        NSMenu* tableMenu = BuildSubmenu(
            "Table",
            {{"Insert Table...", @selector(insertTable:)},
             {"Add Row Above", @selector(addRowAbove:)},
             {"Add Row Below", @selector(addRowBelow:)},
             {"Remove Row", @selector(removeRow:)},
             {"Add Column Left", @selector(addColumnLeft:)},
             {"Add Column Right", @selector(addColumnRight:)},
             {"Remove Column", @selector(removeColumn:)},
             {"Split Cell", @selector(splitCell:)},
             {"Align Left", @selector(alignLeft:)},
             {"Align Center", @selector(alignCenter:)},
             {"Align Right", @selector(alignRight:)},
             {"Delete Table", @selector(removeTable:)}},
            {"", "", "", "", "", "", "", "", "", "", "", ""});
        AddTopLevel(mainMenu, "Table", tableMenu);
    }

    return mainMenu;
}

}  // namespace mac
