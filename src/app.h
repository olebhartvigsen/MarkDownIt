#pragma once

#include <windows.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "dom.h"
#include "renderer.h"
#include "diagramcache.h"
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
    void ToggleWrap();
    bool IsWrapEnabled() const;
    void ShowAbout();
    bool Save();
    bool SaveAs();
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
    bool AddTableRow();
    bool RemoveTableRow();
    bool AddTableColumn();
    bool RemoveTableColumn();
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

    // Called by CRibbonApplication::OnViewChanged when ribbon height changes.
    void OnRibbonHeightChanged();

    HWND GetHwnd() const { return hwnd_; }
    HWND GetContentHwnd() const { return hwnd_content_; }

private:
    HWND    hwnd_ = nullptr;
    HWND    hwnd_content_ = nullptr;
    HINSTANCE hinst_ = nullptr;

    ID2D1Factory*          d2d_factory_ = nullptr;
    ID2D1HwndRenderTarget* rt_ = nullptr;
    IDWriteFactory*        dw_factory_ = nullptr;

    Renderer               renderer_;
    bool                   renderer_inited_ = false;

    Document               doc_;
    std::wstring           file_path_;

    FileWatcher            watcher_;

    // Scroll state (in DIPs)
    float  scrollY_    = 0.0f;  // current scroll position (animated)
    float  totalH_     = 0.0f;
    int    clientH_    = 0;
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

    float  ClampScroll(float y) const;

    // Editor state
    TextBuffer   buffer_;
    LayoutCache  layout_cache_;
    mermaid::DiagramCache diagram_cache_;
    Selection    sel_;
    bool         caret_visible_ = false;
    int          caret_height_ = 0;   // current caret height in px (for resize on font change)
    bool         editing_ = false;
    bool         source_view_ = false;  // raw markdown source view
    // Triple-click detection: last click time and y position.
    DWORD        last_click_time_ = 0;
    int          last_click_y_ = -1;
    int          click_count_ = 0;
    bool         has_focus_ = false;
    EditController  editor_;
    UndoStack       undo_stack_;
    wchar_t      surrogate_buf_ = 0;  // high surrogate waiting for low
    bool         has_surrogate_ = false;
    float        desiredX_ = -1.0f;  // preserved column for vertical nav
    UINT_PTR    reparse_timer_ = 0;
    bool        reparse_pending_ = false;

    // File lifecycle
    bool        dirty_ = false;
    bool        use_crlf_ = false;
    bool        has_bom_ = false;
    std::wstring pending_file_;  // file to open after init completes

    // Persisted settings
    AppSettings    settings_;

    // Welcome screen (shown when no file is open)
    WelcomeScreen  welcome_;
    bool           welcome_mode_ = false;
    int            welcome_hover_ = -1;

    // Margin drag selection: when true, mouse drag selects whole lines.
    bool           margin_selecting_ = false;
    int            margin_anchor_block_ = -1;  // block index where drag started
    uint32_t       margin_anchor_start_ = 0;   // start of anchor visual line
    uint32_t       margin_anchor_end_ = 0;     // end of anchor visual line
    float          margin_anchor_y_ = 0.0f;    // doc-space y of anchor line top

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
    void OnSetFocus(HWND hwnd);
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
    void RecreateRenderTarget();
    void EnsureRenderer();
    void UpdateDpi();         // Query monitor DPI and apply to render target.
    void UpdateScrollInfo();
    void LoadSampleDoc();
    void Repaint();
    void ForceRepaintNow();  // Immediate repaint (for ribbon-triggered edits).
    void RecreateRenderer();

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK ContentWndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
