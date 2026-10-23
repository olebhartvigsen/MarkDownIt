#pragma once

// The NSView that draws the document.
//
// The view is a thin shell around the ContentView: it draws through the
// RenderTarget seam and turns mouse and key events into edits. It is
// declared in its own header because it is Objective-C, and the rest of
// the app layer is plain C++.

#import <AppKit/AppKit.h>

namespace mac {

class App;
class ContentView;

}  // namespace mac

// The content view of the window.
@interface MdContentView : NSView

// Creates a view that draws the given content. The app is where the
// edits go.
- (instancetype)initWithContent:(mac::ContentView*)content
                            app:(mac::App*)app;

// Applies the zoom around the middle of the viewport, the way the menu
// and the buttons do it.
- (void)zoomTo:(float)factor;

// Told every time the zoom lands somewhere, so the window can save it.
- (void)setZoomChangedHandler:(void (^)(float))handler;

// The document changed under the caret: the blink restarts, the caret
// is brought back into view, the remembered column is dropped and the
// view redraws. The window calls this after a paste that does not go
// through the view's own key handling.
- (void)documentEdited;

@end
