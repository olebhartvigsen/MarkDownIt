// The macOS entry point.
//
// The Windows build has a wWinMain that creates the app and shows a
// window. This one has a standard NSApplication, an application
// delegate and a window, and the file the user opened comes either from
// the command line or from the Finder.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>

#include "app_mac.h"
#include "content_nsview_mac.h"
#include "outline_nsview_mac.h"
#include "outline_pane_mac.h"
#include "content_view_mac.h"
#include <sstream>

#include "formatting.h"
#include "office/docx_export.h"
#include "office/docx_import.h"
#include "office/markdown_bridge.h"
#include "office/pdf_export.h"
#include "welcome_mac.h"
#include "findbar_mac.h"
#include "menu_mac.h"
#include "ribbon_mac.h"
#include "table_picker_mac.h"
#include "crash_trace_mac.h"
#include "fonts_mac.h"
#include "settings_mac.h"
#include "zoommodel.h"

// The helpers of the command strip. The strip asks what state the
// caret is in, and these read the text for it.
namespace {

// The heading level of the line at |lineStart|, 0 when the line is not
// a heading. The strip lights H1 to H3 from this.
int HeadingLevelAt(const std::string& text, uint32_t lineStart) {
    size_t level = 0;
    while (lineStart + level < text.size() && text[lineStart + level] == '#') {
        ++level;
    }
    if (level == 0 || level > 6) return 0;
    const size_t after = lineStart + level;
    if (after < text.size() && text[after] == ' ')
        return static_cast<int>(level);
    return 0;
}

// Whether the line at |lineStart| opens with |prefix|.
bool LineHasPrefix(const std::string& text, uint32_t lineStart,
                   const char* prefix) {
    const size_t length = std::strlen(prefix);
    return lineStart + length <= text.size() &&
           text.compare(lineStart, length, prefix) == 0;
}

}  // namespace

// The window and the app.
//
// The delegate owns the app object, the window and the content view. It
// implements the AppKit selectors, which the menu items and the command
// strip are wired to.
@interface MdAppDelegate
    : NSObject <NSApplicationDelegate, NSWindowDelegate, MdRibbonHost> {
    mac::App* app_;
    mac::ContentView* content_;
    // The window content, which holds the command strip, the find bar
    // and the document.
    NSStackView* container_;
    // The command strip, the mac twin of the Windows ribbon.
    MdRibbonView* ribbon_;

    // The outline pane and the view that draws it.
    mac::OutlinePane* outlinePane_;
    MdOutlineView* outlineView_;
    NSSplitView* splitView_;
    bool outlineVisible_;
    mac::MacSettings settings_;
    NSWindow* window_;
    NSScrollView* scrollView_;
    MdContentView* view_;
    float zoom_;
    bool sourceView_;
    // Whether the text is wrapped.
    bool wrap_;
    // The width mode of the text column.
    int contentWidthMode_;
}
- (instancetype)init;
- (void)OpenPath:(const std::string&)path;

// The menu commands, as AppKit selectors.
- (void)newDocument:(id)sender;
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
- (void)exportWord:(id)sender;
- (void)exportPdf:(id)sender;
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
- (void)toggleWrap:(id)sender;
- (void)cycleContentWidth:(id)sender;
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

@implementation MdAppDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        app_ = new mac::App();
        content_ = new mac::ContentView(app_);
        app_->SetContentView(content_);
        zoom_ = zoom::kDefaultZoom;
        sourceView_ = false;
        wrap_ = true;
        contentWidthMode_ = 0;
    }
    return self;
}

