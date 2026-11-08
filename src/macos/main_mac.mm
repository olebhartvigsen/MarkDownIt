// The macOS entry point.
//
// The Windows build has a wWinMain that creates the app and shows a
// window. This one has a standard NSApplication, an application
// delegate and a window, and the file the user opened comes either from
// the command line or from the Finder.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>

#include "app_mac.h"
#include "content_nsview_mac.h"
#include "outline_nsview_mac.h"
#include "outline_pane_mac.h"
#include "content_view_mac.h"
#include "outline.h"
#import "settings_window_mac.h"
#include "system_theme_mac.h"
#include "navigation.h"
#include "pdf_ocr_mac.h"
#include <sstream>

#include "formatting.h"
#include "office/docx_export.h"
#include "office/docx_import.h"
#include "office/markdown_bridge.h"
#include "office/pdf_export.h"
#include "office/pdf_import.h"
#include "welcome_mac.h"
#include "findbar_mac.h"
#include "menu_mac.h"
#include "toolbar_mac.h"
#include "table_picker_mac.h"

namespace mac {

// Opens a URL in the user's browser. The caller has checked the
// scheme; this only performs the handoff.
bool OpenExternalUrl(const std::string& url) {
    if (url.empty()) return false;
    NSString* text = [NSString stringWithUTF8String:url.c_str()];
    if (text == nil) return false;
    NSURL* url_target = [NSURL URLWithString:text];
    if (url_target == nil) return false;
    return [[NSWorkspace sharedWorkspace] openURL:url_target] ? true
                                                             : false;
}

}  // namespace mac
#include "crash_trace_mac.h"
#include "fonts_mac.h"
#include "settings_mac.h"
#include "zoommodel.h"

// The window and the app.
//
// The delegate owns the app object, the window and the content view. It
// implements the AppKit selectors, which the menu items and the command
// strip are wired to.
@interface MdAppDelegate
    : NSObject <NSApplicationDelegate, NSWindowDelegate,
                NSMenuDelegate, MacSettingsHost> {
    std::unique_ptr<mac::App> app_;
    // Owned by app_; this is a non-owning observer.
    mac::ContentView* content_;
    // The window content, which holds the command strip, the find bar
    // and the document.
    NSStackView* container_;
    // The native toolbar, the standard mac command surface. The
    // Windows window keeps its ribbon; this window follows the
    // system's own window design.
    MdToolbarController* toolbar_;

    // The outline pane and the view that draws it.
    std::unique_ptr<mac::OutlinePane> outlinePane_;
    MdOutlineView* outlineView_;
    NSScrollView* outlineScroll_;
    // The window material the pane sits on.
    NSVisualEffectView* backdrop_;
    NSSplitView* splitView_;
    // What the reader chose for the pane, and what the window actually
    // shows. A page without headings has nothing to list, so the pane
    // stands down for it while the reader's choice stands (outline
    // spec 2).
    bool outlinePreferred_;
    bool outlineVisible_;
    // Whether the open document has headings at all.
    bool outlineHasHeadings_;
    mac::MacSettings settings_;
    NSWindow* window_;
    // Set when windowWillClose saved the frame, so the terminate path
    // does not read a window that AppKit has already taken down.
    BOOL windowFrameSaved_;
    // One deferred layout pass per resize is enough.
    BOOL settlePending_;
    NSScrollView* scrollView_;
    MdContentView* view_;
    float zoom_;
    bool sourceView_;
    // Whether the text is wrapped.
    bool wrap_;
    // The width mode of the text column.
    int contentWidthMode_;
    // The find bar's height, which follows its visibility.
    NSLayoutConstraint* findBarHeightConstraint_;
    // The Settings window, made the first time the menu asks for it.
    MacSettingsWindow* settingsWindow_;
}
- (instancetype)init;
- (void)OpenPath:(const std::string&)path;
- (void)showRecents:(id)sender;
// The outline's active heading: the caret's section, or the reading
// line when followViewport is set.
- (void)outlineActiveRefresh:(BOOL)followViewport;
// A live scroll of the document, including a scrollbar drag.
- (void)documentScrollDidLiveScroll:(NSNotification*)notification;
// The system share sheet for the current document.
- (void)shareDocument:(id)sender;
// The QA walk: the states the launch smoke photographs.
- (void)runQAStates:(NSString*)list;
- (void)qaStep:(NSArray<NSString*>*)states at:(NSUInteger)index;
- (void)applyQAState:(NSString*)state;
- (void)qaScrollToHeading:(int)index drop:(float)drop;
- (void)qaKeysRun:(const char*)label;
- (void)qaScrollToTop;

// The menu commands, as AppKit selectors.
- (void)newDocument:(id)sender;
- (void)findNext:(id)sender;
- (void)findPrevious:(id)sender;
- (void)openRecentItem:(id)sender;
- (void)clearOpenRecentMenu:(id)sender;
- (void)showHelp:(id)sender;
- (void)openDocument:(id)sender;
- (void)reloadDocument:(id)sender;
- (void)openSettings:(id)sender;
- (void)setAsDefaultReader:(id)sender;
- (void)findDocument:(id)sender;
- (void)replaceInDocument:(id)sender;
- (void)saveDocument:(id)sender;
- (void)saveDocumentAs:(id)sender;
- (void)closeDocument:(id)sender;
- (void)importWord:(id)sender;
- (void)importPdf:(id)sender;
- (void)exportWord:(id)sender;
- (void)exportPdf:(id)sender;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
- (void)cut:(id)sender;
- (void)copy:(id)sender;
- (void)paste:(id)sender;
- (void)selectAll:(id)sender;
- (void)zoomIn:(id)sender;
- (void)zoomOut:(id)sender;
- (void)zoomReset:(id)sender;
// The find bar's layout hook: its height and the focus follow its
// visibility.
- (void)syncFindBarLayout:(BOOL)visible;
// The zoom landed somewhere, so the saved value follows.
- (void)zoomDidChange:(float)factor;
- (void)toggleSourceView:(id)sender;
- (void)toggleOutline:(id)sender;
- (void)toggleWrap:(id)sender;
- (void)toggleBold:(id)sender;
- (void)toggleStrike:(id)sender;
- (void)setHeading:(int)level;
- (void)setHeading1:(id)sender;
- (void)setHeading2:(id)sender;
- (void)setHeading3:(id)sender;
- (void)toggleBullets:(id)sender;
- (void)toggleNumbering:(id)sender;
- (void)toggleQuote:(id)sender;
- (void)indentBlock:(id)sender;
- (void)outdentBlock:(id)sender;
- (void)insertTable:(id)sender;
- (void)insertTableFromPickerCols:(int)cols rows:(int)rows;
- (void)addRowAbove:(id)sender;
- (void)addRowBelow:(id)sender;
- (void)removeRow:(id)sender;
- (void)addColumnLeft:(id)sender;
- (void)addColumnRight:(id)sender;
- (void)removeColumn:(id)sender;
- (void)splitCell:(id)sender;
- (void)alignLeft:(id)sender;
- (void)alignCenter:(id)sender;
- (void)alignRight:(id)sender;
- (void)toggleItalic:(id)sender;
- (void)toggleCode:(id)sender;
- (void)toggleLink:(id)sender;
- (void)toggleMark:(id)sender;
- (void)clearFormat:(id)sender;
@end

// The OCR seam for the PDF import: the shared importer asks about a
// page with no text, and Vision answers (Danish included; the
// shared side never links Vision itself).
static std::string OcrPdfPage(void* data, const std::string& path,
                              std::size_t page_index) {
    (void)data;
    return mac::RecognizePdfPage(path, page_index);
}

// Counts the refreshes the scroll path asks for, so a dead wiring and a
// stale probe cannot look the same in the trace (diag only).

@implementation MdAppDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        app_ = std::make_unique<mac::App>();
        app_->SetContentView(std::make_unique<mac::ContentView>(app_.get()));
        content_ = app_->contentView();
        zoom_ = zoom::kDefaultZoom;
        sourceView_ = false;
        wrap_ = true;
        // The pane fits the window with a margin on the mac; the
        // width modes belong to the Windows build.
        contentWidthMode_ = 3;
        windowFrameSaved_ = NO;
    }
    return self;
}

- (void)dealloc {
    // The app object owns the content view, so both go with app_.
}

