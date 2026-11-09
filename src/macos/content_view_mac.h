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
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app_mac.h"
#include "caret.h"
#include "renderer_mac.h"
#include "welcome_mac.h"
#include "searchreplace.h"
#include "platform/seams.h"

namespace platform {

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

    // The button on the welcome screen that opens a document.
    bool WelcomeOpenAt(float x, float y) const;
    bool OpenButtonHovered() const { return openHover_; }
    void SetOpenButtonHovered(bool hovered) { openHover_ = hovered; }

    // Redraws the content area.
    void Invalidate();

    // Resolves the palette again, for the appearance the view is in
    // now. The platform layer calls this at startup and whenever the
    // effective appearance changes, so Light Mode and Dark Mode both
    // get the colours that belong to them.
    void RefreshTheme();

    // The height of the whole document, in points. The scroll view needs
    // it to size the content.
    float DocumentHeight() const { return documentHeight_; }

    // The width of the text column, in points.
    float ContentWidth() const { return contentWidth_; }

    // The view's size in points, set by the window.
    void SetSize(float width, float height);

    // The vertical scroll offset, in points. It mirrors the scroll
    // view's place; the scrolling itself belongs to the scroll view.
    void SetScrollOffset(float offset);

    // The zoom the layout is at.
    float zoom() const;

    // The scroll asks the platform layer to move, because the scroll
    // view owns the place and its scrollers have to stay in step.
    void SetScrollToHandler(std::function<void(float)> handler) {
        scrollTo_ = handler;
    }

    // The content changed size, so the platform layer can let the
    // document view take the content's size.
    void SetLayoutChangedHandler(std::function<void()> handler) {
        layoutChanged_ = handler;
    }

    // The scroll place moved. The platform layer calls it from its
    // own clip-view notification. `programmatic` is true when the app
    // asked for the scroll (an outline jump, a find result); a reader
    // scroll leaves it false, so the outline can follow the viewport
    // even while the caret is still on screen (outline spec 19).
    void SetScrollChangedHandler(std::function<void(bool)> handler) {
        scrollChanged_ = handler;
    }
    void NotifyScrollChanged(bool programmatic);

    // Scrolls the place of the offset into view, so the outline pane
    // can jump to a heading.
    void ScrollToOffset(uint32_t offset);

    // The same jump with a fixed drop: the place lands `drop` points
    // below the top of the view. The QA walk uses it, so the two
    // platforms can frame the same content.
    void ScrollToOffsetPlaced(uint32_t offset, float drop);
    float scrollOffset() const { return scrollOffset_; }
    // Whether the app itself is moving the viewport right now.
    bool programmaticScroll() const { return programmaticScroll_; }

    // Whether the caret is inside the visible band right now. The
    // outline keeps the caret's section highlighted only while the
    // caret is on screen (outline spec 6 and 19).
    bool CaretIsOnScreen() const;

    // The source offset at the reading line, a quarter down the
    // visible area, so scrolling moves the outline highlight. It is
    // UINT32_MAX when nothing is drawn there (outline spec 19).
    uint32_t ViewportProbeOffset() const;

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

    // The accessibility map (table spec 47): the tables of the current
    // layout with their rows, cells and the header row, in view
    // coordinates, straight from the geometry the painter and the hit
    // test already share.
    struct AxCell {
        float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
        std::string text;
        int row = 0, column = 0;
        bool header = false;
        bool caretCell = false;
    };
    struct AxTable {
        float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
        std::vector<AxCell> cells;
    };
    std::vector<AxTable> AccessibilityTables() const;

    // The offset of the document point that a view point falls on.
    // UINT32_MAX when the point is in the margin, where the Word-like
    // line selection takes over.
    uint32_t HitTestPoint(float x, float y) const;

    // The position on the line delta lines above (negative) or below
    // (positive) the caret, at the column nearest desiredX.
    bool VerticalNeighbour(uint32_t offset, float desiredX, int delta,
                           uint32_t* outOffset) const;

    // The nearest drawn position to a source offset, so a caret from
    // outside the document model never lands inside a marker.
    uint32_t NormalizeToRenderedCaret(uint32_t offset) const;

