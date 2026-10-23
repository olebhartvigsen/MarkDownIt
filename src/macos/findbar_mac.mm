// The macOS find bar.
//
// The bar is an AppKit view with a search field, a replace field, the
// buttons and the counter. The search state lives in the shared model,
// so the bar and the Windows bar behave the same.
//
// The bar only collects the query, the replacement and the options. The
// owner refreshes the model against the document and turns the result
// into a selection, so the bar never touches the document itself.
//
// The AppKit parts sit at the global scope, because Objective-C
// declarations are not allowed inside a namespace. The C++ class stays
// in the mac namespace.

#import <AppKit/AppKit.h>

#include "findbar_mac.h"
#include "find_strings.h"
#include "fonts_mac.h"
#include "strings_mac.h"

#include <string>

#include "textenc.h"

namespace {

// The height of the bar, in points. The two rows of controls need this
// much room.
constexpr float kFindBarHeight = 68.0f;

// The width of the search and the replace field, in points.
constexpr float kFindFieldWidth = 240.0f;

// The width of the counter, in points.
constexpr float kCounterWidth = 120.0f;

}  // namespace

// The wide string of the model, as an AppKit string.
static NSString* NarrowText(const std::wstring& text) {
    return [NSString stringWithUTF8String:textenc::WideToUtf8(text).c_str()];
}

// The text of a field, in the wide string the model uses.
static std::wstring WideText(NSString* text) {
    if (!text) return std::wstring();
    return textenc::Utf8ToWide(std::string([text UTF8String]));
}

// The counter the Windows bar shows: "3 of 17" while matches exist,
// "No matches" when the query matched nothing, and nothing at all
// while the field is empty (guide 3, 21 and 52). The strings come
// from the shared table, so both bars say the same thing.
static std::wstring CounterText(const FindBarModel& model) {
    if (model.status() == FindStatus::Empty) return std::wstring();
    if (model.status() == FindStatus::NoMatches)
        return findstrings::kNoMatches;
    std::wstring text = std::to_wstring(model.countCurrent());
    text += findstrings::kCounterSeparator;
    text += std::to_wstring(model.countTotal());
    return text;
}

// The AppKit view that holds the controls.
//
// It is written in Objective-C++ because the controls are AppKit
// objects, and the model behind them is C++.
@interface MdFindBarView : NSView <NSTextFieldDelegate>
- (instancetype)initWithModel:(FindBarModel*)model;
- (void)setReplaceMode:(BOOL)replaceMode;
- (void)updateFromModel;
- (void)focusSearchField;
@end

@implementation MdFindBarView {
    FindBarModel* model_;
    NSTextField* searchField_;
    NSTextField* replaceField_;
    NSButton* previousButton_;
    NSButton* nextButton_;
    NSButton* matchCaseButton_;
    NSButton* wholeWordButton_;
    NSButton* replaceButton_;
    NSButton* replaceAllButton_;
    NSButton* closeButton_;
    NSButton* historyButton_;
    NSTextField* counterLabel_;
    NSTextField* statusLabel_;
    NSStackView* searchRow_;
    NSStackView* replaceRow_;
}

- (instancetype)initWithModel:(FindBarModel*)model {
    self = [super initWithFrame:NSMakeRect(0, 0, 640, kFindBarHeight)];
    if (!self) return nil;

    model_ = model;

    [self buildControls];
    [self buildRows];

    return self;
}

// Builds the controls. Every control is created here, so the layout
// code only has to place them.
- (void)buildControls {
    searchField_ = [NSTextField textFieldWithString:@""];
    [searchField_ setPlaceholderString:macstrings::kFindPlaceholder];
    [searchField_ setDelegate:self];

    replaceField_ = [NSTextField textFieldWithString:@""];
    [replaceField_ setPlaceholderString:macstrings::kReplacePlaceholder];
    [replaceField_ setDelegate:self];

    previousButton_ = [self makeButton:@"\u25B2" tooltip:macstrings::kPreviousMatch
                              action:@selector(previousMatch:)];
    nextButton_ = [self makeButton:@"\u25BC" tooltip:macstrings::kNextMatch
                          action:@selector(nextMatch:)];
    matchCaseButton_ = [self makeCheckbox:macstrings::kMatchCase
                                   action:@selector(optionsChanged:)];
    wholeWordButton_ = [self makeCheckbox:macstrings::kWholeWord
                                  action:@selector(optionsChanged:)];
    replaceButton_ = [self makeButton:@"Replace"
                              tooltip:macstrings::kReplaceTheMatch
                              action:@selector(replaceCurrent:)];
    replaceAllButton_ = [self makeButton:@"All"
                                tooltip:macstrings::kReplaceEveryMatch
                                action:@selector(replaceAll:)];
    closeButton_ = [self makeButton:@"\u2715" tooltip:macstrings::kCloseFindBar
                            action:@selector(closeBar:)];
    historyButton_ = [self makeButton:@"\u25BE"
                             tooltip:macstrings::kRecentSearches
                              action:@selector(showHistory:)];

    // The glyph buttons need real names for assistive technology
    // (find spec 2.3 and 44).
    [previousButton_ setAccessibilityLabel:macstrings::kPreviousMatch];
    [nextButton_ setAccessibilityLabel:macstrings::kNextMatch];
    [closeButton_ setAccessibilityLabel:macstrings::kCloseFindBarLabel];
    [historyButton_ setAccessibilityLabel:macstrings::kRecentSearches];
    [searchField_ setAccessibilityLabel:macstrings::kSearchText];
    [replaceField_ setAccessibilityLabel:macstrings::kReplaceText];

    counterLabel_ = [NSTextField labelWithString:@""];
    [counterLabel_ setAlignment:NSTextAlignmentRight];
    [counterLabel_ setTextColor:[NSColor secondaryLabelColor]];

    statusLabel_ = [NSTextField labelWithString:@""];
    [statusLabel_ setTextColor:[NSColor secondaryLabelColor]];
    [statusLabel_ setFont:mac::BundledUIFont(11.0f)];
}

