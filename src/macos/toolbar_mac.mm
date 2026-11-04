// The native toolbar at the top of the mac window.
//
// See toolbar_mac.h for why the mac build carries a toolbar instead of
// a ribbon. The items are deliberately few: Save, Undo, Redo, Find and
// Highlight at the leading edge, then the edit-mode switch and the
// outline toggle at the trailing edge, the arrangement document
// windows use. Everything else lives in the menu bar.
//
// Most items are system symbols, drawn by AppKit, so the toolbar
// follows the system appearance, the user's accent colour and the
// accessibility settings without any colour of our own. Save is the
// exception: SF Symbols has no classic diskette, so it is a small
// template outline drawn in the same ink. The one text button, the
// edit-mode switch, uses the standard toolbar button shape: the
// guidelines name editing as the kind of action a symbol does not
// represent well.

#import "toolbar_mac.h"

// The classic save diskette. Template images take the toolbar's tint,
// so the outline follows the system appearance like the symbols do.
static NSImage* FloppySaveImage(void) {
    NSImage* image = [NSImage
        imageWithSize:NSMakeSize(18.0, 18.0)
              flipped:YES
       drawingHandler:^BOOL(NSRect rect) {
        const CGFloat s = rect.size.width / 24.0;
        [NSColor.blackColor setFill];

        // The solid case, with the top-right corner clipped the way a
        // 3.5-inch diskette is. The shutter and the hub are holes in
        // the silhouette, not extra ink, so the shape stays readable
        // at toolbar size.
        NSBezierPath* plate = [NSBezierPath bezierPath];
        [plate moveToPoint:NSMakePoint(4.0 * s, 4.0 * s)];
        [plate lineToPoint:NSMakePoint(16.0 * s, 4.0 * s)];
        [plate lineToPoint:NSMakePoint(20.0 * s, 8.0 * s)];
        [plate lineToPoint:NSMakePoint(20.0 * s, 19.0 * s)];
        [plate lineToPoint:NSMakePoint(18.0 * s, 21.0 * s)];
        [plate lineToPoint:NSMakePoint(6.0 * s, 21.0 * s)];
        [plate lineToPoint:NSMakePoint(4.0 * s, 19.0 * s)];
        [plate closePath];
        [plate appendBezierPath:[NSBezierPath
            bezierPathWithRect:NSMakeRect(8.0 * s, 4.0 * s,
                                          8.0 * s, 4.0 * s)]];
        [plate appendBezierPath:[NSBezierPath
            bezierPathWithOvalInRect:NSMakeRect(9.0 * s, 11.0 * s,
                                                6.0 * s, 6.0 * s)]];
        [plate setWindingRule:NSEvenOddWindingRule];
        [plate fill];

        // The slot in the shutter and the spindle in the hub.
        [[NSBezierPath bezierPathWithRect:NSMakeRect(10.0 * s, 5.0 * s,
                                                     4.0 * s, 1.5 * s)] fill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(11.0 * s,
                                                          13.0 * s,
                                                          2.0 * s,
                                                          2.0 * s)] fill];
        return YES;
    }];
    [image setTemplate:YES];
    return image;
}

static NSString* const kIdSave = @"MdSave";
static NSString* const kIdShare = @"MdShare";
static NSString* const kIdUndo = @"MdUndo";
static NSString* const kIdRedo = @"MdRedo";
static NSString* const kIdFind = @"MdFind";
static NSString* const kIdHighlight = @"MdHighlight";
static NSString* const kIdEditMode = @"MdEditMode";
static NSString* const kIdOutline = @"MdOutline";

@implementation MdToolbarController {
    id _host;
    NSToolbar* _toolbar;
}

+ (instancetype)controllerWithHost:(id)host {
    MdToolbarController* controller = [[MdToolbarController alloc] init];
    if (controller) controller->_host = host;
    return controller;
}

- (instancetype)init {
    self = [super init];
    if (!self) return nil;
    _toolbar = [[NSToolbar alloc] initWithIdentifier:@"MarkDownItMainToolbar"];
    [_toolbar setDelegate:self];
    // Symbols alone, the way the system's own windows keep the
    // toolbar quiet; the labels stay for the overflow menu, the
    // customization palette and VoiceOver.
    [_toolbar setDisplayMode:NSToolbarDisplayModeIconOnly];
    [_toolbar setAllowsUserCustomization:YES];
    [_toolbar setAutosavesConfiguration:YES];
    return self;
}

- (NSToolbar*)toolbar {
    return _toolbar;
}

