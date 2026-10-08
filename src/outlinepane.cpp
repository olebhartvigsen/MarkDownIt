// Outline pane implementation: docked left-side child window with D2D
// painting on its own HwndRenderTarget. See outlinepane.h for the
// division of work and the owner contract.

#include "outlinepane.h"

#include <algorithm>
#include <cmath>

#include <d2d1helper.h>
#include <windowsx.h>

namespace {

// Window class name. Registered once per process.
const wchar_t* kWindowClass = L"MarkDownItOutlinePane";

// Layout constants in DIP, converted per-paint from dpi_.
constexpr float kSplitterZoneDip = 4.f;

float Dip(int dpi, float v) { return v * static_cast<float>(dpi) / 96.f; }
float Px(int dpi, float dip) { return dip * 96.f / static_cast<float>(dpi); }
float Clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// UTF-32 to UTF-16: DirectWrite takes UTF-16, the outline model holds
// UTF-32 (dom encoding). Code points above the BMP expand into
// surrogate pairs so emoji in headings paint correctly.
std::u16string ToUtf16(const std::u32string& s32) {
    std::u16string out;
    out.reserve(s32.size());
    for (char32_t cp : s32) {
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char16_t>(cp));
        }
        // Unpaired or out-of-range values are dropped rather than
        // corrupting the rest of the row.
    }
    return out;
}

// Expandable: an item has children when some later item is deeper.
bool HasChildren(const std::vector<OutlineItem>& items, int i) {
    if (i + 1 >= static_cast<int>(items.size())) return false;
    return items[i + 1].level > items[i].level;
}

}  // namespace

OutlinePane::OutlinePane() {
    InitColors();
}

OutlinePane::~OutlinePane() {
    Destroy();
}

void OutlinePane::InitColors() {
    // Light, warm neutrals that match the app palette.
    colBg_         = D2D1::ColorF(0xFFFAF6F1);
    colSep_        = D2D1::ColorF(0xFFE0D6CC);
    colText_       = D2D1::ColorF(0xFF3A342C);
    colTextMuted_  = D2D1::ColorF(0xFF8A8072);
    colActiveBg_   = D2D1::ColorF(0xFFE8DCC8);
    colActiveText_ = D2D1::ColorF(0xFF2A2118);
    colHoverBg_    = D2D1::ColorF(0xFFF0E9DF);
    colArrow_      = D2D1::ColorF(0xFF6B6154);
}

bool OutlinePane::RegisterOutlineClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = &OutlinePane::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (RegisterClassW(&wc) == 0) return false;
    registered = true;
    return true;
}

bool OutlinePane::Create(HWND parent, ID2D1Factory* d2d,
                         IDWriteFactory* dw, int dpi) {
    if (hwnd_) return true;
    if (!parent || !d2d || !dw) return false;
    if (!RegisterOutlineClass()) return false;
    d2d_ = d2d;
    dw_ = dw;
    dpi_ = dpi > 0 ? dpi : 96;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    // Initial bounds are placeholders; SetRect from the owner's layout
    // pass sizes the pane properly right after creation.
    hwnd_ = CreateWindowExW(0, kWindowClass, L"Outline",
                           WS_CHILD | WS_CLIPSIBLINGS,
                           0, 0, 10, 10, parent, nullptr, inst, this);
    if (!hwnd_) {
        d2d_ = nullptr;
        dw_ = nullptr;
        return false;
    }
    CreateFonts();
    return true;
}

void OutlinePane::Destroy() {
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    ReleaseRt();
    ReleaseFonts();
    d2d_ = nullptr;
    dw_ = nullptr;
}

