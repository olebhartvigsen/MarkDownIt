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

#include "outline_pane_mac.h"

#include <algorithm>

namespace mac {

namespace {

// A colour from the palette.
Color ToColor(const ThemeColor& c) {
    Color out;
    out.r = c.r;
    out.g = c.g;
    out.b = c.b;
    out.a = c.a;
    return out;
}

// The size of the triangle that opens and closes a subtree.
constexpr float kTriangleSizeDip = 8.0f;

// The size of the font of a heading.
constexpr float kFontSizeDip = 13.0f;

// The number of characters a heading may show before it is cut. The
// tooltip carries the whole heading (outline spec 16).
constexpr size_t kMaxTextChars = 64;

// The family of the font of a heading.
const char* kFontFamily = "Segoe UI";

// The UTF-32 text of a heading, as UTF-8.
std::string ToUtf8(const std::u32string& text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// The text of a heading, cut to a number of characters, so a long
// heading does not run past the pane.
std::string TrimmedText(const OutlineItem& item, size_t maxChars) {
    std::u32string text = item.text;
    if (text.size() > maxChars) {
        text.resize(maxChars);
        text.push_back(U'\u2026');
    }
    return ToUtf8(text);
}

}  // namespace

OutlinePane::OutlinePane() {
    // The pane has no background of its own until the owner gives it
    // the colours.
    background_ = Color{1.0f, 1.0f, 1.0f, 1.0f};
    text_ = Color{0.1f, 0.1f, 0.1f, 1.0f};
    highlight_ = Color{0.9f, 0.94f, 1.0f, 1.0f};
    muted_ = Color{0.5f, 0.5f, 0.5f, 1.0f};
}

OutlinePane::~OutlinePane() = default;

void OutlinePane::SetDocument(const Document* doc) {
    doc_ = doc;
    Refresh();
}

void OutlinePane::SetCaretOffset(uint32_t offset) {
    caretOffset_ = offset;
    // The heading of the section the caret is in is the one that is
    // highlighted.
    currentItem_ = ItemIndexForOffset(items_, offset);
    // The keyboard selection starts at the active row; once the user
    // has moved it with the keys it stays where they left it (outline
    // spec 13).
    if (selectedVisibleRow_ < 0) selectedVisibleRow_ = ActiveVisibleRow();
}

void OutlinePane::SetNavigateCallback(std::function<void(uint32_t offset)> cb) {
    navigate_ = std::move(cb);
}

void OutlinePane::Refresh() {
    if (!doc_) {
        items_.clear();
        visible_.clear();
        currentItem_ = -1;
        return;
    }

    items_ = CollectHeadings(*doc_);
    // The collapse state is keyed by the place in the list, so a
    // reparse that shortens the list drops the entries that are gone.
    collapse_.ReResolve(items_.size());
    visible_ = VisibleItems(items_, collapse_);
    currentItem_ = ItemIndexForOffset(items_, caretOffset_);
    // A shorter list may have taken the selection and the hover away.
    if (selectedVisibleRow_ >= static_cast<int>(visible_.size()))
        selectedVisibleRow_ = static_cast<int>(visible_.size()) - 1;
    if (hoveredVisibleRow_ >= static_cast<int>(visible_.size()))
        hoveredVisibleRow_ = -1;
}

float OutlinePane::ContentHeight() const {
    return static_cast<float>(visible_.size()) * kRowHeightDip;
}

float OutlinePane::RowY(int index) const {
    return static_cast<float>(index) * kRowHeightDip;
}

float OutlinePane::IndentFor(const OutlineItem& item) const {
    // The first level sits at the padding, and every level after that
    // steps in, up to a limit, so a deep heading does not push the text
    // off the pane.
    const int levels = item.level > 0 ? item.level - 1 : 0;
    const float indent = static_cast<float>(levels) * kIndentStepDip;
    return kPadXDip + std::min(indent, kIndentMaxDip);
}

int OutlinePane::ActiveVisibleRow() const {
    if (currentItem_ < 0) return -1;
    for (size_t row = 0; row < visible_.size(); row++) {
        if (visible_[row] == currentItem_) return static_cast<int>(row);
    }
    return -1;
}

int OutlinePane::VisibleRowAt(float y) const {
    if (y < 0.0f) return -1;
    const int row = static_cast<int>(y / kRowHeightDip);
    if (row < 0 || row >= static_cast<int>(visible_.size())) return -1;
    return row;
}

Rect OutlinePane::RowBounds(int visibleRow, float width) const {
    Rect out;
    out.x = 0.0f;
    out.y = RowY(visibleRow);
    out.width = width;
    out.height = kRowHeightDip;
    return out;
}

void OutlinePane::SetSelectedVisibleRow(int row) {
    if (visible_.empty()) {
        selectedVisibleRow_ = -1;
        return;
    }
    if (row < 0) row = 0;
    if (row >= static_cast<int>(visible_.size()))
        row = static_cast<int>(visible_.size()) - 1;
    selectedVisibleRow_ = row;
}

void OutlinePane::MoveSelection(int delta) {
    if (visible_.empty()) return;
    const int start =
        selectedVisibleRow_ < 0 ? ActiveVisibleRow() : selectedVisibleRow_;
    SetSelectedVisibleRow((start < 0 ? 0 : start) + delta);
}

bool OutlinePane::SelectionHasChildren() const {
    if (selectedVisibleRow_ < 0 ||
        selectedVisibleRow_ + 1 >= static_cast<int>(visible_.size()))
        return false;
    const int item = visible_[selectedVisibleRow_];
    const int next = visible_[selectedVisibleRow_ + 1];
    return items_[next].level > items_[item].level;
}

void OutlinePane::SetSelectionCollapsed(bool collapsed) {
    if (selectedVisibleRow_ < 0 || !SelectionHasChildren()) return;
    const int item = visible_[selectedVisibleRow_];
    if (collapse_.IsCollapsed(item) == collapsed) return;
    collapse_.Toggle(item);
    visible_ = VisibleItems(items_, collapse_);
    // The tree renumbered under the selection: keep the same heading
    // selected.
    for (size_t row = 0; row < visible_.size(); row++) {
        if (visible_[row] == item) {
            selectedVisibleRow_ = static_cast<int>(row);
            break;
        }
    }
}

void OutlinePane::ActivateSelection() {
    if (selectedVisibleRow_ < 0) return;
    const int item = visible_[selectedVisibleRow_];
    if (navigate_ && item >= 0 && item < static_cast<int>(items_.size())) {
        navigate_(items_[item].offset);
    }
}

std::string OutlinePane::FullTextAt(int visibleRow) const {
    if (visibleRow < 0 || visibleRow >= static_cast<int>(visible_.size()))
        return std::string();
    return ToUtf8(items_[visible_[visibleRow]].text);
}

bool OutlinePane::IsTruncated(int visibleRow) const {
    if (visibleRow < 0 || visibleRow >= static_cast<int>(visible_.size()))
        return false;
    const OutlineItem& heading = items_[visible_[visibleRow]];
    if (heading.text.size() > kMaxTextChars) return true;
    const float textX = IndentFor(heading) + kTriangleSizeDip + 4.0f;
    const float available = lastWidth_ - textX - kPadXDip;
    const float estimated = static_cast<float>(heading.text.size()) * 7.0f;
    return estimated > available;
}

void OutlinePane::SetFocused(bool focused) {
    focused_ = focused;
}

void OutlinePane::SetHoveredVisibleRow(int row) {
    hoveredVisibleRow_ = row;
}

int OutlinePane::ItemAt(float y) const {
    if (y < 0.0f) return -1;
    const int index = static_cast<int>(y / kRowHeightDip);
    if (index < 0 || index >= static_cast<int>(visible_.size())) return -1;
    return visible_[index];
}

bool OutlinePane::ClickAt(float x, float y) {
    const int item = ItemAt(y);
    if (item < 0) return false;

    // The triangle sits at the left of the row, before the text of the
    // heading. A click on it opens and closes the subtree, and does not
    // jump.
    const OutlineItem& heading = items_[item];
    const float triangleLeft = kPadXDip;
    const float triangleRight = triangleLeft + kTriangleSizeDip;
    if (x >= triangleLeft && x <= triangleRight) {
        collapse_.Toggle(item);
        visible_ = VisibleItems(items_, collapse_);
        return true;
    }

    // Any other click on the row asks the owner to jump to the heading.
    if (navigate_) {
        navigate_(heading.offset);
    }
    return false;
}

void OutlinePane::SetColors(const Color& background, const Color& text,
                             const Color& highlight, const Color& muted) {
    background_ = background;
    text_ = text;
    highlight_ = highlight;
    muted_ = muted;
}

void OutlinePane::SetPalette(const Palette& palette) {
    background_ = ToColor(palette.codeBg);
    text_ = ToColor(palette.textPrimary);
    highlight_ = ToColor(palette.selectionBg);
    muted_ = ToColor(palette.textMuted);
}

void OutlinePane::Draw(RenderTarget* target, float width, float height) {
    if (!target) return;
    if (width <= 0.0f || height <= 0.0f) return;

    // The background of the pane.
    Rect background;
    background.x = 0.0f;
    background.y = 0.0f;
    background.width = width;
    background.height = height;
    target->FillRect(background, background_);
    lastWidth_ = width;

    // An empty outline says so, and says what fills it (outline spec
    // 17).
    if (items_.empty()) {
        FontSpec emptyFont;
        emptyFont.family = kFontFamily;
        emptyFont.size = kFontSizeDip;
        target->DrawText("No headings in this document.", emptyFont,
                         kPadXDip, kRowHeightDip, text_);
        target->DrawText("Add headings to your document to create an outline.",
                         emptyFont, kPadXDip, kRowHeightDip * 2.0f, muted_);
        return;
    }

    FontSpec font;
    font.family = kFontFamily;
    font.size = kFontSizeDip;

    for (size_t row = 0; row < visible_.size(); ++row) {
        const int item = visible_[row];
        if (item < 0 || item >= static_cast<int>(items_.size())) continue;

        const OutlineItem& heading = items_[item];
        const float y = RowY(static_cast<int>(row));

        // The heading the caret is in gets a highlight behind it. It
        // stays identifiable whether or not the pane has the focus
        // (outline spec 13).
        if (item == currentItem_) {
            Rect band;
            band.x = 0.0f;
            band.y = y;
            band.width = width;
            band.height = kRowHeightDip;
            target->FillRect(band, highlight_);
        }

        // The hover is a line under the row, the keyboard selection is
        // a ring around it, and both are distinct from the caret's
        // band (outline spec 13).
        if (static_cast<int>(row) == hoveredVisibleRow_) {
            Rect line;
            line.x = 0.0f;
            line.y = y + kRowHeightDip - 1.0f;
            line.width = width;
            line.height = 1.0f;
            target->FillRect(line, muted_);
        }
        if (focused_ && static_cast<int>(row) == selectedVisibleRow_) {
            Rect ring;
            ring.x = 0.0f;
            ring.y = y + 0.5f;
            ring.width = width;
            ring.height = kRowHeightDip - 1.0f;
            target->FillRect(ring, text_);
            Rect inner;
            inner.x = 1.0f;
            inner.y = y + 1.5f;
            inner.width = width - 2.0f;
            inner.height = kRowHeightDip - 3.0f;
            target->FillRect(inner, background_);
        }

        // The triangle, when the heading has a subtree. A heading
        // without a child has no triangle, so the text of the heading
        // starts at the same place either way.
        const bool hasChildren =
            (row + 1) < visible_.size() &&
            items_[visible_[row + 1]].level > heading.level;
        if (hasChildren) {
            const bool collapsed = collapse_.IsCollapsed(item);
            const float centerX = kPadXDip + kTriangleSizeDip * 0.5f;
            const float centerY = y + kRowHeightDip * 0.5f;
            const float half = kTriangleSizeDip * 0.35f;

            Rect shape;
            shape.x = centerX - half;
            shape.y = centerY - half;
            shape.width = half * 2.0f;
            shape.height = half * 2.0f;
            target->FillRect(shape, muted_);

            // A collapsed heading shows a bar, so the two states do not
            // rely on the shape alone.
            if (collapsed) {
                Rect bar;
                bar.x = shape.x;
                bar.y = centerY - 0.5f;
                bar.width = shape.width;
                bar.height = 1.0f;
                target->FillRect(bar, background_);
            }
        }

        // The text of the heading, indented by its level.
        const float textX = IndentFor(heading) + kTriangleSizeDip + 4.0f;
        const float baseline = y + kRowHeightDip * 0.5f + kFontSizeDip * 0.35f;
        const size_t maxChars = kMaxTextChars;
        const Color color = (item == currentItem_) ? text_ : muted_;
        target->DrawText(TrimmedText(heading, maxChars), font, textX, baseline,
                         color);
    }

    // The keyboard focus is visible as a ring around the pane (outline
    // spec 22).
    if (focused_) {
        Rect edges[4] = {
            {0.0f, 0.0f, width, 1.0f},
            {0.0f, height - 1.0f, width, 1.0f},
            {0.0f, 0.0f, 1.0f, height},
            {width - 1.0f, 0.0f, 1.0f, height},
        };
        for (const Rect& edge : edges) target->FillRect(edge, text_);
    }
}

}  // namespace mac
