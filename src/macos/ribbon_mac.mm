// The command strip.
//
// The Windows build runs a ribbon: a Quick Access Toolbar, an
// application menu and two tabs of groups, Home and Edit, drawn by the
// Windows Ribbon Framework. The mac build draws the same surface here:
// the same tabs, the same groups, the same commands, the same labels
// and the same icon files the Windows markup names. The layout mirrors
// src/ribbon.xml, and tools/check_mac_ribbon.py fails the build when
// the two drift apart.
//
// The look follows the ribbon instead of AppKit's default bezels: flat
// until the pointer is over a command, a soft fill while it is
// pressed, and an accent fill while a toggle is on. The File menu of
// the Windows build lives on the mac menu bar, which is where the
// system expects it.
//
// Every command is a selector on the host, so the strip and the menu
// carry out the same code. The strip asks the host what it cannot know
// itself: whether a toggle is on, and whether a command is available.
//
// One deviation, on purpose: the Windows Edit tab opens with an Edit
// toggle that switches the window between reading and editing. The mac
// app has no such split, the document is editable all the time, so
// there is nothing for that toggle to switch. The check tool carries
// the omission in its allowlist.

#include "ribbon_mac.h"
#include "fonts_mac.h"

#import <Foundation/Foundation.h>

#include <string>

// ---------------------------------------------------------------------------
// The command table, mirroring src/ribbon.xml
// ---------------------------------------------------------------------------