bool OutlinePane::IsVisible() const {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

void OutlinePane::SetWidthDip(float w) {
    widthDip_ = Clampf(w, kMinWidthDip, kMaxWidthDip);
}

float OutlinePane::PreferredWidthDip() const {
    // Longest visible heading text plus indents, arrows and padding,
    // clamped to the same range as SetWidthDip.
    float longest = 0.f;
    for (int idx : visible_) {
        const OutlineItem& it = items_[idx];
        float w = IndentDipFor(it) + Dip(dpi_, kPadXDip * 2.f);
        // Rough text width: average glyph advance assumption is fine
        // for a preferred-size helper; the real width comes from the
        // text layout at layout time (LayoutRows uses fmtItem_).
        w += static_cast<float>(it.text.size()) * Dip(dpi_, 6.5f);
        longest = std::max(longest, w);
    }
    return Clampf(longest, kMinWidthDip, kMaxWidthDip);
}

bool OutlinePane::HitSplitter(int px, int py) const {
    const float zone = Dip(dpi_, kSplitterZoneDip);
    const float w = static_cast<float>(pixelW_);
    return px >= w - zone && px < w && py >= 0 && py < pixelH_;
}

void OutlinePane::BeginSplitterDrag(int px) {
    splitterDrag_ = true;
    splitterStartPx_ = static_cast<float>(px);
    splitterStartW_ = widthDip_;
}

void OutlinePane::DragSplitter(int px) {
    if (!splitterDrag_) return;
    // Dragging right widens: pane-local px grows as the pane widens, so
    // the delta is measured against the drag anchor.
    const float movedDip = (static_cast<float>(px) - splitterStartPx_)
        * 96.f / static_cast<float>(dpi_);
    SetWidthDip(splitterStartW_ + movedDip);
    Repaint();
}

void OutlinePane::EndSplitterDrag() {
    if (!splitterDrag_) return;
    splitterDrag_ = false;
    // Report the committed width so the owner persists it.
    if (widthCommitted_) widthCommitted_(widthDip_);
}

void OutlinePane::SetItems(const std::vector<OutlineItem>& items) {
    // Re-resolve collapse by (level, offset) against the previous list:
    // folded branches survive text edits elsewhere in the document.
    // Candidate policy identical to the markers sidecar adoption.
    std::map<int, bool> keep;
    if (!items_.empty() && !items.empty()) {
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            if (!collapse_.IsCollapsed(i)) continue;
            // Match the previously collapsed item by level and offset.
            for (int j = 0; j < static_cast<int>(items.size()); ++j) {
                if (items[j].level == items_[i].level &&
                    items[j].offset == items_[i].offset) {
                    keep[j] = true;
                    break;
                }
            }
        }
    }
    items_ = items;
    collapse_ = OutlineCollapse();
    for (auto& kv : keep) collapse_.Toggle(kv.first);
    visible_ = VisibleItems(items_, collapse_);
    if (active_ >= static_cast<int>(items_.size())) active_ = -1;
    if (selected_ >= static_cast<int>(items_.size())) selected_ = -1;
    LayoutRows();
    ClampScroll();
    UpdateScroll();
    Repaint();
}

void OutlinePane::SetActive(int index) {
    if (index >= static_cast<int>(items_.size())) index = -1;
    if (active_ == index) return;
    active_ = index;
    Repaint();
}

void OutlinePane::SetNavigateCallback(
        std::function<void(uint32_t offset)> cb) {
    navigate_ = std::move(cb);
}

void OutlinePane::SetFocusCycleCallback(std::function<void()> cb) {
    focusCycle_ = std::move(cb);
}

void OutlinePane::SetWidthCommittedCallback(
        std::function<void(float)> cb) {
    widthCommitted_ = std::move(cb);
}

