#pragma once

#include <windows.h>
#include <shellapi.h>
#include <d2d1.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <string>
#include "dom.h"
#include "renderer.h"
#include "markers.h"
#include "filewatch.h"
#include "textbuffer.h"
#include "layoutcache.h"
#include "caret.h"
#include "editcontroller.h"
#include "navigation.h"
#include "undostack.h"
#include "clipboard.h"
#include "formatting.h"
#include "autoformat.h"
#include "inputfilter.h"
#include "settings.h"
#include "welcomescreen.h"
#include "outlinepane.h"
#include "searchreplace.h"
#include "findbar.h"
#include "textdrag.h"

// Active formatting state at the caret position, used to set
// the pressed/unpressed state of ribbon toggle buttons.
struct FormatState {
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool strike = false;
    int  headingLevel = 0;
    bool inBullets = false;
    bool inNumbering = false;
    bool inQuote = false;
};

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    bool Init(HINSTANCE hInst, int nCmdShow, const std::wstring& cmdLine = {});
    int  Run();

    void OpenFile(const std::wstring& path);
    void Reload();

    // Public action methods, called by the Ribbon command handler.
    void OpenFileDialog();
    void ZoomIn();
    void ZoomOut();
    void ResetZoom();
    // True when + and - would still change the zoom. The ribbon greys the
    // buttons out at the limits rather than letting them do nothing.
    bool CanZoomIn() const;
    bool CanZoomOut() const;
    void ToggleWrap();
    bool IsWrapEnabled() const;

    // Outline pane: toggle, navigation target and active-heading sync.
    // NavigateToHeading is the primitive the pane callback lands in
    // (view mode scrolls, edit mode also moves the caret).
    // Content viewport height in DIP (render target size).
    float ViewHeightDip();

    void ToggleOutline();
    bool IsOutlineVisible() const;
    void NavigateToHeading(uint32_t offset);
    void UpdateOutlineActive();
    void UpdateOutlineActiveFromView();
    std::vector<OutlineItem> OutlineItems() const { return outline_items_; }
    void ShowAbout();
    bool Save();
    bool SaveAs();
    // Word / PDF interop (ribbon: Home tab, Office group).
    void ImportWordDocx();
    void ExportWordDocx();
    void ExportPdf();
    bool IsDirty() const { return dirty_; }
    void SetEdit(bool on);
    bool IsEditing() const { return editing_; }
    void ToggleSourceView();
    bool IsSourceView() const { return source_view_; }
    bool CanUndo() const { return undo_stack_.CanUndo(); }
    bool CanRedo() const { return undo_stack_.CanRedo(); }
    FormatState GetFormatState() const;
    // Remove ALL formatting of the given type from the current selection.
    void RemoveAllFormattingInSelection(bool wantStrong, bool wantEm,
                                        bool wantCode, bool wantStrike,
                                        uint32_t mlen);
    // Expand selection to cover a formatting span + its markers, so
    // ToggleInlineMarker can detect and remove them. Returns true if
    // the selection was expanded.
    bool ExpandSelectionToFormatSpan(bool wantStrong, bool wantEm,
                                      bool wantCode, bool wantStrike,
                                      uint32_t mlen);
    void InvalidateFormatButtons();
    void ToggleEdit() { SetEdit(!editing_); }

    // Marker layer (ribbon: Home tab, Markers group; Ctrl+Shift+H). A
    // marker annotates rendered text: it never touches the Markdown
    // document, and marker changes never mark the document dirty
    // (marker guide 12, 17 and 18).
    void MarkSelection();
    void RemoveMarkerAtSelection();
    // Re-query the ribbon for the marker toggle's enabled and pressed
    // state. Selection changes and marker changes both need it; view
    // mode skips the caret path that normally carries this invalidation.
    void InvalidateMarkerToggleUI();
    // One ribbon button, one shortcut: a selection that touches a marker
    // unmarks it, any other selection marks it (combined on user request).
    // Markers are always drawn; there is no visibility toggle to keep in
    // sync with the ribbon.
    void ToggleMarkSelection();
    bool HasSelection() const { return !sel_.Empty(); }
    bool SelectionHasMarker() const;

    // Settings (Fil menu)
    bool IsMdRegistered() const;
    void ToggleMdAssociation();
    int  GetContentWidthMode() const;
    void SetContentWidthMode(int mode);
    void InvalidateSettingsButtons();

    void ShowContextMenu(int x, int y);
    void SelectAll();

    void OpenPendingFile(const std::wstring& path) {
        pending_file_ = path;
        if (hwnd_) SetTimer(hwnd_, 3, 50, nullptr);
    }
    void ProcessPendingFile();
    void ToggleBold();
    void ToggleItalic();
    void ToggleStrike();
    void ToggleCode();
    void InsertLinkCmd();
    void InsertTableCmd();
    void InsertTableFromGrid(int cols, int rows);
    bool AddTableRow(bool below = true);
    bool RemoveTableRow();
    bool AddTableColumn(bool right = true);
    bool RemoveTableColumn();
    // Write an alignment marker into the caret column's delimiter cell.
    // TableAlignMark::None clears it.
    bool SetTableColumnAlign(TableAlignMark mark);

    // Split the caret's cell into two. Refused on the header and delimiter
    // rows. Markdown cannot express a merged cell, so there is no merge.
    bool SplitTableCell();
    void RemoveTable();
    // True when the caret (or the start of a selection) sits inside a
    // Markdown table. Table Tools use it to enable only the commands that
    // act on the current table.
    bool CaretInTable() const;
    // What the table commands can currently do at the caret. The ribbon
    // uses this to grey out a command rather than offer a live button that
    // silently does nothing.
    TableCapabilities CaretTableCapabilities() const;
    // Splice the buffer and record an undo entry in one step.
    void SpliceWithUndo(uint32_t offset, uint32_t length,
                        const std::string& replacement);
    void ClearFormat();
    void SetHeading(int level);
    void ToggleBullets();
    void ToggleNumbering();
    void ToggleQuote();
    void Indent();
    void Outdent();
    void UndoAction();
    void RedoAction();
    void ShowFindReplace(bool replaceMode);
    void NewDocument();

    // Called by CRibbonApplication::OnViewChanged when ribbon height changes.
    void OnRibbonHeightChanged();

    HWND GetHwnd() const { return hwnd_; }
    HWND GetContentHwnd() const { return hwnd_content_; }

