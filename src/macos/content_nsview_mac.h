#pragma once

// The NSView that draws the document.
//
// The view is a thin shell around the ContentView: it draws through the
// RenderTarget seam and turns mouse and key events into edits. It is
// declared in its own header because it is Objective-C, and the rest of
// the app layer is plain C++.

#import <AppKit/AppKit.h>

namespace platform {

class App;
class ContentView;

}  // namespace platform

// The content view of the window.
//
// The view takes part in the text input protocol so that an input
// method (a composed accented letter, an IME) can write marked text
// through it; the composition itself lives in the shared edit
// controller, where the history rules of the text editing spec apply.
@interface MdContentView : NSView <NSTextInputClient>

// Creates a view that draws the given content. The app is where the
// edits go.
- (instancetype)initWithContent:(platform::ContentView*)content
                            app:(platform::App*)app;

// Applies the zoom around the middle of the viewport, the way the menu
// and the buttons do it.
- (void)zoomTo:(float)factor;

// Told every time the zoom lands somewhere, so the window can save it.
- (void)setZoomChangedHandler:(void (^)(float))handler;

// Told when the palette was resolved again for a changed system
// appearance, so the windows around the document refresh their own
// colours.
- (void)setThemeChangedHandler:(void (^)(void))handler;

// The document changed under the caret: the blink restarts, the caret
// is brought back into view, the remembered column is dropped and the
// view redraws. The window calls this after a paste that does not go
// through the view's own key handling.
- (void)documentEdited;

// The QA walk parks the caret off and stops its blink: a still
// capture must not depend on the blink phase.
- (void)qaParkCaret;

// The caret moved: the blink restarts, the caret is scrolled into view
// and the view redraws. The key handlers call it after every move, and
// so does the window after a jump it made itself.
- (void)caretMoved;

// The document view takes the content's size, or the viewport's when
// there is no document to scroll: the recents page is one viewport.
- (void)syncFrameToContent;

// Scrolls the clip view to a point in the document, clamped to the
// content, in DIPs from the page's own origin.
- (void)scrollToDocumentPoint:(NSPoint)point;

// Edit mode changed: start the blink in edit mode, stop and hide it in
// reading mode. The window's key state decides whether focus is there.
- (void)editingModeChanged:(BOOL)editing;

@end
