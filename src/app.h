#pragma once

#include <windows.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "dom.h"
#include "renderer.h"
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

    bool Init(HINSTANCE hInst, int nCmdShow);
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
    FormatState GetFormatState() const;
    void InvalidateFormatButtons();
    void ToggleEdit() { SetEdit(!editing_); }

    // Settings (Fil menu)
    bool IsMdRegistered() const;
    void ToggleMdAssociation();
    int  GetContentWidthMode() const;
    void SetContentWidthMode(int mode);
    void InvalidateSettingsButtons();

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
    void ClearFormat();
    void SetHeading(int level);
    void ToggleBullets();
    void ToggleNumbering();
    void ToggleQuote();
    void Indent();
    void Outdent();

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
    float  targetY_   = 0.0f;  // target scroll position for animation
    float  totalH_     = 0.0f;
    int    clientW_    = 0;
    int    clientH_    = 0;

    // Smooth scroll animation
    UINT_PTR scroll_timer_ = 0;
    float  scroll_vel_  = 0.0f;  // velocity for inertial scrolling
    float  scroll_anim_start_ = 0.0f;  // start value for ease-out
    float  scroll_anim_target_ = 0.0f;  // target value for ease-out
    DWORD  scroll_anim_start_time_ = 0;  // GetTickCount at animation start
    static const DWORD SCROLL_ANIM_MS = 220;  // animation duration
    DWORD  last_wheel_time_ = 0;  // for trackpad vs mouse detection
    bool   is_trackpad_ = false;  // true if recent input looks like trackpad
    float  ClampScroll(float y) const;

    // Editor state
    TextBuffer   buffer_;
    LayoutCache  layout_cache_;
    Selection    sel_;
    bool         caret_visible_ = false;
    bool         editing_ = false;
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
    void StartScrollAnimation(float targetY);
    void OnScrollTimer();
    void StopScrollAnimation();
    void OnContentSize(HWND hwnd, int width, int height);
    void OnDropFiles(HWND hwnd, HDROP hDrop);
    void OnReload();
    void OnBufferChanged();
    void UpdateCaretPosition();
    void OnLButtonDown(HWND hwnd, int x, int y);
    void OnLButtonDblClk(HWND hwnd, int x, int y);
    void OnMouseMove(HWND hwnd, int x, int y);
    void OnLButtonUp(HWND hwnd);
    void OnSetFocus(HWND hwnd);
    void OnKillFocus(HWND hwnd);
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
    void UpdateScrollInfo();
    void LoadSampleDoc();
    void Repaint();
    void RecreateRenderer();

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK ContentWndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