namespace {

struct RibbonCommand {
    const char* id;        // the command name of the Windows markup
    const char* label;     // the button text, the markup's label
    const char* selector;  // the method the host carries it out with
    const char* icon;      // <icon>_16.png and <icon>_32.png in the bundle
    bool toggle;           // shows as pressed while the command is on
    const char* tooltip;
};

struct RibbonGroup {
    const char* label;
    const RibbonCommand* commands;
    int count;
};

struct RibbonTab {
    const char* label;
    const RibbonGroup* groups;
    int count;
};

// Home.
const RibbonCommand kHomeHistory[] = {
    {"cmdUndo", "Undo", "undoEdit:", "undo", false, "Undo (\u2318Z)"},
    {"cmdRedo", "Redo", "redoEdit:", "redo", false, "Redo (\u21e7\u2318Z)"},
};
const RibbonCommand kHomeView[] = {
    {"cmdZoomIn", "Zoom In", "zoomIn:", "zoom-in", false, "Zoom In (\u2318+)"},
    {"cmdZoomOut", "Zoom Out", "zoomOut:", "zoom-out", false,
     "Zoom Out (\u2318-)"},
    {"cmdWrap", "Wrap", "toggleWrap:", "wrap", true,
     "Wrap the text to the window"},
    {"cmdOutline", "Outline", "toggleOutline:", "outline", true,
     "Toggle the outline pane"},
};
const RibbonCommand kHomeFind[] = {
    {"cmdFind", "Find", "findDocument:", "find", false, "Find (\u2318F)"},
};
const RibbonCommand kHomeMarkers[] = {
    {"cmdMark", "Highlight", "toggleMark:", "mark", true,
     "Highlight or unhighlight the selection"},
};
const RibbonGroup kHomeGroups[] = {
    {"History", kHomeHistory, 2},
    {"View", kHomeView, 4},
    {"Find", kHomeFind, 1},
    {"Highlight", kHomeMarkers, 1},
};

// Edit.
const RibbonCommand kEditEditing[] = {
    {"cmdViewSource", "View Source", "toggleSourceView:", "code", true,
     "Show the markdown source"},
    {"cmdReplace", "Replace", "replaceInDocument:", "replace", false,
     "Search and replace (\u2318H)"},
};
const RibbonCommand kEditFormat[] = {
    {"cmdBold", "Bold", "toggleBold:", "bold", true, "Bold (\u2318B)"},
    {"cmdItalic", "Italic", "toggleItalic:", "italic", true,
     "Italic (\u2318I)"},
    {"cmdStrike", "Strikethrough", "toggleStrike:", "strikethrough", true,
     "Strikethrough"},
    {"cmdCode", "Code", "toggleCode:", "code", true, "Inline code"},
    {"cmdLink", "Link", "toggleLink:", "link", false,
     "Insert a link (\u2318K)"},
    {"cmdClearFormat", "Clear Format", "clearFormat:", "clear-format", false,
     "Take the formatting away"},
};
const RibbonCommand kEditParagraph[] = {
    {"cmdBullets", "Bullets", "toggleBullets:", "bullets", true,
     "Make a bulleted list"},
    {"cmdNumbering", "Numbering", "toggleNumbering:", "numbering", true,
     "Make a numbered list"},
    {"cmdQuote", "Quote", "toggleQuote:", "quote", true, "Make a quote"},
    {"cmdIndent", "Indent", "indentBlock:", "indent", false,
     "Indent the line"},
    {"cmdOutdent", "Outdent", "outdentBlock:", "outdent", false,
     "Outdent the line"},
};
const RibbonCommand kEditHeadings[] = {
    {"cmdHeading1", "H1", "setHeading1:", "h1", true, "Heading 1 (\u23181)"},
    {"cmdHeading2", "H2", "setHeading2:", "h2", true, "Heading 2 (\u23182)"},
    {"cmdHeading3", "H3", "setHeading3:", "h3", true, "Heading 3 (\u23183)"},
};
const RibbonCommand kEditTable[] = {
    {"cmdInsertTable", "Table", "insertTable:", "table", false,
     "Insert a table"},
    {"cmdTableTools", "Table Tools", "showTableToolsMenu:", "table-tools",
     false, "Rows, columns and alignment"},
};
const RibbonGroup kEditGroups[] = {
    {"Editing", kEditEditing, 2},
    {"Format", kEditFormat, 6},
    {"Paragraph", kEditParagraph, 5},
    {"Headings", kEditHeadings, 3},
    {"Table", kEditTable, 2},
};

const RibbonTab kTabs[] = {
    {"Home", kHomeGroups, 4},
    {"Edit", kEditGroups, 5},
};

// The Quick Access Toolbar, the small strip at the top left.
const RibbonCommand kQuickAccess[] = {
    {"cmdSave", "Save", "saveDocument:", "save", false, "Save (\u2318S)"},
    {"cmdUndo", "Undo", "undoEdit:", "undo", false, "Undo (\u2318Z)"},
    {"cmdRedo", "Redo", "redoEdit:", "redo", false, "Redo (\u21e7\u2318Z)"},
};

// The menu of the Table Tools button, group for group like the markup.
const RibbonCommand kTableToolsItems[] = {
    {"cmdAddRow", "Add Row", "addRowBelow:", "table-add-row", false,
     "Add a row below the cursor"},
    {"cmdAddRowAbove", "Add Row Above", "addRowAbove:", "table-add-row-above",
     false, "Add a row above the cursor"},
    {"cmdRemoveRow", "Remove Row", "removeRow:", "table-remove-row", false,
     "Remove the row the cursor is in"},
    {"cmdAddColumn", "Add Column", "addColumnRight:", "table-add-column",
     false, "Add a column to the right"},
    {"cmdAddColumnLeft", "Add Column Left", "addColumnLeft:",
     "table-add-column-left", false, "Add a column to the left"},
    {"cmdRemoveColumn", "Remove Column", "removeColumn:",
     "table-remove-column", false, "Remove the column the cursor is in"},
    {"cmdSplitCell", "Split Cell", "splitCell:", "split-cell", false,
     "Split this cell into two"},
    {"cmdAlignLeft", "Align Left", "alignLeft:", "align-left", false,
     "Left-align this column"},
    {"cmdAlignCenter", "Align Centre", "alignCenter:", "align-center", false,
     "Centre this column"},
    {"cmdAlignRight", "Align Right", "alignRight:", "align-right", false,
     "Right-align this column"},
    {"cmdRemoveTable", "Delete Table", "removeTable:", "table-remove", false,
     "Remove the whole table the cursor is in"},
};
// The items the menu draws a separator before.
const int kTableToolsGroups[] = {0, 7, 10};

}  // namespace

// ---------------------------------------------------------------------------
// The metrics of the strip
// ---------------------------------------------------------------------------