int OutlinePane::RowAt(int px, int py) const {
    // Binary search would be premature: row counts are small.
    const float fy = static_cast<float>(py) * 96.f / dpi_ + scrollY_;
    for (int v = 0; v < static_cast<int>(rowRects_.size()); ++v) {
        if (fy >= rowRects_[v].top && fy < rowRects_[v].bottom)
            return visible_[v];
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

float OutlinePane::IndentDipFor(const OutlineItem& item) const {
    // Indent by depth relative to H1, capped so deep nests do not eat
    // the reading width. H2 -> one step, H3 -> two, and so on.
    const int depth = std::max(0, item.level - 1);
    return std::min(kIndentMaxDip, static_cast<float>(depth) * kIndentStepDip);
}

void OutlinePane::LayoutRows() {
    rowRects_.clear();
    arrowRects_.clear();
    const float padX = kPadXDip;
    const float rowH = kRowHeightDip;
    float y = 0.f;
    for (int v = 0; v < static_cast<int>(visible_.size()); ++v) {
        const OutlineItem& it = items_[visible_[v]];
        const float indent = IndentDipFor(it);
        // Disclosure arrow zone before the text: 12 DIP square aligned
        // to the row, present only when the row can fold.
        const bool expandable = HasChildren(items_, visible_[v]);
        const float arrowBaseX = padX + indent;
        const float textX = arrowBaseX + (expandable ? 14.f : 6.f);
        D2D1_RECT_F row = D2D1::RectF(0.f, y, 10000.f, y + rowH);
        D2D1_RECT_F arrow = expandable
            ? D2D1::RectF(arrowBaseX, y + 5.f, arrowBaseX + 12.f, y + 17.f)
            : D2D1::RectF(0.f, -1.f, 0.f, -1.f);
        rowRects_.push_back(row);
        arrowRects_.push_back(arrow);
        // Store the text start inside the row rect (layout uses left).
        rowRects_.back().left = textX;
        y += rowH;
    }
}

float OutlinePane::TotalHeightDip() const {
    return static_cast<float>(visible_.size()) * kRowHeightDip;
}

float OutlinePane::RowTopDip(int visibleIdx) const {
    return static_cast<float>(visibleIdx) * kRowHeightDip + scrollY_;
}

// ---------------------------------------------------------------------------
// Fonts and render target
// ---------------------------------------------------------------------------

void OutlinePane::CreateFonts() {
    ReleaseFonts();
    if (!dw_) return;
    // 12 pt (16 px) item font, 11 pt muted hint for the empty state.
    const float itemPt = 12.f * static_cast<float>(dpi_) / 96.f;
    const float emptyPt = 11.f * static_cast<float>(dpi_) / 96.f;
    if (FAILED(dw_->CreateTextFormat(L"Segoe UI", nullptr,
                                     DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL,
                                     itemPt, L"en-us", &fmtItem_)))
        fmtItem_ = nullptr;
    if (FAILED(dw_->CreateTextFormat(L"Segoe UI", nullptr,
                                     DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL,
                                     emptyPt, L"en-us", &fmtEmpty_)))
        fmtEmpty_ = nullptr;
    if (fmtItem_) fmtItem_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

void OutlinePane::ReleaseFonts() {
    if (fmtItem_) { fmtItem_->Release(); fmtItem_ = nullptr; }
    if (fmtEmpty_) { fmtEmpty_->Release(); fmtEmpty_ = nullptr; }
}

void OutlinePane::EnsureRt() {
    if (rt_) return;
    if (!d2d_ || !hwnd_ || pixelW_ <= 0 || pixelH_ <= 0) return;
    // Own HwndRenderTarget, never the document's: sizing or recreating
    // it can never disturb the document render target (app.cpp keeps
    // one target per HWND for exactly this reason).
    D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties();
    rp.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rp.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp = {};
    hp.hwnd = hwnd_;
    hp.pixelSize.width = static_cast<UINT32>(pixelW_);
    hp.pixelSize.height = static_cast<UINT32>(pixelH_);
    if (FAILED(d2d_->CreateHwndRenderTarget(&rp, &hp, &rt_)))
        rt_ = nullptr;
}

void OutlinePane::ReleaseRt() {
    if (rt_) { rt_->Release(); rt_ = nullptr; }
}

void OutlinePane::BeginPaintPass() {
    EnsureRt();
    if (!rt_) return;
    if (brush_) { brush_->Release(); brush_ = nullptr; }
    if (FAILED(rt_->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Black), &brush_)))
        brush_ = nullptr;
}

// ---------------------------------------------------------------------------
// Painting (welcome-screen style: one pass, whole face)
// ---------------------------------------------------------------------------

void OutlinePane::PaintBackground(ID2D1RenderTarget* rt,
                                  const D2D1_RECT_F& rc) {
    if (!brush_) return;
    brush_->SetColor(colBg_);
    rt->FillRectangle(rc, brush_);
}

void OutlinePane::PaintSeparator(ID2D1RenderTarget* rt,
                                 const D2D1_RECT_F& rc) {
    if (!brush_) return;
    brush_->SetColor(colSep_);
    // One vertical hairline on the right edge.
    const float x = rc.right - 1.f;
    rt->FillRectangle(D2D1::RectF(x, rc.top, x + 1.f, rc.bottom), brush_);
}

void OutlinePane::PaintRows(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc) {
    if (!brush_ || !fmtItem_) return;
    const float scale = static_cast<float>(dpi_) / 96.f;
    for (int v = 0; v < static_cast<int>(visible_.size()); ++v) {
        const int idx = visible_[v];
        const D2D1_RECT_F& arrow = arrowRects_[v];
        // View transform: row y in DIP-space of the scroll, scaled by
        // DPI into pixels.
        const float yDip = RowTopDip(v) - scrollY_;
        D2D1_RECT_F rowPx = D2D1::RectF(
            0.f, yDip * scale, rc.right, (yDip + kRowHeightDip) * scale);
        if (rowPx.bottom < rc.top || rowPx.top > rc.bottom) continue;

        // Row backgrounds first.
        if (idx == active_) {
            brush_->SetColor(colActiveBg_);
            rt->FillRectangle(rowPx, brush_);
        } else if (v == hoverVis_) {
            brush_->SetColor(colHoverBg_);
            rt->FillRectangle(rowPx, brush_);
        }
        // Focus ring (dotted) around the keyboard-selected row while
        // the pane holds focus; the active highlight stays visible in
        // the unfocused pane (spec 13).
        if (idx == selected_ && GetFocus() == hwnd_) {
            brush_->SetColor(colText_);
            rt->DrawRectangle(rowPx, brush_, 1.0f);
        }

        // Disclosure arrow when the row can fold.
        if (arrowRects_[v].top >= 0.f && HasChildren(items_, idx)) {
            const bool folded = collapse_.IsCollapsed(idx);
            brush_->SetColor(colArrow_);
            // Small filled triangle: 7 DIP wide, in the arrow zone.
            const float ax = (kPadXDip + IndentDipFor(items_[idx])) * scale;
            const float ay = (yDip + 6.f) * scale;
            const float s = 6.f * scale;
            D2D1_POINT_2F p1 = D2D1::Point2F(ax, ay);
            D2D1_POINT_2F p2 = D2D1::Point2F(ax, ay + s);
            D2D1_POINT_2F p3 = folded
                ? D2D1::Point2F(ax + s, ay + s * 0.5f)
                : D2D1::Point2F(ax + s * 0.5f, ay + s);
            ID2D1PathGeometry* geo = nullptr;
            if (SUCCEEDED(d2d_->CreatePathGeometry(&geo)) && geo) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(geo->Open(&sink)) && sink) {
                    sink->BeginFigure(p1, D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLines(&p2, 1);
                    sink->AddLines(&p3, 1);
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();
                    rt->FillGeometry(geo, brush_);
                }
                geo->Release();
            }
        }

        // Row text. Width clamps at the pane edge minus padding.
        const float textX = (kPadXDip + IndentDipFor(items_[idx])
            + (HasChildren(items_, idx) ? 14.f : 6.f)) * scale;
        const float textW = rc.right / scale - textX / scale - kPadXDip;
        D2D1_RECT_F textRc = D2D1::RectF(
            textX, yDip * scale, rc.right - kPadXDip * scale,
            (yDip + kRowHeightDip) * scale);
        brush_->SetColor(idx == active_ ? colActiveText_ : colText_);
        // Layout a short-lived IDWriteTextLayout for trimming. The
        // item text is UTF-32 and must be widened to UTF-16 before it
        // reaches DirectWrite: a reinterpret_cast would hand over
        // twice the byte count and only half the text would paint.
        const std::u16string text16 = ToUtf16(items_[idx].text);
        IDWriteTextLayout* layout = nullptr;
        if (fmtItem_ && SUCCEEDED(dw_->CreateTextLayout(
                reinterpret_cast<const wchar_t*>(text16.c_str()),
                static_cast<UINT32>(text16.size()), fmtItem_,
                textRc.right - textRc.left, textRc.bottom - textRc.top,
                &layout)) && layout) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            layout->SetParagraphAlignment(
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            DWRITE_TRIMMING trim = {};
            trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            layout->SetTrimming(&trim, nullptr);
            rt->DrawTextLayout(
                D2D1::Point2F(textRc.left, textRc.top), layout, brush_);
            layout->Release();
        }
    }
}

