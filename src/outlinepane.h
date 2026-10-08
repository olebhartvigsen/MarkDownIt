#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <cstdint>
#include <functional>
#include <vector>

#include "outline.h"

// Outline pane: the heading navigation tree, docked on the left side of
// the content window as a child HWND.
//
// DIVISION OF WORK (mirrors the FindBar contract). The pane owns VISUAL
// and NAVIGATION STATE only:
//   * It displays the collected heading list, the collapse state and
//     the active heading.
//   * It NEVER touches the document, the selection, the undo stack or
//     the buffer, and it never scrolls the document itself. Every jump
//     request flows through the navigate callback the owner installs.
// The pane holds no document state of its own beyond the copy in its
// item list, which is rebuilt after every reparse.
//
// Windowing follows the FindBar precedent (own window class, own
// window procedure, GWLP_USERDATA for the this-pointer). Painting
// follows the welcome screen (D2D on the pane's own HwndRenderTarget):
// a second render target over a separate HWND cannot disturb the
// document's coordinate space, which is why the pane is a child window
// rather than a painted strip inside the content window.
//
// Owner contract:
//   1. Create(parent, d2d, dw, dpi) once at startup; the pane starts
//      hidden.
//   2. ResizeContentWindow calls SetRect on every layout pass, docking
//      the pane to the left edge of the content band. Geometry is
//      always the owner's decision.
//   3. On reparse: SetItems(CollectHeadings(doc)) refreshes the list
//      and re-resolves the collapse state.
//   4. On caret move or scroll: SetActive(itemIndex) keeps the active
//      heading in sync (one way; the pane never pushes back).
//   5. ToggleOutline on the owner shows or hides and lays out.

class OutlinePane {
public:
    OutlinePane();
    ~OutlinePane();

    OutlinePane(const OutlinePane&) = delete;
    OutlinePane& operator=(const OutlinePane&) = delete;

    // Registers the class lazily and creates the hidden child window.
    // The factories are borrowed (the app owns their lifetime); the
    // render target is NOT borrowed: the pane creates its own so it can
    // never invalidate the document render target. Returns false only
    // when the window could not be created, in which case the owner
    // carries on without an outline.
    bool Create(HWND parent, ID2D1Factory* d2d, IDWriteFactory* dw,
                int dpi);
    void Destroy();

    bool IsCreated() const { return hwnd_ != nullptr; }
    bool IsVisible() const;
    HWND Handle() const { return hwnd_; }

    // Geometry. WidthDip/SetWidthDip is the remembered pane width in
    // DIP (persisted by the owner); PreferredWidthDip sizes from the
    // current items, clamped to kMaxWidthDip. SetRect is called from
    // the owner's layout pass on every resize.
    float WidthDip() const { return widthDip_; }
    void SetWidthDip(float w);
    float PreferredWidthDip() const;
    static constexpr float kDefaultWidthDip = 200.f;
    static constexpr float kMinWidthDip = 140.f;
    static constexpr float kMaxWidthDip = 420.f;

    // Splitter behavior. HitSplitter tests pane-local pixels against
    // the 4 DIP drag zone on the right edge. BeginSplitterDrag /
    // DragSplitter / EndSplitterDrag run the resize; the owner just
    // forwards the mouse messages while the pointer is inside.
    bool HitSplitter(int px, int py) const;
    bool splitterDrag() const { return splitterDrag_; }
    void BeginSplitterDrag(int px);
    void DragSplitter(int px);
    void EndSplitterDrag();

    // The item list after a reparse. Re-resolves collapse by level and
    // offset so edited rows keep their folded state (same policy as
    // the markers sidecar). An empty list shows the empty state.
    void SetItems(const std::vector<OutlineItem>& items);

    // Active heading (the section the caret is in). index is an index
    // into items, -1 clears. One-way sync from the owner.
    void SetActive(int index);
    int  Active() const { return active_; }

    // The ONLY way the pane affects the document: the owner receives
    // the jump target (source byte offset) and does the scroll and
    // caret work with the existing primitives.
    void SetNavigateCallback(std::function<void(uint32_t offset)> cb);
    bool HasNavigateCallback() const { return navigate_ != nullptr; }

    // F6 inside the pane asks the owner where focus should cycle to
    // (the pane has no knowledge of the document window).
    void SetFocusCycleCallback(std::function<void()> cb);

    // The user finished dragging the splitter: report the committed
    // width so the owner can persist it.
    void SetWidthCommittedCallback(std::function<void(float)> cb);