    // The source offset one grapheme cluster to the left or right. The
    // step walks the drawn text, so a hidden marker is skipped.
    uint32_t StepGrapheme(uint32_t offset, bool left) const;

    // The source offset one word to the left or right, on the shared
    // word boundaries.
    uint32_t StepWord(uint32_t offset, bool left) const;

    // The first or the last source offset of the visual line that
    // holds the offset.
    uint32_t LineBoundary(uint32_t offset, bool end) const;

    // The rectangle a document offset occupies, in document coordinates.
    bool OffsetToRect(uint32_t offset, float* outX, float* outY,
                      float* outWidth, float* outHeight) const;

    // Places the caret at a view point, which the mouse handler calls.
    void PlaceCaretAt(float x, float y);

    // Selects the word or the paragraph under a view point, which a
    // double or a triple click calls.
    void SelectWordOrParagraphAt(float x, float y, bool paragraph);

    // Selects from the anchor to a view point, which a drag calls. The
    // mode is the one the last click started: a character, a word or a
    // paragraph drag.
    void ExtendSelectionTo(float x, float y, int mode);

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

    // Told when edit mode changes, so the mac view can start and stop
    // the blink timer with the mode. The timer is presentation only and
    // never touches the document model.
    void SetEditingChangedHandler(std::function<void(bool)> handler) {
        editingChanged_ = std::move(handler);
    }

    // Marks the ranges the find bar matched. A null pointer clears the
    // marks.
    // Hands the marked ranges to the renderer.
    void SetMarkers(const std::vector<TextMatch>* markers);

    void SetSearchMatches(const std::vector<TextMatch>* matches,
                          int currentIndex);

    // Turns the word wrap on or off.
    void SetWrap(bool wrap);

    // Switches the drawing to the raw buffer (find guide 4).
    void SetSourceMode(bool on);

    // The text drag (text spec, drag and drop): a press inside
    // the selection arms it, the system threshold starts it, and
    // the release drops it as one undo entry. A press without a
    // drag collapses the selection at the release.
    bool ArmTextDrag(float x, float y);
    void DragTextTo(float x, float y);
    bool textDragArmed() const { return textDragArmed_; }
    bool textDragActive() const { return textDragActive_; }
    uint32_t TextDragDropOffset() const { return textDragTarget_; }
    void CancelTextDrag();

    // Sets the width of the text column. The modes are the ones the
    // settings keep.
    void SetContentWidthMode(int mode);

    // Sets the zoom of the document, so the text and the spacing follow
    // the zoom the way the Windows window does.
    void SetZoom(float zoom);

    // Sets the reading or editing presentation: reading draws the
    // blank line next to a list as a thin strip, editing keeps its
    // full line, and the layout is rebuilt for the new mode.
    void SetEditing(bool on);

private:
    // The layout of the document, computed by the renderer.
    void Relayout();

    App* app_;
    std::unique_ptr<TextEngine> engine_;
    RenderTarget* target_;
    MacRenderer renderer_;
    // The welcome screen, which is shown when no document is open.
    WelcomeScreen welcome_;
    // The card of the welcome screen under the mouse.
    int hoverCard_ = -1;
    // Whether the pointer is on the Open button of the welcome screen.
    bool openHover_ = false;

    float width_ = 0.0f;
    float height_ = 0.0f;
    float contentWidth_ = 0.0f;
    float documentHeight_ = 0.0f;
    float scrollOffset_ = 0.0f;
    // True while the app itself is moving the viewport.
    bool programmaticScroll_ = false;
    std::function<void(bool)> scrollChanged_;
    // The platform layer's scroll request.
    std::function<void(float)> scrollTo_;
    // The platform layer's layout request.
    std::function<void()> layoutChanged_;
    // The state of the text drag: armed by a press inside the
    // selection, active past the drag threshold, with the drop
    // target kept in source offsets.
    bool textDragArmed_ = false;
    bool textDragActive_ = false;
    float textDragDownX_ = 0.0f;
    float textDragDownY_ = 0.0f;
    uint32_t textDragTarget_ = UINT32_MAX;
    // Whether the caret is showing right now.
    bool caretVisible_ = true;
    std::function<void(bool)> editingChanged_;
};

}  // namespace platform