- (void)dealloc {
    // The content view belongs to the app object, so it goes with it.
    delete app_;
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
    contentWidthMode_ = settings_.settings().contentWidthMode;
    if (contentWidthMode_ < 0 || contentWidthMode_ > 3) {
        contentWidthMode_ = 0;
    }
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
    [window_ setMinSize:NSMakeSize(480, 320)];

    // The content view goes in a scroll view, so the document scrolls
    // when it is longer than the window.
    view_ = [[MdContentView alloc] initWithContent:content_ app:app_];
    scrollView_ = [[NSScrollView alloc] initWithFrame:frame];
    [scrollView_ setHasVerticalScroller:YES];
    [scrollView_ setHasHorizontalScroller:NO];
    [scrollView_ setAutohidesScrollers:YES];
    [scrollView_ setDocumentView:view_];

    // The outline pane sits to the left of the document, in a split
    // view, so the reader can drag the divider between them.
    outlinePane_ = new mac::OutlinePane();
    outlineView_ = [[MdOutlineView alloc] initWithPane:outlinePane_];
    outlineVisible_ = settings_.settings().outlineVisible;
    [outlineView_ setHidden:!outlineVisible_];

    // The pane borrows the document of the app and reads it again
    // after every reparse.
    outlinePane_->SetDocument(&app_->GetDocument());
    outlinePane_->SetPalette(BasePalette());
    app_->SetDocumentChangedHandler([self] {
        [self outlinePaneRefresh];
        // The strip follows the document: its toggles and its table
        // commands depend on what the caret is in.
        [self ribbonSync];
    });

    // The strip lights its format toggles from the selection, so it
    // reads the state again after every move of the caret.
    app_->SetSelectionChangedHandler([self] {
        [self ribbonSync];
    });

    // A click on a heading puts the caret there and scrolls it into
    // view. The pane never touches the document itself.
    outlinePane_->SetNavigateCallback([self](uint32_t offset) {
        app_->SetCaret(offset);
        content_->ScrollToOffset(offset);
        [view_ setNeedsDisplay:YES];
    });

    // The find bar sits above the document. It is in the layout from
    // the start and is hidden when it is not in use, so the document
    // does not jump around when the bar opens and closes.
    NSView* findBarView = (__bridge NSView*)app_->findBar().view();
    const float findBarHeight = app_->findBar().Height();
    [findBarView setTranslatesAutoresizingMaskIntoConstraints:NO];
    [NSLayoutConstraint activateConstraints:@[
        [findBarView.heightAnchor constraintEqualToConstant:findBarHeight]
    ]];

    splitView_ = [[NSSplitView alloc] initWithFrame:frame];
    [splitView_ setVertical:YES];
    [splitView_ setDividerStyle:NSSplitViewDividerStyleThin];
    [splitView_ addSubview:outlineView_];
    [splitView_ addSubview:scrollView_];
    // The place of the divider is the width of the pane, because the
    // pane is the first of the two.
    [splitView_ setPosition:settings_.settings().outlineWidthDip
               ofDividerAtIndex:0];

    container_ = [[NSStackView alloc] initWithFrame:frame];
    [container_ setOrientation:NSUserInterfaceLayoutOrientationVertical];
    [container_ setDistribution:NSStackViewDistributionFill];

    // The command strip sits at the top, the way the Windows ribbon
    // does.
    ribbon_ = [MdRibbonView ribbonWithHost:self];
    [ribbon_ setTranslatesAutoresizingMaskIntoConstraints:NO];
    [[ribbon_ heightAnchor]
        constraintEqualToConstant:[MdRibbonView ribbonHeight]].active = YES;
    [container_ addArrangedSubview:ribbon_];
    [container_ addArrangedSubview:findBarView];
    [container_ addArrangedSubview:splitView_];

    [window_ setContentView:container_];
    [window_ makeKeyAndOrderFront:nil];
    // The document view takes the keyboard from the start, so typing
    // reaches the edit controller without a click first.
    [window_ makeFirstResponder:view_];
    mac::TraceMac("the window is on screen");

    // The delegate takes the files that are dropped on the window.
    [window_ registerForDraggedTypes:@[NSPasteboardTypeFileURL]];

    // Without a file the welcome screen takes the place of the
    // document.
    [self showWelcomeScreen];
    mac::TraceMac("the welcome screen is up");

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
    [self saveWindowFrame];
    settings_.Save();
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

    [window_ setTitle:[NSString stringWithUTF8String:path.c_str()]];
    [view_ setNeedsDisplay:YES];
    mac::TraceMac("open: done");
}