// ---------------------------------------------------------------------------
// The application life cycle
// ---------------------------------------------------------------------------

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    mac::TraceMac("didFinishLaunching enter");
    // The bundled fonts come first: everything laid out after this
    // line, the welcome screen included, measures with them.
    mac::RegisterBundledFonts();
    settings_.Load();
    zoom_ = zoom::Clamp(settings_.settings().zoomFactor);
    // The pane always fits the window with the margin the padding
    // leaves, the way the rest of the window sizes on the mac. The
    // width modes belong to the Windows build, so the stored value
    // is not consulted here.
    contentWidthMode_ = 3;
    content_->SetContentWidthMode(contentWidthMode_);
    content_->SetZoom(zoom_);

    // The window. A document based app would use NSWindowController, but
    // this app has a single window, so the delegate owns it.
    // The window opens where it was left. A window that has never been
    // placed opens in the middle of the screen.
    NSRect frame;
    const AppSettings& stored = settings_.settings();
    if (stored.windowFrameWidth > 0.0f && stored.windowFrameHeight > 0.0f) {
        frame = NSMakeRect(stored.windowFrameX, stored.windowFrameY,
                           stored.windowFrameWidth, stored.windowFrameHeight);
    } else {
        frame = [self centeredFrame];
    }
    window_ = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    // The delegate takes the files that are dropped on the window, and
    // it is asked before the window closes with unsaved changes.
    [window_ setDelegate:self];
    [window_ setTitle:@"MarkDownIt"];
    // The window wears the standard title bar; the document name
    // goes on it through refreshWindowTitle, with the file icon.
    [window_ setMinSize:NSMakeSize(480, 320)];
    // The delegate owns the window for the whole run. The last-window
    // close path calls applicationWillTerminate after the window has
    // closed, and the frame save there must not touch a released
    // window. The settings window already opts out of the default.
    [window_ setReleasedWhenClosed:NO];
    // The window is tabbable and keeps the tab bar visible even with
    // one document, so the bar is a stable part of the window instead
    // of appearing only when a second window joins the group.
    [window_ setTabbingMode:NSWindowTabbingModePreferred];
    [window_ setTabbingIdentifier:@"MarkDownItDocument"];
    // The window keeps the system's own appearance: the document
    // palette is resolved from the system's semantic colours, so the
    // chrome and the document both belong to Light Mode or Dark Mode
    // (the visual style guide asks for it).

    // The content view goes in a scroll view, so the document scrolls
    // when it is longer than the window.
    view_ = [[MdContentView alloc] initWithContent:content_ app:app_.get()];
    // The view applies the zoom (the wheel, a pinch and the menu all
    // go through it, so the anchor point stays put), and the window
    // keeps and saves what it landed on.
    __weak id weakZoomSelf = self;
    [view_ setZoomChangedHandler:^(float factor) {
        [weakZoomSelf zoomDidChange:factor];
    }];
    scrollView_ = [[NSScrollView alloc] initWithFrame:frame];
    // The stack and split view resize their children; keep the scroll
    // host sizing with them so a wide window cannot leave the text at
    // its launch width.
    [scrollView_ setAutoresizingMask:NSViewWidthSizable |
                                    NSViewHeightSizable];
    [scrollView_ setHasVerticalScroller:YES];
    // The horizontal axis is for the nowrap lines a wide zoomed column
    // produces (zoom guideline 4).
    [scrollView_ setHasHorizontalScroller:YES];
    [scrollView_ setAutohidesScrollers:YES];
    [scrollView_ setDocumentView:view_];

    // The outline pane sits to the left of the document, in a split
    // view, so the reader can drag the divider between them.
    outlinePane_ = std::make_unique<mac::OutlinePane>();
    outlineView_ = [[MdOutlineView alloc] initWithPane:outlinePane_.get()];
    outlineVisible_ = settings_.settings().outlineVisible;

    // The pane scrolls on its own, so a long table of contents stays
    // reachable whatever the window height (outline spec 16).
    outlineScroll_ =
        [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 220, 400)];
    [outlineScroll_ setDocumentView:outlineView_];
    [outlineScroll_ setHasVerticalScroller:YES];
    [outlineScroll_ setAutohidesScrollers:YES];
    [outlineScroll_ setDrawsBackground:NO];
    [outlineView_ setAutoresizingMask:NSViewWidthSizable];
    [outlineScroll_ setAutoresizingMask:NSViewWidthSizable |
                                        NSViewHeightSizable];
    // The pane sits on the window's sidebar material: the vibrancy
    // below supplies the backdrop, the system supplies the selection
    // and label colours, and the pane paints no paper of its own, the
    // way a sidebar does it.
    backdrop_ = [[NSVisualEffectView alloc]
        initWithFrame:NSMakeRect(0, 0, 220, 400)];
    [backdrop_ setMaterial:NSVisualEffectMaterialSidebar];
    [backdrop_ setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
    [backdrop_ setState:NSVisualEffectStateFollowsWindowActiveState];
    [backdrop_ setHidden:!outlineVisible_];

    // The pane borrows the document of the app and reads it again
    // after every reparse.
    outlinePane_->SetDocument(&app_->GetDocument());
    outlinePane_->SetChromePalette(
        mac::SidebarPaletteForAppearance([view_ effectiveAppearance]));
    // The palette belongs to the appearance, so a switch refreshes the
    // outline pane beside the document too. Wired here, after the pane
    // exists: with the handler wired early, the first palette resolve
    // (which happens during view setup) reached a still nil pane, and
    // the launch smoke caught the crash.
    // Locals, so the block does not capture self: the handler lives on
    // the content view, and a self capture would make the two keep
    // each other alive.
    mac::OutlinePane* themePane = outlinePane_.get();
    NSView* themePaneView = outlineView_;
    __weak MdContentView* themeContentView = view_;
    [view_ setThemeChangedHandler:^{
        if (themePane) {
            themePane->SetChromePalette(
                mac::SidebarPaletteForAppearance(
                    [themeContentView effectiveAppearance]));
            [themePaneView setNeedsDisplay:YES];
        }
    }];
    app_->SetDocumentChangedHandler([self] {
        // The window carries the edited dot while there are changes.
        [window_ setDocumentEdited:YES];
        [self outlinePaneRefresh];
        // The toolbar follows the document: undo and redo depend on
        // what is pending, and the buttons mirror it.
        [self toolbarSync];
    });

    // The toolbar reads the command states again from the selection,
    // the way the menu bar revalidates around the caret.
    app_->SetSelectionChangedHandler([self] {
        [self toolbarSync];
        // The active heading follows the caret, and the pane scrolls
        // it into view when the caret enters another section (outline
        // spec 6 and 6.1).
        [self outlineActiveRefresh:NO];
    });

    // A click on a heading puts the caret there and scrolls it into
    // view. The pane never touches the document itself.
    outlinePane_->SetNavigateCallback([self](uint32_t offset) {
        app_->SetCaret(offset);
        content_->ScrollToOffset(offset);
        // The document takes the keyboard back after the jump, so
        // typing resumes in the editor (outline spec 7).
        [window_ makeFirstResponder:view_];
        [view_ setNeedsDisplay:YES];
    });

    // Scrolling moves the highlight when the caret is out of view,
    // so reading through a long document keeps the pane in step
    // (outline spec 19).
    content_->SetScrollChangedHandler([self](bool programmatic) {
        // A reader's scroll follows the reading line even while the
        // caret is still on screen; an app-driven jump keeps the
        // caret's section. The refresh runs whether or not the pane is
        // on screen: the visibility flag can lag the pane, and the row
        // it holds is what the reader sees the moment the pane returns.
        [self outlineActiveRefresh:!programmatic];
    });
    // `scrollWheel:` is not the only path: dragging the scroller and a
    // trackpad glide go straight through the scroll view. Observe it
    // here as well, so the outline is refreshed for every reader
    // scroll and not only for the wheel override.
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(documentScrollDidLiveScroll:)
               name:NSScrollViewDidLiveScrollNotification
             object:scrollView_];

    // The find bar sits at the bottom of the window, the one strip
    // position the find spec names. It is in the layout from the
    // start, hidden until it is used.
    //
    // The height read here is zero, because the bar starts hidden, so
    // the constraint is kept and the bar pushes its real height
    // through the callback below when it comes and goes.
    NSView* findBarView = (__bridge NSView*)app_->findBar().view();
    [findBarView setTranslatesAutoresizingMaskIntoConstraints:NO];
    findBarHeightConstraint_ =
        [findBarView.heightAnchor
            constraintEqualToConstant:app_->findBar().Height()];
    [findBarHeightConstraint_ setActive:YES];
    __weak id weakSelf = self;
    app_->SetFindVisibilityCallback([weakSelf](bool visible) {
        [weakSelf syncFindBarLayout:visible];
    });

    splitView_ = [[NSSplitView alloc] initWithFrame:frame];
    [splitView_ setAutoresizingMask:NSViewWidthSizable |
                                    NSViewHeightSizable];
    [splitView_ setVertical:YES];
    [splitView_ setDividerStyle:NSSplitViewDividerStyleThin];
    [backdrop_ setAutoresizingMask:NSViewWidthSizable |
                                   NSViewHeightSizable];
    [backdrop_ addSubview:outlineScroll_];
    [splitView_ addSubview:backdrop_];
    [splitView_ addSubview:scrollView_];
    // The document takes every point the outline does not need. Without
    // this priority the split view can grow the sidebar with a wide
    // window and leave the document at its old width.
    [splitView_ setHoldingPriority:NSLayoutPriorityDefaultHigh
                    forSubviewAtIndex:0];
    [splitView_ setHoldingPriority:NSLayoutPriorityDefaultLow
                    forSubviewAtIndex:1];
    // The place of the divider is the width of the pane, because the
    // pane is the first of the two.
    [splitView_ setPosition:settings_.settings().outlineWidthDip
               ofDividerAtIndex:0];
    // The pane is on screen from the first frame, so the flag follows
    // the screen rather than the saved value, which defaults to hidden.
    // A flag that disagreed made the first click on Hide Outline show a
    // pane that was already there.
    outlinePreferred_ = true;
    outlineVisible_ = true;
    [backdrop_ setHidden:NO];

    container_ = [[NSStackView alloc] initWithFrame:frame];
    [container_ setAutoresizingMask:NSViewWidthSizable |
                                    NSViewHeightSizable];
    [container_ setOrientation:NSUserInterfaceLayoutOrientationVertical];
    [container_ setDistribution:NSStackViewDistributionFill];
    // The rows take the window's width, not their own natural one.
    [container_ setAlignment:NSLayoutAttributeLeading];

    // The native toolbar goes with the window: one standard row of
    // commands in the title bar, with system symbols, the way the
    // macOS guidelines ask for.
    toolbar_ = [MdToolbarController controllerWithHost:self];
    [window_ setToolbar:[toolbar_ toolbar]];
    [container_ addArrangedSubview:splitView_];
    // The strip is the last row of the stack, so it docks at the
    // bottom of the window (find spec: one strip at the bottom).
    [container_ addArrangedSubview:findBarView];

    // The stack arranges its rows top to bottom and does not stretch
    // them across the window: an arranged view without a width
    // constraint keeps the width it was created with, so a grown window
    // left the document column at the old frame with a strip beside it.
    // Both rows are pinned to the window's edges.
    for (NSView* row in @[ splitView_, findBarView ]) {
        [row.leadingAnchor
            constraintEqualToAnchor:container_.leadingAnchor].active = YES;
        [row.trailingAnchor
            constraintEqualToAnchor:container_.trailingAnchor].active = YES;
    }

    [window_ setContentView:container_];
    [window_ makeKeyAndOrderFront:nil];
    // A preferred window still starts with the tab bar hidden when it
    // is the only window in its group. The explicit toggle makes the
    // bar visible for the first document too, unless the user hid it on
    // a previous run. The View menu carries the same toggle, so a one
    // document window can hide the bar without losing the document.
    NSWindowTabGroup* tabGroup = [window_ tabGroup];
    const BOOL tabBarHidden =
        [[NSUserDefaults standardUserDefaults]
            boolForKey:@"MarkDownItTabBarHidden"];
    if (!tabBarHidden && tabGroup && ![tabGroup isTabBarVisible]) {
        [window_ toggleTabBar:nil];
    }
    // The window is up, so the toolbar is built; the commands start
    // in the state the document is in.
    [self toolbarSync];
    // The document view takes the keyboard from the start, so typing
    // reaches the edit controller without a click first.
    [window_ makeFirstResponder:view_];
    mac::TraceMac("the window is on screen");

    // The delegate takes the files that are dropped on the window.
    [window_ registerForDraggedTypes:@[NSPasteboardTypeFileURL]];

    // Without a file the welcome screen takes the place of the
    // document. A file handed over by the Finder arrives before this
    // window exists, so it is already open here: the screen would bury
    // the document behind a list of recent ones.
    if (app_->filePath().empty()) {
        [self showWelcomeScreen];
        mac::TraceMac("the welcome screen is up");
    } else {
        [self hideWelcomeScreen];
        [self refreshWindowTitle:app_->filePath()];
        [self applyOutlineHeads];
        [view_ setNeedsDisplay:YES];
    }
    mac::TraceMac("launch: file=%s welcome=%d",
                  app_->filePath().c_str(), app_->WelcomeMode() ? 1 : 0);

    // F5 in the document asks the window to reload the file, because
    // the window asks the user before the changes are thrown away.
    //
    // The window and the app both live for the whole run of the
    // program, and the handler is cleared when the app goes away.
    MdAppDelegate* delegate = self;
    app_->SetReloadHandler([delegate] {
        [delegate OnReload];
    });
    app_->SetOpenFileHandler([delegate](const std::string& path) {
        [delegate OpenPath:path];
    });
    // The button on the welcome screen opens the panel, which the
    // window owns.
    app_->SetOpenPanelHandler([delegate] {
        [delegate OnOpen];
    });

    // The menu bar. The items find their command through the responder
    // chain, which ends at this delegate.
    NSMenu* menu = mac::BuildMainMenu();
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

    // The close smoke: close the window the way a person does, so the
    // last-window terminate path is exercised in CI. A signal would
    // never run windowWillClose or applicationWillTerminate.
    const char* closeSmoke = getenv("MARKDOWNIT_CLOSE_SMOKE");
    if (closeSmoke && *closeSmoke) {
        dispatch_after(
            dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.0 * NSEC_PER_SEC)),
            dispatch_get_main_queue(), ^{
                [window_ performClose:nil];
            });
        return;
    }

    // The QA walk: with MARKDOWNIT_QA_STATES set, the app steps
    // through the named states and announces each one in the launch
    // trace. A watcher outside the app photographs the window on every
    // announcement, so a CI run carries pictures of the real states
    // without driving the UI: no accessibility permission, nothing to
    // click. Without the variable, none of this runs.
    const char* qaStates = getenv("MARKDOWNIT_QA_STATES");
    if (qaStates && *qaStates) {
        [self runQAStates:[NSString stringWithUTF8String:qaStates]];
    }
}

// The QA walk, one state per step. Each state is applied, announced
// in the launch trace, and left on the screen for a beat so the
// watcher can capture it; the app then moves on by itself and quits at
// the end, so a run needs no input at all.
- (void)runQAStates:(NSString*)list {
    NSArray<NSString*>* states = [list componentsSeparatedByString:@","];
    // A beat before the first state, with the window brought forward:
    // without it the first picture raced the window onto the screen and
    // showed the desktop behind it.
    [window_ makeKeyAndOrderFront:nil];
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.0 * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{
            [self qaStep:states at:0];
        });
}

- (void)qaStep:(NSArray<NSString*>*)states at:(NSUInteger)index {
    if (index >= states.count) {
        mac::TraceMac("qa states done");
        // One more beat for the last capture, then the walk is over.
        dispatch_after(
            dispatch_time(DISPATCH_TIME_NOW, (int64_t)(4 * NSEC_PER_SEC)),
            dispatch_get_main_queue(), ^{
                [NSApp terminate:nil];
            });
        return;
    }
    [self applyQAState:states[index]];
    // A blinking caret is a coin toss in a still picture; park it so
    // every capture is in the same phase on both platforms.
    [view_ qaParkCaret];
    // The document pane in screen pixels with a top-left origin, the
    // same reporting the Windows walk does: the capture is cropped to
    // this rectangle so the platforms are compared over the part they
    // share. The backing scale keeps the numbers in capture pixels on
    // a retina display too.
    NSRect paneInWindow = [scrollView_ convertRect:[scrollView_ bounds]
                                            toView:nil];
    NSRect paneOnScreen = [window_ convertRectToScreen:paneInWindow];
    const CGFloat screenHeight =
        NSHeight([[[NSScreen screens] firstObject] frame]);
    const CGFloat scale = window_.backingScaleFactor;
    NSString* line = [NSString stringWithFormat:
        @"qa state ready: %@ rect=%d,%d,%d,%d",
        states[index],
        (int)lround(NSMinX(paneOnScreen) * scale),
        (int)lround((screenHeight - NSMaxY(paneOnScreen)) * scale),
        (int)lround(NSWidth(paneOnScreen) * scale),
        (int)lround(NSHeight(paneOnScreen) * scale)];
    mac::TraceMac([line UTF8String]);
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{
            [self qaStep:states at:index + 1];
        });
}