namespace {

constexpr CGFloat kTabRowHeight = 28.0;
constexpr CGFloat kGroupRowHeight = 68.0;
constexpr CGFloat kCommandWidth = 74.0;
constexpr CGFloat kCommandHeight = 52.0;
constexpr CGFloat kLargeIcon = 32.0;
constexpr CGFloat kSmallIcon = 16.0;
constexpr CGFloat kGroupPad = 8.0;

// An icon from the bundle's Resources/icons, with the next size up as
// the 2x representation when it is there, so the strip stays crisp on
// a Retina display. The base name carries the size ("bold_32"), and
// the 2x file is "bold_64".
NSImage* RibbonIcon(NSString* base, CGFloat size) {
    NSBundle* bundle = [NSBundle mainBundle];
    NSString* oneX = [bundle pathForResource:base ofType:@"png"
                                 inDirectory:@"icons"];
    if (!oneX) return nil;
    NSImageRep* rep1 = [NSBitmapImageRep imageRepWithContentsOfFile:oneX];
    if (!rep1) return nil;
    NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(size, size)];
    rep1.size = NSMakeSize(size, size);
    [image addRepresentation:rep1];

    NSString* twoX = [base stringByReplacingOccurrencesOfString:@"_16"
                                                     withString:@"_32"];
    if ([twoX isEqualToString:base]) {
        twoX = [base stringByReplacingOccurrencesOfString:@"_32"
                                               withString:@"_64"];
    }
    NSString* path2 = [bundle pathForResource:twoX ofType:@"png"
                                  inDirectory:@"icons"];
    if (path2) {
        NSImageRep* rep2 =
            [NSBitmapImageRep imageRepWithContentsOfFile:path2];
        if (rep2) {
            rep2.size = NSMakeSize(size, size);
            [image addRepresentation:rep2];
        }
    }
    return image;
}

}  // namespace

// ---------------------------------------------------------------------------
// The buttons
// ---------------------------------------------------------------------------

// A command button that draws the ribbon's own look: flat until the
// pointer is over it, a soft fill while it is pressed, and an accent
// fill while a toggle is on. The default AppKit bezels do not have
// that look.
@interface MdRibbonButton : NSButton
@property(nonatomic, copy) NSString* commandId;
// Whether the command shows as pressed while it is on. The markup says
// which commands are toggles, and the button carries that with it.
@property(nonatomic, assign) BOOL isToggle;
@end

@implementation MdRibbonButton {
    NSTrackingArea* tracking_;
    BOOL hover_;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (tracking_) [self removeTrackingArea:tracking_];
    tracking_ = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
             options:(NSTrackingMouseEnteredAndExited |
                      NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect)
               owner:self
            userInfo:nil];
    [self addTrackingArea:tracking_];
}

- (void)mouseEntered:(NSEvent*)event {
    (void)event;
    hover_ = YES;
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    hover_ = NO;
    [self setNeedsDisplay:YES];
}

- (void)highlight:(BOOL)flag {
    [super highlight:flag];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    const NSRect inner = NSInsetRect(self.bounds, 1.0, 1.0);
    NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:inner
                                                        xRadius:4.0
                                                        yRadius:4.0];
    const BOOL on = (self.state == NSControlStateValueOn);
    const BOOL pressed = [self isHighlighted];
    NSColor* accent = [NSColor controlAccentColor];
    if (on && self.enabled) {
        [[accent colorWithAlphaComponent:(pressed ? 0.34 : 0.20)] setFill];
        [path fill];
        [[accent colorWithAlphaComponent:0.40] setStroke];
        [path setLineWidth:1.0];
        [path stroke];
    } else if (pressed && self.enabled) {
        [[[NSColor labelColor] colorWithAlphaComponent:0.14] setFill];
        [path fill];
    } else if (hover_ && self.enabled) {
        [[[NSColor labelColor] colorWithAlphaComponent:0.07] setFill];
        [path fill];
    }
    [super drawRect:dirtyRect];
}

@end

// A tab of the strip: plain text, filled while it is the active one,
// with the accent line along its bottom edge the way the ribbon has
// it.
@interface MdRibbonTabButton : NSButton
@property(nonatomic, assign) BOOL active;
@end

@implementation MdRibbonTabButton {
    NSTrackingArea* tracking_;
    BOOL hover_;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (tracking_) [self removeTrackingArea:tracking_];
    tracking_ = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
             options:(NSTrackingMouseEnteredAndExited |
                      NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect)
               owner:self
            userInfo:nil];
    [self addTrackingArea:tracking_];
}

- (void)mouseEntered:(NSEvent*)event {
    (void)event;
    hover_ = YES;
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    hover_ = NO;
    [self setNeedsDisplay:YES];
}

