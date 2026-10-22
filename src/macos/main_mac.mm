// The macOS entry point.
//
// The Windows build has a wWinMain that creates the app and shows a
// window. This one has a standard NSApplication, an application
// delegate and a window, and the file the user opened comes either from
// the command line or from the Finder.

#include <cstdio>
#include <fstream>
#include <string>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "app_mac.h"
#include "content_nsview_mac.h"
#include "content_view_mac.h"
#include "formatting.h"
#include "menu_mac.h"
#include "settings_mac.h"
#include "zoommodel.h"

// The menu adapter.
//
// The adapter implements the MenuActions interface, which the menu
// calls, and forwards every command to the delegate. It is defined
// after the delegate, because it calls the delegate's selectors.
class MdMenuAdapter;

// The window and the app.
//
// The delegate owns the app object, the window and the content view. It
// implements the AppKit selectors, which the menu items are wired to.
@interface MdAppDelegate : NSObject <NSApplicationDelegate> {
    mac::App* app_;
    mac::ContentView* content_;
    mac::MacSettings settings_;
    NSWindow* window_;
    NSScrollView* scrollView_;
    MdContentView* view_;
    MdMenuAdapter* adapter_;
    float zoom_;
    bool sourceView_;
}
- (instancetype)init;
- (void)OpenPath:(const std::string&)path;

// The menu commands, as AppKit selectors.
- (void)newDocument:(id)sender;
- (void)openDocument:(id)sender;
- (void)saveDocument:(id)sender;
- (void)saveDocumentAs:(id)sender;
- (void)closeDocument:(id)sender;
- (void)undoEdit:(id)sender;
- (void)redoEdit:(id)sender;
- (void)cutSelection:(id)sender;
- (void)copySelection:(id)sender;
- (void)pasteSelection:(id)sender;
- (void)selectAll:(id)sender;
- (void)zoomIn:(id)sender;
- (void)zoomOut:(id)sender;
- (void)zoomReset:(id)sender;
- (void)toggleSourceView:(id)sender;
- (void)toggleOutline:(id)sender;
- (void)toggleBold:(id)sender;
- (void)toggleItalic:(id)sender;
- (void)toggleCode:(id)sender;
- (void)toggleLink:(id)sender;

// Creates the menu adapter. It is defined in a category, because the
// adapter type is only complete further down in the file.
- (MdMenuAdapter*)makeAdapter;
@end

// The menu adapter.
//
// It implements the MenuActions interface and forwards every command to
// the delegate. The menu holds it through the interface pointer.
class MdMenuAdapter : public mac::MenuActions {
public:
    explicit MdMenuAdapter(MdAppDelegate* delegate) : delegate_(delegate) {}

    void OnNewDocument() override { [delegate_ newDocument:nil]; }
    void OnOpen() override { [delegate_ openDocument:nil]; }
    void OnSave() override { [delegate_ saveDocument:nil]; }
    void OnSaveAs() override { [delegate_ saveDocumentAs:nil]; }
    void OnClose() override { [delegate_ closeDocument:nil]; }
    void OnUndo() override { [delegate_ undoEdit:nil]; }
    void OnRedo() override { [delegate_ redoEdit:nil]; }
    void OnCut() override { [delegate_ cutSelection:nil]; }
    void OnCopy() override { [delegate_ copySelection:nil]; }
    void OnPaste() override { [delegate_ pasteSelection:nil]; }
    void OnSelectAll() override { [delegate_ selectAll:nil]; }
    void OnZoomIn() override { [delegate_ zoomIn:nil]; }
    void OnZoomOut() override { [delegate_ zoomOut:nil]; }
    void OnZoomReset() override { [delegate_ zoomReset:nil]; }
    void OnToggleSourceView() override { [delegate_ toggleSourceView:nil]; }
    void OnToggleOutline() override { [delegate_ toggleOutline:nil]; }
    void OnToggleBold() override { [delegate_ toggleBold:nil]; }
    void OnToggleItalic() override { [delegate_ toggleItalic:nil]; }
    void OnToggleCode() override { [delegate_ toggleCode:nil]; }
    void OnToggleLink() override { [delegate_ toggleLink:nil]; }

private:
    MdAppDelegate* delegate_;
};