- (void)applyQAState:(NSString*)state {
    if ([state isEqualToString:@"doc"]) {
        // The document opened at launch; the state is the picture of
        // it as it lands.
    } else if ([state isEqualToString:@"find"]) {
        app_->ShowFind(FindBarMode::Find);
        // A live query, so the picture carries the match highlighting
        // and the count, not just an empty bar.
        app_->findBar().model().SetSearchText(L"list");
        app_->RefreshFindResults(false);
    } else if ([state isEqualToString:@"edit"]) {
        app_->SetEditing(true);
        // The mac window has no ribbon tabs, so there is no page to
        // switch here; both walks show the state their app opens on.
        [window_ makeFirstResponder:view_];
        // Land at the top of the document, the way the Windows walk
        // arrives: its caret rule leaves the view there, so the two
        // pictures show the same part of the page. Both the core's
        // offset and the scroller take the position; setting only the
        // scroller left the drawing where the caret reveal had put it.
        content_->SetScrollOffset(0.0f);
        NSClipView* clip = [scrollView_ contentView];
        [clip scrollToPoint:NSMakePoint(0.0, 0.0)];
        [scrollView_ reflectScrolledClipView:clip];
        [self toolbarSync];
        [self outlineActiveRefresh:NO];
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"outline"]) {
        [self applyOutlineVisible:YES];
        [self outlineActiveRefresh:NO];
    } else if ([state isEqualToString:@"settings"]) {
        // The Settings window is a surface of its own; it stays up
        // for its picture and the walk quits with it open.
        [self OnOpenSettings];
    } else if ([state isEqualToString:@"welcome-scrolled"]) {
        // The reader scrolls a page sideways, then asks for the recents
        // page. It has to fit the window and start at its own origin.
        //
        // The clip is put where a sideways scroll leaves it. The clip's
        // own scrolling does not clamp (its callers do), so this is the
        // state itself, not a guess about a font metric.
        // A standalone SVG keeps its intrinsic size, so a wide diagram
        // is a page wider than the window: the reader scrolls right.
        NSString* dir = [[window_ representedFilename]
            stringByDeletingLastPathComponent];
        if ([dir length] > 0) {
            [self OpenPath:std::string(
                [[dir stringByAppendingPathComponent:@"wide-diagram.svg"]
                    UTF8String])];
        }
        NSClipView* clip = [[view_ enclosingScrollView] contentView];
        [view_ syncFrameToContent];
        [view_ scrollToDocumentPoint:NSMakePoint(240.0, 0.0)];
        mac::TraceMac(
            "qa welcome-scrolled: before offset=%.0f frame=%.0f clip=%.0f",
            (double)[clip bounds].origin.x, (double)NSWidth([view_ frame]),
            (double)NSWidth([clip bounds]));
        [self showRecents:nil];
        mac::TraceMac(
            "qa welcome-scrolled: after offset=%.0f frame=%.0f clip=%.0f",
            (double)[clip bounds].origin.x, (double)NSWidth([view_ frame]),
            (double)NSWidth([clip bounds]));
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"source"]) {
        // The raw markdown view: the renderer lays the buffer out as
        // one monospace block, its own drawing path.
        if (!sourceView_) [self OnToggleSourceView];
    } else if ([state isEqualToString:@"keys-source"]) {
        // The three everyday actions, in the raw view.
        if (!sourceView_) [self OnToggleSourceView];
        [self qaKeysRun:"source"];
    } else if ([state isEqualToString:@"keys-doc"]) {
        // The same three actions in the rendered view, for comparison.
        if (sourceView_) [self OnToggleSourceView];
        [self qaKeysRun:"doc"];
    } else if ([state isEqualToString:@"lower"]) {
        // The lower half of the page: the tasks, the quote, the table
        // and the code block, which the states above cannot reach. The
        // source state leaves the raw view on, so the picture goes
        // back to the rendered page first; the jump anchors to a
        // heading's own offset a fixed drop below the top, so both
        // platforms frame the same content even though their bars and
        // metrics differ a little.
        if (sourceView_) [self OnToggleSourceView];
        [self qaScrollToHeading:2 drop:96.0f];
    } else if ([state isEqualToString:@"end"]) {
        // The foot of the page: the small headings, the break and the
        // diagram.
        if (sourceView_) [self OnToggleSourceView];
        [self qaScrollToHeading:5 drop:96.0f];
    } else if ([state isEqualToString:@"tail"]) {
        // The foot the guideline review added: the deep nesting, the
        // long unbroken tokens and the wide table, anchored close so
        // the frame holds them all.
        if (sourceView_) [self OnToggleSourceView];
        [self qaScrollToHeading:8 drop:24.0f];
    } else if ([state isEqualToString:@"tail2"]) {
        // The rest of the tail: the long code block, which is taller
        // than one frame.
        if (sourceView_) [self OnToggleSourceView];
        [self qaScrollToHeading:10 drop:24.0f];
    } else if ([state isEqualToString:@"narrow"]) {
        // The page at a narrow window: the padding slides down and the
        // column follows the pane.
        if (sourceView_) [self OnToggleSourceView];
        [window_ setContentSize:NSMakeSize(500.0, 720.0)];
        [window_ center];
        [self qaScrollToTop];
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"wide"]) {
        // The page in the widest window the screen takes: the column
        // follows it with the theme padding as the only margin.
        if (sourceView_) [self OnToggleSourceView];
        [window_ setContentSize:NSMakeSize(1024.0, 720.0)];
        [window_ center];
        [self qaScrollToTop];
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"svg"]) {
        // The reader's own sequence: a page with headings first, so the
        // pane is up, and then a picture, which has to stand it down.
        NSString* dir = [[window_ representedFilename]
            stringByDeletingLastPathComponent];
        if ([dir length] > 0) {
            [self OpenPath:std::string(
                [[dir stringByAppendingPathComponent:@"layout-showcase.md"]
                    UTF8String])];
            mac::TraceMac(
                "qa svg: markdown headings=%d visible=%d hidden=%d document=%.0f",
                outlineHasHeadings_ ? 1 : 0, outlineVisible_ ? 1 : 0,
                [backdrop_ isHidden] ? 1 : 0, (double)NSWidth([view_ frame]));
            [self OpenPath:std::string(
                [[dir stringByAppendingPathComponent:@"diagram.svg"]
                    UTF8String])];
            mac::TraceMac(
                "qa svg: picture headings=%d visible=%d hidden=%d document=%.0f",
                outlineHasHeadings_ ? 1 : 0, outlineVisible_ ? 1 : 0,
                [backdrop_ isHidden] ? 1 : 0, (double)NSWidth([view_ frame]));
        }
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"svg-fresh"]) {
        // A second picture, opened as the very first document: the
        // reference for the swap state below.
        NSString* dir = [[window_ representedFilename]
            stringByDeletingLastPathComponent];
        if ([dir length] > 0) {
            [self OpenPath:std::string(
                [[dir stringByAppendingPathComponent:@"two-boxes.svg"]
                    UTF8String])];
            mac::TraceMac("qa svg: fresh file=%s",
                          [[[window_ representedFilename] lastPathComponent]
                              UTF8String]);
        }
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"svg-swap"]) {
        // One picture, then another. Only the second may show.
        NSString* dir = [[window_ representedFilename]
            stringByDeletingLastPathComponent];
        if ([dir length] > 0) {
            [self OpenPath:std::string(
                [[dir stringByAppendingPathComponent:@"diagram.svg"]
                    UTF8String])];
            [self OpenPath:std::string(
                [[dir stringByAppendingPathComponent:@"two-boxes.svg"]
                    UTF8String])];
            mac::TraceMac("qa svg: swap file=%s",
                          [[[window_ representedFilename] lastPathComponent]
                              UTF8String]);
        }
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"no-headings"]) {
        // A page with no headings: the pane has nothing to list, so it
        // stands down and the diagram takes the width.
        // The pane is asked for first, so the hide that follows can
        // only come from the document and not from the walk's order.
        [self applyOutlineVisible:YES];
        mac::TraceMac(
            "qa no-headings: before headings=%d visible=%d document=%.0f",
            outlineHasHeadings_ ? 1 : 0, outlineVisible_ ? 1 : 0,
            (double)NSWidth([view_ frame]));

        NSString* dir = [[window_ representedFilename]
            stringByDeletingLastPathComponent];
        if ([dir length] > 0) {
            NSString* path =
                [dir stringByAppendingPathComponent:@"no-headings.md"];
            [self OpenPath:std::string([path UTF8String])];
        }
        NSClipView* clip = [scrollView_ contentView];
        mac::TraceMac(
            "qa no-headings: after headings=%d visible=%d document=%.0f clip=%.0f",
            outlineHasHeadings_ ? 1 : 0, outlineVisible_ ? 1 : 0,
            (double)NSWidth([view_ frame]),
            clip ? (double)NSWidth([clip bounds]) : 0.0);
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"mark"]) {
        // A selection sweeps up the blank beside the words. The mark must
        // land on the words: the stored anchor, and the wash the renderer
        // paints, both stop at the drawn characters.
        NSString* dir = [[window_ representedFilename]
            stringByDeletingLastPathComponent];
        if ([dir length] > 0) {
            [self OpenPath:std::string(
                [[dir stringByAppendingPathComponent:@"mark-space.md"]
                    UTF8String])];
        }
        // The clip reads the renderer's layout, and the open only marked
        // the view dirty, so paint once before any range is clipped.
        [view_ display];
        app_->SetEditing(true);
        app_->markers().Clear();
        // "text with spaces " with its trailing space, at bytes 0 to 17,
        // then " leading" with its leading one, at bytes 23 to 31.
        app_->SetSelection(0, 17);
        app_->ToggleMarkSelection();
        app_->SetSelection(23, 31);
        app_->ToggleMarkSelection();
        // A code span: its chip is wider than the glyphs and painted over
        // the global wash layer, so the mark must be painted again above
        // the chip, or it is invisible and the chip looks like the mark.
        app_->SetSelection(48, 56);
        app_->ToggleMarkSelection();
        // Drop the selection, so the capture shows the marks and not the
        // blue selection band over them.
        app_->SetSelection(0, 0);
        const std::vector<TextMarker>& marks = app_->markers().Markers();
        for (const TextMarker& mark : marks) {
            mac::TraceMac("qa mark: exact=\"%s\" start=%u end=%u",
                          mark.exact.c_str(), mark.start, mark.end);
        }
        mac::TraceMac("qa mark: markers=%zu", marks.size());
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"welcome"]) {
        // The screen without a document, reached the way the toolbar
        // reaches it. The walk has opened the fixture, so the recent
        // list has a card to show.
        [self showRecents:nil];
        NSMutableString* items = [NSMutableString string];
        for (NSToolbarItem* item in [[window_ toolbar] items]) {
            if ([items length] > 0) [items appendString:@","];
            [items appendString:[item itemIdentifier]];
            if ([[item itemIdentifier] isEqualToString:@"MdRecent"]) {
                [items appendFormat:@"(on=%d)", [item isEnabled] ? 1 : 0];
            }
        }
        mac::TraceMac("qa welcome: mode=%d toolbar=%s",
                      app_->WelcomeMode() ? 1 : 0, [items UTF8String]);
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"wheel"]) {
        // Injected events wait in the queue until the app dequeues them,
        // and this state runs inside a dispatch of its own, so the queue
        // is drained by hand after every batch.
        void (^pump)(void) = ^{
            for (int guard = 0; guard < 200; ++guard) {
                NSEvent* next = [NSApp
                    nextEventMatchingMask:NSEventMaskAny
                                untilDate:[NSDate distantPast]
                                   inMode:NSDefaultRunLoopMode
                                  dequeue:YES];
                if (!next) break;
                [NSApp sendEvent:next];
            }
        };

        // Real wheel events posted to the session, so AppKit routes them
        // the way it routes a hand on a mouse. The trace carries where
        // the document lands and whether the command wheel zoomed.
        [window_ makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        pump();

        NSClipView* clip = [scrollView_ contentView];
        NSRect home = [clip bounds];
        home.origin = NSMakePoint(0.0, 0.0);
        [clip scrollToPoint:home.origin];
        [scrollView_ reflectScrolledClipView:clip];
        const CGFloat startClip = [clip bounds].origin.y;
        [self outlineActiveRefresh:YES];
        pump();
        mac::TraceMac("qa wheel: outline before row=%d probe=%u clip=%.1f",
                      outlinePane_->ActiveVisibleRow(),
                      content_->ViewportProbeOffset(), startClip);

        NSRect frame = [window_ frame];
        const CGFloat cx = NSMidX(frame);
        const CGFloat cy = NSMidY(frame);
        CGPoint screenPoint = CGPointMake(cx, CGDisplayPixelsHigh(0) - cy);
        CGWarpMouseCursorPosition(screenPoint);

        // A notched mouse wheel reports lines; a trackpad reports pixels.
        // Both are posted, because both should pan the document.
        struct Batch { int32_t unit; int32_t amount; const char* label; };
        const Batch batches[] = {
            {kCGScrollEventUnitPixel, -600, "pixels"},
            {kCGScrollEventUnitPixel, -600, "pixels again"},
        };
        for (const Batch& batch : batches) {
            for (int k = 0; k < 5; ++k) {
                CGEventRef ev = CGEventCreateScrollWheelEvent(
                    NULL, static_cast<CGScrollEventUnit>(batch.unit), 1,
                    batch.amount);
                CGEventSetLocation(ev, screenPoint);
                CGEventPost(kCGHIDEventTap, ev);
                CFRelease(ev);
                pump();
            }
        }

        // A sideways tilt of the wheel, which standard apps pan with.
        for (int k = 0; k < 5; ++k) {
            CGEventRef ev = CGEventCreateScrollWheelEvent(
                NULL, kCGScrollEventUnitPixel, 2, 0, -40);
            CGEventSetLocation(ev, screenPoint);
            CGEventPost(kCGHIDEventTap, ev);
            CFRelease(ev);
            pump();
        }

        // The command wheel is ours: it must zoom and must not scroll.
        const CGFloat docBefore = [view_ frame].size.height;
        for (int k = 0; k < 5; ++k) {
            CGEventRef ev = CGEventCreateScrollWheelEvent(
                NULL, kCGScrollEventUnitPixel, 1, -40);
            CGEventSetFlags(ev, kCGEventFlagMaskCommand);
            CGEventSetLocation(ev, screenPoint);
            CGEventPost(kCGHIDEventTap, ev);
            CFRelease(ev);
            pump();
        }
        mac::TraceMac("qa wheel: start=%.1f end=%.1f x=%.1f doc=%.1f->%.1f",
                      startClip, [clip bounds].origin.y, [clip bounds].origin.x,
                      docBefore, [view_ frame].size.height);
        mac::TraceMac("qa wheel: outline after row=%d probe=%u clip=%.1f",
                      outlinePane_->ActiveVisibleRow(),
                      content_->ViewportProbeOffset(),
                      [clip bounds].origin.y);
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"no-outline"]) {
        // The pane closed at the top of the page: the text should take
        // the width the pane held.
        NSClipView* clip = [scrollView_ contentView];
        if (clip) {
            [clip scrollToPoint:NSMakePoint(0.0, 0.0)];
            [scrollView_ reflectScrolledClipView:clip];
        }
        [self outlineActiveRefresh:NO];
        mac::TraceMac("qa no-outline: before pane=%.0f document=%.0f",
                      (double)NSWidth([backdrop_ frame]),
                      (double)NSWidth([view_ frame]));
        [self applyOutlineVisible:NO];
        mac::TraceMac("qa no-outline: after pane=%.0f document=%.0f clip=%.0f",
                      (double)NSWidth([backdrop_ frame]),
                      (double)NSWidth([view_ frame]),
                      (double)NSWidth([clip bounds]));
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"grow"]) {
        // The reader's report: the pane is closed, and then the
        // window grows. The text has to take the new frame, so the
        // trace records every frame the width could come from.
        if (sourceView_) [self OnToggleSourceView];
        [self applyOutlineVisible:NO];
        // Start from a narrow window, so the growth is a real one and
        // not the 1024 frame the wide state left behind.
        [window_ setContentSize:NSMakeSize(640.0, 720.0)];
        [window_ center];
        [self windowDidResize:nil];
        [self qaScrollToTop];
        mac::TraceMac(
            "qa grow: before win=%.0f container=%.0f split=%.0f "
            "scroll=%.0f clip=%.0f contentSize=%.0f document=%.0f "
            "content=%.0f",
            (double)NSWidth([[window_ contentView] bounds]),
            (double)NSWidth([container_ bounds]),
            (double)NSWidth([splitView_ frame]),
            (double)NSWidth([scrollView_ frame]),
            (double)NSWidth([[scrollView_ contentView] bounds]),
            (double)[scrollView_ contentSize].width,
            (double)NSWidth([view_ frame]),
            (double)content_->ContentWidth());
        [window_ setContentSize:NSMakeSize(1024.0, 720.0)];
        [window_ center];
        mac::TraceMac(
            "qa grow: first win=%.0f container=%.0f split=%.0f "
            "scroll=%.0f clip=%.0f contentSize=%.0f document=%.0f "
            "content=%.0f",
            (double)NSWidth([[window_ contentView] bounds]),
            (double)NSWidth([container_ bounds]),
            (double)NSWidth([splitView_ frame]),
            (double)NSWidth([scrollView_ frame]),
            (double)NSWidth([[scrollView_ contentView] bounds]),
            (double)[scrollView_ contentSize].width,
            (double)NSWidth([view_ frame]),
            (double)content_->ContentWidth());
        // A second pass on the settled frames: if this one has the
        // right width, the first pass read a frame AppKit had not
        // resized yet.
        [self windowDidResize:nil];
        mac::TraceMac(
            "qa grow: second win=%.0f container=%.0f split=%.0f "
            "scroll=%.0f clip=%.0f contentSize=%.0f document=%.0f "
            "content=%.0f",
            (double)NSWidth([[window_ contentView] bounds]),
            (double)NSWidth([container_ bounds]),
            (double)NSWidth([splitView_ frame]),
            (double)NSWidth([scrollView_ frame]),
            (double)NSWidth([[scrollView_ contentView] bounds]),
            (double)[scrollView_ contentSize].width,
            (double)NSWidth([view_ frame]),
            (double)content_->ContentWidth());
        // AppKit's own layout pass, after ours: the width the text
        // kept has to survive it, which is exactly what the reader
        // saw fail when the frame grew.
        [container_ layoutSubtreeIfNeeded];
        mac::TraceMac(
            "qa grow: settled win=%.0f container=%.0f split=%.0f "
            "scroll=%.0f clip=%.0f contentSize=%.0f document=%.0f "
            "content=%.0f",
            (double)NSWidth([[window_ contentView] bounds]),
            (double)NSWidth([container_ bounds]),
            (double)NSWidth([splitView_ frame]),
            (double)NSWidth([scrollView_ frame]),
            (double)NSWidth([[scrollView_ contentView] bounds]),
            (double)[scrollView_ contentSize].width,
            (double)NSWidth([view_ frame]),
            (double)content_->ContentWidth());
        [view_ setNeedsDisplay:YES];
    } else if ([state isEqualToString:@"dark"]) {
        if (sourceView_) [self OnToggleSourceView];
        // The top of the page in the dark appearance, so the picture
        // shows the page's head the way the doc state does. The size
        // comes back first: the narrow and wide states resize the
        // window, and the pictures after them want the standard frame.
        [window_ setContentSize:[self centeredFrame].size];
        [window_ center];
        [self qaScrollToTop];
        window_.appearance =
            [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        [self toolbarSync];
        [view_ setNeedsDisplay:YES];
    }
}

