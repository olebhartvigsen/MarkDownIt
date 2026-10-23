#pragma once

// The macOS content view.
//
// The view owns the layout of the document: it asks the renderer to lay
// the document out, it keeps the scroll offset, and it turns mouse and
// keyboard events into edits through the shared edit controller.
//
// The drawing goes through the TextEngine and RenderTarget seams, so the
// view never touches a Core Graphics call directly. That keeps the
// drawing code testable and keeps the platform API in one file.

#include <cstdint>
#include <string>
#include <vector>

#include "app_mac.h"
#include "caret.h"
#include "renderer_mac.h"
#include "welcome_mac.h"
#include "searchreplace.h"
#include "seams.h"

namespace mac {

class ContentView {
public:
    explicit ContentView(App* app);
    ~ContentView();

    // The document changed, so the layout is stale.
    void OnDocumentChanged();

    // Sets the files the welcome screen shows.
    void SetWelcomeFiles(const std::vector<RecentFile>& files);

    // Which card of the welcome screen is under a point, or -1 when
    // there is none.
    int WelcomeCardAt(float x, float y) const;

    // The file a card of the welcome screen stands for.
    std::string WelcomePathForCard(int index) const;

    // The card under the mouse, which is drawn in another colour.
    int HoveredWelcomeCard() const { return hoverCard_; }
    void SetHoveredWelcomeCard(int index) { hoverCard_ = index; }

    // Redraws the content area.
    void Invalidate();

    // The height of the whole document, in points. The scroll view needs
    // it to size the content.
    float DocumentHeight() const { return documentHeight_; }

    // The width of the text column, in points.
    float ContentWidth() const { return contentWidth_; }

    // The view's size in points, set by the window.
    void SetSize(float width, float height);

    // The vertical scroll offset, in points.
    void SetScrollOffset(float offset);

    // Scrolls the place of the offset into view, so the outline pane
    // can jump to a heading.
    void ScrollToOffset(uint32_t offset);
    float scrollOffset() const { return scrollOffset_; }

    // The view's bounds, for the hit tester.
    float viewWidth() const { return width_; }
    float viewHeight() const { return height_; }

    // The text engine, so the app can measure text outside the view.
    TextEngine& textEngine() { return *engine_; }

    // The application the view draws.
    App* app() const { return app_; }

    // Draws the document with the given target, in view coordinates.
    void Draw(RenderTarget* target);

    // The renderer, which holds the laid out blocks.
    const MacRenderer& renderer() const { return renderer_; }

    // The offset of the document point that a view point falls on.
    // UINT32_MAX when the point is in the margin, where the Word-like
    // line selection takes over.
    uint32_t HitTestPoint(float x, float y) const;

    // The nearest drawn position to a source offset, so a caret from
    // outside the document model never lands inside a marker.
    uint32_t NormalizeToRenderedCaret(uint32_t offset) const;

    // The rectangle a document offset occupies, in document coordinates.
    bool OffsetToRect(uint32_t offset, float* outX, float* outY,
                      float* outWidth, float* outHeight) const;

    // Places the caret at a view point, which the mouse handler calls.
    void PlaceCaretAt(float x, float y);

    // Selects the word or the paragraph under a view point, which a
    // double or a triple click calls.
    void SelectWordOrParagraphAt(float x, float y, bool paragraph);

    // Selects from the anchor to a view point, which a drag calls.
    void ExtendSelectionTo(float x, float y);

    // The height of the caret at the current offset, in points.
    float CaretHeight() const;

    // The colour of the selection, and of the caret.
    Color SelectionColor() const;
    Color CaretColor() const;

    // Whether the caret is showing. The blink timer flips this, so the
    // caret appears and disappears like it does in the Windows app.
    bool caretVisible() const { return caretVisible_; }

    // Sets whether the caret is showing, and asks for a redraw when the
    // state changed.
    void SetCaretVisible(bool visible);

    // Marks the ranges the find bar matched. A null pointer clears the
    // marks.
    // Hands the marked ranges to the renderer.
    void SetMarkers(const std::vector<TextMatch>* markers);

    void SetSearchMatches(const std::vector<TextMatch>* matches,
                          int currentIndex);

    // Turns the word wrap on or off.
    void SetWrap(bool wrap);

    // Sets the width of the text column. The modes are the ones the
    // settings keep.
    void SetContentWidthMode(int mode);

    // Sets the zoom of the document, so the text and the spacing follow
    // the zoom the way the Windows window does.
    void SetZoom(float zoom);

private:
    // The layout of the document, computed by the renderer.
    void Relayout();

    App* app_;
    TextEngine* engine_;
    RenderTarget* target_;
    MacRenderer renderer_;
    // The welcome screen, which is shown when no document is open.
    WelcomeScreen welcome_;
    // The card of the welcome screen under the mouse.
    int hoverCard_ = -1;

    float width_ = 0.0f;
    float height_ = 0.0f;
    float contentWidth_ = 0.0f;
    float documentHeight_ = 0.0f;
    float scrollOffset_ = 0.0f;
    // Whether the caret is showing right now.
    bool caretVisible_ = true;
};

}  // namespace mac