- (void)highlight:(BOOL)flag {
    [super highlight:flag];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    if (self.active) {
        [[NSColor windowBackgroundColor] setFill];
        [[NSBezierPath bezierPathWithRect:NSInsetRect(self.bounds, 1.0, 1.0)]
            fill];
        [[NSColor controlAccentColor] setFill];
        [[NSBezierPath bezierPathWithRect:NSMakeRect(
            NSMinX(self.bounds) + 2.0, NSMinY(self.bounds) + 0.5,
            NSWidth(self.bounds) - 4.0, 2.0)] fill];
    } else if (([self isHighlighted] || hover_) && self.enabled) {
        [[[NSColor labelColor] colorWithAlphaComponent:0.07] setFill];
        [[NSBezierPath bezierPathWithRect:NSInsetRect(self.bounds, 1.0, 1.0)]
            fill];
    }
    [super drawRect:dirtyRect];
}

@end

// A group, which draws the separator on its right edge the way the
// ribbon marks the boundaries between groups.
@interface MdRibbonGroupView : NSView
@end

@implementation MdRibbonGroupView
- (BOOL)isFlipped {
    return YES;
}
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[[NSColor separatorColor] colorWithAlphaComponent:0.7] setFill];
    [[NSBezierPath bezierPathWithRect:NSMakeRect(
        NSWidth(self.bounds) - 1.0, 4.0, 1.0, NSHeight(self.bounds) - 14.0)]
        fill];
}
@end

// ---------------------------------------------------------------------------
// The strip
// ---------------------------------------------------------------------------

@interface MdRibbonView () <NSMenuDelegate>
@property(nonatomic, weak) id<MdRibbonHost> host;
@end

@implementation MdRibbonView {
    NSMutableArray<MdRibbonButton*>* commandButtons_;
    NSMutableArray<MdRibbonTabButton*>* tabButtons_;
    NSMutableArray<NSView*>* tabPages_;
    NSMutableArray<NSMutableArray<MdRibbonGroupView*>*>* pageGroups_;
    NSMutableArray<MdRibbonButton*>* quickAccessButtons_;
    NSMenu* tableToolsMenu_;
    NSUInteger activeTab_;
    CGFloat qatEndX_;
}

+ (CGFloat)ribbonHeight {
    return kTabRowHeight + kGroupRowHeight;
}

+ (MdRibbonView*)ribbonWithHost:(id<MdRibbonHost>)host {
    MdRibbonView* view = [[MdRibbonView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, 900.0, [self ribbonHeight])];
    view.host = host;
    [view buildStrip];
    return view;
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)isOpaque {
    return YES;
}

- (void)buildStrip {
    commandButtons_ = [NSMutableArray array];
    tabButtons_ = [NSMutableArray array];
    tabPages_ = [NSMutableArray array];
    pageGroups_ = [NSMutableArray array];
    quickAccessButtons_ = [NSMutableArray array];
    activeTab_ = 0;

    // The Quick Access Toolbar, the small strip the Windows build keeps
    // in its caption.
    for (int i = 0; i < 3; ++i) {
        MdRibbonButton* button = [self buttonForCommand:kQuickAccess[i]
                                                   size:kSmallIcon
                                              withLabel:NO];
        [quickAccessButtons_ addObject:button];
        [self addSubview:button];
    }

    // The tab row.
    for (int i = 0; i < 2; ++i) {
        MdRibbonTabButton* button =
            [[MdRibbonTabButton alloc] initWithFrame:NSZeroRect];
        [button setButtonType:NSButtonTypeMomentaryPushIn];
        [button setBordered:NO];
        [button setTitle:[NSString stringWithUTF8String:kTabs[i].label]];
        [button setFont:mac::BundledUIFont(12.0f)];
        [button setTarget:self];
        [button setAction:@selector(selectTab:)];
        [button setTag:i];
        [button setActive:(i == 0)];
        [tabButtons_ addObject:button];
        [self addSubview:button];
    }

    // The page of each tab: its groups, each one a cluster of command
    // buttons with the group label underneath.
    for (int t = 0; t < 2; ++t) {
        NSView* page = [[NSView alloc] initWithFrame:NSZeroRect];
        [page setHidden:(t != 0)];
        NSMutableArray<MdRibbonGroupView*>* groups = [NSMutableArray array];
        for (int g = 0; g < kTabs[t].count; ++g) {
            const RibbonGroup& group = kTabs[t].groups[g];
            const CGFloat width =
                2.0 * kGroupPad + group.count * kCommandWidth +
                (group.count - 1) * 2.0;
            MdRibbonGroupView* groupView = [[MdRibbonGroupView alloc]
                initWithFrame:NSMakeRect(0.0, 0.0, width, kGroupRowHeight)];
            for (int c = 0; c < group.count; ++c) {
                MdRibbonButton* button =
                    [self buttonForCommand:group.commands[c]
                                      size:kLargeIcon
                                 withLabel:YES];
                [button setFrame:NSMakeRect(
                    kGroupPad + c * (kCommandWidth + 2.0), 2.0,
                    kCommandWidth, kCommandHeight)];
                [groupView addSubview:button];
            }
            NSTextField* label = [NSTextField
                labelWithString:[NSString stringWithUTF8String:group.label]];
            [label setFont:mac::BundledUIFont(10.0f)];
            [label setTextColor:[NSColor secondaryLabelColor]];
            [label setAlignment:NSTextAlignmentCenter];
            [label setFrame:NSMakeRect(0.0, kCommandHeight + 4.0, width, 12.0)];
            [groupView addSubview:label];
            [groups addObject:groupView];
            [page addSubview:groupView];
        }
        [tabPages_ addObject:page];
        [pageGroups_ addObject:groups];
        [self addSubview:page];
    }
}