// ---------------------------------------------------------------------------
// The menu actions, as AppKit selectors
// ---------------------------------------------------------------------------

- (void)newDocument:(id)sender { (void)sender; [self OnNewDocument]; }
- (void)openDocument:(id)sender { (void)sender; [self OnOpen]; }
- (void)reloadDocument:(id)sender { (void)sender; [self OnReload]; }
- (void)openSettings:(id)sender { (void)sender; [self OnOpenSettings]; }
- (void)setAsDefaultReader:(id)sender {
    (void)sender;
    [self OnSetAsDefaultReader];
}
- (void)saveDocument:(id)sender { (void)sender; [self OnSave]; }
- (void)saveDocumentAs:(id)sender { (void)sender; [self OnSaveAs]; }
- (void)closeDocument:(id)sender { (void)sender; [self OnClose]; }
- (void)importWord:(id)sender { (void)sender; [self OnImportWord]; }
- (void)exportWord:(id)sender { (void)sender; [self OnExportWord]; }
- (void)exportPdf:(id)sender { (void)sender; [self OnExportPdf]; }
- (void)findDocument:(id)sender { (void)sender; [self OnFind]; }
- (void)replaceInDocument:(id)sender { (void)sender; [self OnReplace]; }
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
- (void)toggleWrap:(id)sender { (void)sender; [self OnToggleWrap]; }
- (void)cycleContentWidth:(id)sender { (void)sender; [self OnCycleContentWidth]; }
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

// ---------------------------------------------------------------------------
// The commands
// ---------------------------------------------------------------------------

- (void)OnNewDocument {
    if (![self confirmDiscardChanges]) return;
    app_->NewDocument();
    [window_ setTitle:@"Untitled"];
    [self showWelcomeScreen];
    [view_ setNeedsDisplay:YES];
}

// Shows the welcome screen, which takes the place of the document when
// no file is open.
- (void)showWelcomeScreen {
    app_->SetWelcomeMode(true);
    content_->SetWelcomeFiles(settings_.recentFilesWithPreview());
    // The strip's document commands have nothing to act on here.
    [self ribbonSync];
}

// Hides the welcome screen, because a document is open now.
- (void)hideWelcomeScreen {
    app_->SetWelcomeMode(false);
    [self ribbonSync];
}

- (void)OnOpen {
    if (![self confirmDiscardChanges]) return;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setAllowedFileTypes:@[@"md", @"markdown", @"mdown", @"mkd"]];
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

    app_->MarkClean();
    settings_.AddRecentFile(path);
    settings_.Save();
    [window_ setTitle:[NSString stringWithUTF8String:path.c_str()]];
    return YES;
}

- (void)OnImportWord {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setAllowedFileTypes:@[@"docx"]];
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
        [self reportError:@"The Word file could not be read."];
        return;
    }

    // The document becomes the markdown of the Word file.
    const std::string markdown = office::OfficeModelToMarkdown(model, report);
    app_->SetText(markdown);
    [self hideWelcomeScreen];
    [view_ setNeedsDisplay:YES];
}

- (void)OnExportWord {
    [self exportOfficeDocument:false];
}

- (void)OnExportPdf {
    [self exportOfficeDocument:true];
}

// Writes the document as a Word file or as a PDF.
- (void)exportOfficeDocument:(bool)asPdf {
    office::DocModel model;
    office::CompatReport report;
    std::string error;
    if (!office::MarkdownToOfficeModel(app_->sourceText(), model, report,
                                      error)) {
        [self reportError:@"The document could not be read."];
        return;
    }

    std::string bytes;
    if (asPdf) {
        office::PdfExportOptions options;
        if (!office::PdfExport(model, options, bytes, report, error)) {
            [self reportError:@"The PDF could not be written."];
            return;
        }
    } else {
        if (!office::DocxExport(model, bytes, report, error)) {
            [self reportError:@"The Word file could not be written."];
            return;
        }
    }

    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setAllowedFileTypes:asPdf ? @[@"pdf"] : @[@"docx"]];
    [panel setNameFieldStringValue:asPdf ? @"Untitled.pdf" : @"Untitled.docx"];
    if ([panel runModal] != NSModalResponseOK) return;

    const std::string path = [[panel URL] path].UTF8String;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        [self reportError:@"The file could not be saved."];
        return;
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
}