// A button with a title, a tooltip and an action.
- (NSButton*)makeButton:(NSString*)title
                tooltip:(NSString*)tooltip
                 action:(SEL)action {
    NSButton* button = [NSButton buttonWithTitle:title
                                          target:self
                                          action:action];
    [button setBezelStyle:NSBezelStyleRounded];
    [button setToolTip:tooltip];
    return button;
}

// A checkbox with a title and an action.
- (NSButton*)makeCheckbox:(NSString*)title action:(SEL)action {
    NSButton* button = [NSButton checkboxWithTitle:title
                                            target:self
                                            action:action];
    return button;
}

// The two rows of controls.
- (void)buildRows {
    searchRow_ = [NSStackView stackViewWithViews:@[
        searchField_, previousButton_, nextButton_, matchCaseButton_,
        wholeWordButton_, counterLabel_, historyButton_
    ]];
    [searchRow_ setOrientation:NSUserInterfaceLayoutOrientationHorizontal];
    [searchRow_ setAlignment:NSLayoutAttributeCenterY];
    [searchRow_ setSpacing:8.0];

    replaceRow_ = [NSStackView stackViewWithViews:@[
        replaceField_, replaceButton_, replaceAllButton_, statusLabel_
    ]];
    [replaceRow_ setOrientation:NSUserInterfaceLayoutOrientationHorizontal];
    [replaceRow_ setAlignment:NSLayoutAttributeCenterY];
    [replaceRow_ setSpacing:8.0];

    NSStackView* rows = [NSStackView stackViewWithViews:@[searchRow_, replaceRow_]];
    [rows setOrientation:NSUserInterfaceLayoutOrientationVertical];
    [rows setAlignment:NSLayoutAttributeLeading];
    [rows setSpacing:4.0];

    NSStackView* everything =
        [NSStackView stackViewWithViews:@[rows, closeButton_]];
    [everything setOrientation:NSUserInterfaceLayoutOrientationHorizontal];
    [everything setAlignment:NSLayoutAttributeCenterY];
    [everything setSpacing:8.0];

    [self addSubview:everything];
    [everything setTranslatesAutoresizingMaskIntoConstraints:NO];
    [closeButton_ setTranslatesAutoresizingMaskIntoConstraints:NO];

    [NSLayoutConstraint activateConstraints:@[
        [everything.leadingAnchor constraintEqualToAnchor:self.leadingAnchor
                                                 constant:8.0],
        [everything.trailingAnchor constraintEqualToAnchor:self.trailingAnchor
                                                  constant:-8.0],
        [everything.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [closeButton_.topAnchor constraintEqualToAnchor:self.topAnchor
                                                constant:4.0],
        [searchField_.widthAnchor constraintEqualToConstant:kFindFieldWidth],
        [replaceField_.widthAnchor constraintEqualToConstant:kFindFieldWidth],
        [counterLabel_.widthAnchor constraintEqualToConstant:kCounterWidth],
    ]];
}

// Both rows stay on screen in both modes, so the strip keeps its
// height and the layout never jumps; the mode only decides whether
// the replace half is live (findbar_model.h, guide 2.2).
- (void)setReplaceMode:(BOOL)replaceMode {
    (void)replaceMode;
    [self updateFromModel];
}

// The bar shows what the model holds.
- (void)updateFromModel {
    const std::wstring search = model_->GetSearchText();
    if (![searchField_.stringValue isEqualToString:NarrowText(search)]) {
        [searchField_ setStringValue:NarrowText(search)];
    }
    const std::wstring replace = model_->GetReplaceText();
    if (![replaceField_.stringValue isEqualToString:NarrowText(replace)]) {
        [replaceField_ setStringValue:NarrowText(replace)];
    }
    [matchCaseButton_ setState:model_->GetCaseSensitive()
                                   ? NSControlStateValueOn
                                   : NSControlStateValueOff];
    [wholeWordButton_ setState:model_->GetWholeWord()
                                  ? NSControlStateValueOn
                                  : NSControlStateValueOff];

    // The Windows rules, so the two bars cannot disagree: navigation
    // needs a match, and the replace half needs the replace mode and
    // an editable document.
    const bool hasQuery = model_->state().HasQuery();
    const bool navigable = hasQuery &&
                           model_->status() == FindStatus::Found &&
                           model_->countTotal() > 0;
    const bool replaceUsable = model_->Mode() == FindBarMode::FindReplace;
    const bool replaceAvailable =
        replaceUsable && model_->IsReplaceEnabled() && navigable;

    [previousButton_ setEnabled:navigable];
    [nextButton_ setEnabled:navigable];
    [replaceField_ setEnabled:replaceUsable && model_->IsReplaceEnabled()];
    [replaceButton_ setEnabled:replaceAvailable];
    [replaceAllButton_ setEnabled:replaceAvailable];
    [matchCaseButton_ setEnabled:YES];
    [wholeWordButton_ setEnabled:YES];
    [closeButton_ setEnabled:YES];

    // The counter and the status line, the pair the Windows bar keeps
    // (guide 3, 15, 21 and 52).
    [counterLabel_ setStringValue:NarrowText(CounterText(*model_))];
    [statusLabel_ setStringValue:NarrowText(model_->FormatStatusText())];
}

- (void)focusSearchField {
    [self.window makeFirstResponder:searchField_];
}

// ---------------------------------------------------------------------------
// The actions
// ---------------------------------------------------------------------------

// The text of the search field changed.
- (void)controlTextDidChange:(NSNotification*)notification {
    (void)notification;
    if ([notification.object isEqual:searchField_]) {
        model_->SetSearchText(WideText(searchField_.stringValue));
        // A large document waits for a pause in the typing instead
        // of searching on every keystroke, so the bar stays
        // responsive (find spec, large documents); a small one
        // searches right away.
        if ([self debouncedRefreshNeeded]) {
            [NSObject cancelPreviousPerformRequestsWithTarget:self
                                                      selector:@selector(commitSearch:)
                                                        object:nil];
            [self performSelector:@selector(commitSearch:)
                       withObject:nil
                       afterDelay:kFindDebounceDelaySeconds];
        } else {
            const FindBarListener& listener = model_->listener();
            if (listener.onSearchChanged) {
                listener.onSearchChanged();
            }
        }
        // The count and the buttons move with every keystroke.
        [self updateFromModel];
    } else if ([notification.object isEqual:replaceField_]) {
        model_->SetReplaceText(WideText(replaceField_.stringValue));
    }
}

// Whether this keystroke should wait for a pause: the owner answers
// how large the document is, and only a large one gets the delay
// (find spec, large documents).
- (BOOL)debouncedRefreshNeeded {
    const FindBarListener& listener = model_->listener();
    if (!listener.documentSize) return NO;
    return FindRefreshDebounceNeeded(listener.documentSize()) ? YES : NO;
}

// The delayed search, one pause after the last keystroke.
- (void)commitSearch:(id)sender {
    (void)sender;
    const FindBarListener& listener = model_->listener();
    if (listener.onSearchChanged) {
        listener.onSearchChanged();
    }
    [self updateFromModel];
}

- (void)optionsChanged:(id)sender {
    (void)sender;
    model_->SetCaseSensitive(matchCaseButton_.state == NSControlStateValueOn);
    model_->SetWholeWord(wholeWordButton_.state == NSControlStateValueOn);
    const FindBarListener& listener = model_->listener();
    if (listener.onSearchChanged) {
        listener.onSearchChanged();
    }
    [self updateFromModel];
}

// The caret offset the navigation starts from.
- (uint32_t)caretOffset {
    const FindBarListener& listener = model_->listener();
    return listener.caretOffset ? listener.caretOffset() : 0u;
}

// The query the user is working with, remembered whenever a search
// is actually run (find spec: the session remembers recently used
// search strings).
- (void)rememberQuery {
    model_->RememberSearchQuery(model_->GetSearchText());
}

- (void)nextMatch:(id)sender {
    (void)sender;
    [self rememberQuery];
    model_->NavigateNext([self caretOffset]);
    // The counter reads the ordinal the navigation just moved.
    model_->SyncFromState();
    [self updateFromModel];
}

- (void)previousMatch:(id)sender {
    (void)sender;
    [self rememberQuery];
    model_->NavigatePrevious([self caretOffset]);
    // The counter reads the ordinal the navigation just moved.
    model_->SyncFromState();
    [self updateFromModel];
}

- (void)replaceCurrent:(id)sender {
    (void)sender;
    [self rememberQuery];
    const FindBarListener& listener = model_->listener();
    TextMatch current{};
    if (model_->state().CurrentMatch(&current) && listener.onReplaceCurrent) {
        listener.onReplaceCurrent(current.start, current.length);
    }
    model_->SyncFromState();
    [self updateFromModel];
}

- (void)replaceAll:(id)sender {
    (void)sender;
    [self rememberQuery];
    const FindBarListener& listener = model_->listener();
    if (listener.onReplaceAll) {
        listener.onReplaceAll(model_->GetSearchText(), model_->GetReplaceText(),
                              model_->state().Options());
    }
    model_->SyncFromState();
    [self updateFromModel];
}

- (void)closeBar:(id)sender {
    (void)sender;
    [self rememberQuery];
    const FindBarListener& listener = model_->listener();
    if (listener.onClose) {
        listener.onClose();
    }
}

// The session's recent queries, as a menu under the history button.
- (void)showHistory:(NSButton*)sender {
    const std::vector<std::wstring>& recent = model_->RecentSearchQueries();
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
    if (recent.empty()) {
        NSMenuItem* item = [menu addItemWithTitle:macstrings::kNoRecentSearches
                                           action:nil
                                    keyEquivalent:@""];
        [item setEnabled:NO];
    } else {
        for (std::size_t i = 0; i < recent.size(); ++i) {
            NSMenuItem* item =
                [menu addItemWithTitle:NarrowText(recent[i])
                                action:@selector(selectHistoryQuery:)
                         keyEquivalent:@""];
            [item setTarget:self];
            [item setTag:static_cast<NSInteger>(i)];
        }
    }
    [menu popUpMenuPositioningItem:nil
                        atLocation:NSMakePoint(0, NSHeight(sender.bounds))
                            inView:sender];
}

// A query from the history becomes the current one and searches
// again, the same as typing it would.
- (void)selectHistoryQuery:(NSMenuItem*)item {
    const std::vector<std::wstring>& recent = model_->RecentSearchQueries();
    const std::size_t index = static_cast<std::size_t>(item.tag);
    if (index >= recent.size()) return;
    [searchField_ setStringValue:NarrowText(recent[index])];
    model_->SetSearchText(recent[index]);
    model_->RememberSearchQuery(recent[index]);
    const FindBarListener& listener = model_->listener();
    if (listener.onSearchChanged) {
        listener.onSearchChanged();
    }
    [self updateFromModel];
}

// The keys the guide gives the bar: Enter steps to the next match,
// Shift+Enter to the previous one, and Escape closes the bar
// (guide 2.3 and 2.4).
- (BOOL)control:(NSControl*)control
             textView:(NSTextView*)textView
    doCommandBySelector:(SEL)commandSelector {
    (void)control;
    (void)textView;
    if (commandSelector == @selector(insertNewline:)) {
        const NSEventModifierFlags flags =
            [[NSApp currentEvent] modifierFlags] &
            NSEventModifierFlagDeviceIndependentFlagsMask;
        if ((flags & NSEventModifierFlagShift) != 0) {
            [self previousMatch:nil];
        } else {
            [self nextMatch:nil];
        }
        return YES;
    }

    if (commandSelector == @selector(cancelOperation:)) {
        [self closeBar:nil];
        return YES;
    }

    return NO;
}

@end

namespace mac {

FindBar::FindBar() {
    MdFindBarView* bar = [[MdFindBarView alloc] initWithModel:&model_];
    [bar setHidden:YES];
    // The view stays in the bar, so the pointer is bridged and the bar
    // keeps it alive.
    view_ = (__bridge void*)bar;
}

FindBar::~FindBar() = default;

void FindBar::Show(FindBarMode mode) {
    model_.SetMode(mode);
    visible_ = true;
    MdFindBarView* bar = (__bridge MdFindBarView*)view_;
    // The mode greys the replace half in or out and re-reads the
    // model, so the controls open in the right state.
    [bar setReplaceMode:mode == FindBarMode::FindReplace];
    [bar setHidden:NO];
    [bar focusSearchField];
}

void FindBar::Hide() {
    visible_ = false;
    MdFindBarView* bar = (__bridge MdFindBarView*)view_;
    [bar setHidden:YES];
}

void FindBar::FocusSearchField() {
    MdFindBarView* bar = (__bridge MdFindBarView*)view_;
    [bar focusSearchField];
}

void FindBar::SyncControls() {
    MdFindBarView* bar = (__bridge MdFindBarView*)view_;
    [bar updateFromModel];
}

float FindBar::Height() const {
    return visible_ ? kFindBarHeight : 0.0f;
}

}  // namespace mac