// One command button. The Table Tools dropdown runs its own menu;
// every other command lands on the host.
- (MdRibbonButton*)buttonForCommand:(const RibbonCommand&)command
                               size:(CGFloat)size
                          withLabel:(BOOL)withLabel {
    MdRibbonButton* button = [[MdRibbonButton alloc] initWithFrame:NSZeroRect];
    button.commandId = [NSString stringWithUTF8String:command.id];
    button.isToggle = command.toggle;
    [button setButtonType:(command.toggle ? NSButtonTypePushOnPushOff
                                          : NSButtonTypeMomentaryPushIn)];
    [button setBordered:NO];
    [button setImagePosition:(withLabel ? NSImageAbove : NSImageOnly)];
    [button setAlignment:NSTextAlignmentCenter];
    [button setFont:mac::BundledUIFont(withLabel ? 11.0f : 10.0f)];
    NSString* base = [NSString stringWithFormat:@"%s_%.0f", command.icon, size];
    NSImage* image = RibbonIcon(base, size);
    if (image) [button setImage:image];
    [button setToolTip:[NSString stringWithUTF8String:command.tooltip]];
    if (withLabel) {
        [button setTitle:[NSString stringWithUTF8String:command.label]];
    } else {
        // An icon-only button still needs a name for a screen reader.
        [button setAccessibilityLabel:
            [NSString stringWithUTF8String:command.label]];
    }

    const std::string selector(command.selector);
    if (selector == "showTableToolsMenu:") {
        [button setTarget:self];
        [button setAction:@selector(showTableToolsMenu:)];
    } else {
        [button setTarget:self.host];
        [button setAction:NSSelectorFromString(
            [NSString stringWithUTF8String:command.selector])];
    }
    [commandButtons_ addObject:button];
    return button;
}

// The Table Tools dropdown.
- (void)showTableToolsMenu:(id)sender {
    NSButton* button = (NSButton*)sender;
    [tableToolsMenu_ popUpMenuPositioningItem:nil
                                   atLocation:NSMakePoint(0.0, 0.0)
                                       inView:button];
}

- (void)buildTableToolsMenu {
    tableToolsMenu_ = [[NSMenu alloc] initWithTitle:@"Table Tools"];
    [tableToolsMenu_ setAutoenablesItems:NO];
    [tableToolsMenu_ setDelegate:self];
    int nextGroup = 1;
    for (int i = 0; i < 11; ++i) {
        if (nextGroup < 3 && i == kTableToolsGroups[nextGroup]) {
            [tableToolsMenu_ addItem:[NSMenuItem separatorItem]];
            ++nextGroup;
        }
        const RibbonCommand& command = kTableToolsItems[i];
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithUTF8String:command.label]
                   action:NSSelectorFromString(
                              [NSString stringWithUTF8String:command.selector])
            keyEquivalent:@""];
        [item setTag:i];
        [tableToolsMenu_ addItem:item];
    }
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
    (void)menu;
    id<MdRibbonHost> host = self.host;
    for (NSMenuItem* item in [tableToolsMenu_ itemArray]) {
        if ([item isSeparatorItem]) continue;
        const RibbonCommand& command = kTableToolsItems[[item tag]];
        NSString* commandId = [NSString stringWithUTF8String:command.id];
        [item setEnabled:(host ? [host ribbonCommandAvailable:commandId]
                               : YES)];
    }
}