void OutlinePane::PaintEmptyState(ID2D1RenderTarget* rt,
                                  const D2D1_RECT_F& rc) {
    if (!brush_ || !fmtEmpty_) return;
    brush_->SetColor(colTextMuted_);
    const wchar_t* msg = L"No headings in this document.";
    const float w = rc.right - 2.f * kPadXDip * (dpi_ / 96.f);
    IDWriteTextLayout* layout = nullptr;
    if (SUCCEEDED(dw_->CreateTextLayout(
            msg, static_cast<UINT32>(wcslen(msg)), fmtEmpty_, w,
            40.f * dpi_ / 96.f, &layout)) && layout) {
        rt->DrawTextLayout(D2D1::Point2F(kPadXDip * (dpi_ / 96.f),
                                         10.f * dpi_ / 96.f), layout,
                           brush_);
        layout->Release();
    }
}

// ---------------------------------------------------------------------------
// Scroll
// ---------------------------------------------------------------------------

void OutlinePane::ClampScroll() {
    const float viewH = static_cast<float>(pixelH_) * 96.f
        / static_cast<float>(dpi_);
    const float total = TotalHeightDip();
    const float maxScroll = std::max(0.f, total - viewH);
    scrollY_ = Clampf(scrollY_, 0.f, maxScroll);
}