    // Keyboard selection (pane-local). SelectedItem is an item index
    // or -1; SelectItem moves it and scrolls it into view;
    // ActivateSelection is Enter. Collapse selection helpers for
    // Left/Right.
    int  SelectedItem() const { return selected_; }
    void SelectItem(int item);
    void MoveSelection(int delta);
    void CollapseSelected();
    void ExpandSelected();
    void ActivateSelection();

    // Scroll a row fully into view (owner calls after SetActive).
    void ScrollIntoView(int index);

    void Repaint();

    // Geometry from the owner's layout pass (ResizeContentWindow).
    // The pane adapts its render target, scrollbar and scroll clamp.
    void SetRect(int x, int y, int pixelW, int pixelH, int dpi);

    // DPI changed (per-monitor change or a moved window): rebuild
    // fonts and the render target.
    void SetDpi(int dpi);

    // Row metrics exposed for layout and tests.
    static constexpr float kPadXDip = 8.f;
    static constexpr float kRowHeightDip = 24.f;
    static constexpr float kIndentStepDip = 16.f;
    static constexpr float kIndentMaxDip = 80.f;
    float IndentDipFor(const OutlineItem& item) const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp,
                                    LPARAM lp);
    static bool RegisterOutlineClass();

    // Paint pipeline (welcome-screen style).
    void EnsureRt();
    void ReleaseRt();
    void CreateFonts();
    void ReleaseFonts();
    void LayoutRows();
    void Paint(HDC hdc);
    void BeginPaintPass();
    void PaintBackground(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc);
    void PaintSeparator(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc);
    void PaintRows(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc);
    void PaintEmptyState(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc);

    // Hit helpers: visible-row index or -1 for pane-local pixels.
    int  RowAt(int px, int py) const;
    int  VisibleIndexOf(int itemIndex) const;
    bool ArrowAt(int px, int py) const;
    float RowTopDip(int visibleIdx) const;
    float TotalHeightDip() const;

    void UpdateScroll();
    void ClampScroll();
    void HoverSet(int visibleIdx);
    void OnLButtonDown(int px, int py);
    void OnLButtonUp(int px, int py);
    void OnMouseMove(int px, int py);
    void OnMouseWheel(int delta);
    void OnKeyDown(WPARAM vk);
    void NavigateFrom(int itemIndex);

    // Items and derived state.
    std::vector<OutlineItem> items_;
    std::vector<int> visible_;    // item indices, top to bottom
    std::vector<D2D1_RECT_F> rowRects_;   // rows of visible_, DIP
    std::vector<D2D1_RECT_F> arrowRects_;  // disclosure hit zones
    OutlineCollapse collapse_;
    int active_ = -1;      // item index
    int selected_ = -1;    // item index
    int hoverVis_ = -1;    // visible-row index
    bool trackingMouse_ = false;  // leave-notice armed (see OnMouseMove)
    float scrollY_ = 0.f;  // pane-local scroll in DIP

    // Splitter drag state.
    bool splitterDrag_ = false;
    float splitterStartPx_ = 0.f;
    float splitterStartW_ = 0.f;
    float widthDip_ = kDefaultWidthDip;

    // Win32 and D2D plumbing.
    HWND hwnd_ = nullptr;
    ID2D1Factory* d2d_ = nullptr;          // borrowed
    IDWriteFactory* dw_ = nullptr;         // borrowed
    ID2D1HwndRenderTarget* rt_ = nullptr;  // own, small
    IDWriteTextFormat* fmtItem_ = nullptr;
    IDWriteTextFormat* fmtEmpty_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    int pixelW_ = 0, pixelH_ = 0;
    int dpi_ = 96;

    // Colors (light theme, pane-local; see CreateFonts note in .cpp).
    D2D1_COLOR_F colBg_ = {};
    D2D1_COLOR_F colSep_ = {};
    D2D1_COLOR_F colText_ = {};
    D2D1_COLOR_F colTextMuted_ = {};
    D2D1_COLOR_F colActiveBg_ = {};
    D2D1_COLOR_F colActiveText_ = {};
    D2D1_COLOR_F colHoverBg_ = {};
    D2D1_COLOR_F colArrow_ = {};
    void InitColors();

    std::function<void(uint32_t offset)> navigate_;
    std::function<void()> focusCycle_;
    std::function<void(float)> widthCommitted_;
};
