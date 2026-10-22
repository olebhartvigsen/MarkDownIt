// The macOS menu bar.
//
// The commands the Windows build puts in its ribbon live here as a
// standard macOS menu bar: File, Edit, View and Format.
//
// The menu targets the app through the MenuActions interface, so the
// menu does not know anything about the app object.

#include "menu_mac.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

// The menu target.
//
// AppKit sends the menu actions to an object that responds to the
// selectors. This class holds the MenuActions and implements the
// selectors, which keeps the Objective-C part small and out of the rest
// of the app.
//
// NSMenuItem holds its target weakly, so the target is kept alive here
// for as long as the menu is installed.
@interface MdMenuTarget : NSObject
- (instancetype)initWithActions:(mac::MenuActions*)actions;
- (void)run:(SEL)selector;
@end

// The installed menu target, so it outlives the menu.
static MdMenuTarget* gMenuTarget = nil;

@implementation MdMenuTarget {
    mac::MenuActions* actions_;
}

- (instancetype)initWithActions:(mac::MenuActions*)actions {
    self = [super init];
    if (self) {
        actions_ = actions;
    }
    return self;
}

- (void)run:(SEL)selector {
    if (!actions_) return;

    if (selector == @selector(newDocument:)) {
        actions_->OnNewDocument();
    } else if (selector == @selector(openDocument:)) {
        actions_->OnOpen();
    } else if (selector == @selector(saveDocument:)) {
        actions_->OnSave();
    } else if (selector == @selector(saveDocumentAs:)) {
        actions_->OnSaveAs();
    } else if (selector == @selector(closeDocument:)) {
        actions_->OnClose();
    } else if (selector == @selector(undoEdit:)) {
        actions_->OnUndo();
    } else if (selector == @selector(redoEdit:)) {
        actions_->OnRedo();
    } else if (selector == @selector(cutSelection:)) {
        actions_->OnCut();
    } else if (selector == @selector(copySelection:)) {
        actions_->OnCopy();
    } else if (selector == @selector(pasteSelection:)) {
        actions_->OnPaste();
    } else if (selector == @selector(selectAll:)) {
        actions_->OnSelectAll();
    } else if (selector == @selector(zoomIn:)) {
        actions_->OnZoomIn();
    } else if (selector == @selector(zoomOut:)) {
        actions_->OnZoomOut();
    } else if (selector == @selector(zoomReset:)) {
        actions_->OnZoomReset();
    } else if (selector == @selector(toggleSourceView:)) {
        actions_->OnToggleSourceView();
    } else if (selector == @selector(toggleOutline:)) {
        actions_->OnToggleOutline();
    } else if (selector == @selector(setAsDefaultReader:)) {
        actions_->OnSetAsDefaultReader();
    } else if (selector == @selector(importWord:)) {
        actions_->OnImportWord();
    } else if (selector == @selector(exportWord:)) {
        actions_->OnExportWord();
    } else if (selector == @selector(exportPdf:)) {
        actions_->OnExportPdf();
    } else if (selector == @selector(insertTable:)) {
        actions_->OnInsertTable();
    } else if (selector == @selector(addRowBelow:)) {
        actions_->OnAddTableRow(true);
    } else if (selector == @selector(addRowAbove:)) {
        actions_->OnAddTableRow(false);
    } else if (selector == @selector(removeRow:)) {
        actions_->OnRemoveTableRow();
    } else if (selector == @selector(addColumnRight:)) {
        actions_->OnAddTableColumn(true);
    } else if (selector == @selector(addColumnLeft:)) {
        actions_->OnAddTableColumn(false);
    } else if (selector == @selector(removeColumn:)) {
        actions_->OnRemoveTableColumn();
    } else if (selector == @selector(splitCell:)) {
        actions_->OnSplitTableCell();
    } else if (selector == @selector(alignLeft:)) {
        actions_->OnAlignTableColumn(0);
    } else if (selector == @selector(alignCenter:)) {
        actions_->OnAlignTableColumn(1);
    } else if (selector == @selector(alignRight:)) {
        actions_->OnAlignTableColumn(2);
    } else if (selector == @selector(toggleBold:)) {
        actions_->OnToggleBold();
    } else if (selector == @selector(toggleItalic:)) {
        actions_->OnToggleItalic();
    } else if (selector == @selector(toggleStrike:)) {
        actions_->OnToggleStrike();
    } else if (selector == @selector(setHeading1:)) {
        actions_->OnHeading(1);
    } else if (selector == @selector(setHeading2:)) {
        actions_->OnHeading(2);
    } else if (selector == @selector(setHeading3:)) {
        actions_->OnHeading(3);
    } else if (selector == @selector(toggleBullets:)) {
        actions_->OnToggleBullets();
    } else if (selector == @selector(toggleNumbering:)) {
        actions_->OnToggleNumbering();
    } else if (selector == @selector(toggleQuote:)) {
        actions_->OnToggleQuote();
    } else if (selector == @selector(indentBlock:)) {
        actions_->OnIndent();
    } else if (selector == @selector(outdentBlock:)) {
        actions_->OnOutdent();
    } else if (selector == @selector(toggleCode:)) {
        actions_->OnToggleCode();
    } else if (selector == @selector(toggleMark:)) {
        actions_->OnToggleMark();
    } else if (selector == @selector(clearFormat:)) {
        actions_->OnClearFormat();
    } else if (selector == @selector(toggleLink:)) {
        actions_->OnToggleLink();
    }
}