// The QA walk's scroll, anchored to a heading: the offset comes from
// the same outline data the pane shows, and the fixed drop puts the
// heading just below the top of the frame, so the two platforms frame
// the same content even when their bars and metrics differ a little.
// Reports what the three everyday actions do right now: an arrow run,
// a vertical step and one typed character, plus where the view scrolled
// to when the caret went to the end. The QA walk runs it in both modes
// so the two can be compared line by line.
- (void)qaKeysRun:(const char*)label {
    const std::string& text = app_->buffer().Text();
    uint32_t start = static_cast<uint32_t>(text.find("## Lists and tasks"));
    if (start == std::string::npos) start = 0;
    app_->SetCaret(start);
    const uint32_t caretBefore = app_->selection().active.offset;
    for (int i = 0; i < 5; ++i) {
        [self doCommandBySelector:@selector(moveRight:)];
    }
    const uint32_t afterRight = app_->selection().active.offset;
    [self doCommandBySelector:@selector(moveDown:)];
    const uint32_t afterDown = app_->selection().active.offset;
    const uint32_t lengthBefore = static_cast<uint32_t>(text.size());
    [self insertText:@"Z"];
    const uint32_t lengthAfter = static_cast<uint32_t>(app_->buffer().Text().size());
    const uint32_t afterTyping = app_->selection().active.offset;
    app_->SetCaret(lengthAfter);
    [self caretMoved];
    mac::TraceMac(
        "qa keys %s: caret=%u right=+%d down=%u typed_len=%u->%u "
        "caret_after_type=%u scroll=%.0f height=%.0f view=%.0f",
        label, caretBefore, (int)(afterRight - caretBefore), afterDown,
        lengthBefore, lengthAfter, afterTyping,
        (double)content_->scrollOffset(), (double)content_->DocumentHeight(),
        (double)content_->viewHeight());
}

- (void)qaScrollToHeading:(int)index drop:(float)drop {
    const std::vector<OutlineItem> items =
        CollectHeadings(app_->GetDocument());
    if (index < 0 || index >= static_cast<int>(items.size())) return;
    content_->ScrollToOffsetPlaced(items[index].offset, drop);
    [self outlineActiveRefresh:NO];
    [view_ setNeedsDisplay:YES];
}

// The top of the page, on the core's offset and the scroller together,
// the way the edit state lands there.
- (void)qaScrollToTop {
    content_->SetScrollOffset(0.0f);
    NSClipView* clip = [scrollView_ contentView];
    [clip scrollToPoint:NSMakePoint(0.0, 0.0)];
    [scrollView_ reflectScrolledClipView:clip];
    [self outlineActiveRefresh:NO];
    [view_ setNeedsDisplay:YES];
}

// The frame of a window in the middle of the screen.
- (NSRect)centeredFrame {
    const NSSize size = NSMakeSize(1000, 760);
    const NSRect screen = [[NSScreen mainScreen] visibleFrame];
    return NSMakeRect(screen.origin.x + (screen.size.width - size.width) / 2.0,
                      screen.origin.y + (screen.size.height - size.height) / 2.0,
                      size.width, size.height);
}

// The window remembers where it was, so it opens there next time.
- (void)saveWindowFrame {
    const NSRect frame = [window_ frame];
    AppSettings& stored = settings_.settings();
    stored.windowFrameX = static_cast<float>(frame.origin.x);
    stored.windowFrameY = static_cast<float>(frame.origin.y);
    stored.windowFrameWidth = static_cast<float>(frame.size.width);
    stored.windowFrameHeight = static_cast<float>(frame.size.height);
    settings_.Save();
}

- (BOOL)application:(NSApplication*)application
            openFile:(NSString*)filename {
    (void)application;
    mac::TraceMac("apple event: open file");
    [self OpenPath:[filename UTF8String]];
    return YES;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    (void)sender;
    return NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    mac::TraceMac("drag: drop");
    NSPasteboard* pasteboard = [sender draggingPasteboard];
    NSArray<NSURL*>* urls =
        [pasteboard readObjectsForClasses:@[[NSURL class]]
                                 options:@{NSPasteboardURLReadingFileURLsOnlyKey:
                                               @YES}];
    for (NSURL* url in urls) {
        if (!url.isFileURL) continue;
        [self OpenPath:[url.path UTF8String]];
        break;
    }
    return YES;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

// The window asks before it closes with unsaved changes, and a Quit
// asks the same question.
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    return [self confirmDiscardChanges];
}

// The window is still alive here, so this is the safe place to read
// its frame. The terminate path can run after the close, and it skips
// the save once this one has happened.
- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    [self saveWindowFrame];
    windowFrameSaved_ = YES;
    // Remember the user's tab bar choice while the tab group still
    // exists. The next launch must not force the bar back into a window
    // where the user hid it with a single document.
    NSWindowTabGroup* group = [window_ tabGroup];
    if (group) {
        [[NSUserDefaults standardUserDefaults]
            setBool:![group isTabBarVisible]
             forKey:@"MarkDownItTabBarHidden"];
    }
}

// A live resize always lays out at the new width. The clip view
// usually posts its own change, but AppKit can coalesce it during a
// drag; the explicit path keeps the text column following the window.
// The stack view does not promise to stretch an arranged split view
// across the window. Settle both panes here, using the width the
// container actually has, so a wide window cannot leave the document at
// the frame it launched with.
- (void)fitContentPanesToWindow {
    if (!container_ || !splitView_ || !scrollView_) return;

    NSRect splitFrame = [splitView_ frame];
    const CGFloat containerWidth = [container_ bounds].size.width;
    if (splitFrame.size.width != containerWidth) {
        splitFrame.size.width = containerWidth;
        [splitView_ setFrame:splitFrame];
    }
    [splitView_ adjustSubviews];

    const CGFloat outlineWidth =
        outlineVisible_ ? [backdrop_ frame].size.width : 0.0;
    const CGFloat divider =
        outlineVisible_ ? [splitView_ dividerThickness] : 0.0;
    CGFloat documentWidth = splitFrame.size.width - outlineWidth - divider;
    if (documentWidth < 1.0) documentWidth = splitFrame.size.width;

    // The document sits beside the pane, or at the window's edge when
    // the pane is closed. Placing it here, rather than trusting the
    // split view's own arrangement, keeps the text full width for the
    // whole session and not only for the frame that closed the pane.
    const CGFloat documentX = outlineVisible_ ? outlineWidth + divider : 0.0;
    NSRect scrollFrame = [scrollView_ frame];
    if (scrollFrame.origin.x != documentX ||
        scrollFrame.size.width != documentWidth) {
        scrollFrame.origin.x = documentX;
        scrollFrame.size.width = documentWidth;
        [scrollView_ setFrame:scrollFrame];
    }
    [scrollView_ tile];
}

- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    [self layoutDocumentForWindow];
    // A live resize can hand out frames that are one pass behind: the
    // clip still reports the width from before the drag, the text is
    // laid out for it, and nothing else asks for another pass. The
    // deferred pass runs on the settled frames, so a closed pane can
    // never leave the column at the width it had before the drag.
    [self scheduleSettleLayout];
}

// The layout the resize needs: the panes, then the text at the
// viewport's width.
- (void)layoutDocumentForWindow {
    if (!scrollView_ || !content_) return;
    // The stack and split view have their own layout pass. Settle it
    // before reading the clip width, or a fast live resize can hand the
    // content the previous frame and leave the text at that old width.
    [container_ layoutSubtreeIfNeeded];
    [self fitContentPanesToWindow];
    NSClipView* clip = [scrollView_ contentView];
    if (!clip) return;
    const NSRect clipBounds = [clip bounds];
    // `trim` the scrollbars through contentSize; the clip's frame can
    // still be one layout pass behind during a live resize.
    const NSSize viewport = [scrollView_ contentSize];
    const CGFloat layoutWidth = viewport.width > 0.0
        ? viewport.width
        : clipBounds.size.width;
    const CGFloat layoutHeight = viewport.height > 0.0
        ? viewport.height
        : clipBounds.size.height;
    content_->SetSize(static_cast<float>(layoutWidth),
                      static_cast<float>(layoutHeight));
    content_->SetScrollOffset(static_cast<float>(clipBounds.origin.y));
    content_->NotifyScrollChanged(false);
    [self outlineLayout];
}

// One pass after AppKit's own layout, on the frames the window ended
// with. The flag coalesces a drag's many notifications into one pass.
- (void)scheduleSettleLayout {
    if (settlePending_) return;
    settlePending_ = YES;
    __weak id weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        id strongSelf = weakSelf;
        if (!strongSelf) return;
        [strongSelf settleLayout];
    });
}

- (void)settleLayout {
    settlePending_ = NO;
    [self layoutDocumentForWindow];
}

// A live resize can coalesce the intermediate frame notifications. The
// end callback lays the column out once more against the settled pane,
// so a wider window never keeps the launch-width column.
- (void)windowDidEndLiveResize:(NSNotification*)notification {
    [self windowDidResize:notification];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:
    (NSApplication*)sender {
    (void)sender;
    if (![self confirmDiscardChanges]) return NSTerminateCancel;
    return NSTerminateNow;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    // The handlers point at this object, so they go away with it.
    app_->SetReloadHandler(nullptr);
    app_->SetOpenFileHandler(nullptr);
    (void)notification;
    settings_.settings().zoomFactor = zoom_;
    // A closed window saved its frame in windowWillClose. Reading it
    // here after the close was the crash.
    if (!windowFrameSaved_) {
        [self saveWindowFrame];
    }
    settings_.Save();
}

// The title bar a document app wears: the display name as the title
// and the file itself as the represented file, which gives the title
// its icon and its path popup, and the edited dot off until the next
// change.
- (void)refreshWindowTitle:(const std::string&)path {
    if (path.empty()) {
        [window_ setTitle:@"MarkDownIt"];
        [window_ setSubtitle:@""];
        [window_ setRepresentedFilename:@""];
    } else {
        NSString* full = [NSString stringWithUTF8String:path.c_str()];
        // The document name is the title, the way document windows
        // do it; the app name lives in the menu bar. The folder shows
        // as the subtitle, shortened with the system's own '~'.
        [window_ setTitle:[full lastPathComponent]];
        [window_ setSubtitle:
            [[full stringByDeletingLastPathComponent]
                stringByAbbreviatingWithTildeInPath]];
        [window_ setRepresentedFilename:full];
    }
    [window_ setDocumentEdited:NO];
}

// Opens a file, and reports a failure to the user.
- (void)OpenPath:(const std::string&)path {
    mac::TraceMac("open: %s", path.c_str());
    [self hideWelcomeScreen];
    if (path.empty()) return;

    if (!app_->OpenFile(path)) {
        [self reportError:@"The file could not be opened."];
        return;
    }

    settings_.AddRecentFile(path);
    settings_.Save();

    [self refreshWindowTitle:path];
    // A page without headings stands the pane down, and one with
    // headings brings it back as the reader left it.
    [self applyOutlineHeads];
    [view_ setNeedsDisplay:YES];
    mac::TraceMac("open: done");
}

// ---------------------------------------------------------------------------
// The menu actions, as AppKit selectors
// ---------------------------------------------------------------------------

- (void)newDocument:(id)sender { (void)sender; [self OnNewDocument]; }
- (void)openDocument:(id)sender { (void)sender; [self OnOpen]; }
// The way back to the recent documents, from the toolbar and from the
// View menu. The open document stays where it is, so the list is a
// step aside and not a close.
- (void)showRecents:(id)sender {
    (void)sender;
    [self showWelcomeScreen];
    // The screen takes the keyboard, so a card is a click away.
    [window_ makeFirstResponder:view_];
}
- (void)reloadDocument:(id)sender { (void)sender; [self OnReload]; }
- (void)openSettings:(id)sender { (void)sender; [self OnOpenSettings]; }
- (void)findNext:(id)sender { (void)sender; [self OnFindNext]; }
- (void)findPrevious:(id)sender { (void)sender; [self OnFindPrevious]; }
- (void)openRecentItem:(id)sender {
    NSMenuItem* item = (NSMenuItem*)sender;
    NSString* path = [item representedObject];
    if (![path isKindOfClass:[NSString class]]) return;
    if (![self confirmDiscardChanges]) return;
    [self OpenPath:std::string([path UTF8String])];
}
- (void)clearOpenRecentMenu:(id)sender {
    (void)sender;
    settings_.settings().recentFiles.clear();
    settings_.Save();
    // The welcome screen shows the same list, so it reads again.
    if (app_->WelcomeMode()) {
        [self showWelcomeScreen];
    }
}
- (void)showHelp:(id)sender {
    (void)sender;
    // The project page carries the readme and the guides.
    [[NSWorkspace sharedWorkspace]
        openURL:[NSURL URLWithString:@"https://github.com/olebhartvigsen/MarkDownIt"]];
}
- (void)setAsDefaultReader:(id)sender {
    (void)sender;
    [self OnSetAsDefaultReader];
}
- (void)saveDocument:(id)sender { (void)sender; [self OnSave]; }
- (void)saveDocumentAs:(id)sender { (void)sender; [self OnSaveAs]; }
- (void)shareDocument:(id)sender {
    (void)sender;
    if (!app_ || app_->WelcomeMode()) return;

    // Share the file when there is one, the buffer text when the
    // document is still untitled.
    NSMutableArray* items = [NSMutableArray array];
    if (!app_->filePath().empty()) {
        NSString* path =
            [NSString stringWithUTF8String:app_->filePath().c_str()];
        if (path) [items addObject:[NSURL fileURLWithPath:path]];
    } else {
        NSString* text =
            [NSString stringWithUTF8String:app_->sourceText().c_str()];
        if (text) [items addObject:text];
    }
    if ([items count] == 0) return;

    NSSharingServicePicker* picker =
        [[NSSharingServicePicker alloc] initWithItems:items];
    NSView* anchor = container_ ?: view_;
    const NSRect bounds = [anchor bounds];
    // The picker hangs from the top trailing corner, where the share
    // item lands in the toolbar.
    const NSRect rect = NSMakeRect(NSMaxX(bounds) - 24.0,
                                   NSMaxY(bounds) - 8.0, 1.0, 1.0);
    [picker showRelativeToRect:rect
                        ofView:anchor
                preferredEdge:NSRectEdgeMinY];
}
- (void)closeDocument:(id)sender { (void)sender; [self OnClose]; }
- (void)importWord:(id)sender { (void)sender; [self OnImportWord]; }
- (void)importPdf:(id)sender { (void)sender; [self OnImportPdf]; }
- (void)exportWord:(id)sender { (void)sender; [self OnExportWord]; }
- (void)exportPdf:(id)sender { (void)sender; [self OnExportPdf]; }
- (void)findDocument:(id)sender { (void)sender; [self OnFind]; }
- (void)replaceInDocument:(id)sender { (void)sender; [self OnReplace]; }
// The Edit menu sends the standard selectors, so a focused text field
// handles its own undo and clipboard before the document ever sees the
// command (spec AC-01). The document paths answer here.
- (void)undo:(id)sender {
    (void)sender;
    NSResponder* first = [window_ firstResponder];
    if ([first isKindOfClass:[NSTextView class]]) {
        [[(NSTextView*)first undoManager] undo];
        return;
    }
    [self OnUndo];
}
- (void)redo:(id)sender {
    (void)sender;
    NSResponder* first = [window_ firstResponder];
    if ([first isKindOfClass:[NSTextView class]]) {
        [[(NSTextView*)first undoManager] redo];
        return;
    }
    [self OnRedo];
}
- (void)cut:(id)sender { (void)sender; [self OnCut]; }
- (void)copy:(id)sender { (void)sender; [self OnCopy]; }
- (void)paste:(id)sender { (void)sender; [self OnPaste]; }
- (void)selectAll:(id)sender { (void)sender; [self OnSelectAll]; }
- (void)zoomIn:(id)sender { (void)sender; [self OnZoomIn]; }
- (void)zoomOut:(id)sender { (void)sender; [self OnZoomOut]; }
- (void)zoomReset:(id)sender { (void)sender; [self OnZoomReset]; }
- (void)toggleSourceView:(id)sender { (void)sender; [self OnToggleSourceView]; }
- (void)toggleOutline:(id)sender { (void)sender; [self OnToggleOutline]; }
- (void)toggleWrap:(id)sender { (void)sender; [self OnToggleWrap]; }
// The View menu's tab bar command; the native tab bar keeps its own
// hide button as well.
- (void)toggleTabBar:(id)sender {
    [window_ toggleTabBar:sender];
    NSWindowTabGroup* group = [window_ tabGroup];
    [[NSUserDefaults standardUserDefaults]
        setBool:(group ? ![group isTabBarVisible] : NO)
         forKey:@"MarkDownItTabBarHidden"];
}
- (void)toggleBold:(id)sender { (void)sender; [self OnToggleBold]; }
- (void)toggleStrike:(id)sender { (void)sender; [self OnToggleStrike]; }
- (void)setHeading:(int)level { [self OnHeading:level]; }

// The heading levels, as the selectors the menu items carry.
- (void)setHeading1:(id)sender { (void)sender; [self OnHeading:1]; }
- (void)setHeading2:(id)sender { (void)sender; [self OnHeading:2]; }
- (void)setHeading3:(id)sender { (void)sender; [self OnHeading:3]; }
- (void)toggleBullets:(id)sender { (void)sender; [self OnToggleBullets]; }
- (void)toggleNumbering:(id)sender { (void)sender; [self OnToggleNumbering]; }
- (void)toggleQuote:(id)sender { (void)sender; [self OnToggleQuote]; }
- (void)indentBlock:(id)sender { (void)sender; [self OnIndent]; }
- (void)outdentBlock:(id)sender { (void)sender; [self OnOutdent]; }
- (void)insertTable:(id)sender { (void)sender; [self OnInsertTable]; }

// The Insert Table item greys out while the caret is inside a table;
// there is already one (table guidelines 54 and 58).
// The Open Recent menu fills itself when it opens. The recent list
// is the same one the welcome screen shows, so the menu and the screen
// can never disagree.
- (void)menuNeedsUpdate:(NSMenu*)menu {
    if (![[menu title] isEqualToString:@"Open Recent"]) return;
    [menu removeAllItems];

    const std::vector<std::string>& recent = settings_.recentFiles();
    if (recent.empty()) {
        NSMenuItem* none = [[NSMenuItem alloc]
            initWithTitle:@"No Recent Documents"
                   action:nil
            keyEquivalent:@""];
        [none setEnabled:NO];
        [menu addItem:none];
        return;
    }
    for (const std::string& path : recent) {
        NSString* full = [NSString stringWithUTF8String:path.c_str()];
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[full lastPathComponent]
                   action:@selector(openRecentItem:)
            keyEquivalent:@""];
        [item setRepresentedObject:full];
        [item setToolTip:full];
        [menu addItem:item];
    }
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* clear = [[NSMenuItem alloc]
        initWithTitle:@"Clear Menu"
               action:@selector(clearOpenRecentMenu:)
        keyEquivalent:@""];
    [menu addItem:clear];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if ([item action] == @selector(toggleFullScreen:)) {
        // The system's own wording: the same item enters and exits
        // full screen, so its title follows the window.
        [item setTitle:([window_ styleMask] & NSWindowStyleMaskFullScreen)
                           ? @"Exit Full Screen"
                           : @"Enter Full Screen"];
        return YES;
    }
    if ([item action] == @selector(toggleTabBar:)) {
        NSWindowTabGroup* group = [window_ tabGroup];
        [item setTitle:((group && [group isTabBarVisible])
                            ? @"Hide Tab Bar"
                            : @"Show Tab Bar")];
        return YES;
    }
    if ([item action] == @selector(shareDocument:)) {
        return !app_->WelcomeMode();
    }
    if ([item action] == @selector(toggleMark:)) {
        // The marker is one toggle, so the menu shows the state the next
        // press will change: on when the selection touches a marker
        // (highlighting spec 4, 6 and 42).
        [item setState:(app_->SelectionHasMarker() ? NSControlStateValueOn
                                                   : NSControlStateValueOff)];
        return !app_->WelcomeMode();
    }
    if ([item action] == @selector(toggleOutline:)) {
        // The pane is only on offer while the document has headings.
        [item setState:(outlineVisible_ ? NSControlStateValueOn
                                        : NSControlStateValueOff)];
        return outlineHasHeadings_;
    }
    if ([item action] == @selector(editMode:)) {
        // A checked item shows which mode the document is in.
        [item setState:(app_->Editing() ? NSControlStateValueOn
                                        : NSControlStateValueOff)];
        return YES;
    }
    // Every command that changes the document follows edit mode, the
    // way the Windows ribbon greys its format commands in view mode.
    if (!app_->Editing() && [self actionChangesTheDocument:[item action]]) {
        return NO;
    }
    if ([item action] == @selector(insertTable:)) {
        tableedit::TableLocation location;
        return !app_->CaretTable(&location);
    }
    return YES;
}

