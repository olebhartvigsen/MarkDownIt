#import "format_bar_mac.h"

#include "documentcontroller.h"  // FormatState

// A family of controls that never take the keyboard. Clicking the bar
// must leave the reader typing in the page: a control that accepted the
// first responder would take the caret, the arrows and every keystroke
// with it, the same way a ribbon click did on the other port.
@interface MdBarButton : NSButton
@end

@implementation MdBarButton
- (BOOL)acceptsFirstResponder { return NO; }
- (BOOL)refusesFirstResponder { return YES; }
@end

@interface MdBarSegmented : NSSegmentedControl
@end

@implementation MdBarSegmented
- (BOOL)acceptsFirstResponder { return NO; }
- (BOOL)refusesFirstResponder { return YES; }
@end

@interface MdBarPopup : NSPopUpButton
@end

@implementation MdBarPopup
- (BOOL)acceptsFirstResponder { return NO; }
- (BOOL)refusesFirstResponder { return YES; }
@end

// The hairline that separates one group of controls from the next.
@interface MdBarRule : NSView
@end

@implementation MdBarRule
- (NSSize)intrinsicContentSize { return NSMakeSize(1.0, 18.0); }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[NSColor separatorColor] setFill];
    NSRectFill(NSMakeRect(NSMidX(self.bounds), 0.0, 1.0, NSHeight(self.bounds)));
}
@end

@interface MdFormatBar ()
- (void)headingStyleChanged:(id)sender;
- (void)inlinePressed:(id)sender;
- (void)blockPressed:(id)sender;
- (void)linkPressed:(id)sender;
- (void)tablePressed:(id)sender;
- (void)clearPressed:(id)sender;
- (void)sendCommand:(SEL)action;
@end

namespace {

// A system symbol, or nil when the running macOS does not know the
// name; every caller keeps a worded fallback, so no control is ever
// blank.
NSImage* BarSymbol(NSString* name, NSString* label) {
    return [NSImage imageWithSystemSymbolName:name
                     accessibilityDescription:label];
}

NSButton* BarButton(NSString* symbol, NSString* label, id target, SEL action) {
    MdBarButton* button = [[MdBarButton alloc] initWithFrame:NSZeroRect];
    [button setTranslatesAutoresizingMaskIntoConstraints:NO];
    NSImage* image = BarSymbol(symbol, label);
    if (image) {
        [button setImage:image];
        [button setImagePosition:NSImageOnly];
    } else {
        [button setTitle:label];
    }
    [button setBezelStyle:NSBezelStyleAccessoryBarAction];
    [button setTarget:target];
    [button setAction:action];
    [button setToolTip:label];
    [button setAccessibilityLabel:label];
    return button;
}

MdBarSegmented* BarSegments(NSArray<NSString*>* symbols,
                            NSArray<NSString*>* labels,
                            id target, SEL action) {
    MdBarSegmented* control = [[MdBarSegmented alloc] initWithFrame:NSZeroRect];
    [control setTranslatesAutoresizingMaskIntoConstraints:NO];
    [control setSegmentCount:(NSInteger)symbols.count];
    // Momentary: a segment presses and lets go. The pressed state the
    // reader sees is the caret's, pushed back in after every change, so
    // the row can never show a mark the document does not have.
    [control setTrackingMode:NSSegmentSwitchTrackingMomentary];
    [control setControlSize:NSControlSizeSmall];
    [control setSegmentStyle:NSSegmentStyleAutomatic];
    [control setTarget:target];
    [control setAction:action];
    for (NSInteger i = 0; i < (NSInteger)symbols.count; ++i) {
        NSImage* image = BarSymbol(symbols[i], labels[i]);
        if (image) {
            [control setImage:image forSegment:i];
            [control setImageScaling:NSImageScaleProportionallyDown forSegment:i];
        } else {
            [control setLabel:labels[i] forSegment:i];
        }
        [control setWidth:30.0 forSegment:i];
        [control setToolTip:labels[i] forSegment:i];
    }
    return control;
}

}  // namespace

