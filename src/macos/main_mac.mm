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
#include <fstream>
#include <sstream>

#include "formatting.h"
#include "office/docx_export.h"
#include "office/docx_import.h"
#include "office/markdown_bridge.h"
#include "office/pdf_export.h"
#include "welcome_mac.h"
#include "findbar_mac.h"
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
    // The window content, which holds the find bar and the document.
    NSStackView* container_;
    mac::MacSettings settings_;
    NSWindow* window_;
    NSScrollView* scrollView_;
    MdContentView* view_;
    MdMenuAdapter* adapter_;
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
- (void)toggleBullets:(id)sender;
- (void)toggleNumbering:(id)sender;
- (void)toggleQuote:(id)sender;
- (void)indentBlock:(id)sender;
- (void)outdentBlock:(id)sender;
- (void)insertTable:(id)sender;
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
    void OnReload() override { [delegate_ reloadDocument:nil]; }
    void OnOpenSettings() override { [delegate_ openSettings:nil]; }
    void OnSave() override { [delegate_ saveDocument:nil]; }
    void OnSaveAs() override { [delegate_ saveDocumentAs:nil]; }
    void OnClose() override { [delegate_ closeDocument:nil]; }
    void OnImportWord() override { [delegate_ importWord:nil]; }
    void OnExportWord() override { [delegate_ exportWord:nil]; }
    void OnExportPdf() override { [delegate_ exportPdf:nil]; }
    void OnFind() override { [delegate_ findDocument:nil]; }
    void OnReplace() override { [delegate_ replaceInDocument:nil]; }
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
    void OnToggleWrap() override { [delegate_ toggleWrap:nil]; }
    void OnCycleContentWidth() override { [delegate_ cycleContentWidth:nil]; }
    void OnToggleBold() override { [delegate_ toggleBold:nil]; }
    void OnToggleStrike() override { [delegate_ toggleStrike:nil]; }
    void OnHeading(int level) override { [delegate_ setHeading:level]; }
    void OnToggleBullets() override { [delegate_ toggleBullets:nil]; }
    void OnToggleNumbering() override { [delegate_ toggleNumbering:nil]; }
    void OnToggleQuote() override { [delegate_ toggleQuote:nil]; }
    void OnIndent() override { [delegate_ indentBlock:nil]; }
    void OnOutdent() override { [delegate_ outdentBlock:nil]; }
    void OnInsertTable() override { [delegate_ insertTable:nil]; }
    void OnAddTableRow(bool below) override {
        if (below) {
            [delegate_ addRowBelow:nil];
        } else {
            [delegate_ addRowAbove:nil];
        }
    }
    void OnRemoveTableRow() override { [delegate_ removeRow:nil]; }
    void OnAddTableColumn(bool right) override {
        if (right) {
            [delegate_ addColumnRight:nil];
        } else {
            [delegate_ addColumnLeft:nil];
        }
    }
    void OnRemoveTableColumn() override { [delegate_ removeColumn:nil]; }
    void OnSplitTableCell() override { [delegate_ splitCell:nil]; }
    void OnAlignTableColumn(int mark) override {
        if (mark == 1) {
            [delegate_ alignLeft:nil];
        } else if (mark == 2) {
            [delegate_ alignCenter:nil];
        } else {
            [delegate_ alignRight:nil];
        }
    }
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
        wrap_ = true;
        contentWidthMode_ = 0;
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
    contentWidthMode_ = settings_.settings().contentWidthMode;
    if (contentWidthMode_ < 0 || contentWidthMode_ > 3) {
        contentWidthMode_ = 0;
    }
    content_->SetContentWidthMode(contentWidthMode_);

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

    // The find bar sits above the document. It is in the layout from
    // the start and is hidden when it is not in use, so the document
    // does not jump around when the bar opens and closes.
    NSView* findBarView = (__bridge NSView*)app_->findBar().view();
    const float findBarHeight = app_->findBar().Height();
    [findBarView setTranslatesAutoresizingMaskIntoConstraints:NO];
    [NSLayoutConstraint activateConstraints:@[
        [findBarView.heightAnchor constraintEqualToConstant:findBarHeight]
    ]];

    container_ = [[NSStackView alloc] initWithFrame:frame];
    [container_ setOrientation:NSUserInterfaceLayoutOrientationVertical];
    [container_ setDistribution:NSStackViewDistributionFill];
    [container_ addArrangedSubview:findBarView];
    [container_ addArrangedSubview:scrollView_];

    [window_ setContentView:container_];
    [window_ makeKeyAndOrderFront:nil];

    // Without a file the welcome screen takes the place of the
    // document.
    [self showWelcomeScreen];

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
    [self OpenPath:[filename UTF8String]];
    return YES;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    // The handler points at this object, so it goes away with it.
    app_->SetReloadHandler(nullptr);
    (void)notification;
    settings_.settings().zoomFactor = zoom_;
    [self saveWindowFrame];
    settings_.Save();
}

