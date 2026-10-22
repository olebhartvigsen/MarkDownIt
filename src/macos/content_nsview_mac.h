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

@end
