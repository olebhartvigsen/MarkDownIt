#pragma once

// The outline pane for the mac layer.
//
// The pane shows the headings of the document and lets the reader jump
// to one of them. It borrows the document after every reparse and
// derives its list from the shared outline model, so it keeps no second
// copy of the document.
//
// The pane never touches the document, the selection, the buffer or the
// undo stack. Every jump goes through the navigate callback the owner
// installs, and the owner does the scroll and the caret work. That is
// the same contract the Windows pane has.
//
// The header is plain C++, so the plain C++ files can include it. The
// Objective-C view is behind an opaque pointer.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "dom.h"
#include "outline.h"
#include "seams.h"
#include "theme.h"

namespace mac {

// The pane model and the drawing, without any AppKit.
class OutlinePane {
public:
    // The row metrics, the same ones the Windows pane uses.
    static constexpr float kPadXDip = 8.0f;
    static constexpr float kRowHeightDip = 24.0f;
    static constexpr float kIndentStepDip = 16.0f;
    static constexpr float kIndentMaxDip = 80.0f;

    OutlinePane();
    ~OutlinePane();

    // The document the pane shows. The pane reads it and derives the
    // list of headings. The document must outlive the pane, or the pane
    // must be told about a new one.
    void SetDocument(const Document* doc);

    // The caret is in a section of the document. The pane highlights
    // the heading of that section.
    void SetCaretOffset(uint32_t offset);

    // The jump request. The owner does the scroll and the caret work.
    void SetNavigateCallback(std::function<void(uint32_t offset)> cb);

    // The list of the headings, in document order.
    const std::vector<OutlineItem>& Items() const { return items_; }

    // The headings that are visible under the collapse state.
    const std::vector<int>& Visible() const { return visible_; }

    // The height the pane needs, in points.
    float ContentHeight() const;

    // A click at the point. The point is in the pane's own
    // coordinates, with the origin at the top left.
    //
    // The return value is true when the click was on the disclosure
    // triangle, which means the click only opens and closes the
    // subtree, and not a jump.
    bool ClickAt(float x, float y);

    // Draws the pane through the render target. The target is in the
    // same coordinates as the click, with the origin at the top left.
    void Draw(RenderTarget* target, float width, float height);

    // The colour of the background, the text and the highlight, taken
    // from the theme.
    void SetColors(const Color& background, const Color& text,
                   const Color& highlight, const Color& muted);

    // The colours of the theme, so the pane looks like the document.
    void SetPalette(const Palette& palette);

private:
    // Rebuilds the list of the headings from the document.
    void Refresh();

    // The row of a visible item.
    float RowY(int index) const;

    // The left edge of the text of a heading, indented by its level.
    float IndentFor(const OutlineItem& item) const;

    // The visible item under the point, or -1.
    int ItemAt(float y) const;

    const Document* doc_ = nullptr;
    uint32_t caretOffset_ = 0;

    std::vector<OutlineItem> items_;
    std::vector<int> visible_;
    OutlineCollapse collapse_;

    int currentItem_ = -1;

    std::function<void(uint32_t offset)> navigate_;

    Color background_;
    Color text_;
    Color highlight_;
    Color muted_;
};

}  // namespace mac