// The menu actions that change the document: the format, paragraph,
// heading, table, undo and clipboard cut commands. Copy, Find, the
// zoom, the view switches and the markers keep working in read mode.
- (BOOL)actionChangesTheDocument:(SEL)action {
    static const char* const kMutatingActions[] = {
        "undo:", "redo:", "cut:", "paste:",
        "toggleBold:", "toggleItalic:", "toggleStrike:", "toggleCode:",
        "setHeading1:", "setHeading2:", "setHeading3:",
        "toggleBullets:", "toggleNumbering:", "toggleQuote:",
        "indentBlock:", "outdentBlock:", "toggleLink:", "clearFormat:",
        "insertTable:", "addRowAbove:", "addRowBelow:", "removeRow:",
        "addColumnLeft:", "addColumnRight:", "removeColumn:",
        "removeTable:", "splitCell:",
        "alignLeft:", "alignCenter:", "alignRight:",
    };
    for (const char* const name : kMutatingActions) {
        if (action == NSSelectorFromString(
                          [NSString stringWithUTF8String:name])) {
            return YES;
        }
    }
    return NO;
}
- (void)addRowAbove:(id)sender { (void)sender; [self OnAddTableRow:NO]; }
- (void)addRowBelow:(id)sender { (void)sender; [self OnAddTableRow:YES]; }
- (void)removeRow:(id)sender { (void)sender; [self OnRemoveTableRow]; }
- (void)addColumnLeft:(id)sender { (void)sender; [self OnAddTableColumn:NO]; }
- (void)addColumnRight:(id)sender { (void)sender; [self OnAddTableColumn:YES]; }
- (void)removeColumn:(id)sender { (void)sender; [self OnRemoveTableColumn]; }
- (void)removeTable:(id)sender { (void)sender; [self OnRemoveTable]; }
- (void)splitCell:(id)sender { (void)sender; [self OnSplitTableCell]; }
- (void)alignLeft:(id)sender { (void)sender; [self OnAlignTableColumn:1]; }
- (void)alignCenter:(id)sender { (void)sender; [self OnAlignTableColumn:2]; }
- (void)alignRight:(id)sender { (void)sender; [self OnAlignTableColumn:3]; }
- (void)toggleItalic:(id)sender { (void)sender; [self OnToggleItalic]; }
- (void)toggleCode:(id)sender { (void)sender; [self OnToggleCode]; }
- (void)toggleLink:(id)sender { (void)sender; [self OnToggleLink]; }
- (void)toggleMark:(id)sender { (void)sender; [self OnToggleMark]; }
- (void)clearFormat:(id)sender { (void)sender; [self OnClearFormat]; }
- (void)editMode:(id)sender { (void)sender; [self OnToggleEditMode]; }

// ---------------------------------------------------------------------------
// The commands
// ---------------------------------------------------------------------------

- (void)OnNewDocument {
    if (![self confirmDiscardChanges]) return;
    app_->NewDocument();
    [self refreshWindowTitle:std::string()];
    [self showWelcomeScreen];
    [view_ setNeedsDisplay:YES];
}

// Shows the welcome screen, which takes the place of the document when
// no file is open.
- (void)showWelcomeScreen {
    app_->SetWelcomeMode(true);
    content_->SetWelcomeFiles(settings_.recentFilesWithPreview());
    // The recents page is one viewport. A document that was scrolled
    // sideways leaves the document view wider than the clip, and the
    // page would be drawn shifted left, clipped, with a scroller under
    // it. Put the frame back to the viewport and the origin back to 0.
    [view_ syncFrameToContent];
    // The screen is a page of recent files, with no headings of its
    // own, so the pane stands down here too and the cards take the
    // width.
    outlineHasHeadings_ = false;
    [self applyOutlineState];
    // The strip's document commands have nothing to act on here.
    [self toolbarSync];
}

// Hides the welcome screen, because a document is open now.
- (void)hideWelcomeScreen {
    app_->SetWelcomeMode(false);
    // The document is open again, so its own size takes over.
    [view_ syncFrameToContent];
    [self toolbarSync];
}

- (void)OnOpen {
    if (![self confirmDiscardChanges]) return;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    // A standalone SVG is a page image, and the renderer draws it as
    // one, so it can be opened like a document.
    [panel setAllowedFileTypes:@[@"md", @"markdown", @"mdown", @"mkd",
                                 @"svg"]];
    if ([panel runModal] != NSModalResponseOK) return;
    [self OpenPath:[[panel URL] path].UTF8String];
}

- (void)OnSave {
    [self saveCurrentDocument];
}

// Saves the document, and says whether it worked.
- (BOOL)saveCurrentDocument {
    if (app_->filePath().empty()) {
        return [self saveDocumentAs];
    }
    if (![self writeToPath:app_->filePath()]) return NO;
    app_->MarkClean();
    [window_ setDocumentEdited:NO];
    return YES;
}

// Asks what to do with the changes, and says whether the document may
// be replaced.
//
// The user can save the changes, throw them away, or cancel the
// command that asked.
- (BOOL)confirmDiscardChanges {
    if (!app_->IsDirty()) return YES;

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Save the changes before you continue?";
    alert.informativeText = @"The changes are not on the disk yet.";
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Discard"];
    [alert addButtonWithTitle:@"Cancel"];

    const NSModalResponse answer = [alert runModal];
    if (answer == NSAlertFirstButtonReturn) return [self saveCurrentDocument];
    if (answer == NSAlertSecondButtonReturn) {
        // The changes are gone, so nothing asks a second time when the
        // window closes or the app quits right after.
        app_->MarkClean();
        [window_ setDocumentEdited:NO];
        return YES;
    }
    return NO;
}

- (void)OnSaveAs {
    [self saveDocumentAs];
}

// Saves the document under a new name, and says whether it worked.
- (BOOL)saveDocumentAs {
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setAllowedFileTypes:@[@"md", @"markdown"]];
    [panel setNameFieldStringValue:@"Untitled.md"];
    if ([panel runModal] != NSModalResponseOK) return NO;

    const std::string path = [[panel URL] path].UTF8String;
    if (![self writeToPath:path]) return NO;

    // The document has a new identity, so the marker sidecar follows
    // it and the marks on screen stay with the document (highlighting
    // guide 12.7 and 37).
    app_->AdoptSavedPath(path);

    app_->MarkClean();
    settings_.AddRecentFile(path);
    settings_.Save();
    [self refreshWindowTitle:path];
    return YES;
}

- (void)OnImportWord {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    // Word files arrive as .docx packages, and a legacy .doc is
    // offered too so the importer can say what it is instead of
    // the panel filtering the file away (office spec, Word
    // import).
    [panel setAllowedFileTypes:@[@"docx", @"doc"]];
    if ([panel runModal] != NSModalResponseOK) return;

    const std::string path = [[panel URL] path].UTF8String;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        [self reportError:@"The file could not be read."];
        return;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string bytes = contents.str();

    office::DocModel model;
    office::CompatReport report;
    std::string error;
    if (!office::DocxImport(bytes, model, report, error)) {
        // The importer names what is wrong (a legacy .doc, a
        // damaged package), and the message goes out with it
        // (office spec, general requirements).
        NSString* text = @"The Word file could not be read.";
        if (!error.empty()) {
            text = [NSString stringWithFormat:@"%@\n\n%s", text,
                                              error.c_str()];
        }
        [self reportError:text];
        return;
    }

    // The document becomes the markdown of the Word file. It has no
    // file of its own yet: untitled and dirty, so Save asks for a name
    // and closing asks whether to save.
    const std::string markdown = office::OfficeModelToMarkdown(model, report);
    app_->AdoptImportedText(markdown);
    [self refreshWindowTitle:std::string()];
    [window_ setDocumentEdited:YES];
    [self hideWelcomeScreen];
    [view_ setNeedsDisplay:YES];

    // The report gains everything markdown has no form for, and it is
    // shown the way the Windows build shows it.
    NSString* notes = [self compatNotesText:report];
    if ([notes length] > 0) {
        [self reportInfo:[NSString stringWithFormat:
            @"The document was imported with these compatibility notes:"
            @"\n\n%@", notes]];
    }
}

- (void)OnImportPdf {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setAllowedFileTypes:@[@"pdf"]];
    if ([panel runModal] != NSModalResponseOK) return;

    const std::string path = [[panel URL] path].UTF8String;

    office::PdfImportOptions options;
    // Scanned pages go through Vision, so a page without text is read
    // instead of being skipped (office spec, PDF import).
    options.ocr = OcrPdfPage;

    office::DocModel model;
    office::CompatReport report;
    std::string error;
    bool imported = office::PdfImport(path, options, model, report, error);

    if (!imported && error.find("password") != std::string::npos) {
        // The password is asked for before the file is opened (office
        // spec, PDF import).
        NSAlert* ask = [[NSAlert alloc] init];
        ask.messageText = @"The PDF is password-protected.";
        ask.informativeText = @"Give the password to open it.";
        NSSecureTextField* field = [[NSSecureTextField alloc]
            initWithFrame:NSMakeRect(0, 0, 260, 24)];
        ask.accessoryView = field;
        [ask addButtonWithTitle:@"Open"];
        [ask addButtonWithTitle:@"Cancel"];
        if ([ask runModal] != NSAlertFirstButtonReturn) return;
        const char* typed = field.stringValue.UTF8String;
        options.password = typed != nullptr ? typed : "";
        imported = office::PdfImport(path, options, model, report, error);
    }

    if (!imported) {
        NSString* text = @"The PDF could not be read.";
        if (!error.empty()) {
            text = [NSString stringWithFormat:@"%@\n\n%s", text,
                                              error.c_str()];
        }
        [self reportError:text];
        return;
    }

    // The document becomes the markdown of the PDF. It has no file of
    // its own yet: untitled and dirty, the same contract the Word
    // import has.
    const std::string markdown = office::OfficeModelToMarkdown(model, report);
    app_->AdoptImportedText(markdown);
    [self refreshWindowTitle:std::string()];
    [window_ setDocumentEdited:YES];
    [self hideWelcomeScreen];
    [view_ setNeedsDisplay:YES];

    NSString* notes = [self compatNotesText:report];
    if ([notes length] > 0) {
        [self reportInfo:[NSString stringWithFormat:
            @"The document was imported with these compatibility notes:"
            @"\n\n%@", notes]];
    }
}

- (void)OnExportWord {
    [self exportOfficeDocument:false];
}

- (void)OnExportPdf {
    [self exportOfficeDocument:true];
}