+ (NSArray<NSString*>*)defaultItemIdentifiers {
    return @[ kIdSave, kIdShare, kIdUndo, kIdRedo, kIdFind, kIdHighlight,
              NSToolbarFlexibleSpaceItemIdentifier, kIdEditMode, kIdOutline ];
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:
    (NSToolbar*)toolbar {
    (void)toolbar;
    return [MdToolbarController defaultItemIdentifiers];
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:
    (NSToolbar*)toolbar {
    (void)toolbar;
    NSMutableArray* allowed = [NSMutableArray
        arrayWithArray:[MdToolbarController defaultItemIdentifiers]];
    [allowed addObject:NSToolbarSpaceItemIdentifier];
    return allowed;
}

// One symbol item on the standard toolbar button, wired to the host.
- (NSToolbarItem*)itemWithIdentifier:(NSString*)identifier
                              symbol:(NSString*)symbol
                               label:(NSString*)label
                                 tip:(NSString*)tip
                              action:(SEL)action {
    NSToolbarItem* item =
        [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
    NSImage* image = [NSImage imageWithSystemSymbolName:symbol
                               accessibilityDescription:label];
    if (image) [item setImage:image];
    [item setBordered:YES];
    [item setLabel:label];
    [item setPaletteLabel:label];
    [item setToolTip:tip];
    [item setTarget:_host];
    [item setAction:action];
    // The window delegate sets the states in its toolbarSync, so
    // AppKit must not second-guess them.
    [item setAutovalidates:NO];
    return item;
}

// Save gets the classic diskette, not the download-into-a-tray
// symbol: the two actions read too much alike in this app.
- (NSToolbarItem*)saveItem {
    NSToolbarItem* item =
        [[NSToolbarItem alloc] initWithItemIdentifier:kIdSave];
    NSImage* image = FloppySaveImage();
    if (image) [item setImage:image];
    [item setBordered:YES];
    [item setLabel:@"Save"];
    [item setPaletteLabel:@"Save"];
    [item setToolTip:@"Save the document"];
    [item setTarget:_host];
    [item setAction:@selector(saveDocument:)];
    [item setAutovalidates:NO];
    return item;
}

// The edit-mode switch reads Edit while reading and Done while the
// document is being edited, the pair macOS apps use for the same
// switch. The window delegate swaps the word in its toolbarSync.
- (NSToolbarItem*)editModeItem {
    NSToolbarItem* item =
        [[NSToolbarItem alloc] initWithItemIdentifier:kIdEditMode];
    NSButton* button = [NSButton buttonWithTitle:@"Edit"
                                          target:_host
                                          action:@selector(editMode:)];
    [button setBezelStyle:NSBezelStyleTexturedRounded];
    [item setView:button];
    [item setLabel:@"Edit Mode"];
    [item setPaletteLabel:@"Edit Mode"];
    [item setToolTip:@"Edit the document, or return to reading"];
    [item setAutovalidates:NO];
    return item;
}

// The marker command is a toggle, not a one-shot action: the button
// wears its pressed state while the selection touches a marker, the way
// the Windows ribbon's Mark button does (highlighting spec 4 and 42).
- (NSToolbarItem*)highlightItem {
    NSToolbarItem* item =
        [[NSToolbarItem alloc] initWithItemIdentifier:kIdHighlight];
    NSImage* image = [NSImage imageWithSystemSymbolName:@"highlighter"
                               accessibilityDescription:@"Highlight"];
    NSButton* button = [[NSButton alloc] initWithFrame:NSZeroRect];
    [button setButtonType:NSButtonTypeToggle];
    [button setBezelStyle:NSBezelStyleTexturedRounded];
    [button setBordered:YES];
    [button setImagePosition:NSImageOnly];
    if (image) [button setImage:image];
    [button setTarget:_host];
    [button setAction:@selector(toggleMark:)];
    [button setToolTip:@"Highlight the selection"];
    [button setAccessibilityLabel:@"Highlight"];
    [item setView:button];
    [item setLabel:@"Highlight"];
    [item setPaletteLabel:@"Highlight"];
    [item setToolTip:@"Highlight the selection"];
    [item setAutovalidates:NO];
    return item;
}

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar
        itemForItemIdentifier:(NSToolbarItemIdentifier)itemIdentifier
    willBeInsertedIntoToolbar:(BOOL)flag {
    (void)toolbar;
    (void)flag;
    if ([itemIdentifier isEqualToString:kIdSave]) {
        return [self saveItem];
    }
    if ([itemIdentifier isEqualToString:kIdShare]) {
        return [self itemWithIdentifier:kIdShare
                                 symbol:@"square.and.arrow.up"
                                  label:@"Share"
                                    tip:@"Share the document"
                                 action:@selector(shareDocument:)];
    }
    if ([itemIdentifier isEqualToString:kIdUndo]) {
        return [self itemWithIdentifier:kIdUndo
                                 symbol:@"arrow.uturn.backward"
                                  label:@"Undo"
                                    tip:@"Undo the last change"
                                 action:@selector(undo:)];
    }
    if ([itemIdentifier isEqualToString:kIdRedo]) {
        return [self itemWithIdentifier:kIdRedo
                                 symbol:@"arrow.uturn.forward"
                                  label:@"Redo"
                                    tip:@"Redo the last undone change"
                                 action:@selector(redo:)];
    }
    if ([itemIdentifier isEqualToString:kIdFind]) {
        return [self itemWithIdentifier:kIdFind
                                 symbol:@"magnifyingglass"
                                  label:@"Find"
                                    tip:@"Find in the document"
                                 action:@selector(findDocument:)];
    }
    if ([itemIdentifier isEqualToString:kIdHighlight]) {
        return [self highlightItem];
    }
    if ([itemIdentifier isEqualToString:kIdOutline]) {
        return [self itemWithIdentifier:kIdOutline
                                 symbol:@"sidebar.left"
                                  label:@"Outline"
                                    tip:@"Show or hide the outline"
                                 action:@selector(toggleOutline:)];
    }
    if ([itemIdentifier isEqualToString:kIdEditMode]) {
        return [self editModeItem];
    }
    return nil;
}

@end