@implementation MdAppDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        app_ = new mac::App();
        content_ = new mac::ContentView(app_);
        app_->SetContentView(content_);
        zoom_ = zoom::kDefaultZoom;
        sourceView_ = false;
    }
    return self;
}

- (void)dealloc {
    // The content view belongs to the app object, so it goes with it.
    delete app_;
    delete adapter_;
}

// ---------------------------------------------------------------------------
// The application life cycle
// ---------------------------------------------------------------------------

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    settings_.Load();
    zoom_ = zoom::Clamp(settings_.settings().zoomFactor);

    // The window. A document based app would use NSWindowController, but
    // this app has a single window, so the delegate owns it.
    const NSRect frame = NSMakeRect(0, 0, 1000, 760);
    window_ = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [window_ setTitle:@"MarkDownIt"];
    [window_ setMinSize:NSMakeSize(480, 320)];

    // The content view goes in a scroll view, so the document scrolls
    // when it is longer than the window.
    view_ = [[MdContentView alloc] initWithContent:content_ app:app_];
    scrollView_ = [[NSScrollView alloc] initWithFrame:frame];
    [scrollView_ setHasVerticalScroller:YES];
    [scrollView_ setHasHorizontalScroller:NO];
    [scrollView_ setAutohidesScrollers:YES];
    [scrollView_ setDocumentView:view_];
    [window_ setContentView:scrollView_];
    [window_ makeKeyAndOrderFront:nil];

    // The menu bar.
    adapter_ = [self makeAdapter];
    NSMenu* menu = mac::BuildMainMenu(adapter_);
    [NSApp setMainMenu:menu];
    [NSApp activateIgnoringOtherApps:YES];

    // A file on the command line opens straight away.
    NSArray* arguments = [[NSProcessInfo processInfo] arguments];
    for (NSUInteger i = 1; i < arguments.count; ++i) {
        NSString* argument = arguments[i];
        if ([argument hasPrefix:@"-"]) continue;
        [self OpenPath:[argument UTF8String]];
        break;
    }
}

- (BOOL)application:(NSApplication*)application
            openFile:(NSString*)filename {
    (void)application;
    [self OpenPath:[filename UTF8String]];
    return YES;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    settings_.settings().zoomFactor = zoom_;
    settings_.Save();
}

// Opens a file, and reports a failure to the user.
- (void)OpenPath:(const std::string&)path {
    if (path.empty()) return;

    if (!app_->OpenFile(path)) {
        [self reportError:@"The file could not be opened."];
        return;
    }

    settings_.AddRecentFile(path);
    settings_.Save();

    [window_ setTitle:[NSString stringWithUTF8String:path.c_str()]];
    [view_ setNeedsDisplay:YES];
}

// ---------------------------------------------------------------------------
// The menu actions, as AppKit selectors
// ---------------------------------------------------------------------------

- (void)newDocument:(id)sender { (void)sender; [self OnNewDocument]; }
- (void)openDocument:(id)sender { (void)sender; [self OnOpen]; }
- (void)saveDocument:(id)sender { (void)sender; [self OnSave]; }
- (void)saveDocumentAs:(id)sender { (void)sender; [self OnSaveAs]; }
- (void)closeDocument:(id)sender { (void)sender; [self OnClose]; }
- (void)undoEdit:(id)sender { (void)sender; [self OnUndo]; }
- (void)redoEdit:(id)sender { (void)sender; [self OnRedo]; }
- (void)cutSelection:(id)sender { (void)sender; [self OnCut]; }
- (void)copySelection:(id)sender { (void)sender; [self OnCopy]; }
- (void)pasteSelection:(id)sender { (void)sender; [self OnPaste]; }
- (void)selectAll:(id)sender { (void)sender; [self OnSelectAll]; }
- (void)zoomIn:(id)sender { (void)sender; [self OnZoomIn]; }
- (void)zoomOut:(id)sender { (void)sender; [self OnZoomOut]; }
- (void)zoomReset:(id)sender { (void)sender; [self OnZoomReset]; }
- (void)toggleSourceView:(id)sender { (void)sender; [self OnToggleSourceView]; }
- (void)toggleOutline:(id)sender { (void)sender; [self OnToggleOutline]; }
- (void)toggleBold:(id)sender { (void)sender; [self OnToggleBold]; }
- (void)toggleItalic:(id)sender { (void)sender; [self OnToggleItalic]; }
- (void)toggleCode:(id)sender { (void)sender; [self OnToggleCode]; }
- (void)toggleLink:(id)sender { (void)sender; [self OnToggleLink]; }

