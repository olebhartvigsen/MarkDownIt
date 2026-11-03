// The native toolbar at the top of the mac window.
//
// See toolbar_mac.h for why the mac build carries a toolbar instead of
// a ribbon. The items are deliberately few: Save, Undo, Redo and Find
// at the leading edge, then the edit-mode switch and the outline
// toggle at the trailing edge, the arrangement document windows use.
// Everything else lives in the menu bar.
//
// Every symbol is a system symbol, drawn by AppKit, so the toolbar
// follows the system appearance, the user's accent colour and the
// accessibility settings without any colour of our own. The one text
// button, the edit-mode switch, uses the standard toolbar button
// shape: the guidelines name editing as the kind of action a symbol
// does not represent well.

#import "toolbar_mac.h"

static NSString* const kIdSave = @"MdSave";
static NSString* const kIdShare = @"MdShare";
static NSString* const kIdUndo = @"MdUndo";
static NSString* const kIdRedo = @"MdRedo";
static NSString* const kIdFind = @"MdFind";
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
    return @[ kIdSave, kIdShare, kIdUndo, kIdRedo, kIdFind,
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

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar
        itemForItemIdentifier:(NSToolbarItemIdentifier)itemIdentifier
    willBeInsertedIntoToolbar:(BOOL)flag {
    (void)toolbar;
    (void)flag;
    if ([itemIdentifier isEqualToString:kIdSave]) {
        return [self itemWithIdentifier:kIdSave
                                 symbol:@"square.and.arrow.down"
                                  label:@"Save"
                                    tip:@"Save the document"
                                 action:@selector(saveDocument:)];
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