#pragma once

// The Word-style table-size picker on macOS.
//
// The Windows build opens a grid popup under the Table button: hover the
// cells to choose the size, click to insert. references/ribbon-menu-
// guidelines.md section 12 asks for exactly that, plus keyboard input and
// a position that stays inside the screen's work area. This is the same
// control in AppKit, anchored to the strip's Table button rather than to
// the document caret.
//
// The rules about the size range and the label live in
// src/tablepicker_model.h, which the tests cover. This class owns the
// window, the drawing and the input.

#import <AppKit/AppKit.h>

@interface MdTablePicker : NSObject

// Shows the picker with its top-left corner at the given screen point
// (AppKit coordinates: origin bottom left, y up). The window is clamped
// into the screen's visible area, so a picker opened near the right edge
// or the bottom of the display still lands on screen. The block receives
// the column count and the data row count when the user commits; it does
// not run when the user cancels.
+ (void)presentBelowAnchor:(NSPoint)anchor
                  onCommit:(void (^)(int cols, int rows))commit;

// Closes the picker without committing, if one is open.
+ (void)dismiss;

@end