- (void)selectTab:(id)sender {
    const NSUInteger index = (NSUInteger)[(NSButton*)sender tag];
    if (index >= tabPages_.count) return;
    activeTab_ = index;
    for (NSUInteger i = 0; i < tabPages_.count; ++i) {
        [[tabPages_ objectAtIndex:i] setHidden:(i != index)];
        [[tabButtons_ objectAtIndex:i] setActive:(i == index)];
        [[tabButtons_ objectAtIndex:i] setNeedsDisplay:YES];
    }
    [self setNeedsDisplay:YES];
}

- (void)layout {
    [super layout];
    const CGFloat width = NSWidth(self.bounds);

    CGFloat x = 10.0;
    for (MdRibbonButton* button in quickAccessButtons_) {
        [button setFrame:NSMakeRect(x, 3.0, 26.0, 22.0)];
        x += 28.0;
    }
    qatEndX_ = x + 3.0;
    x += 9.0;

    for (MdRibbonTabButton* button in tabButtons_) {
        const NSSize fitted = [button intrinsicContentSize];
        const CGFloat buttonWidth = MAX(64.0, fitted.width + 26.0);
        [button setFrame:NSMakeRect(x, 1.0, buttonWidth, kTabRowHeight - 2.0)];
        x += buttonWidth + 2.0;
    }

    for (NSUInteger t = 0; t < tabPages_.count; ++t) {
        NSView* page = [tabPages_ objectAtIndex:t];
        [page setFrame:NSMakeRect(0.0, kTabRowHeight, width, kGroupRowHeight)];
        CGFloat groupX = 10.0;
        NSMutableArray<MdRibbonGroupView*>* groups =
            [pageGroups_ objectAtIndex:t];
        for (NSUInteger g = 0; g < groups.count; ++g) {
            MdRibbonGroupView* groupView = [groups objectAtIndex:g];
            [groupView setFrame:NSMakeRect(groupX, 0.0,
                                           NSWidth([groupView frame]),
                                           kGroupRowHeight)];
            groupX += NSWidth([groupView frame]) + 8.0;
        }
    }
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[NSColor windowBackgroundColor] setFill];
    [[NSBezierPath bezierPathWithRect:self.bounds] fill];

    // The line under the tab row, broken under the active tab so the
    // tab reads as attached to the page below it.
    const NSRect active = (activeTab_ < tabButtons_.count
                               ? [[tabButtons_ objectAtIndex:activeTab_] frame]
                               : NSZeroRect);
    [[NSColor separatorColor] setFill];
    const CGFloat lineY = kTabRowHeight - 1.0;
    if (NSWidth(active) > 0.0) {
        [[NSBezierPath bezierPathWithRect:NSMakeRect(0.0, lineY,
                                                     NSMinX(active), 1.0)] fill];
        [[NSBezierPath bezierPathWithRect:NSMakeRect(
            NSMaxX(active), lineY, NSWidth(self.bounds) - NSMaxX(active),
            1.0)] fill];
    } else {
        [[NSBezierPath bezierPathWithRect:NSMakeRect(0.0, lineY,
                                                     NSWidth(self.bounds),
                                                     1.0)] fill];
    }

    // The separator between the quick access tools and the tabs.
    [[[NSColor separatorColor] colorWithAlphaComponent:0.8] setFill];
    [[NSBezierPath bezierPathWithRect:NSMakeRect(qatEndX_, 4.0, 1.0,
                                                 kTabRowHeight - 9.0)] fill];

    // The bottom line of the strip.
    [[NSBezierPath bezierPathWithRect:NSMakeRect(0.0, NSHeight(self.bounds) - 1.0,
                                                 NSWidth(self.bounds), 1.0)]
        fill];
}

- (void)refresh {
    id<MdRibbonHost> host = self.host;
    if (!host) return;
    if (!tableToolsMenu_) [self buildTableToolsMenu];
    for (MdRibbonButton* button in commandButtons_) {
        NSString* commandId = button.commandId;
        [button setEnabled:[host ribbonCommandAvailable:commandId]];
        if (button.isToggle) {
            [button setState:([host ribbonCommandOn:commandId]
                                 ? NSControlStateValueOn
                                 : NSControlStateValueOff)];
        }
        [button setNeedsDisplay:YES];
    }
    [self setNeedsDisplay:YES];
}

@end