// Writes the document as a Word file or as a PDF. The whole document
// is exported: the model carries the page setup, the same contract the
// Windows build has.
- (void)exportOfficeDocument:(bool)asPdf {
    if (app_->WelcomeMode()) {
        [self reportError:@"No document is open. Open a markdown file first."];
        return;
    }
    if (app_->sourceText().empty()) {
        [self reportError:@"The document is empty. Nothing to export."];
        return;
    }

    // The PDF scope (office spec, PDF export): the whole document,
    // a page range or the marked area. The Word export keeps the
    // whole document, the same contract the Windows build has.
    bool exportSelection = false;
    office::PdfExportOptions options;
    if (asPdf) {
        NSAlert* scope = [[NSAlert alloc] init];
        scope.messageText = @"What should the PDF hold?";
        [scope addButtonWithTitle:@"Whole document"];
        [scope addButtonWithTitle:@"Page range\u2026"];
        const bool hasSelection = !app_->selection().Empty();
        if (hasSelection) {
            [scope addButtonWithTitle:@"Selection"];
        }
        [scope addButtonWithTitle:@"Cancel"];
        const NSModalResponse answer = [scope runModal];
        if (answer == NSAlertFirstButtonReturn) {
            options.all_pages = true;
        } else if (answer == NSAlertSecondButtonReturn) {
            options.all_pages = false;
            options.first_page = 1;
            options.last_page = 0;
            NSTextField* firstField = [[NSTextField alloc]
                initWithFrame:NSMakeRect(0, 0, 90, 24)];
            firstField.stringValue = @"1";
            NSTextField* lastField = [[NSTextField alloc]
                initWithFrame:NSMakeRect(100, 0, 90, 24)];
            lastField.placeholderString = @"last";
            NSView* box = [[NSView alloc]
                initWithFrame:NSMakeRect(0, 0, 200, 28)];
            [box addSubview:firstField];
            [box addSubview:lastField];
            NSAlert* range = [[NSAlert alloc] init];
            range.messageText = @"Which pages?";
            range.informativeText =
                @"The first and the last page, counting from one.";
            range.accessoryView = box;
            [range addButtonWithTitle:@"Export"];
            [range addButtonWithTitle:@"Cancel"];
            if ([range runModal] != NSAlertFirstButtonReturn) return;
            if ([firstField intValue] > 0) {
                options.first_page = [firstField intValue];
            }
            if ([lastField intValue] > 0) {
                options.last_page = [lastField intValue];
            }
        } else if (hasSelection &&
                   answer == NSAlertThirdButtonReturn) {
            exportSelection = true;
        } else {
            return;
        }
    }

    // Protection (office spec, PDF export): an optional password pair
    // with the print, copy and change flags, applied through the PDF
    // standard security handler.
    if (asPdf) {
        NSAlert* protect = [[NSAlert alloc] init];
        protect.messageText = @"Protect the PDF?";
        protect.informativeText =
            @"A user password opens the file, an owner password guards "
            @"the permissions.";
        NSView* box = [[NSView alloc]
            initWithFrame:NSMakeRect(0, 0, 260, 140)];
        NSSecureTextField* userField = [[NSSecureTextField alloc]
            initWithFrame:NSMakeRect(0, 112, 260, 24)];
        userField.placeholderString = @"User password (to open)";
        NSSecureTextField* ownerField = [[NSSecureTextField alloc]
            initWithFrame:NSMakeRect(0, 84, 260, 24)];
        ownerField.placeholderString = @"Owner password";
        NSButton* printBox = [NSButton checkboxWithTitle:@"Allow printing"
                                                  target:nil action:nil];
        printBox.frame = NSMakeRect(0, 58, 260, 20);
        printBox.state = NSControlStateValueOn;
        NSButton* copyBox = [NSButton checkboxWithTitle:@"Allow copying"
                                                 target:nil action:nil];
        copyBox.frame = NSMakeRect(0, 36, 260, 20);
        copyBox.state = NSControlStateValueOn;
        NSButton* modifyBox = [NSButton checkboxWithTitle:@"Allow changes"
                                                   target:nil action:nil];
        modifyBox.frame = NSMakeRect(0, 14, 260, 20);
        modifyBox.state = NSControlStateValueOn;
        [box addSubview:userField];
        [box addSubview:ownerField];
        [box addSubview:printBox];
        [box addSubview:copyBox];
        [box addSubview:modifyBox];
        protect.accessoryView = box;
        [protect addButtonWithTitle:@"Protect"];
        [protect addButtonWithTitle:@"No protection"];
        if ([protect runModal] == NSAlertFirstButtonReturn) {
            const char* owner_text = ownerField.stringValue.UTF8String;
            if (owner_text == nullptr || *owner_text == '\0') {
                [self reportError:@"The owner password is needed to protect "
                                    @"the PDF."];
                return;
            }
            options.encrypt = true;
            options.owner_password = owner_text;
            const char* user_text = userField.stringValue.UTF8String;
            options.user_password = user_text != nullptr ? user_text : "";
            options.allow_printing = printBox.state == NSControlStateValueOn;
            options.allow_copying = copyBox.state == NSControlStateValueOn;
            options.allow_modifying = modifyBox.state == NSControlStateValueOn;
        }
    }

    // The marked area exports the selection's own markdown, so the
    // file holds the text the user marked and nothing else.
    std::string source = app_->sourceText();
    if (exportSelection) {
        source = app_->SelectedText();
        if (source.empty()) {
            [self reportError:
                @"The selection is empty. Nothing to export."];
            return;
        }
    }

    office::DocModel model;
    office::CompatReport report;
    std::string error;
    if (!office::MarkdownToOfficeModel(source, model, report, error)) {
        NSString* text = @"The document could not be read.";
        if (!error.empty()) {
            text = [NSString stringWithFormat:@"%@\n\n%s", text,
                                              error.c_str()];
        }
        [self reportError:text];
        return;
    }

    std::string bytes;
    int pageCount = 0;
    if (asPdf) {
        if (!office::PdfExport(model, options, bytes, report, error,
                               &pageCount) ||
            bytes.empty()) {
            NSString* text = @"The PDF could not be written.";
            if (!error.empty()) {
                text = [NSString stringWithFormat:@"%@\n\n%s", text,
                                                  error.c_str()];
            }
            [self reportError:text];
            return;
        }
    } else {
        if (!office::DocxExport(model, bytes, report, error) ||
            bytes.empty()) {
            NSString* text = @"The Word file could not be written.";
            if (!error.empty()) {
                text = [NSString stringWithFormat:@"%@\n\n%s", text,
                                                  error.c_str()];
            }
            [self reportError:text];
            return;
        }
    }

    // Everything the model conversion and the writer had to drop
    // is shown before anything lands on disk (office spec,
    // general requirements), so the user can back out first.
    NSString* exportNotes = [self compatNotesText:report];
    if ([exportNotes length] > 0) {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"The export will lose some content.";
        alert.informativeText = [NSString stringWithFormat:
            @"These features have no form in the file:\n\n%@",
            exportNotes];
        [alert addButtonWithTitle:@"Export"];
        [alert addButtonWithTitle:@"Cancel"];
        if ([alert runModal] != NSAlertFirstButtonReturn) return;
    }

    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setAllowedFileTypes:asPdf ? @[@"pdf"] : @[@"docx"]];
    [panel setNameFieldStringValue:asPdf ? @"Untitled.pdf" : @"Untitled.docx"];
    if ([panel runModal] != NSModalResponseOK) return;

    const std::string path = [[panel URL] path].UTF8String;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        [self reportError:@"Could not write the file. The export was not saved."];
        return;
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();

    // The PDF export reports the page count, and both exports report
    // what they dropped or approximated (office spec, the
    // compatibility report clause).
    NSString* notes = [self compatNotesText:report];
    if (asPdf) {
        NSMutableString* message = [NSMutableString stringWithFormat:
            @"The document was exported to:\n%s\n\nPages: %d",
            path.c_str(), pageCount];
        if ([notes length] > 0) {
            [message appendFormat:@"\n\nCompatibility notes:\n\n%@", notes];
        }
        [self reportInfo:message];
    } else if ([notes length] > 0) {
        NSString* message = [NSString stringWithFormat:
            @"The document was exported to:\n%s\n\nCompatibility "
            @"notes:\n\n%@", path.c_str(), notes];
        [self reportInfo:message];
    }
}

- (void)OnClose {
    // The window asks for the changes itself, so the question is asked
    // in one place.
    [window_ close];
}

// The find bar came on or went off screen. The height constraint is
// pushed here, because the bar reads zero while it is hidden, and the
// document takes the focus back when the bar closes.
- (void)syncFindBarLayout:(BOOL)visible {
    findBarHeightConstraint_.constant = app_->findBar().Height();
    // The document area changes with the bar, and the find code scrolls
    // the match into view right after this, so the layout is settled
    // first.
    [window_ layoutIfNeeded];
    if (!visible) {
        [window_ makeFirstResponder:view_];
    }
}

- (void)OnFind {
    app_->ShowFind(FindBarMode::Find);
}

- (void)OnReplace {
    app_->ShowFind(FindBarMode::FindReplace);
}

- (void)OnUndo {
    app_->Undo();
    // An undo is an edit: the restored selection must come back into
    // view, the way the Windows window scrolls the caret after undo.
    [view_ documentEdited];
}

- (void)OnRedo {
    app_->Redo();
    [view_ documentEdited];
}

- (void)OnCut {
    app_->CutToClipboard();
    [view_ setNeedsDisplay:YES];
}

- (void)OnCopy {
    app_->CopyToClipboard();
}

- (void)OnPaste {
    // The paste pipeline is the app's: plain text, LF endings, inside a
    // cell the structural sanitizing, and tab separated rows become a
    // table.
    app_->PasteFromClipboard();
    [view_ documentEdited];
}

- (void)OnSelectAll {
    app_->SelectAll();
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
    // The app needs the flag too: it decides what a copy carries
    // (spec C2) and which bytes the find scope accepts.
    app_->SetSourceView(sourceView_);
    // The view really switches the drawing now: the renderer lays
    // the raw buffer out as one monospace block, every byte on
    // screen, so Find matches raw bytes the way the Windows view
    // does (find guide 4).
    content_->SetSourceMode(sourceView_);
    [view_ setNeedsDisplay:YES];
    if (app_->findBar().IsVisible()) {
        app_->RefreshFindResults(false);
    }
    [self toolbarSync];
}

- (void)OnReload {
    if (app_->filePath().empty()) {
        return;
    }
    if (app_->IsDirty()) {
        // The changes are thrown away, so the user is asked first.
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Reload the file and lose the changes?";
        [alert addButtonWithTitle:@"Reload"];
        [alert addButtonWithTitle:@"Cancel"];
        if ([alert runModal] != NSAlertFirstButtonReturn) {
            return;
        }
    }
    if (!app_->ReloadFile()) {
        [self reportError:@"The file could not be read."];
    }
    [self applyOutlineHeads];
    [view_ setNeedsDisplay:YES];
}

// The steps the Find menu takes. They are the same steps the find
// bar's own buttons take, from the same place: the caret.
- (void)OnFindNext {
    app_->findBar().model().NavigateNext(app_->selection().Start());
}

- (void)OnFindPrevious {
    app_->findBar().model().NavigatePrevious(app_->selection().Start());
}

- (void)OnOpenSettings {
    // The settings are a standard window now, with the system's own
    // controls. The JSON file stays the place they live.
    if (!settingsWindow_) {
        settingsWindow_ = [[MacSettingsWindow alloc] initWithHost:self];
    }
    [settingsWindow_ showSettings];
}

- (void)OnSetAsDefaultReader {
    // The bundle identifier of the running app is what the system
    // registers as the reader.
    NSString* bundleId = [[NSBundle mainBundle] bundleIdentifier];
    if (bundleId.length == 0) {
        [self reportError:
            @"The app has no bundle identifier, so it cannot become the "
            @"reader."];
        return;
    }

    // The markdown files are plain text, so that is the type to ask
    // the system about.
    NSString* type = @"public.plain-text";
    const OSStatus status = LSSetDefaultRoleHandlerForContentType(
        (__bridge CFStringRef)type, kLSRolesAll,
        (__bridge CFStringRef)bundleId);
    if (status != noErr) {
        [self reportError:
            @"The system did not make MarkDownIt the reader of the "
            @"markdown files."];
        return;
    }
    [self reportMessage:@"MarkDownIt is now the reader of the markdown "
                        @"files."];
}

- (void)OnToggleWrap {
    wrap_ = !wrap_;
    content_->SetWrap(wrap_);
    [self toolbarSync];
    [view_ setNeedsDisplay:YES];
}

// The outline switch reads the pane and goes through the same command
// the View menu takes.
- (BOOL)settingsOutlineVisible {
    return outlineVisible_;
}

- (void)applyOutlineVisible:(BOOL)visible {
    if (visible == outlinePreferred_) return;
    [self OnToggleOutline];
}

// Re-reads the command states of the toolbar: the edit switch reads
// Edit or Done, and the document commands grey out when there is
// nothing for them to act on, the same edit-mode rule the menu bar
// applies.
- (void)toolbarSync {
    for (NSToolbarItem* item in [[window_ toolbar] items]) {
        NSString* identifier = [item itemIdentifier];
        if ([identifier isEqualToString:@"MdEditMode"]) {
            NSButton* button = (NSButton*)[item view];
            if ([button isKindOfClass:[NSButton class]]) {
                [button setTitle:(app_->Editing() ? @"Done" : @"Edit")];
            }
        } else if ([identifier isEqualToString:@"MdRecent"]) {
            // On the screen itself there is nothing to go back to.
            [item setEnabled:!app_->WelcomeMode()];
        } else if ([identifier isEqualToString:@"MdOutline"]) {
            // A page without headings has nothing to list, so the
            // command stands down with the pane.
            [item setEnabled:outlineHasHeadings_];
        } else if ([identifier isEqualToString:@"MdHighlight"]) {
            // The marker item is a real toggle button, not a plain
            // action: its pressed state reports whether the selection
            // touches a marker, in view mode and in edit mode.
            NSButton* button = (NSButton*)[item view];
            if ([button isKindOfClass:[NSButton class]]) {
                [button setEnabled:!app_->WelcomeMode()];
                [button setState:(app_->SelectionHasMarker()
                                       ? NSControlStateValueOn
                                       : NSControlStateValueOff)];
            }
        } else if ([identifier isEqualToString:@"MdSave"] ||
                   [identifier isEqualToString:@"MdFind"] ||
                   [identifier isEqualToString:@"MdShare"]) {
            [item setEnabled:!app_->WelcomeMode()];
        } else if ([identifier isEqualToString:@"MdUndo"]) {
            [item setEnabled:(!app_->WelcomeMode() &&
                               app_->undo().CanUndo())];
        } else if ([identifier isEqualToString:@"MdRedo"]) {
            [item setEnabled:(!app_->WelcomeMode() &&
                               app_->undo().CanRedo())];
        }
    }
}

