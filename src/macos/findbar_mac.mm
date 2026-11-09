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
- (void)announceSearchOutcome;
@end

@implementation MdFindBarView {
    FindBarModel* model_;
    NSTextField* findLabel_;
    NSTextField* replaceLabel_;
    NSTextField* searchField_;
    NSTextField* replaceField_;
    NSSegmentedControl* previousNext_;
    NSButton* matchCaseButton_;
    NSButton* wholeWordButton_;
    NSButton* replaceButton_;
    NSButton* replaceAllButton_;
    NSButton* closeButton_;
    NSTextField* counterLabel_;
    NSTextField* statusLabel_;
    NSStackView* searchRow_;
    NSStackView* replaceRow_;
    // The outcome that was last read aloud, so the same result is not
    // repeated while the user keeps typing (find spec 44).
    std::wstring announced_;
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
    // The system's own search field: the magnifier, the rounded well,
    // the clear button and the focus ring come with it, and it
    // follows the appearance and the contrast settings.
    searchField_ = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    [searchField_ setPlaceholderString:macstrings::kFindPlaceholder];
    [searchField_ setDelegate:self];

    // The system's own text field beside it, bezel and all.
    replaceField_ = [NSTextField textFieldWithString:@""];
    [replaceField_ setPlaceholderString:macstrings::kReplacePlaceholder];
    [replaceField_ setDelegate:self];

    findLabel_ = [NSTextField labelWithString:@"Find:"];
    [findLabel_ setFont:platform::SystemUIFont(12.0f)];
    replaceLabel_ = [NSTextField labelWithString:@"Replace with:"];
    [replaceLabel_ setFont:platform::SystemUIFont(12.0f)];

    // The system's find-bar pair: one segmented control, back and
    // forward, with the system's chevrons. The glyphs are the ones the
    // Windows bar shows, up for the previous match and down for the
    // next.
    previousNext_ = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
    [previousNext_ setSegmentCount:2];
    [previousNext_ setTrackingMode:NSSegmentSwitchTrackingMomentary];
    [previousNext_ setSegmentStyle:NSSegmentStyleAutomatic];
    [previousNext_
        setImage:[NSImage imageWithSystemSymbolName:@"chevron.up"
                           accessibilityDescription:macstrings::kPreviousMatch]
       forSegment:0];
    [previousNext_
        setImage:[NSImage imageWithSystemSymbolName:@"chevron.down"
                           accessibilityDescription:macstrings::kNextMatch]
       forSegment:1];
    [previousNext_ setToolTip:macstrings::kPreviousMatch forSegment:0];
    [previousNext_ setToolTip:macstrings::kNextMatch forSegment:1];
    [previousNext_ setTarget:self];
    [previousNext_ setAction:@selector(navigationSegment:)];
    [previousNext_ setAccessibilityLabel:macstrings::kPreviousMatch];
    matchCaseButton_ = [self makeCheckbox:macstrings::kMatchCase
                                   action:@selector(optionsChanged:)];
    wholeWordButton_ = [self makeCheckbox:macstrings::kWholeWord
                                  action:@selector(optionsChanged:)];
    replaceButton_ = [self makeButton:macstrings::kReplaceButton
                              tooltip:macstrings::kReplaceTheMatch
                              action:@selector(replaceCurrent:)];
    replaceAllButton_ = [self makeButton:macstrings::kReplaceAllButton
                                tooltip:macstrings::kReplaceEveryMatch
                                action:@selector(replaceAll:)];
    closeButton_ = [self makeButton:@"Close" tooltip:macstrings::kCloseFindBar
                            action:@selector(closeBar:)];

    // The glyph buttons need real names for assistive technology
    // (find spec 2.3 and 44).
    [closeButton_ setAccessibilityLabel:macstrings::kCloseFindBarLabel];
    [searchField_ setAccessibilityLabel:macstrings::kSearchText];
    [replaceField_ setAccessibilityLabel:macstrings::kReplaceText];

    counterLabel_ = [NSTextField labelWithString:@""];
    [counterLabel_ setAlignment:NSTextAlignmentRight];
    [counterLabel_ setTextColor:[NSColor labelColor]];

    statusLabel_ = [NSTextField labelWithString:@""];
    [statusLabel_ setTextColor:[NSColor secondaryLabelColor]];
    [statusLabel_ setFont:platform::SystemUIFont(11.0f)];
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
        findLabel_, searchField_, previousNext_,
        matchCaseButton_, wholeWordButton_, counterLabel_
    ]];
    [searchRow_ setOrientation:NSUserInterfaceLayoutOrientationHorizontal];
    [searchRow_ setAlignment:NSLayoutAttributeCenterY];
    [searchRow_ setSpacing:8.0];

    replaceRow_ = [NSStackView stackViewWithViews:@[
        replaceLabel_, replaceField_, replaceButton_, replaceAllButton_,
        statusLabel_
    ]];
    [replaceRow_ setOrientation:NSUserInterfaceLayoutOrientationHorizontal];
    [replaceRow_ setAlignment:NSLayoutAttributeCenterY];
    [replaceRow_ setSpacing:8.0];

    NSStackView* rows = [NSStackView stackViewWithViews:@[searchRow_, replaceRow_]];
    [rows setOrientation:NSUserInterfaceLayoutOrientationVertical];
    [rows setAlignment:NSLayoutAttributeLeading];
    [rows setSpacing:4.0];

    // The rows hug the leading edge and Close the trailing one, the
    // way a find bar arranges its controls. One outer stack holds
    // them no longer: a stack spreads its views across whatever width
    // it is given, which floated the whole group in the middle.
    [self addSubview:rows];
    [self addSubview:closeButton_];
    [rows setTranslatesAutoresizingMaskIntoConstraints:NO];
    [closeButton_ setTranslatesAutoresizingMaskIntoConstraints:NO];

    [NSLayoutConstraint activateConstraints:@[
        [rows.leadingAnchor constraintEqualToAnchor:self.leadingAnchor
                                           constant:8.0],
        [rows.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [closeButton_.trailingAnchor constraintEqualToAnchor:
                                          self.trailingAnchor constant:-8.0],
        [closeButton_.leadingAnchor
            constraintGreaterThanOrEqualToAnchor:rows.trailingAnchor
                                        constant:8.0],
        [closeButton_.topAnchor constraintEqualToAnchor:self.topAnchor
                                                constant:4.0],
        // The two labels share a width, so the fields line up under
        // each other the way the Windows rows do.
        [findLabel_.widthAnchor constraintEqualToConstant:68.0],
        [replaceLabel_.widthAnchor constraintEqualToConstant:68.0],
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

    [previousNext_ setEnabled:navigable forSegment:0];
    [previousNext_ setEnabled:navigable forSegment:1];
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

// One line to the screen reader (find spec 44).
- (void)announce:(const std::wstring&)text {
    if (text.empty()) return;
    NSAccessibilityPostNotificationWithUserInfo(
        self.window ? (id)self.window : (id)NSApp,
        NSAccessibilityAnnouncementRequestedNotification,
        @{ NSAccessibilityAnnouncementKey : NarrowText(text),
           NSAccessibilityPriorityKey : @(NSAccessibilityPriorityMedium) });
}

// What the reader would hear about the current search, as a short key
// so repeats can be filtered. Empty when there is no search to talk
// about.
- (std::wstring)outcomeKey {
    if (!model_->state().HasQuery()) return std::wstring();
    if (model_->status() == FindStatus::NoMatches || model_->countTotal() == 0) {
        return L"none";
    }
    std::wstring key = L"m";
    key += std::to_wstring(model_->countCurrent());
    return key;
}

// The announcements the find spec asks for: no results, the match the
// navigation moved to. Replace All announces itself at its own call
// site, where the count before the replacement is known.
- (void)announceSearchOutcome {
    const std::wstring key = [self outcomeKey];
    if (key.empty()) {
        announced_.clear();
        return;
    }
    if (key == announced_) return;
    announced_ = key;
    if (key == L"none") {
        [self announce:findstrings::kAnnounceNoMatches];
        return;
    }
    std::wstring text = findstrings::kAnnounceMatchPrefix;
    text += std::to_wstring(model_->countCurrent());
    text += findstrings::kCounterSeparator;
    text += std::to_wstring(model_->countTotal());
    text += L".";
    [self announce:text];
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
            // A small document searched right away: the outcome for
            // this keystroke is announced here.
            [self announceSearchOutcome];
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
    [self announceSearchOutcome];
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
    [self announceSearchOutcome];
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

// The navigation segment: 0 is back, 1 is forward, the two
// commands the Windows bar keeps under Previous and Next.
- (void)navigationSegment:(NSSegmentedControl*)sender {
    if ([sender selectedSegment] == 0) {
        [self previousMatch:sender];
    } else {
        [self nextMatch:sender];
    }
}

- (void)nextMatch:(id)sender {
    (void)sender;
    [self rememberQuery];
    model_->NavigateNext([self caretOffset]);
    // The counter reads the ordinal the navigation just moved.
    model_->SyncFromState();
    [self updateFromModel];
    [self announceSearchOutcome];
}

- (void)previousMatch:(id)sender {
    (void)sender;
    [self rememberQuery];
    model_->NavigatePrevious([self caretOffset]);
    // The counter reads the ordinal the navigation just moved.
    model_->SyncFromState();
    [self updateFromModel];
    [self announceSearchOutcome];
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
    // The count before the replacement is what was replaced.
    const size_t replaced = model_->countTotal();
    if (listener.onReplaceAll) {
        listener.onReplaceAll(model_->GetSearchText(), model_->GetReplaceText(),
                              model_->state().Options());
    }
    model_->SyncFromState();
    if (replaced > 0) {
        std::wstring text = findstrings::kAnnounceReplacedPrefix;
        text += std::to_wstring(replaced);
        text += findstrings::kAnnounceReplacedSuffix;
        [self announce:text];
        // The count already spoke; do not repeat the empty result the
        // counter reads now.
        announced_ = [self outcomeKey];
    }
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

namespace platform {

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

}  // namespace platform