@end

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

// Builds the main menu bar and installs it.
//
// The target is kept alive by the menu, because the menu holds a
// reference to it.
NSMenu* BuildMainMenu(MenuActions* actions) {
    MdMenuTarget* target = [[MdMenuTarget alloc] initWithActions:actions];

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

        NSMenuItem* appItem = [[NSMenuItem alloc] init];
        appItem.submenu = appMenu;
        [mainMenu addItem:appItem];
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
             {"Export as Word...", @selector(exportWord:)},
             {"Export as PDF...", @selector(exportPdf:)}},
            {"", "", ""});
        NSMenuItem* officeItem =
            [[NSMenuItem alloc] initWithTitle:@"Office"
                                       action:nil
                                keyEquivalent:@""];
        officeItem.submenu = officeMenu;
        [fileMenu addItem:officeItem];
        [mainMenu addItem:[[NSMenuItem alloc] init]];
        [[mainMenu itemArray] lastObject].submenu = fileMenu;
    }

    // Edit.
    {
        NSMenu* editMenu = BuildSubmenu(
            "Edit",
            {{"Undo", @selector(undoEdit:)},
             {"Redo", @selector(redoEdit:)},
             {"Cut", @selector(cutSelection:)},
             {"Copy", @selector(copySelection:)},
             {"Paste", @selector(pasteSelection:)},
             {"Select All", @selector(selectAll:)},
             {"Find...", @selector(findDocument:)},
             {"Replace...", @selector(replaceInDocument:)}},
            {"z", "Z", "x", "c", "v", "a", "f", "h"});
        [mainMenu addItem:[[NSMenuItem alloc] init]];
        [[mainMenu itemArray] lastObject].submenu = editMenu;
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
        [mainMenu addItem:[[NSMenuItem alloc] init]];
        [[mainMenu itemArray] lastObject].submenu = viewMenu;
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
            {"b", "i", "", "", "k", "", ""});
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

        [mainMenu addItem:[[NSMenuItem alloc] init]];
        [[mainMenu itemArray] lastObject].submenu = formatMenu;
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
             {"Align Right", @selector(alignRight:)}},
            {"", "", "", "", "", "", "", "", "", "", ""});
        [mainMenu addItem:[[NSMenuItem alloc] init]];
        [[mainMenu itemArray] lastObject].submenu = tableMenu;
    }

    // Every item forwards to the target.
    for (NSMenuItem* item in [mainMenu itemArray]) {
        NSMenu* submenu = [item submenu];
        if (!submenu) continue;
        for (NSMenuItem* child in [submenu itemArray]) {
            if (child.action) {
                child.target = target;
            }
        }
    }

    // The menu items hold the target weakly, so it is kept here.
    gMenuTarget = target;

    return mainMenu;
}

}  // namespace mac