@implementation MdFormatBar {
    MdBarPopup* style_;
    MdBarSegmented* inline_;
    MdBarSegmented* block_;
    NSButton* link_;
    NSButton* table_;
    NSButton* clear_;
}

+ (CGFloat)barHeight {
    // One step above the toolbar's own rhythm, so the row reads as a
    // strip of the page and not as a second title bar.
    return 40.0;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (!self) return nil;

    style_ = [[MdBarPopup alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [style_ setTranslatesAutoresizingMaskIntoConstraints:NO];
    [style_ setControlSize:NSControlSizeSmall];
    [style_ setBezelStyle:NSBezelStyleRounded];
    // Body Text first: it is the state a document opens in, and the one
    // the popup names when no heading applies.
    NSArray<NSString*>* styleTitles =
        @[ @"Body Text", @"Heading 1", @"Heading 2", @"Heading 3",
           @"Heading 4", @"Heading 5", @"Heading 6" ];
    for (NSUInteger i = 0; i < styleTitles.count; ++i) {
        [style_ addItemWithTitle:styleTitles[i]];
        [[style_ lastItem] setTag:(NSInteger)i];
    }
    [style_ setTarget:self];
    [style_ setAction:@selector(headingStyleChanged:)];
    [style_ setToolTip:@"Paragraph style"];
    [style_ setAccessibilityLabel:@"Paragraph style"];

    inline_ = BarSegments(@[ @"bold", @"italic", @"strikethrough",
                             @"chevron.left.forwardslash.chevron.right" ],
                          @[ @"Bold", @"Italic", @"Strikethrough", @"Code" ],
                          self, @selector(inlinePressed:));
    block_ = BarSegments(@[ @"list.bullet", @"list.number", @"text.quote" ],
                         @[ @"Bulleted List", @"Numbered List", @"Quote" ],
                         self, @selector(blockPressed:));
    link_ = BarButton(@"link", @"Link", self, @selector(linkPressed:));
    table_ = BarButton(@"tablecells", @"Insert Table", self,
                       @selector(tablePressed:));
    clear_ = BarButton(@"eraser", @"Clear Formatting", self,
                       @selector(clearPressed:));

    MdBarRule* first = [[MdBarRule alloc] initWithFrame:NSZeroRect];
    [first setTranslatesAutoresizingMaskIntoConstraints:NO];
    MdBarRule* second = [[MdBarRule alloc] initWithFrame:NSZeroRect];
    [second setTranslatesAutoresizingMaskIntoConstraints:NO];
    MdBarRule* third = [[MdBarRule alloc] initWithFrame:NSZeroRect];
    [third setTranslatesAutoresizingMaskIntoConstraints:NO];

    // The flexible gap before the trailing control: it takes the room
    // the groups do not need, so the bar reads leading to trailing even
    // when the window is wide.
    NSView* spacer = [[NSView alloc] initWithFrame:NSZeroRect];
    [spacer setTranslatesAutoresizingMaskIntoConstraints:NO];
    [spacer setContentHuggingPriority:1.0
                       forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSStackView* row = [[NSStackView alloc] initWithFrame:NSZeroRect];
    [row setTranslatesAutoresizingMaskIntoConstraints:NO];
    [row setOrientation:NSUserInterfaceLayoutOrientationHorizontal];
    [row setAlignment:NSLayoutAttributeCenterY];
    [row setDistribution:NSStackViewDistributionFill];
    [row setSpacing:8.0];
    for (NSView* view in @[ style_, first, inline_, second, link_, table_,
                            third, block_, spacer, clear_ ]) {
        [row addArrangedSubview:view];
    }
    // Link and Insert Table act on the document the same way; they sit
    // as one pair, closer than the groups are to each other.
    [row setCustomSpacing:2.0 afterView:link_];

    [self addSubview:row];
    [NSLayoutConstraint activateConstraints:@[
        [row.leadingAnchor constraintEqualToAnchor:self.leadingAnchor
                                          constant:14.0],
        [row.trailingAnchor constraintEqualToAnchor:self.trailingAnchor
                                           constant:-14.0],
        [row.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
    ]];

    [self setBarEnabled:NO];
    return self;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    // The window's own background, so the strip follows the appearance
    // without a palette of its own; the hairline is the system's
    // separator, the same one the find bar and the toolbar draw.
    [[NSColor windowBackgroundColor] setFill];
    NSRectFill(self.bounds);
    [[NSColor separatorColor] setFill];
    NSRectFill(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 1.0));
}

- (void)setBarEnabled:(BOOL)enabled {
    [style_ setEnabled:enabled];
    [inline_ setEnabled:enabled];
    [block_ setEnabled:enabled];
    [link_ setEnabled:enabled];
    [table_ setEnabled:enabled];
    [clear_ setEnabled:enabled];
}

- (void)updateWithFormatState:(const FormatState&)state inTable:(BOOL)inTable {
    const NSInteger level = (NSInteger)state.headingLevel;
    for (NSMenuItem* item in [style_ itemArray]) {
        if ([item tag] == level) {
            [style_ selectItem:item];
            break;
        }
    }
    [inline_ setSelected:state.bold ? YES : NO forSegment:0];
    [inline_ setSelected:state.italic ? YES : NO forSegment:1];
    [inline_ setSelected:state.strike ? YES : NO forSegment:2];
    [inline_ setSelected:state.code ? YES : NO forSegment:3];
    [block_ setSelected:state.inBullets ? YES : NO forSegment:0];
    [block_ setSelected:state.inNumbering ? YES : NO forSegment:1];
    [block_ setSelected:state.inQuote ? YES : NO forSegment:2];
    // Inside a table the button still inserts a new one, which is what
    // the menu's Insert Table does; the row and column commands stay in
    // the Table menu, where a caret in a table finds them.
    (void)inTable;
}

- (void)sendCommand:(SEL)action {
    // No target: the responder chain finds the window's delegate, the
    // exact path a menu item with a nil target takes, so a click here
    // and a menu pick run the same code.
    [NSApp sendAction:action to:nil from:self];
}

- (void)headingStyleChanged:(id)sender {
    (void)sender;
    switch ([style_ selectedTag]) {
        case 1: [self sendCommand:@selector(setHeading1:)]; break;
        case 2: [self sendCommand:@selector(setHeading2:)]; break;
        case 3: [self sendCommand:@selector(setHeading3:)]; break;
        case 4: [self sendCommand:@selector(setHeading4:)]; break;
        case 5: [self sendCommand:@selector(setHeading5:)]; break;
        case 6: [self sendCommand:@selector(setHeading6:)]; break;
        default: [self sendCommand:@selector(setBodyText:)]; break;
    }
}

- (void)inlinePressed:(id)sender {
    (void)sender;
    switch ([inline_ selectedSegment]) {
        case 0: [self sendCommand:@selector(toggleBold:)]; break;
        case 1: [self sendCommand:@selector(toggleItalic:)]; break;
        case 2: [self sendCommand:@selector(toggleStrike:)]; break;
        case 3: [self sendCommand:@selector(toggleCode:)]; break;
        default: break;
    }
}

- (void)blockPressed:(id)sender {
    (void)sender;
    switch ([block_ selectedSegment]) {
        case 0: [self sendCommand:@selector(toggleBullets:)]; break;
        case 1: [self sendCommand:@selector(toggleNumbering:)]; break;
        case 2: [self sendCommand:@selector(toggleQuote:)]; break;
        default: break;
    }
}

- (void)linkPressed:(id)sender {
    (void)sender;
    [self sendCommand:@selector(toggleLink:)];
}

- (void)tablePressed:(id)sender {
    (void)sender;
    [self sendCommand:@selector(insertTable:)];
}

- (void)clearPressed:(id)sender {
    (void)sender;
    [self sendCommand:@selector(clearFormat:)];
}

@end
