#pragma once

#import <Cocoa/Cocoa.h>

struct FormatState;

// The format bar: the row of editing controls that belongs to the
// document pane and is on screen exactly while the document is
// editable.
//
// It is chrome over the shared command table. Every control sends the
// same selector a menu item carries, through the responder chain, and
// the host pushes the document's state back in with
// -updateWithFormatState:inTable:. The bar keeps no document state and
// decides nothing on its own; a control that answered from its own
// memory would drift from the caret the moment a key moved it.
@interface MdFormatBar : NSView

// The height the bar asks for while it is on screen. The host pins it,
// so the document pane keeps one number.
+ (CGFloat)barHeight;

// Greys every control out. The bar itself is taken off screen by the
// host when the document is not editable.
- (void)setBarEnabled:(BOOL)enabled;

// The table button inserts a new table, so it stands down on its own
// while the caret is inside one: a table may never hold a table. The
// rest of the bar keeps the answer setBarEnabled: gave it.
- (void)setTableButtonEnabled:(BOOL)enabled;

// Reports the caret's state: the paragraph style the popup names and
// the marks the toggle segments show as pressed. inTable is the caret's
// table context; the table button inserts wherever the caret is.
- (void)updateWithFormatState:(const FormatState&)state inTable:(BOOL)inTable;

@end