private:
    HWND    hwnd_ = nullptr;
    HWND    hwnd_content_ = nullptr;
    HINSTANCE hinst_ = nullptr;

    ID2D1Factory1*         d2d_factory_ = nullptr;
    ID2D1DeviceContext5*   d2d_ctx5_ = nullptr;   // for SVG, may be null
    ID2D1HwndRenderTarget* rt_ = nullptr;
    IDWriteFactory*        dw_factory_ = nullptr;

    Renderer               renderer_;
    bool                   renderer_inited_ = false;

    Document               doc_;
    std::wstring           file_path_;

    FileWatcher            watcher_;

    // Scroll state (in DIPs)
    float  scrollY_    = 0.0f;  // current scroll position (animated)
    // Horizontal offset in DIPs. The content column is 800 DIPs wide at
    // 100% and scales with zoom, so above roughly 150% on a typical
    // window the right-hand part of the text is outside the viewport
    // unless it can be scrolled to.
    float  scrollX_    = 0.0f;
    float  totalH_     = 0.0f;
    int    clientH_    = 0;
    int    clientW_    = 0;  // viewport width, for horizontal scroll range
    UINT   dpi_        = 96;   // current monitor DPI (for DIP↔pixel conversion)

    // ── Smooth scroll physics engine ──────────────────────────────
    // Trackpad and mouse wheel both feed into the same physics: a
    // critically-damped spring that pulls scrollY_ toward targetY_.
    // Trackpad also builds a velocity; when events stop (fingers
    // lifted), momentum carries the scroll until friction drains it.
    //
    // Phases:
    //   IDLE     – no animation, timer off
    //   SPRING   – spring pulling scrollY_ → targetY_ (mouse wheel,
    //              arrow keys, scrollbar line/page)
    //   TRACKPAD – direct 1:1 follow of accumulated trackpad deltas;
    //              momentum velocity is tracked from delta/time.
    //   MOMENTUM – exponential velocity decay after trackpad release
    //
    UINT_PTR scroll_timer_ = 0;       // 16 ms tick timer (ID 4)

    // Spring state (mouse wheel, keyboard, scrollbar)
    float  spring_target_ = 0.0f;     // spring rest position

    // Momentum state (trackpad flick)
    float  momentum_vel_ = 0.0f;      // px per 16ms tick

    // Input tracking
    DWORD  last_wheel_time_ = 0;     // for trackpad vs mouse detection
    int    scroll_phase_ = 0;        // 0=IDLE,1=SPRING,2=TRACKPAD,3=MOMENTUM
    // Ctrl+wheel zoom: leftover wheel delta (less than one notch) carried
    // over between events so precision trackpads (small deltas) also step
    // the zoom once per 120 WHEEL_DELTA units.
    float  wheel_zoom_acc_ = 0.0f;

    float  ClampScroll(float y) const;
    // Change the zoom factor and keep the point under the cursor (or the
    // viewport center) anchored on screen; updates scroll, scrollbars,
    // and repaints.
    // focusDip is the viewport Y (and X once horizontal scrolling exists)
    // that stays fixed across the zoom. Callers pass the pointer position
    // for a wheel gesture and the viewport centre for a button or
    // shortcut, which is what the zoom guide asks for.
    void ApplyZoom(float newZoom, float focusYdip, float focusXdip);

    // Marker layer plumbing (see MarkSelection and friends above).
    std::string MarkerDocumentPath() const;
    void RefreshMarkerRanges();
    void ResolveMarkerAnchors();
    void LoadMarkersForDocument();
    void SaveMarkers();
    void OnContentHScroll(HWND hwnd, int code, int pos);

    // Editor state
    TextBuffer   buffer_;
    LayoutCache  layout_cache_;
    Selection    sel_;
    bool         caret_visible_ = false;
    int          caret_height_ = 0;   // current caret height in px (for resize on font change)
    bool         editing_ = false;
    bool         source_view_ = false;  // raw markdown source view
    // Marker annotations: the in-memory set plus the ranges handed to the
    // renderer, refreshed whenever markers or the document text change.
    MarkerStore marker_store_;
    std::vector<TextMatch> marker_ranges_;
    // Triple-click detection: last click time and y position.
    DWORD        last_click_time_ = 0;
    int          last_click_y_ = -1;
    int          click_count_ = 0;
    bool         has_focus_ = false;
    EditController  editor_;
    UndoStack       undo_stack_;
    wchar_t         surrogate_buf_ = 0;  // high surrogate waiting for low
    bool            has_surrogate_ = false;
    float           desiredX_ = -1.0f;  // preserved column for vertical nav
    bool            pending_bold_ = false;
    bool            pending_italic_ = false;
    bool            pending_bold_set_ = false;
    bool            pending_italic_set_ = false;
    bool            pending_run_active_ = false;
    uint32_t        pending_run_suffix_bytes_ = 0;
    UINT_PTR        reparse_timer_ = 0;
    bool        reparse_pending_ = false;

    // File lifecycle
    bool        dirty_ = false;
    bool        use_crlf_ = false;
    bool        has_bom_ = false;
    // True while the opened file is a standalone Mermaid file (.mmd):
    // the buffer shows the file wrapped in ```mermaid fences for
    // rendering/editing; save unwraps them again (no fences on disk).
    bool        is_mmd_ = false;
    // True when OpenFile/Reload added the synthetic .mmd fence; only
    // then may save unwrap it (user-fenced .mmd files keep theirs).
    bool        mmd_wrapped_ = false;
    // Same pattern for standalone SVG files (.svg on disk, ```svg
    // fence in the buffer; the renderer rasterizes svg blocks).
    bool        is_svg_ = false;
    bool        svg_wrapped_ = false;
    std::wstring pending_file_;  // file to open after init completes

    // Persisted settings
    AppSettings    settings_;

    // Welcome screen (shown when no file is open)
    WelcomeScreen  welcome_;
    bool           welcome_mode_ = false;
    int            welcome_hover_ = -1;
    OutlinePane    outline_;
    std::vector<OutlineItem> outline_items_;

    // Margin drag selection: when true, mouse drag selects whole lines.
    bool           margin_selecting_ = false;
    int            margin_anchor_block_ = -1;  // block index where drag started
    uint32_t       margin_anchor_start_ = 0;   // start of anchor visual line
    uint32_t       margin_anchor_end_ = 0;     // end of anchor visual line
    float          margin_anchor_y_ = 0.0f;    // doc-space y of anchor line top

    // Double- and triple-click drag preserve their semantic granularity.
    bool           word_dragging_ = false;
    bool           paragraph_dragging_ = false;
    bool           selection_dragging_ = false;
    uint32_t       word_anchor_start_ = 0;
    uint32_t       word_anchor_end_ = 0;
    uint32_t       word_anchor_caret_ = 0;
    int            paragraph_anchor_block_ = -1;

    // Internal text drag state. The selection remains intact until the
    // pointer crosses the drag threshold, so a click in a selection can
    // either keep the selection or move it.
    bool           text_drag_candidate_ = false;
    bool           text_dragging_ = false;
    uint32_t       text_drag_start_ = 0;
    uint32_t       text_drag_length_ = 0;
    int            text_drag_last_x_ = 0;
    int            text_drag_last_y_ = 0;
    int            text_drag_down_x_ = 0;
    int            text_drag_down_y_ = 0;

    // Ctrl+A escalation tier inside tables (0 = fresh, 1 = cell, 2 = table).
    int last_selectall_tier_ = 0;
    // Caret position captured at the last tier advance; a caret that
    // moved since breaks the escalation run.
    uint32_t last_selectall_caret_ = UINT32_MAX;

    // What the Find scope filter reads. Declared BEFORE the member that
    // holds it: a nested type is not visible to code above its own
    // declaration point inside the class, so the member would otherwise
    // name an undeclared type.
    struct FindScopeContext {
        const AppWindow* app = nullptr;
        uint32_t selectionStart = 0;
        uint32_t selectionEnd = 0;
        bool withinSelection = false;
    };

    // Find and Replace. The bar is modeless, so the document stays visible
    // and editable while it is open (find guide 31), and the search state
    // lives inside the bar rather than here.
    FindBar        find_bar_;
    // The scope context outlives every Refresh call, so it is a member and
    // not a stack temporary: the filter receives it as a bare void* and
    // would otherwise read freed memory.
    FindScopeContext find_scope_;
    // It is not added to the undo stack until the IME reports a result.
    bool           ime_composing_ = false;
    uint32_t       ime_source_start_ = 0;
    std::string    ime_preedit_;
    std::string    ime_replaced_text_;
    Selection      ime_selection_before_;

    static const wchar_t* kClassName;
    static const wchar_t* kContentClassName;

    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT ContentWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnCreate(HWND hwnd);
    void OnSize(HWND hwnd, int width, int height);
    void ResizeContentWindow();
    void OnContentPaint(HWND hwnd);
    void OnContentVScroll(HWND hwnd, int code, int pos);
    void OnContentMouseWheel(HWND hwnd, int delta);
    void StartSpring(float targetY);
    void BeginTrackpadScroll(float delta, DWORD now);
    void EnterMomentum();
    void OnScrollTick();
    void EnsureScrollTimer();
    void StopScrollTimer();
    void StopScrollAnimation();
    void OnContentSize(HWND hwnd, int width, int height);
    void OnDropFiles(HWND hwnd, HDROP hDrop);
    void OnReload();
    void OnBufferChanged();
    void UpdateCaretPosition();
    // Create the system caret at the selection's active end and show
    // it. Runs whenever focus or edit mode turns the caret on; the
    // caller guards on mode and the current caret state.
    bool EnsureCaretVisible();
    // Scroll the viewport so the caret stays visible after keyboard
    // navigation. Direct jump (no spring animation).
    void ScrollCaretIntoView(float caretY, float caretH);
    // Offset the caret had at the last scroll-follow. Equal offsets
    // skip scrolling so wheel/trackpad scrolling is never fought by
    // caret-follow (the caret doc position did not change).
    uint32_t last_scroll_offset_ = UINT32_MAX;
    void OnLButtonDown(HWND hwnd, int x, int y);
    void OnLButtonDblClk(HWND hwnd, int x, int y);
    void OnMouseMove(HWND hwnd, int x, int y);
    void OnLButtonUp(HWND hwnd);
    void FinishTextDrag();
    // The Find & Replace session. RefreshFindResults re-runs the search
    // against the live document and republishes the counts, the highlights
    // and the current match. Everything else is reached through the bar's
    // listener callbacks.
    void RefreshFindResults(bool adoptCurrentMatch);
    void ApplyFindSelection(uint32_t offset, uint32_t length);
    void UpdateFindHighlight();
    void SyncFindBarFromDocument();
    // The document changed underneath an open Find bar: recompute rather
    // than keep, so no stale offset is ever used.
    void OnFindDocumentChanged();
    static bool FindScopeFilter(uint32_t start, uint32_t length,
                                void* context);
    void WireFindBar();
    void CloseFindBar();
    std::string SelectionForClipboard() const;
    void OnSetFocus(HWND hwnd);
    void OnImeComposition(LPARAM lp);
    void OnImeEndComposition();
    void OnKillFocus(HWND hwnd);

    // Find the URL of a link at the given source offset, if any.
    // Returns empty string if no link is at that offset.
    std::string FindLinkAtOffset(uint32_t offset) const;
    // Open a URL in the default browser (external links) or scroll
    // to an internal anchor (#section).
    void OpenLink(const std::string& url);

    void OnChar(HWND hwnd, wchar_t ch);
    void InitEditor();
    void OnKeyDown(HWND hwnd, WPARAM vk, LPARAM lp);
    void ApplyUndo(bool redo);
    void RecordPendingFormatUndo(bool beforeBold, bool beforeItalic,
                                 bool beforeBoldSet, bool beforeItalicSet);
    void OnReparseTimer();
    void ScheduleReparse();
    void OnDestroy();
    void OnClose();
    bool DoSave(const std::wstring& path);
    void MarkDirty();
    void ClearDirty();
    void UpdateTitleBar();
    std::wstring SaveDialog();
    int  PromptSaveDiscardCancel();
    // Load UTF-8 markdown into the editor: the sequence OpenFile runs for
    // text read from disk, shared with the .docx import. An empty path
    // means the text has no file on disk: untitled document, nothing
    // watched, and Save falls back to Save As.
    void LoadDocumentText(const std::string& raw, const std::wstring& path);
    // Save dialog for an export, pre-filled with a name derived from the
    // current document. Empty result means the user cancelled.
    std::wstring ExportSaveDialog(const wchar_t* filter, const wchar_t* defExt,
                                  const std::wstring& defaultName);
    // Default export file name: the current document's path with ext in
    // place of its own extension, or "document" plus ext when it has no
    // file on disk.
    std::wstring ExportDefaultName(const wchar_t* ext) const;
    void RecreateRenderTarget();
    void EnsureRenderer();
    void UpdateDpi();         // Query monitor DPI and apply to render target.
    void UpdateScrollInfo();
    void LoadSampleDoc();
    // Drop recent entries whose file no longer exists on disk, both
    // from the in-memory list and from the saved settings.
    void PruneMissingRecentFiles();
    void Repaint();
    void ForceRepaintNow();  // Immediate repaint (for ribbon-triggered edits).
    void RecreateRenderer();

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK ContentWndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};