- (void)OnClose {
    // The window asks for the changes itself, so the question is asked
    // in one place.
    [window_ close];
}

- (void)OnFind {
    app_->ShowFind(FindBarMode::Find);
}

- (void)OnReplace {
    app_->ShowFind(FindBarMode::FindReplace);
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
    [self ribbonSync];
    [view_ setNeedsDisplay:YES];
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
    [view_ setNeedsDisplay:YES];
}

- (void)OnOpenSettings {
    // The settings are a JSON file, so they open in the editor the user
    // already has. The path is in the same place the Windows build keeps
    // its settings.
    NSString* path = [NSString stringWithFormat:
        @"~/Library/Application Support/MarkDownIt/settings.json"];
    NSString* full = [path stringByExpandingTildeInPath];
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:full]];
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
    [self ribbonSync];
    [view_ setNeedsDisplay:YES];
}

- (void)OnCycleContentWidth {
    // The width modes are the ones the settings keep, and the command
    // walks through them.
    contentWidthMode_ = (contentWidthMode_ + 1) % 4;
    content_->SetContentWidthMode(contentWidthMode_);
    settings_.settings().contentWidthMode = contentWidthMode_;
    settings_.Save();
    [view_ setNeedsDisplay:YES];
}

// The two questions the command strip asks. The strip carries its
// commands out through the same selectors the menu items use; these
// methods only report the state the buttons show.

- (BOOL)ribbonCommandOn:(NSString*)commandId {
    const std::string& text = app_->sourceText();
    const uint32_t start = app_->selection().Start();
    const uint32_t end = app_->selection().End();
    const bool hasSelection = !app_->selection().Empty();

    if ([commandId isEqualToString:@"cmdWrap"]) return wrap_;
    if ([commandId isEqualToString:@"cmdOutline"]) return outlineVisible_;
    if ([commandId isEqualToString:@"cmdViewSource"]) return sourceView_;
    if ([commandId isEqualToString:@"cmdBold"])
        return hasSelection && IsWrappedIn(text, start, end, "**");
    if ([commandId isEqualToString:@"cmdItalic"])
        return hasSelection && IsWrappedIn(text, start, end, "*");
    if ([commandId isEqualToString:@"cmdStrike"])
        return hasSelection && IsWrappedIn(text, start, end, "~~");
    if ([commandId isEqualToString:@"cmdCode"])
        return hasSelection && IsWrappedIn(text, start, end, "`");
    if ([commandId isEqualToString:@"cmdMark"]) {
        if (!hasSelection) return NO;
        for (const TextMarker& marker : app_->markers().Markers()) {
            if (!marker.resolved) continue;
            if (marker.start <= start && end <= marker.end) return YES;
        }
        return NO;
    }

    const uint32_t lineStart = app_->buffer().LineStart(start);
    if ([commandId isEqualToString:@"cmdHeading1"])
        return HeadingLevelAt(text, lineStart) == 1;
    if ([commandId isEqualToString:@"cmdHeading2"])
        return HeadingLevelAt(text, lineStart) == 2;
    if ([commandId isEqualToString:@"cmdHeading3"])
        return HeadingLevelAt(text, lineStart) == 3;
    if ([commandId isEqualToString:@"cmdBullets"])
        return LineHasPrefix(text, lineStart, "- ") ||
               LineHasPrefix(text, lineStart, "* ") ||
               LineHasPrefix(text, lineStart, "+ ");
    if ([commandId isEqualToString:@"cmdNumbering"]) {
        return lineStart + 2 < text.size() && text[lineStart] >= '0' &&
               text[lineStart] <= '9' && text[lineStart + 1] == '.' &&
               text[lineStart + 2] == ' ';
    }
    if ([commandId isEqualToString:@"cmdQuote"])
        return LineHasPrefix(text, lineStart, "> ");
    return NO;
}