void OutlinePane::UpdateScroll() {
    if (!hwnd_) return;
    const float viewH = static_cast<float>(pixelH_) * 96.f
        / static_cast<float>(dpi_);
    const float total = TotalHeightDip();
    if (total <= 0.f || pixelH_ <= 0) {
        // Nothing to scroll: remove the scrollbar entirely.
        SCROLLINFO si = { sizeof(SCROLLINFO), SIF_RANGE };
        si.nMin = 0; si.nMax = 0;
        SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
        return;
    }
    SCROLLINFO si = {};
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = static_cast<int>(total * dpi_ / 96.f);
    si.nPage = static_cast<UINT>(viewH);
    si.nPos = static_cast<int>(scrollY_ * dpi_ / 96.f);
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

void OutlinePane::ScrollIntoView(int index) {
    // Find the visible slot of index; hidden rows cannot be scrolled
    // to, so collapse is left alone (the owner re-activates after a
    // SetItems pass).
    int v = -1;
    for (int i = 0; i < static_cast<int>(visible_.size()); ++i)
        if (visible_[i] == index) { v = i; break; }
    if (v < 0) return;
    const float scale = static_cast<float>(dpi_) / 96.f;
    const float viewH = static_cast<float>(pixelH_);
    const float topPx = RowTopDip(v) * scale - scrollY_ * scale;
    const float botPx = topPx + kRowHeightDip * scale;
    if (topPx < 0.f) {
        scrollY_ = RowTopDip(v);
    } else if (botPx > viewH) {
        scrollY_ = RowTopDip(v) + kRowHeightDip
            - viewH / scale;
    } else {
        return;
    }
    ClampScroll();
    UpdateScroll();
    Repaint();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void OutlinePane::OnLButtonDown(int px, int py) {
    // Splitter first: it owns the right edge.
    if (HitSplitter(px, py)) {
        SetFocus(hwnd_);
        BeginSplitterDrag(px);
        SetCapture(hwnd_);
        return;
    }
    // Disclosure arrows.
    const float scale = static_cast<float>(dpi_) / 96.f;
    const float fy = static_cast<float>(py) / scale + scrollY_;
    const float fx = static_cast<float>(px) / scale;
    for (int v = 0; v < static_cast<int>(arrowRects_.size()); ++v) {
        const D2D1_RECT_F& a = arrowRects_[v];
        if (a.top < 0.f) continue;  // not expandable
        if (fx >= a.left && fx < a.right && fy >= a.top && fy < a.bottom) {
            const int idx = visible_[v];
            collapse_.Toggle(idx);
            visible_ = VisibleItems(items_, collapse_);
            // Keyboard selection must survive a fold that hides it.
            if (selected_ >= 0) {
                bool found = false;
                for (int i : visible_) if (i == selected_) { found = true; break; }
                if (!found) selected_ = -1;
            }
            LayoutRows();
            ClampScroll();
            UpdateScroll();
            Repaint();
            return;
        }
    }
    // A row click navigates (the callback is the only path).
    const int idx = RowAt(px, py);
    if (idx >= 0) {
        SetFocus(hwnd_);
        selected_ = idx;
        NavigateFrom(idx);
    }
}

void OutlinePane::OnLButtonUp(int px, int py) {
    (void)px; (void)py;
    if (splitterDrag_) {
        // Test for the pane's own lost-mouse-capture case.
        ReleaseCapture();
        EndSplitterDrag();
        return;
    }
    ReleaseCapture();
}

void OutlinePane::OnMouseMove(int px, int py) {
    if (splitterDrag_) {
        DragSplitter(px);
        return;
    }
    const int idx = RowAt(px, py);
    const int v = idx >= 0 ? VisibleIndexOf(idx) : -1;
    HoverSet(v);
    // Splitter cursor over the drag zone.
    if (HitSplitter(px, py)) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
    }
}

int OutlinePane::VisibleIndexOf(int itemIndex) const {
    for (int v = 0; v < static_cast<int>(visible_.size()); ++v)
        if (visible_[v] == itemIndex) return v;
    return -1;
}

void OutlinePane::HoverSet(int visibleIdx) {
    if (hoverVis_ == visibleIdx) return;
    hoverVis_ = visibleIdx;
    Repaint();
}

void OutlinePane::OnMouseWheel(int delta) {
    const float stepPx = 3.f * kRowHeightDip * static_cast<float>(dpi_)
        / 96.f;
    scrollY_ += (delta > 0 ? -stepPx : stepPx);
    ClampScroll();
    UpdateScroll();
    Repaint();
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

void OutlinePane::MoveSelection(int delta) {
    if (visible_.empty()) return;
    // Move within the visible rows, wrapping at the ends.
    int v = VisibleIndexOf(selected_);
    v = (v < 0) ? (delta > 0 ? 0 :
                    static_cast<int>(visible_.size()) - 1)
                : (v + delta + static_cast<int>(visible_.size()))
                    % static_cast<int>(visible_.size());
    selected_ = visible_[v];
    ScrollIntoView(selected_);
    Repaint();
}

void OutlinePane::SelectItem(int item) {
    if (item < 0 || item >= static_cast<int>(items_.size())) return;
    selected_ = item;
    ScrollIntoView(item);
    Repaint();
}

void OutlinePane::CollapseSelected() {
    if (selected_ < 0) return;
    if (!HasChildren(items_, selected_)) return;
    if (!collapse_.IsCollapsed(selected_)) {
        collapse_.Toggle(selected_);
        visible_ = VisibleItems(items_, collapse_);
        LayoutRows();
        ClampScroll();
        UpdateScroll();
        Repaint();
    }
}

void OutlinePane::ExpandSelected() {
    if (selected_ < 0) return;
    if (collapse_.IsCollapsed(selected_)) {
        collapse_.Toggle(selected_);
        visible_ = VisibleItems(items_, collapse_);
        LayoutRows();
        ClampScroll();
        UpdateScroll();
        Repaint();
    }
}

void OutlinePane::ActivateSelection() {
    if (selected_ < 0) return;
    NavigateFrom(selected_);
}

void OutlinePane::NavigateFrom(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(items_.size()))
        return;
    if (navigate_) navigate_(items_[itemIndex].offset);
}

// ---------------------------------------------------------------------------
// Repaint, DPI, SetRect
// ---------------------------------------------------------------------------

void OutlinePane::Repaint() {
    if (!hwnd_) return;
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void OutlinePane::SetDpi(int dpi) {
    if (dpi <= 0) dpi = 96;
    if (dpi_ == dpi) return;
    dpi_ = dpi;
    CreateFonts();
    LayoutRows();
    ClampScroll();
    UpdateScroll();
    ReleaseRt();  // rebuilt at new size/DPI by EnsureRt
    Repaint();
}

void OutlinePane::SetRect(int x, int y, int pixelW, int pixelH, int dpi) {
    (void)x; (void)y;  // position owned by the caller's SetWindowPos
    if (dpi > 0) dpi_ = dpi;
    if (pixelW_ != pixelW || pixelH_ != pixelH) {
        pixelW_ = pixelW;
        pixelH_ = pixelH;
        // The HwndRenderTarget must follow the window size.
        ReleaseRt();
    }
    ClampScroll();
    UpdateScroll();
    Repaint();
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK OutlinePane::WndProc(HWND hwnd, UINT msg, WPARAM wp,
                                       LPARAM lp) {
    OutlinePane* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<OutlinePane*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<OutlinePane*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self && self->hwnd_ == nullptr && msg == WM_NCCREATE)
        self->hwnd_ = hwnd;
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;  // whole face painted in one pass
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            self->Paint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            self->pixelW_ = LOWORD(lp);
            self->pixelH_ = HIWORD(lp);
            self->ReleaseRt();
            self->ClampScroll();
            self->UpdateScroll();
            return 0;
        case WM_VSCROLL: {
            const float page = 3.f * self->kRowHeightDip
                * self->dpi_ / 96.f;
            switch (LOWORD(wp)) {
                case SB_LINEUP:    self->scrollY_ -= page / 3.f; break;
                case SB_LINEDOWN: self->scrollY_ += page / 3.f; break;
                case SB_PAGEUP:   self->scrollY_ -= page; break;
                case SB_PAGEDOWN: self->scrollY_ += page; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: {
                    SCROLLINFO si = { sizeof(SCROLLINFO), SIF_TRACKPOS };
                    if (GetScrollInfo(hwnd, SB_VERT, &si))
                        self->scrollY_ = static_cast<float>(si.nTrackPos)
                            * 96.f / self->dpi_;
                    break;
                }
            }
            self->ClampScroll();
            self->UpdateScroll();
            self->Repaint();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            self->OnMouseWheel(delta);
            return 0;
        }
        case WM_LBUTTONDOWN:
            self->OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_LBUTTONUP:
            self->OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSEMOVE:
            self->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_CAPTURECHANGED:
            self->EndSplitterDrag();
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            self->Repaint();  // focus ring on the active row
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
            self->OnKeyDown(wp);
            return 0;
        case WM_DESTROY:
            self->hwnd_ = nullptr;
            self->ReleaseRt();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void OutlinePane::OnKeyDown(WPARAM vk) {
    switch (vk) {
        case VK_UP:    MoveSelection(-1); break;
        case VK_DOWN:  MoveSelection(1); break;
        case VK_HOME:  if (!visible_.empty()) {
                           selected_ = visible_.front();
                           ScrollIntoView(selected_);
                           Repaint();
                       } break;
        case VK_END:   if (!visible_.empty()) {
                           selected_ = visible_.back();
                           ScrollIntoView(selected_);
                           Repaint();
                       } break;
        case VK_LEFT:  CollapseSelected(); break;
        case VK_RIGHT: ExpandSelected(); break;
        case VK_RETURN: ActivateSelection(); break;
        case VK_F6:
            if (focusCycle_) focusCycle_();
            break;
        case VK_ESCAPE: /* owner decides; pane stays put */ break;
    }
}

// ---------------------------------------------------------------------------
// Paint dispatcher
// ---------------------------------------------------------------------------

void OutlinePane::Paint(HDC hdc) {
    (void)hdc;  // D2D renders directly to hwnd_ via rt_
    BeginPaintPass();  // EnsureRt + fresh brush_
    if (!rt_) return;
    const D2D1_RECT_F rc = D2D1::RectF(0.f, 0.f,
        static_cast<float>(pixelW_), static_cast<float>(pixelH_));
    rt_->BeginDraw();
    PaintBackground(rt_, rc);
    PaintSeparator(rt_, rc);
    if (visible_.empty()) {
        PaintEmptyState(rt_, rc);
    } else {
        PaintRows(rt_, rc);
    }
    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseRt();
    }
}