// Gives the pane the height its rows need, so the scroll view can
// scroll it, and keeps the width of the scroll host (outline spec 16).
- (void)outlineLayout {
    CGFloat height = outlinePane_->ContentHeight();
    const CGFloat visibleHeight = [outlineScroll_ contentSize].height;
    if (height < visibleHeight) height = visibleHeight;
    NSRect frame = NSMakeRect(0, 0, [outlineScroll_ contentSize].width,
                              height);
    [outlineView_ setFrame:frame];
}

// The caret moved: the active heading follows it, and the pane scrolls
// it into view when another section was entered (outline spec 6.1).
//
// The caret's section owns the highlight while the reader works in
// that section. A reader's scroll takes the reading line instead,
// even while the caret is still on screen, so the pane shows where
// the reader is looking; an app-driven jump keeps the caret's section
// (outline spec 19).
- (void)documentScrollDidLiveScroll:(NSNotification*)notification {
    (void)notification;
    if (!content_ || !app_) return;
    NSClipView* clip = [scrollView_ contentView];
    if (!clip) return;
    content_->SetScrollOffset(static_cast<float>([clip bounds].origin.y));
    content_->NotifyScrollChanged(false);
}

- (void)outlineActiveRefresh:(BOOL)followViewport {
    uint32_t offset = app_->selection().Start();
    if (followViewport || !content_->CaretIsOnScreen()) {
        const uint32_t probe = content_->ViewportProbeOffset();
        if (probe != UINT32_MAX) offset = probe;
    }
    outlinePane_->SetCaretOffset(offset);
    // The row follows the reading line whether or not the pane is on
    // screen. The visibility flag can say hidden while the split view
    // still draws the pane, so a guard on it left the band on whatever
    // section it had when the pane was last touched by hand.
    const int row = outlinePane_->ActiveVisibleRow();
    if (row >= 0) {
        [outlineView_ scrollRowIntoView:row];
    }
    [outlineView_ setNeedsDisplay:YES];
}

// The document changed, so the pane reads it again. The rows are
// only rebuilt when the headings themselves changed, so editing
// body text keeps the pane's rows, its place and its state (outline
// spec 23).
- (void)outlinePaneRefresh {
    outlinePane_->SetDocument(&app_->GetDocument());
    outlinePane_->SetCaretOffset(app_->selection().Start());
    [self outlineLayout];
    if (outlinePane_->TakeListChanged()) {
        [outlineView_ refreshToolTips];
    }
    [self outlineActiveRefresh:NO];
}

// The pane the window shows: the reader's choice, and only when the
// open document has headings to list. A page without headings, a
// diagram or a picture has an empty outline, and a strip of nothing
// beside it would only take width from what is on screen.
- (void)applyOutlineState {
    const bool show = outlinePreferred_ && outlineHasHeadings_;
    if (show == outlineVisible_) {
        // The pane stays as it is, but the command's enabled state
        // follows the document either way.
        [self toolbarSync];
        return;
    }
    outlineVisible_ = show;
    [backdrop_ setHidden:!show];

    // The width is remembered, so the pane comes back the way it was
    // left, and hiding it takes the divider with it: the split pane
    // collapses to nothing, so every point the pane held goes back to
    // the text instead of leaving a strip beside it.
    if (show) {
        CGFloat width = settings_.settings().outlineWidthDip;
        if (width < 120.0) width = 120.0;
        [splitView_ setPosition:width ofDividerAtIndex:0];
        [self outlineLayout];
    } else {
        const NSRect frame = [backdrop_ frame];
        if (frame.size.width > 0.0) {
            settings_.settings().outlineWidthDip =
                static_cast<float>(frame.size.width);
        }
        [splitView_ setPosition:0.0 ofDividerAtIndex:0];
    }
    [splitView_ adjustSubviews];
    [self fitContentPanesToWindow];

    // The viewport changed width, so the document measures itself again
    // for the new line length. The reflow is ours, not a reader's
    // scroll, so the outline keeps the caret's section while it lands
    // (outline spec 16).
    NSClipView* clip = [scrollView_ contentView];
    if (clip && content_) {
        const NSRect clipBounds = [clip bounds];
        content_->SetSize(static_cast<float>(clipBounds.size.width),
                          static_cast<float>(clipBounds.size.height));
        content_->SetScrollOffset(static_cast<float>(clipBounds.origin.y));
        content_->NotifyScrollChanged(true);
    }

    [self toolbarSync];
    [view_ setNeedsDisplay:YES];
}

// A document given to the window decides whether the pane is shown at
// all. The rule is read here, when a file is displayed, and not while
// it is edited: the pane would otherwise come and go under the
// reader's hands as headings are typed, and take the line length with
// it.
- (void)applyOutlineHeads {
    // The Finder can hand a file over before the window exists, so the
    // panes come first here and the rule is read again once they are
    // built.
    if (!outlinePane_ || !splitView_ || !scrollView_ || !backdrop_) return;
    outlinePane_->SetDocument(&app_->GetDocument());
    outlineHasHeadings_ = !outlinePane_->Items().empty();
    [self applyOutlineState];
}

// The reader's command: it moves the choice, and the document decides
// whether the pane can follow it.
- (void)OnToggleOutline {
    if (!outlineHasHeadings_) return;
    outlinePreferred_ = !outlinePreferred_;
    settings_.settings().outlineVisible = outlinePreferred_;
    settings_.Save();
    [self applyOutlineState];
}

- (void)OnToggleMark {
    app_->ToggleMarkSelection();
    [self toolbarSync];
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleEditMode {
    app_->ToggleEditing();
    [self toolbarSync];
    [view_ setNeedsDisplay:YES];
}

- (void)OnClearFormat {
    app_->ClearFormat();
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleStrike {
    ToggleInlineMarker(&app_->buffer(), &app_->selection(), "~~", &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnHeading:(int)level {
    SetHeadingLevel(&app_->buffer(), &app_->selection(), level,
                    &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleBullets {
    ToggleUnorderedList(&app_->buffer(), &app_->selection(), &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleNumbering {
    ToggleOrderedList(&app_->buffer(), &app_->selection(), &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleQuote {
    ToggleBlockquote(&app_->buffer(), &app_->selection(), &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnInsertTable {
    // The same Word-style grid picker the Windows build opens. The
    // Windows picker hangs below its Table button; the mac has no
    // Table button on the toolbar, so the picker opens below the
    // pointer. The guideline forbids following the document caret,
    // and this never does.
    const NSPoint point = [NSEvent mouseLocation];

    __weak MdAppDelegate* weakSelf = self;
    [MdTablePicker presentBelowAnchor:point
                             onCommit:^(int cols, int rows) {
        MdAppDelegate* strongSelf = weakSelf;
        if (!strongSelf) return;
        [strongSelf insertTableFromPickerCols:cols rows:rows];
    }];
}

- (void)insertTableFromPickerCols:(int)cols rows:(int)rows {
    app_->InsertTable(cols, rows);
    [view_ setNeedsDisplay:YES];
}

- (void)OnAddTableRow:(BOOL)below {
    app_->AddTableRow(below ? true : false);
    [view_ setNeedsDisplay:YES];
}

- (void)OnRemoveTableRow {
    app_->RemoveTableRow();
    [view_ setNeedsDisplay:YES];
}

- (void)OnAddTableColumn:(BOOL)right {
    app_->AddTableColumn(right ? true : false);
    [view_ setNeedsDisplay:YES];
}

- (void)OnRemoveTableColumn {
    app_->RemoveTableColumn();
    [view_ setNeedsDisplay:YES];
}

- (void)OnSplitTableCell {
    app_->SplitTableCell();
    [view_ setNeedsDisplay:YES];
}

- (void)OnAlignTableColumn:(int)mark {
    app_->AlignTableColumn(mark);
    [view_ setNeedsDisplay:YES];
}

- (void)OnRemoveTable {
    app_->RemoveTable();
    [self toolbarSync];
    [view_ setNeedsDisplay:YES];
}

- (void)OnIndent {
    IndentLine(&app_->buffer(), &app_->selection(), &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnOutdent {
    OutdentLine(&app_->buffer(), &app_->selection(), &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleBold {
    app_->ToggleBold();
    [view_ setNeedsDisplay:YES];
    [self toolbarSync];
}

- (void)OnToggleItalic {
    app_->ToggleItalic();
    [view_ setNeedsDisplay:YES];
    [self toolbarSync];
}

- (void)OnToggleCode {
    ToggleInlineMarker(&app_->buffer(), &app_->selection(), "`", &app_->undo());
    app_->OnTextChanged();
    [view_ setNeedsDisplay:YES];
}

// Cmd+K opens the link dialog. The document only changes when there is
// a selection and the destination is not empty; a cancel, an empty
// destination or no selection changes nothing (text guide, links on a
// selection).
- (void)OnToggleLink {
    const bool hadSelection = !app_->selection().Empty();

    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Insert Link"];
    [alert setInformativeText:@"The destination of the link."];
    [alert addButtonWithTitle:@"Insert"];
    [alert addButtonWithTitle:@"Cancel"];

    NSTextField* urlField =
        [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 264, 24)];
    // A selection that is exactly a link shows its destination, so the
    // dialog edits the link instead of retyping it (guide rule 6).
    std::string existing;
    if (LinkDestinationAt(app_->buffer(), app_->selection(), &existing) &&
        !existing.empty()) {
        [urlField setStringValue:[NSString stringWithUTF8String:existing.c_str()]];
    }
    [alert setAccessoryView:urlField];

    [alert beginSheetModalForWindow:window_
                  completionHandler:^(NSModalResponse response) {
        const char* entered = [[urlField stringValue] UTF8String];
        const std::string url = entered ? entered : "";
        if (response != NSAlertFirstButtonReturn || url.empty() ||
            !hadSelection) {
            return;
        }
        InsertLink(&app_->buffer(), &app_->selection(), url, &app_->undo());
        app_->OnTextChanged();
        [view_ setNeedsDisplay:YES];
    }];
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
    const std::string text = app_->TextForSaving();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return YES;
}

// Sets the zoom factor, and stores it for the next launch.
// The zoom goes through the view, which keeps the anchor point where
// it was (zoom guideline 1.3 and 2.3).
- (void)setZoom:(float)factor {
    [view_ zoomTo:factor];
}

// The zoom landed somewhere: the window keeps the value, so the next
// settings write saves it.
- (void)zoomDidChange:(float)factor {
    zoom_ = factor;
    settings_.settings().zoomFactor = factor;
}

// Shows an error to the user.
// Says that something worked.
- (void)reportMessage:(NSString*)message {
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:message];
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

- (void)reportError:(NSString*)message {
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:message];
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

// Reports what an office operation did.
- (void)reportInfo:(NSString*)message {
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:message];
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

// The compatibility report as alert text: one "- feature: detail" line
// per dropped or approximated feature. Empty when there was nothing to
// report.
- (NSString*)compatNotesText:(const office::CompatReport&)report {
    if (report.warnings.empty()) return @"";
    NSMutableString* text = [NSMutableString string];
    for (const office::CompatWarning& warning : report.warnings) {
        [text appendString:@"- "];
        [text appendString:[NSString
            stringWithUTF8String:warning.feature.c_str()]];
        if (!warning.detail.empty()) {
            [text appendString:@": "];
            [text appendString:[NSString
                stringWithUTF8String:warning.detail.c_str()]];
        }
        [text appendString:@"\n"];
    }
    return text;
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
        // The breadcrumbs name each stage of the start-up, so a launch
        // that dies before the window appears can be read back from
        // ~/Library/Logs/MarkDownIt/crash_trace.log.
        mac::TraceMac("main enter");
        // A crash writes its backtrace to the same log, so the next
        // report names the code that faulted.
        mac::InstallCrashHandler();

        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];

        MdAppDelegate* delegate = [[MdAppDelegate alloc] init];
        [application setDelegate:delegate];
        mac::TraceMac("the delegate is assigned and the run loop starts");
        [application run];
    }
    return 0;
}