// ---------------------------------------------------------------------------
// The commands
// ---------------------------------------------------------------------------

- (void)OnNewDocument {
    app_->NewDocument();
    [window_ setTitle:@"Untitled"];
    [view_ setNeedsDisplay:YES];
}

- (void)OnOpen {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setAllowedFileTypes:@[@"md", @"markdown", @"mdown", @"mkd"]];
    if ([panel runModal] != NSModalResponseOK) return;
    [self OpenPath:[[panel URL] path].UTF8String];
}

- (void)OnSave {
    if (app_->filePath().empty()) {
        [self OnSaveAs];
        return;
    }
    if (![self writeToPath:app_->filePath()]) return;
    app_->MarkClean();
}

- (void)OnSaveAs {
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setAllowedFileTypes:@[@"md", @"markdown"]];
    [panel setNameFieldStringValue:@"Untitled.md"];
    if ([panel runModal] != NSModalResponseOK) return;

    const std::string path = [[panel URL] path].UTF8String;
    if (![self writeToPath:path]) return;

    app_->MarkClean();
    settings_.AddRecentFile(path);
    settings_.Save();
    [window_ setTitle:[NSString stringWithUTF8String:path.c_str()]];
}

- (void)OnClose {
    [window_ close];
}

- (void)OnUndo {
    app_->Undo();
    [view_ setNeedsDisplay:YES];
}

- (void)OnRedo {
    app_->Redo();
    [view_ setNeedsDisplay:YES];
}

- (void)OnCut {
    if (!app_->HasSelection()) return;
    app_->SetClipboardText(app_->SelectedText());
    app_->DeleteBackward();
    [view_ setNeedsDisplay:YES];
}

- (void)OnCopy {
    if (!app_->HasSelection()) return;
    app_->SetClipboardText(app_->SelectedText());
}

- (void)OnPaste {
    const std::string text = app_->ClipboardText();
    if (text.empty()) return;
    app_->InsertText(text);
    [view_ setNeedsDisplay:YES];
}

- (void)OnSelectAll {
    app_->SetSelection(0, static_cast<uint32_t>(app_->buffer().Length()));
    [view_ setNeedsDisplay:YES];
}

- (void)OnZoomIn {
    [self setZoom:zoom::Step(zoom_, true)];
}

- (void)OnZoomOut {
    [self setZoom:zoom::Step(zoom_, false)];
}

- (void)OnZoomReset {
    [self setZoom:zoom::kDefaultZoom];
}

- (void)OnToggleSourceView {
    sourceView_ = !sourceView_;
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleOutline {
    // The outline pane is not on the mac layer yet. The command is in
    // the menu so the key bindings match the Windows build, and it
    // reports that rather than doing nothing silently.
    [self reportError:@"The outline is not available in this build yet."];
}

- (void)OnToggleBold {
    ToggleInlineMarker(&app_->buffer(), &app_->selection(), "**", &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleItalic {
    ToggleInlineMarker(&app_->buffer(), &app_->selection(), "*", &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleCode {
    ToggleInlineMarker(&app_->buffer(), &app_->selection(), "`", &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleLink {
    InsertLink(&app_->buffer(), &app_->selection(), "", &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Writes the document to the given path.
- (BOOL)writeToPath:(const std::string&)path {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        [self reportError:@"The file could not be saved."];
        return NO;
    }
    const std::string& text = app_->sourceText();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return YES;
}

// Sets the zoom factor, and stores it for the next launch.
- (void)setZoom:(float)factor {
    zoom_ = zoom::Clamp(factor);
    settings_.settings().zoomFactor = zoom_;
    [view_ setNeedsDisplay:YES];
}

// Shows an error to the user.
- (void)reportError:(NSString*)message {
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:message];
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

@end

@implementation MdAppDelegate (Menu)
- (MdMenuAdapter*)makeAdapter {
    return new MdMenuAdapter(self);
}
@end

// The program entry point.
//
// The app does not own a nib, so the delegate is created here and the
// application is run with it.
int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];

        MdAppDelegate* delegate = [[MdAppDelegate alloc] init];
        [application setDelegate:delegate];
        [application run];
    }
    return 0;
}