// Whether a strip command is available right now. The table commands
// need the caret in a table; the undo pair needs something to undo.
- (BOOL)ribbonCommandAvailable:(NSString*)commandId {
    if (app_->WelcomeMode()) {
        // The welcome screen holds no document, so a document command
        // has nothing to act on.
        return NO;
    }
    if ([commandId isEqualToString:@"cmdUndo"]) return app_->undo().CanUndo();
    if ([commandId isEqualToString:@"cmdRedo"]) return app_->undo().CanRedo();
    if ([commandId hasPrefix:@"cmdAdd"] || [commandId hasPrefix:@"cmdAlign"] ||
        [commandId isEqualToString:@"cmdRemoveRow"] ||
        [commandId isEqualToString:@"cmdRemoveColumn"] ||
        [commandId isEqualToString:@"cmdSplitCell"] ||
        [commandId isEqualToString:@"cmdRemoveTable"]) {
        tableedit::TableLocation location;
        return app_->CaretTable(&location);
    }
    return YES;
}

// Re-reads the command states of the strip.
- (void)ribbonSync {
    [ribbon_ refresh];
}

// The document changed, so the pane reads it again.
- (void)outlinePaneRefresh {
    outlinePane_->SetDocument(&app_->GetDocument());
    outlinePane_->SetCaretOffset(app_->selection().Start());
    [outlineView_ setNeedsDisplay:YES];
}

- (void)OnToggleOutline {
    outlineVisible_ = !outlineVisible_;
    // Hiding the view takes the divider with it, so the document takes
    // the whole width.
    [outlineView_ setHidden:!outlineVisible_];

    // The width is remembered, so the pane comes back the way it was
    // left.
    if (outlineVisible_) {
        [splitView_ setPosition:settings_.settings().outlineWidthDip
                   ofDividerAtIndex:0];
    } else {
        const NSRect frame = [outlineView_ frame];
        if (frame.size.width > 0.0) {
            settings_.settings().outlineWidthDip =
                static_cast<float>(frame.size.width);
        }
    }
    settings_.settings().outlineVisible = outlineVisible_;
    settings_.Save();

    [self ribbonSync];
    [view_ setNeedsDisplay:YES];
}

- (void)OnToggleMark {
    app_->ToggleMarkSelection();
    [self ribbonSync];
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
    // The same Word-style grid picker the Windows build opens, hung below
    // the Table button in the strip. When the command comes from the menu
    // bar the button may be on another tab, so the picker hangs below the
    // strip at the pointer instead. The guideline forbids following the
    // document caret, and this never does.
    NSView* anchor = [ribbon_ viewForCommand:@"cmdInsertTable"];
    NSPoint point = NSZeroPoint;
    BOOL anchored = NO;
    if (anchor && [anchor window] && ![anchor isHiddenOrHasHiddenAncestor]) {
        const NSRect inWindow = [anchor convertRect:[anchor bounds] toView:nil];
        const NSRect onScreen = [[anchor window] convertRectToScreen:inWindow];
        // AppKit counts y upward, so the button's bottom edge is its minY,
        // and the picker's top-left corner hangs from there.
        point = NSMakePoint(NSMinX(onScreen), NSMinY(onScreen));
        anchored = YES;
    }
    if (!anchored) {
        const NSPoint pointer = [NSEvent mouseLocation];
        const NSRect strip = [ribbon_ convertRect:[ribbon_ bounds] toView:nil];
        const NSRect onScreen = [[ribbon_ window] convertRectToScreen:strip];
        point = NSMakePoint(pointer.x, NSMinY(onScreen));
    }

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
    [self ribbonSync];
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
    const std::string text = app_->TextForSaving();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return YES;
}

// Sets the zoom factor, and stores it for the next launch.
- (void)setZoom:(float)factor {
    zoom_ = zoom::Clamp(factor);
    settings_.settings().zoomFactor = zoom_;
    // The document follows the zoom, the way the Windows window does.
    content_->SetZoom(zoom_);
    [view_ setNeedsDisplay:YES];
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