// Opens a file, and reports a failure to the user.
- (void)OpenPath:(const std::string&)path {
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
}

// ---------------------------------------------------------------------------
// The menu actions, as AppKit selectors
// ---------------------------------------------------------------------------

- (void)newDocument:(id)sender { (void)sender; [self OnNewDocument]; }
- (void)openDocument:(id)sender { (void)sender; [self OnOpen]; }
- (void)reloadDocument:(id)sender { (void)sender; [self OnReload]; }
- (void)openSettings:(id)sender { (void)sender; [self OnOpenSettings]; }
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
- (void)splitCell:(id)sender { (void)sender; [self OnSplitTableCell]; }
- (void)alignLeft:(id)sender { (void)sender; [self OnAlignTableColumn:1]; }
- (void)alignCenter:(id)sender { (void)sender; [self OnAlignTableColumn:2]; }
- (void)alignRight:(id)sender { (void)sender; [self OnAlignTableColumn:3]; }
- (void)toggleItalic:(id)sender { (void)sender; [self OnToggleItalic]; }
- (void)toggleCode:(id)sender { (void)sender; [self OnToggleCode]; }
- (void)toggleLink:(id)sender { (void)sender; [self OnToggleLink]; }

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
}

// Hides the welcome screen, because a document is open now.
- (void)hideWelcomeScreen {
    app_->SetWelcomeMode(false);
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
    if (answer == NSAlertSecondButtonReturn) return YES;
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
    if (![self confirmDiscardChanges]) return;
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

- (void)OnToggleWrap {
    wrap_ = !wrap_;
    content_->SetWrap(wrap_);
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

- (void)OnToggleOutline {
    // The outline pane is not on the mac layer yet. The command is in
    // the menu so the key bindings match the Windows build, and it
    // reports that rather than doing nothing silently.
    [self reportError:@"The outline is not available in this build yet."];
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
    // The size of the new table is asked for, the way the Windows build
    // asks for it with its grid picker.
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Insert Table"];
    [alert addButtonWithTitle:@"Insert"];
    [alert addButtonWithTitle:@"Cancel"];

    NSTextField* columns = [[NSTextField alloc] initWithFrame:
        NSMakeRect(0, 0, 60, 24)];
    columns.stringValue = @"3";
    NSTextField* rows = [[NSTextField alloc] initWithFrame:
        NSMakeRect(0, 0, 60, 24)];
    rows.stringValue = @"3";

    NSTextField* columnsLabel = [[NSTextField alloc] initWithFrame:
        NSZeroRect];
    columnsLabel.stringValue = @"Columns";
    columnsLabel.editable = NO;
    columnsLabel.bordered = NO;
    columnsLabel.drawsBackground = NO;
    NSTextField* rowsLabel = [[NSTextField alloc] initWithFrame:
        NSZeroRect];
    rowsLabel.stringValue = @"Rows";
    rowsLabel.editable = NO;
    rowsLabel.bordered = NO;
    rowsLabel.drawsBackground = NO;

    NSStackView* stack = [NSStackView stackViewWithViews:
        @[columnsLabel, columns, rowsLabel, rows]];
    stack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    [alert setAccessoryView:stack];

    if ([alert runModal] != NSAlertFirstButtonReturn) return;

    const int cols = columns.integerValue;
    const int rowsCount = rows.integerValue;
    if (cols < 1 || cols > 20 || rowsCount < 1 || rowsCount > 50) return;

    app_->InsertTable(cols, rowsCount);
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
