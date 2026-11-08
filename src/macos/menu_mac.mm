// The macOS menu bar.
//
// The commands the Windows build puts in its ribbon live here as a
// standard macOS menu bar: File, Edit, View, Format, Table, Window and
// Help, in the order a document app wears them, with the standard
// items every mac app carries: Open Recent, the Find submenu, Enter
// Full Screen, Minimize and Zoom, and the app menu's Services and Hide
// Others.
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

// Adds a submenu under the given title inside another menu.
void AddSubmenu(NSMenu* menu, const char* title, NSMenu* submenu) {
    NSMenuItem* item = [[NSMenuItem alloc]
        initWithTitle:[NSString stringWithUTF8String:title]
               action:nil
        keyEquivalent:@""];
    item.submenu = submenu;
    [menu addItem:item];
}

}  // namespace

// Builds the main menu bar.
//
// The commands reach the app delegate through the responder chain, so
// no item is wired to a target here. The window, help and services
// menus are handed to NSApp too, so the system can fill them the way
// it does for every mac app.
NSMenu* BuildMainMenu() {
    NSMenu* mainMenu = [[NSMenu alloc] init];

    // The application menu, the one that carries the name of the app.
    {
        NSMenu* appMenu = [[NSMenu alloc] init];
        [appMenu addItemWithTitle:@"About MarkDownIt"
                           action:@selector(orderFrontStandardAboutPanel:)
                    keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Settings\u2026"
                           action:@selector(openSettings:)
                    keyEquivalent:@","];
        [appMenu addItemWithTitle:@"Set as Default Reader\u2026"
                           action:@selector(setAsDefaultReader:)
                    keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];
        // The Services menu is the system's own: the app menu holds
        // the item, and the system fills the submenu.
        NSMenu* servicesMenu = [[NSMenu alloc] initWithTitle:@"Services"];
        AddSubmenu(appMenu, "Services", servicesMenu);
        [NSApp setServicesMenu:servicesMenu];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Hide MarkDownIt"
                           action:@selector(hide:)
                    keyEquivalent:@"h"];
        NSMenuItem* hideOthers = [appMenu addItemWithTitle:@"Hide Others"
                                                    action:@selector(hideOtherApplications:)
                                             keyEquivalent:@"h"];
        [hideOthers setKeyEquivalentModifierMask:NSEventModifierFlagCommand |
                                                  NSEventModifierFlagOption];
        [appMenu addItemWithTitle:@"Show All"
                           action:@selector(unhideAllApplications:)
                    keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Quit MarkDownIt"
                           action:@selector(terminate:)
                    keyEquivalent:@"q"];
        AddTopLevel(mainMenu, "MarkDownIt", appMenu);
    }

    // File.
    {
        NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
        [fileMenu addItemWithTitle:@"New"
                            action:@selector(newDocument:)
                     keyEquivalent:@"n"];
        [fileMenu addItemWithTitle:@"Open\u2026"
                            action:@selector(openDocument:)
                     keyEquivalent:@"o"];

        // Open Recent fills itself from the same list the welcome
        // screen shows, whenever it opens: the app delegate is the
        // menu's delegate, so the menu cannot go stale.
        NSMenu* recentMenu = [[NSMenu alloc] initWithTitle:@"Open Recent"];
        [recentMenu setDelegate:(id<NSMenuDelegate>)[NSApp delegate]];
        AddSubmenu(fileMenu, "Open Recent", recentMenu);

        [fileMenu addItemWithTitle:@"Reload"
                            action:@selector(reloadDocument:)
                     keyEquivalent:@"r"];
        [fileMenu addItemWithTitle:@"Close"
                            action:@selector(closeDocument:)
                     keyEquivalent:@"w"];
        [fileMenu addItemWithTitle:@"Save"
                            action:@selector(saveDocument:)
                     keyEquivalent:@"s"];
        [fileMenu addItemWithTitle:@"Save As\u2026"
                            action:@selector(saveDocumentAs:)
                     keyEquivalent:@"S"];
        [fileMenu addItemWithTitle:@"Share\u2026"
                            action:@selector(shareDocument:)
                     keyEquivalent:@""];
        [fileMenu addItem:[NSMenuItem separatorItem]];

        // The office commands sit in their own menu, the way the other
        // editors have them.
        NSMenu* officeMenu = BuildSubmenu(
            "Office",
            {{"Import from Word\u2026", @selector(importWord:)},
             {"Import from PDF\u2026", @selector(importPdf:)},
             {"Export as Word\u2026", @selector(exportWord:)},
             {"Export as PDF\u2026", @selector(exportPdf:)}},
            {"", "", "", ""});
        AddSubmenu(fileMenu, "Office", officeMenu);
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
             {"Select All", @selector(selectAll:)}},
            {"z", "Z", "x", "c", "v", "a"});
        // The standard selectors travel the responder chain, so a
        // focused text field handles its own undo and clipboard before
        // the document does (spec AC-01).
        [editMenu addItem:[NSMenuItem separatorItem]];

        // Find is a submenu, the way every mac app carries it: the
        // field, the two steps through the matches, and replace.
        NSMenu* findMenu = BuildSubmenu(
            "Find",
            {{"Find\u2026", @selector(findDocument:)},
             {"Find Next", @selector(findNext:)},
             {"Find Previous", @selector(findPrevious:)},
             {"Replace\u2026", @selector(replaceInDocument:)}},
            {"f", "g", "G", "f"});
        [[findMenu itemWithTitle:@"Find Previous"]
            setKeyEquivalentModifierMask:NSEventModifierFlagCommand |
                                        NSEventModifierFlagShift];
        // Replace opens with Cmd+Option+F, because Cmd+H belongs to
        // Hide (spec AC-11).
        [[findMenu itemWithTitle:@"Replace\u2026"]
            setKeyEquivalentModifierMask:NSEventModifierFlagCommand |
                                        NSEventModifierFlagOption];
        AddSubmenu(editMenu, "Find", findMenu);
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
             {"Edit Mode", @selector(editMode:)},
             {"Outline", @selector(toggleOutline:)},
             {"Recent Documents", @selector(showRecents:)},
             {"Show Tab Bar", @selector(toggleTabBar:)},
             {"Wrap Text", @selector(toggleWrap:)}},
            {"+", "-", "0", "", "", "O", "", "", ""});
        [viewMenu addItem:[NSMenuItem separatorItem]];
        // Full screen is a View command on the mac, with the system's
        // own key: Control, Command, F.
        NSMenuItem* fullScreen =
            [viewMenu addItemWithTitle:@"Enter Full Screen"
                                action:@selector(toggleFullScreen:)
                         keyEquivalent:@"f"];
        [fullScreen setKeyEquivalentModifierMask:NSEventModifierFlagControl |
                                                 NSEventModifierFlagCommand];
        AddTopLevel(mainMenu, "View", viewMenu);

        // Command with the plain equals key zooms in as well. On
        // most layouts the "+" above needs Shift, and a zoom key
        // must stay a command with a modifier rather than a bare
        // character, or it would eat typing (zoom guide,
        // keyboard safety). The item stays hidden; AppKit still
        // routes its key equivalent.
        NSMenuItem* zoomInPlain = [[NSMenuItem alloc]
            initWithTitle:@"Zoom In"
                   action:@selector(zoomIn:)
            keyEquivalent:@"="];
        [zoomInPlain setHidden:YES];
        [viewMenu addItem:zoomInPlain];
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
        AddSubmenu(formatMenu, "Blocks", blockMenu);
        AddTopLevel(mainMenu, "Format", formatMenu);
    }

    // Table. The commands act on the table the caret is in, and they
    // do nothing when the caret is not in one.
    {
        NSMenu* tableMenu = BuildSubmenu(
            "Table",
            {{"Insert Table\u2026", @selector(insertTable:)},
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

    // Window. NSApp gets the menu as well, so the system can append
    // the window list the way it does in every app.
    {
        NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
        [windowMenu addItemWithTitle:@"Minimize"
                              action:@selector(performMiniaturize:)
                       keyEquivalent:@"m"];
        [windowMenu addItemWithTitle:@"Zoom"
                              action:@selector(performZoom:)
                       keyEquivalent:@""];
        [windowMenu addItem:[NSMenuItem separatorItem]];
        [windowMenu addItemWithTitle:@"Bring All to Front"
                              action:@selector(arrangeInFront:)
                       keyEquivalent:@""];
        AddTopLevel(mainMenu, "Window", windowMenu);
        [NSApp setWindowsMenu:windowMenu];
    }

    // Help. NSApp gets the menu so the system can add its own help
    // item machinery.
    {
        NSMenu* helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];
        [helpMenu addItemWithTitle:@"MarkDownIt Help"
                            action:@selector(showHelp:)
                     keyEquivalent:@""];
        AddTopLevel(mainMenu, "Help", helpMenu);
        [NSApp setHelpMenu:helpMenu];
    }

    return mainMenu;
}

}  // namespace mac
