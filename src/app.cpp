#include "app.h"
#include "resource.h"
#include "ribbon.h"
#include "fileassoc.h"
#include "parser.h"
#include <commdlg.h>
#include <windowsx.h>

#include <windows.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cmath>






const wchar_t* AppWindow::kClassName = L"MarkDownItWindow";
const wchar_t* AppWindow::kContentClassName = L"MarkDownItContent";

template <typename T>
inline void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

static UINT GetWindowDpi(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        auto fn = (PFN_GetDpiForWindow)GetProcAddress(user32, "GetDpiForWindow");
        if (fn) return fn(hwnd);
    }
    return 96;
}

AppWindow::AppWindow() {}

// Process selected text for clipboard copy in view mode.
// Converts soft line breaks (single \n within a paragraph) to spaces,
// preserving:
//   - Paragraph breaks: \n\n (double newline) stays as-is
//   - Forced breaks: two trailing spaces + \n stays as-is
//   - Single \n not part of the above becomes a space
static std::string CleanSelectionForCopy(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        if (c != '\n') {
            out += c;
            continue;
        }
        // Check for paragraph break (\n\n) or start-of-text \n
        bool prevIsNL = (i > 0 && src[i - 1] == '\n');
        bool nextIsNL = (i + 1 < src.size() && src[i + 1] == '\n');
        // Check for forced break: two spaces before \n
        bool forcedBreak = (i >= 2 && src[i - 1] == ' ' && src[i - 2] == ' ');

        if (prevIsNL || nextIsNL || forcedBreak) {
            // Keep paragraph breaks and forced breaks as-is
            out += '\n';
        } else {
            // Soft break within a paragraph: replace with space.
            // Avoid double spaces if the previous char is already a space.
            if (out.empty() || out.back() == ' ' || out.back() == '\n')
                continue;  // skip, already separated
            out += ' ';
        }
    }
    return out;
}
AppWindow::~AppWindow() {
    SafeRelease(rt_);
    SafeRelease(d2d_factory_);
    SafeRelease(dw_factory_);
    renderer_.Release();
}

static void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
        auto fn = (PFN_SetProcessDpiAwarenessContext)
            GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) {
            if (fn((HANDLE)(LONG_PTR)-4)) return;
            if (fn((HANDLE)(LONG_PTR)-3)) return;
            if (fn((HANDLE)(LONG_PTR)-2)) return;
        }
    }
    SetProcessDPIAware();
}

// Save window placement to registry. Called at shutdown.
static void SaveWinPlacement(HWND hwnd) {
    WINDOWPLACEMENT wp;
    ZeroMemory(&wp, sizeof(wp));
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd, &wp)) return;

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\MarkDownIt", 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return;

    DWORD maximized = IsZoomed(hwnd) ? 1u : 0u;
    RegSetValueExW(hKey, L"WindowPlacement", 0, REG_BINARY,
        reinterpret_cast<BYTE*>(&wp), sizeof(wp));
    RegSetValueExW(hKey, L"WindowMaximized", 0, REG_DWORD,
        reinterpret_cast<BYTE*>(&maximized), sizeof(maximized));

    RegCloseKey(hKey);
}

// Restore window placement from registry if available and on-screen.
// Returns true on successful restore (also calls ShowWindow itself).
static bool RestoreWinPlacement(HWND hwnd) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\MarkDownIt", 0,
            KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD size = sizeof(WINDOWPLACEMENT);
    DWORD type = 0;
    WINDOWPLACEMENT wp;
    ZeroMemory(&wp, sizeof(wp));
    wp.length = sizeof(wp);
    LSTATUS lr = RegQueryValueExW(hKey, L"WindowPlacement", nullptr, &type,
        reinterpret_cast<BYTE*>(&wp), &size);

    DWORD maxVal = 0;
    DWORD maxSz = sizeof(maxVal);
    DWORD maxType = 0;
    RegQueryValueExW(hKey, L"WindowMaximized", nullptr, &maxType,
        reinterpret_cast<BYTE*>(&maxVal), &maxSz);

    RegCloseKey(hKey);

    if (lr != ERROR_SUCCESS || type != REG_BINARY || wp.length != sizeof(WINDOWPLACEMENT))
        return false;

    // Validate that the restored rect is at least partially on-screen.
    // Minimum visible window: 200x100 pixels.
    RECT rc = wp.rcNormalPosition;
    HMONITOR hMon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    if (!hMon) return false;

    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return false;

    RECT work = mi.rcWork;
    int visLeft = (rc.left > work.left) ? rc.left : work.left;
    int visTop = (rc.top > work.top) ? rc.top : work.top;
    int visRight = (rc.right < work.right) ? rc.right : work.right;
    int visBottom = (rc.bottom < work.bottom) ? rc.bottom : work.bottom;
    int visW = visRight - visLeft;
    int visH = visBottom - visTop;
    if (visW < 200 || visH < 100) return false;

    wp.showCmd = SW_SHOWNORMAL;
    wp.flags = 0;
    SetWindowPlacement(hwnd, &wp);
    ShowWindow(hwnd, maxVal ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    return true;
}

// Fil button hidden via COM API (OnCreateUICommand returns E_NOTIMPL for APPLICATIONMENU)

bool AppWindow::Init(HINSTANCE hInst, int nCmdShow, const std::wstring& cmdLine) {
    hinst_ = hInst;
    if (!cmdLine.empty()) {
        pending_file_ = cmdLine;
    }
    EnableDpiAwareness();

    // Register the main window class.
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProcThunk;
    wc.hInstance     = hInst;
    wc.lpszClassName = kClassName;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    if (!RegisterClassW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClass failed", L"MarkDownIt", MB_ICONERROR);
        return false;
    }

    // Register the content child window class.
    WNDCLASSW wc2 = {};
    wc2.lpfnWndProc   = ContentWndProcThunk;
    wc2.hInstance     = hInst;
    wc2.lpszClassName = kContentClassName;
    wc2.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc2.hbrBackground = nullptr;
    wc2.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    if (!RegisterClassW(&wc2)) {
        MessageBoxW(nullptr, L"RegisterClass (content) failed", L"MarkDownIt", MB_ICONERROR);
        return false;
    }

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = 900, h = 640;
    int x = (sw - w) / 2, y = (sh - h) / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_ACCEPTFILES, kClassName, L"MarkDownIt",
        WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        nullptr, nullptr, hInst, this);

    if (!hwnd_) {
        MessageBoxW(nullptr, L"CreateWindow failed", L"MarkDownIt", MB_ICONERROR);
        return false;
    }

    // Restore saved window placement, or use default if not available.
    if (!RestoreWinPlacement(hwnd_)) {
        ShowWindow(hwnd_, nCmdShow);
    }
    UpdateWindow(hwnd_);

    // Initialize the Ribbon Framework after the window is visible.
    InitRibbon(hwnd_, this);

    // Force a content resize + repaint. The ribbon may not fire
    // OnViewChanged immediately, so we ensure the content window
    // is positioned correctly right after initialization.
    ResizeContentWindow();
    Repaint();

    // Set a one-shot timer to re-layout and repaint after the ribbon
    // has had time to report its height. The ribbon's OnViewChanged
    // callback may fire asynchronously, so this is a safety net.
    SetTimer(hwnd_, 1, 300, nullptr);

    // Register .md/.markdown file association based on the saved setting.
    // (settings_ was already loaded in OnCreate, before LoadSampleDoc.)
    {
        if (settings_.fileAssoc) {
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(hInst, exePath, MAX_PATH);
            RegisterMdAssociation(exePath);
        }
        // Apply content width setting to the renderer.
        renderer_.SetContentWidthMode(settings_.contentWidthMode);
    }

    return true;
}

int AppWindow::Run() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void AppWindow::EnsureRenderer() {
    if (renderer_inited_ || !dw_factory_) return;
    renderer_inited_ = renderer_.Init(dw_factory_);
    renderer_.SetLayoutCache(&layout_cache_);
}

void AppWindow::InitEditor() {
    editor_ = EditController(&buffer_, &sel_);
    editor_.SetUndoStack(&undo_stack_);
}

void AppWindow::OnKeyDown(HWND hwnd, WPARAM vk, LPARAM lp) {
    if (!has_focus_) return;

    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

    // Toggle edit mode with Ctrl+E
    if (ctrl && !shift && vk == 0x45) {
        SetEdit(!editing_);
        return;
    }

    // In view mode, suppress editing keys but allow navigation, selection,
    // and copy (Ctrl+C).
    if (!editing_) {
        bool isNavigation = (vk == VK_LEFT || vk == VK_RIGHT ||
            vk == VK_UP || vk == VK_DOWN ||
            vk == VK_HOME || vk == VK_END ||
            vk == VK_PRIOR || vk == VK_NEXT);
        bool isCopy = (ctrl && vk == 0x43);  // Ctrl+C
        // Shift+navigation is allowed (extends selection for copy).
        if (!isNavigation && !isCopy && !shift) return;
    }

    switch (vk) {
        case VK_LEFT: {
            uint32_t newOffset = ctrl
                ? MoveWordLeft(buffer_, sel_.active.offset)
                : MoveLeft(buffer_, sel_.active.offset);
            desiredX_ = -1.0f;
            if (!shift) editor_.BreakUndoCoalesce();
            if (shift) sel_.active = {newOffset};
            else sel_.Collapse({newOffset});
            UpdateCaretPosition();
            Repaint();
            break;
        }
        case VK_RIGHT: {
            uint32_t newOffset = ctrl
                ? MoveWordRight(buffer_, sel_.active.offset)
                : MoveRight(buffer_, sel_.active.offset);
            desiredX_ = -1.0f;
            if (!shift) editor_.BreakUndoCoalesce();
            if (shift) sel_.active = {newOffset};
            else sel_.Collapse({newOffset});
            UpdateCaretPosition();
            Repaint();
            break;
        }
        case VK_UP: {
            if (!editing_) {
                // View mode: scroll up by one line height.
                float lineHeight = 20.0f;
                if (rt_) {
                    D2D1_SIZE_F sz = rt_->GetSize();
                    lineHeight = sz.height / 40.0f;
                    if (lineHeight < 16.0f) lineHeight = 16.0f;
                }
                StartSpring(scrollY_ - lineHeight);
                break;
            }
            float lineHeight = 20.0f;
            if (rt_) {
                D2D1_SIZE_F sz = rt_->GetSize();
                lineHeight = sz.height / 40.0f;  // rough estimate
                if (lineHeight < 16.0f) lineHeight = 16.0f;
            }
            uint32_t newOffset = MoveVertical(layout_cache_,
                sel_.active.offset, -1, &desiredX_, scrollY_, lineHeight);
            if (shift) sel_.active = {newOffset};
            else sel_.Collapse({newOffset});
            UpdateCaretPosition();
            Repaint();
            break;
        }
        case VK_DOWN: {
            if (!editing_) {
                // View mode: scroll down by one line height.
                float lineHeight = 20.0f;
                if (rt_) {
                    D2D1_SIZE_F sz = rt_->GetSize();
                    lineHeight = sz.height / 40.0f;
                    if (lineHeight < 16.0f) lineHeight = 16.0f;
                }
                StartSpring(scrollY_ + lineHeight);
                break;
            }
            float lineHeight = 20.0f;
            if (rt_) {
                D2D1_SIZE_F sz = rt_->GetSize();
                lineHeight = sz.height / 40.0f;
                if (lineHeight < 16.0f) lineHeight = 16.0f;
            }
            uint32_t newOffset = MoveVertical(layout_cache_,
                sel_.active.offset, 1, &desiredX_, scrollY_, lineHeight);
            if (shift) sel_.active = {newOffset};
            else sel_.Collapse({newOffset});
            UpdateCaretPosition();
            Repaint();
            break;
        }
        case VK_HOME: {
            uint32_t newOffset;
            if (ctrl) {
                newOffset = 0;
            } else {
                newOffset = MoveLineStart(layout_cache_, sel_.active.offset);
            }
            desiredX_ = -1.0f;
            if (!shift) editor_.BreakUndoCoalesce();
            if (shift) sel_.active = {newOffset};
            else sel_.Collapse({newOffset});
            UpdateCaretPosition();
            Repaint();
            break;
        }
        case VK_END: {
            uint32_t newOffset;
            if (ctrl) {
                newOffset = static_cast<uint32_t>(buffer_.Length());
            } else {
                newOffset = MoveLineEnd(layout_cache_, sel_.active.offset);
            }
            desiredX_ = -1.0f;
            if (!shift) editor_.BreakUndoCoalesce();
            if (shift) sel_.active = {newOffset};
            else sel_.Collapse({newOffset});
            UpdateCaretPosition();
            Repaint();
            break;
        }
        case VK_NEXT: {  // Page Down
            float page = 0.0f;
            if (rt_) page = rt_->GetSize().height;
            else page = static_cast<float>(clientH_);
            StartSpring(scrollY_ + page);
            break;
        }
        case VK_PRIOR: {  // Page Up
            float page = 0.0f;
            if (rt_) page = rt_->GetSize().height;
            else page = static_cast<float>(clientH_);
            StartSpring(scrollY_ - page);
            break;
        }
        case VK_F5:
            Reload();
            break;
        case 0x31:  // Ctrl+1 = H1
        case 0x32:  // Ctrl+2 = H2
        case 0x33:  // Ctrl+3 = H3
        case 0x34:  // Ctrl+4 = H4
        case 0x35:  // Ctrl+5 = H5
        case 0x36:  // Ctrl+6 = H6
            if (ctrl && !shift) {
                int level = static_cast<int>(vk - 0x30);
                SetHeading(level);
            }
            break;
        case 0x30:  // Ctrl+0 = remove heading
            if (ctrl && !shift) {
                SetHeadingLevel(&buffer_, &sel_, 0);
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case 0x37:  // Ctrl+7/Ctrl+Shift+7 = ordered list
            if (ctrl && shift) {
                ToggleOrderedList(&buffer_, &sel_);
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case 0x38:  // Ctrl+8/Ctrl+Shift+8 = unordered list
            if (ctrl && shift) {
                ToggleUnorderedList(&buffer_, &sel_);
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case VK_OEM_PERIOD:  // Ctrl+Shift+. = blockquote
            if (ctrl && shift) {
                ToggleBlockquote(&buffer_, &sel_);
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case VK_TAB:
            if (shift) {
                OutdentLine(&buffer_, &sel_);
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            } else {
                IndentLine(&buffer_, &sel_);
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case 0x42:  // Ctrl+B = bold
            if (ctrl && !shift) {
                ToggleInlineMarker(&buffer_, &sel_, "**");
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case 0x49:  // Ctrl+I = italic
            if (ctrl && !shift) {
                ToggleInlineMarker(&buffer_, &sel_, "*");
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case 0x4B:  // Ctrl+K = link
            if (ctrl && !shift) {
                InsertLink(&buffer_, &sel_, "https://");
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case 0x58:  // Ctrl+X = cut, Ctrl+Shift+X = strikethrough
            if (ctrl && shift) {
                ToggleInlineMarker(&buffer_, &sel_, "~~");
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            } else if (ctrl && !shift) {
                if (!sel_.Empty()) {
                    uint32_t s = sel_.Start();
                    uint32_t len = sel_.Length();
                    std::string sel_text = buffer_.Text().substr(s, len);
                    ClipboardCut(hwnd_content_, sel_text);
                    editor_.DeleteSelection();
                    OnBufferChanged();
                }
            }
            break;
        case VK_OEM_3:  // Ctrl+` = inline code (backtick key)
            if (ctrl && !shift) {
                ToggleInlineMarker(&buffer_, &sel_, "`");
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case 0x53:  // Ctrl+S = save, Ctrl+Shift+S = save as
            if (ctrl && shift) {
                SaveAs();
            } else if (ctrl && !shift) {
                Save();
            }
            break;
        case 0x43:  // Ctrl+C = copy
            if (ctrl && !shift) {
                if (!sel_.Empty()) {
                    uint32_t s = sel_.Start();
                    uint32_t len = sel_.Length();
                    std::string sel_text = buffer_.Text().substr(s, len);
                    // In view mode, convert soft line breaks (single \n within
                    // a paragraph) to spaces, preserving paragraph breaks
                    // (\n\n) and forced breaks (two trailing spaces + \n).
                    if (!editing_) {
                        sel_text = CleanSelectionForCopy(sel_text);
                    }
                    ClipboardCopy(hwnd_content_, sel_text);
                }
            }
            break;

        case 0x56:  // Ctrl+V = paste
            if (ctrl && !shift) {
                std::string text = ClipboardPaste(hwnd_content_);
                if (!text.empty()) {
                    editor_.InsertText(text);
                    OnBufferChanged();
                }
            }
            break;
        case 0x5A:  // Ctrl+Z = undo, Ctrl+Y = redo
            if (ctrl && !shift) {
                editor_.Undo();
                OnBufferChanged();
            } else if ((ctrl && shift) || (ctrl && vk == 0x59)) {
                editor_.Redo();
                OnBufferChanged();
            }
            break;
        case VK_BACK:
            editor_.DeleteBackward();
            OnBufferChanged();
            break;
        case VK_DELETE:
            editor_.DeleteForward();
            OnBufferChanged();
            break;
        case VK_RETURN:
            editor_.InsertParagraphBreak(doc_);
            OnBufferChanged();
            break;
        default:
            DefWindowProcW(hwnd, WM_KEYDOWN, vk, lp);
            break;
    }
}

void AppWindow::LoadSampleDoc() {
    // Instead of showing a placeholder text, show the welcome screen
    // with recent files cards.
    welcome_mode_ = true;
    diaglog("LoadSampleDoc: welcome_mode_=true, recentFiles=%zu\n",
        settings_.recentFiles.size());
    if (dw_factory_) {
        welcome_.Init(dw_factory_);
        welcome_.SetRecentFiles(settings_.recentFiles);
        diaglog("LoadSampleDoc: SetRecentFiles called\n");
    } else {
        diaglog("LoadSampleDoc: dw_factory_ is NULL!\n");
    }
    if (rt_) {
        D2D1_SIZE_F sz = rt_->GetSize();
        welcome_.Layout(sz.width, sz.height);
    }
    // Clear any document content
    doc_ = Document{};
    buffer_.SetText("");
    layout_cache_.Clear();
    scrollY_ = 0.0f;
    totalH_ = 0.0f;
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::Repaint() {
    if (hwnd_content_) InvalidateRect(hwnd_content_, nullptr, FALSE);
}

void AppWindow::ForceRepaintNow() {
    if (!hwnd_content_) return;
    RedrawWindow(hwnd_content_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    UpdateCaretPosition();
}

void AppWindow::ScheduleReparse() {
    // For small documents (< 100 KB), reparse immediately.
    // For larger ones, debounce with a 150 ms timer.
    if (buffer_.Text().size() < 100 * 1024) {
        if (reparse_timer_) { KillTimer(hwnd_content_, reparse_timer_); reparse_timer_ = 0; }
        reparse_pending_ = false;
        OnReparseTimer();
        return;
    }
    if (reparse_timer_) return;  // already scheduled
    reparse_pending_ = true;
    reparse_timer_ = SetTimer(hwnd_content_, 2, 150, nullptr);
}

void AppWindow::OnReparseTimer() {
    reparse_pending_ = false;
    if (reparse_timer_) { KillTimer(hwnd_content_, reparse_timer_); reparse_timer_ = 0; }
    doc_ = Document{};
    ParseMarkdown(buffer_.Text(), doc_);
    layout_cache_.Clear();
    UpdateScrollInfo();
    Repaint();
    UpdateCaretPosition();
}

void AppWindow::OnBufferChanged() {
    MarkDirty();
    InvalidateFormatButtons();
    layout_cache_.Clear();
    ScheduleReparse();
}

void AppWindow::UpdateCaretPosition() {
    if (!has_focus_ || !hwnd_content_) return;
    if (!editing_) return;  // No caret in view mode.
    InvalidateFormatButtons();
    float x, y, h;
    if (layout_cache_.OffsetToCaretRect(sel_.active.offset, &x, &y, &h)) {
        int cx = static_cast<int>(x);
        int cy = static_cast<int>(y - scrollY_);
        // Recreate the caret if its height changed (e.g. cursor moved
        // from body text to a heading or vice versa).
        int newH = static_cast<int>(h);
        if (newH < 1) newH = 1;
        if (caret_height_ != newH) {
            DestroyCaret();
            CreateCaret(hwnd_content_, nullptr, 2, newH);
            ShowCaret(hwnd_content_);
            caret_height_ = newH;
        }
        SetCaretPos(cx, cy);
    }
}

void AppWindow::OnLButtonDown(HWND hwnd, int x, int y) {
    SetFocus(hwnd);

    // Welcome screen: clicking a card opens that file.
    if (welcome_mode_) {
        D2D1_SIZE_F sz = rt_ ? rt_->GetSize() : D2D1::SizeF(800, 600);
        welcome_.Layout(sz.width, sz.height);
        int idx = welcome_.HitTest(static_cast<float>(x), static_cast<float>(y));
        if (idx >= 0) {
            std::wstring path = welcome_.GetPath(idx);
            if (!path.empty()) {
                OpenFile(path);
            }
        }
        return;
    }

    SetCapture(hwnd);
    float docX = static_cast<float>(x);
    float docY = static_cast<float>(y) + scrollY_;

    // Check if the click is in the left margin (no text block hit at x).
    // If so, select the visual line at that y position (like Word).
    uint32_t offset = layout_cache_.PointToOffset(docX, docY);
    if (offset == UINT32_MAX) {
        // Click missed all text blocks — in the left margin.
        // Find the block at this y and select the single visual line.
        int blkIdx = layout_cache_.FindBlockAtY(docY);
        diaglog("OnLButtonDown margin: blkIdx=%d, docY=%.1f, blocks=%zu\n",
            blkIdx, docY, layout_cache_.Blocks().size());
        if (blkIdx >= 0) {
            const auto& blocks0 = layout_cache_.Blocks();
            diaglog("  block[%d]: y=%.1f h=%.1f x=%.1f w=%.1f srcOff=%u srcLen=%u\n",
                blkIdx, blocks0[blkIdx].y, blocks0[blkIdx].height,
                blocks0[blkIdx].x, blocks0[blkIdx].width,
                blocks0[blkIdx].srcOffset, blocks0[blkIdx].srcLength);
            uint32_t lineStart = 0, lineEnd = 0;
            float lineTopRel = 0.0f;
            bool gotLine = layout_cache_.GetLineRangeAtY(blkIdx, docY,
                    &lineStart, &lineEnd, &lineTopRel);
            diaglog("  GetLineRangeAtY: got=%d start=%u end=%u lineTopRel=%.1f\n",
                gotLine, lineStart, lineEnd, lineTopRel);
            if (gotLine) {
                sel_.anchor = {lineStart};
                sel_.active = {lineEnd};
            } else {
                const auto& blocks = layout_cache_.Blocks();
                const auto& bl = blocks[blkIdx];
                sel_.anchor = {bl.srcOffset};
                sel_.active = {bl.srcOffset + bl.srcLength};
                lineStart = bl.srcOffset;
                lineEnd = bl.srcOffset + bl.srcLength;
                lineTopRel = 0.0f;
            }
            margin_selecting_ = true;
            margin_anchor_block_ = blkIdx;
            margin_anchor_start_ = lineStart;
            margin_anchor_end_ = lineEnd;
            {
                const auto& blocks = layout_cache_.Blocks();
                margin_anchor_y_ = blocks[blkIdx].y + lineTopRel;
            }
            if (editing_) UpdateCaretPosition();
            Repaint();
            return;
        }
    }

    margin_selecting_ = false;
    if (offset != UINT32_MAX) {
        sel_.Collapse({offset});
    } else {
        // Click landed on empty space (no text block, no margin line).
        // Clear any active selection.
        sel_.Collapse({sel_.active.offset});
    }
    // Only update caret position in edit mode; in view mode we have no caret.
    if (editing_) UpdateCaretPosition();
    Repaint();
}

void AppWindow::OnLButtonDblClk(HWND hwnd, int x, int y) {
    SetFocus(hwnd);
    if (welcome_mode_) return;  // single-click handles welcome screen clicks
    float docX = static_cast<float>(x);
    float docY = static_cast<float>(y) + scrollY_;
    uint32_t offset = layout_cache_.PointToOffset(docX, docY);
    if (offset == UINT32_MAX) return;

    const std::string& text = buffer_.Text();
    if (offset >= text.size()) return;

    uint32_t start = offset;
    while (start > 0 && !isspace(static_cast<unsigned char>(text[start - 1])))
        start--;
    uint32_t end = offset;
    while (end < text.size() && !isspace(static_cast<unsigned char>(text[end])))
        end++;

    sel_.anchor = {start};
    sel_.active = {end};
    if (editing_) UpdateCaretPosition();
    Repaint();
}

void AppWindow::OnMouseMove(HWND hwnd, int x, int y) {
    // Welcome screen: track hover for card highlight.
    if (welcome_mode_) {
        D2D1_SIZE_F sz = rt_ ? rt_->GetSize() : D2D1::SizeF(800, 600);
        welcome_.Layout(sz.width, sz.height);
        int newHover = welcome_.HitTest(static_cast<float>(x),
            static_cast<float>(y));
        if (newHover != welcome_hover_) {
            welcome_hover_ = newHover;
            Repaint();
        }
        return;
    }

    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
    float docX = static_cast<float>(x);
    float docY = static_cast<float>(y) + scrollY_;

    if (margin_selecting_ && margin_anchor_block_ >= 0) {
        // Extending a margin selection by visual lines.
        // Find the block and visual line at the current y position.
        int blkIdx = layout_cache_.FindBlockAtY(docY);
        if (blkIdx >= 0) {
            uint32_t curStart = 0, curEnd = 0;
            bool gotLine = layout_cache_.GetLineRangeAtY(
                blkIdx, docY, &curStart, &curEnd);
            if (!gotLine) {
                const auto& blocks = layout_cache_.Blocks();
                curStart = blocks[blkIdx].srcOffset;
                curEnd = blocks[blkIdx].srcOffset + blocks[blkIdx].srcLength;
            }

            // Determine drag direction by comparing current y with anchor y.
            const auto& blocks = layout_cache_.Blocks();
            bool draggingDown;
            if (blkIdx > margin_anchor_block_) {
                draggingDown = true;
            } else if (blkIdx < margin_anchor_block_) {
                draggingDown = false;
            } else {
                // Same block: compare current docY with anchor line's y.
                draggingDown = (docY >= margin_anchor_y_);
            }

            if (draggingDown) {
                // anchor = start of anchor line, active = end of current line
                sel_.anchor = {margin_anchor_start_};
                sel_.active = {curEnd};
            } else {
                // anchor = end of anchor line, active = start of current line
                sel_.anchor = {margin_anchor_end_};
                sel_.active = {curStart};
            }
        }
        if (editing_) UpdateCaretPosition();
        Repaint();
        return;
    }

    uint32_t offset = layout_cache_.PointToOffset(docX, docY);
    if (offset != UINT32_MAX) {
        sel_.active = {offset};
    }
    if (editing_) UpdateCaretPosition();
    Repaint();
}

void AppWindow::OnLButtonUp(HWND hwnd) {
    ReleaseCapture();
    margin_selecting_ = false;
    margin_anchor_block_ = -1;
}

void AppWindow::OnSetFocus(HWND hwnd) {
    has_focus_ = true;
    // Only create and show caret in edit mode.
    if (!editing_) return;
    float x, y, h;
    if (layout_cache_.OffsetToCaretRect(sel_.active.offset, &x, &y, &h)) {
        CreateCaret(hwnd, nullptr, 2, static_cast<int>(h));
        caret_height_ = static_cast<int>(h);
        SetCaretPos(static_cast<int>(x),
                    static_cast<int>(y - scrollY_));
        ShowCaret(hwnd);
        caret_visible_ = true;
    }
}

void AppWindow::OnKillFocus(HWND hwnd) {
    has_focus_ = false;
    DestroyCaret();
}

void AppWindow::OnChar(HWND hwnd, wchar_t ch) {
    if (!has_focus_) return;

    // In view mode, no character input is accepted.
    // Edit mode is entered explicitly via the Edit button or Ctrl+E.
    if (!editing_) return;

    // Handle surrogate pairs: emoji and CJK arrive as two WM_CHAR messages.
    if (ch >= 0xD800 && ch <= 0xDBFF) {
        // High surrogate: buffer it and wait for the low.
        surrogate_buf_ = ch;
        has_surrogate_ = true;
        return;
    }
    if (has_surrogate_ && ch >= 0xDC00 && ch <= 0xDFFF) {
        // Low surrogate: combine with the buffered high.
        char32_t cp = 0x10000 + ((surrogate_buf_ - 0xD800) << 10) + (ch - 0xDC00);
        char utf8[5] = {};
        if (cp <= 0x7F) { utf8[0] = (char)cp; }
        else if (cp <= 0x7FF) {
            utf8[0] = 0xC0 | (cp >> 6);
            utf8[1] = 0x80 | (cp & 0x3F);
        } else if (cp <= 0xFFFF) {
            utf8[0] = 0xE0 | (cp >> 12);
            utf8[1] = 0x80 | ((cp >> 6) & 0x3F);
            utf8[2] = 0x80 | (cp & 0x3F);
        } else {
            utf8[0] = 0xF0 | (cp >> 18);
            utf8[1] = 0x80 | ((cp >> 12) & 0x3F);
            utf8[2] = 0x80 | ((cp >> 6) & 0x3F);
            utf8[3] = 0x80 | (cp & 0x3F);
        }
        has_surrogate_ = false;
        std::string ins(utf8);
        ins = EscapeForInsert(buffer_, sel_.active.offset, ins);
        editor_.InsertText(ins);
        OnBufferChanged();
        return;
    }
    has_surrogate_ = false;

    // Filter control characters below 0x20 except tab.
    if (ch < 0x20 && ch != '	') return;

    // Convert UTF-16 code unit to UTF-8.
    char32_t cp = static_cast<char32_t>(ch);
    char utf8[5] = {};
    if (cp <= 0x7F) {
        utf8[0] = (char)cp;
    } else if (cp <= 0x7FF) {
        utf8[0] = 0xC0 | (cp >> 6);
        utf8[1] = 0x80 | (cp & 0x3F);
    } else {
        utf8[0] = 0xE0 | (cp >> 12);
        utf8[1] = 0x80 | ((cp >> 6) & 0x3F);
        utf8[2] = 0x80 | (cp & 0x3F);
    }
    std::string ins2(utf8);
    // Escape markdown metacharacters in the typed text.
    ins2 = EscapeForInsert(buffer_, sel_.active.offset, ins2);
    editor_.InsertText(ins2);
    if (!ins2.empty() && !ins2.empty()) CheckAutoformat(&buffer_, &sel_, ins2[0]);
    OnBufferChanged();
}

void AppWindow::OpenFile(const std::wstring& path) {
    // Exit edit mode: destroy caret, invalidate ribbon state.
    if (editing_) {
        SetEdit(false);
    }
    welcome_mode_ = false;  // leaving welcome screen

    // Track this file in the recent list.
    AddRecentFile(settings_, path);
    SaveSettings(settings_);
    diaglog("OpenFile: added recent file, total=%zu\n", settings_.recentFiles.size());

    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open()) {
        MessageBoxW(hwnd_, L"Could not open file", L"MarkDownIt", MB_ICONWARNING);
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string raw = ss.str();

    // Detect BOM (UTF-8 BOM: EF BB BF)
    has_bom_ = (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF);
    std::string utf8 = has_bom_ ? raw.substr(3) : raw;

    // Detect line endings: check for CR LF (0x0D 0x0A)
    use_crlf_ = (utf8.find("\x0D\x0A") != std::string::npos);

    doc_ = Document{};
    ParseMarkdown(utf8, doc_);
    buffer_.SetText(utf8);
    undo_stack_.Clear();
    file_path_ = path;
    ClearDirty();
    scrollY_ = 0.0f;
    StopScrollAnimation();
    totalH_ = 0.0f;
    sel_.Collapse({0});
    layout_cache_.Clear();

    std::wstring title = L"MarkDownIt";
    size_t slash = path.find_last_of(L"\\/");
    std::wstring base = (slash != std::wstring::npos)
        ? path.substr(slash + 1) : path;
    if (!base.empty()) title = L"MarkDownIt - " + base;
    SetWindowTextW(hwnd_, title.c_str());

    UpdateScrollInfo();
    // Force render target recreation — the D2D hwnd target can become
    // invalid after the GetOpenFileNameW modal dialog closes.
    SafeRelease(rt_);
    RedrawWindow(hwnd_content_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);

    watcher_.Start(hwnd_, path);
}

void AppWindow::Reload() {
    if (file_path_.empty()) return;
    std::ifstream f(file_path_.c_str(), std::ios::binary);
    if (!f.is_open()) return;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string utf8 = ss.str();

    float savedY = scrollY_;
    doc_ = Document{};
    ParseMarkdown(utf8, doc_);
    buffer_.SetText(utf8);
    undo_stack_.Clear();
    UpdateScrollInfo();
    if (scrollY_ > savedY) scrollY_ = savedY;
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::OnReload() {
    if (dirty_) {
        // Buffer has unsaved changes. Prompt the user.
        int result = MessageBoxW(hwnd_,
            L"The file has been changed on disk. Keep your changes, "
            L"or reload the file from disk?",
            L"MarkDownIt - File Changed",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
        if (result != IDNO) return;  // IDYES = keep mine
    }

    // Save caret position so we can restore it after reload.
    uint32_t savedOffset = sel_.active.offset;

    Reload();

    // Restore caret, clamped to new text length.
    uint32_t textLen = static_cast<uint32_t>(buffer_.Text().size());
    if (savedOffset > textLen) savedOffset = textLen;
    sel_.Collapse({savedOffset});
    UpdateCaretPosition();
    Repaint();
}

void AppWindow::OnDropFiles(HWND hwnd, HDROP hDrop) {
    wchar_t path[MAX_PATH] = {};
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    if (count > 0) {
        DragQueryFileW(hDrop, 0, path, MAX_PATH);
        OpenFile(path);
    }
    DragFinish(hDrop);
}

void AppWindow::OnCreate(HWND hwnd) {
    hwnd_ = hwnd;

    // Load settings FIRST — LoadSampleDoc (below) needs recentFiles.
    settings_ = LoadSettings();

    D2D1_FACTORY_OPTIONS opts = {};
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, opts, &d2d_factory_);
    if (FAILED(hr) || !d2d_factory_) {
        MessageBoxW(hwnd, L"D2D1CreateFactory failed", L"MarkDownIt", MB_ICONERROR);
        return;
    }

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&dw_factory_));
    if (FAILED(hr) || !dw_factory_) {
        MessageBoxW(hwnd, L"DWriteCreateFactory failed", L"MarkDownIt", MB_ICONERROR);
        return;
    }

    // Create the content child window. The D2D render target will be
    // created on this child, NOT on the main window. This prevents
    // the D2D Present() from overwriting the ribbon's rendering.
    hwnd_content_ = CreateWindowExW(
        0, kContentClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS,
        0, 0, 0, 0,
        hwnd, nullptr, hinst_, this);
    if (!hwnd_content_) {
        MessageBoxW(hwnd, L"CreateWindow (content) failed", L"MarkDownIt", MB_ICONERROR);
        return;
    }

    // Create D2D render target on the child window.
    RECT rc;
    GetClientRect(hwnd_content_, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(
        (rc.right - rc.left) > 0 ? (rc.right - rc.left) : 1,
        (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : 1);
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.pixelFormat = D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    hr = d2d_factory_->CreateHwndRenderTarget(
        rtProps, D2D1::HwndRenderTargetProperties(hwnd_content_, size), &rt_);
    if (FAILED(hr)) {
        MessageBoxW(hwnd, L"CreateHwndRenderTarget failed", L"MarkDownIt", MB_ICONERROR);
        return;
    }

    EnsureRenderer();
    if (pending_file_.empty()) {
        LoadSampleDoc();
    }
    // else: a file will be opened via timer 3 from OpenPendingFile.
    // Show a blank document until then (no flash of sample text).
    InitEditor();
    UpdateScrollInfo();
}

void AppWindow::UpdateScrollInfo() {
    if (!hwnd_content_) return;

    float clientHDip = static_cast<float>(clientH_);
    if (rt_) {
        D2D1_SIZE_F rtSize = rt_->GetSize();
        clientHDip = rtSize.height;
    }

    float maxScroll = 0.0f;
    if (totalH_ > clientHDip) {
        maxScroll = totalH_ - clientHDip;
    }

    if (scrollY_ < 0.0f) scrollY_ = 0.0f;
    if (scrollY_ > maxScroll) scrollY_ = maxScroll;

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = static_cast<int>(totalH_);
    si.nPage  = static_cast<UINT>(clientHDip > 0 ? clientHDip : 1);
    si.nPos   = static_cast<int>(scrollY_);
    SetScrollInfo(hwnd_content_, SB_VERT, &si, TRUE);
}

void AppWindow::ResizeContentWindow() {
    if (!hwnd_ || !hwnd_content_) return;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    int contentY = static_cast<int>(g_ribbonHeight);
    int contentH = rc.bottom - contentY;
    if (contentH < 1) contentH = 1;

    SetWindowPos(hwnd_content_, nullptr,
        0, contentY,
        rc.right - rc.left, contentH,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void AppWindow::OnSize(HWND hwnd, int width, int height) {
    ResizeContentWindow();
    // Debounce-save window placement so bounds persist even if the app
    // crashes or is killed (not just on clean shutdown).
    SetTimer(hwnd_, 5, 500, nullptr);
}

void AppWindow::OnContentSize(HWND hwnd, int width, int height) {
    clientW_ = width;
    clientH_ = height;
    if (rt_) {
        D2D1_SIZE_U size = D2D1::SizeU(
            width > 0 ? width : 1, height > 0 ? height : 1);
        HRESULT hr = rt_->Resize(size);
        if (FAILED(hr) || hr == D2DERR_RECREATE_TARGET) {
            RecreateRenderTarget();
        }
    } else {
        RecreateRenderTarget();
    }
    UpdateScrollInfo();
    // Force immediate repaint (not deferred). This ensures the newly
    // exposed area gets painted right away during a resize drag.
    RedrawWindow(hwnd, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
}


// Clamp a scroll position to valid range [0, maxScroll].
float AppWindow::ClampScroll(float y) const {
    float clientHDip = static_cast<float>(clientH_);
    if (rt_) clientHDip = rt_->GetSize().height;
    float maxScroll = 0.0f;
    if (totalH_ > clientHDip) maxScroll = totalH_ - clientHDip;
    if (y < 0.0f) y = 0.0f;
    if (y > maxScroll) y = maxScroll;
    return y;
}

// ── Smooth scroll physics engine ─────────────────────────────────
//
// Three phases, all driven by a single 16 ms timer (ID 4):
//
// 1. SPRING – mouse wheel, arrow keys, scrollbar line/page clicks.
//    spring_target_ is the rest position; each tick applies:
//       force   = (target − pos) × stiffness
//       vel    += force − damping × vel
//       pos    += vel
//    Critically damped → smooth ease-out, multi-notch accumulation.
//
// 2. TRACKPAD – precision touchpad / high-frequency wheel.
//    Each WM_MOUSEWHEEL event applies its delta immediately (1:1)
//    and records an exponential moving average of recent velocity.
//    The timer runs but does nothing while events arrive.
//    When events stop for >80 ms, → MOMENTUM.
//
// 3. MOMENTUM – fingers lifted, scroll glides with friction.
//    pos += vel; vel *= friction (0.955 per 16 ms tick).
//    Stops when |vel| < 0.5 or hit an edge.
//
// SB_THUMBTRACK (scrollbar drag) bypasses all physics — instant jump.
// ─────────────────────────────────────────────────────────────────

// Spring constants (tuned for ~300 ms settle with critical damping).
static const float SPRING_STIFFNESS = 0.12f;  // per 16ms tick (stiffer = faster settle)
static const float SPRING_DAMPING   = 0.45f;   // per 16ms tick (higher = less oscillation)
static const float SPRING_SETTLE    = 0.5f;    // stop when within 0.5px of target
static const float SPRING_MIN_VEL   = 0.3f;    // stop when velocity below this

// Mouse wheel: pixels per notch (WHEEL_DELTA = 120).
static const float WHEEL_STEP_PX = 120.0f;

// Trackpad: scale raw delta to screen DIPs.
static const float TRACKPAD_SCALE = 0.75f;

// Momentum friction: vel *= FRICTION each 16ms tick.
// 0.955 ≈ 3.5% velocity loss per tick ≈ ~2 s glide from 20 px/tick.
static const float MOMENTUM_FRICTION = 0.955f;
static const float MOMENTUM_MIN_VEL  = 0.5f;     // stop threshold (px/tick)

void AppWindow::EnsureScrollTimer() {
    if (!scroll_timer_) {
        scroll_timer_ = SetTimer(hwnd_content_, 4, 16, nullptr);
    }
}

void AppWindow::StopScrollTimer() {
    if (scroll_timer_) {
        KillTimer(hwnd_content_, scroll_timer_);
        scroll_timer_ = 0;
    }
    scroll_phase_ = 0;  // IDLE
    momentum_vel_ = 0.0f;
}

// Full stop (also called from OpenFile, OnDestroy).
void AppWindow::StopScrollAnimation() {
    StopScrollTimer();
    spring_target_ = scrollY_;
}

// Start a spring animation toward targetY (mouse wheel, keyboard, scrollbar).
void AppWindow::StartSpring(float targetY) {
    spring_target_ = ClampScroll(targetY);
    float diff = std::fabs(spring_target_ - scrollY_);
    if (diff < SPRING_SETTLE) {
        scrollY_ = spring_target_;
        StopScrollTimer();
        UpdateScrollInfo();
        Repaint();
        return;
    }
    // If coming from momentum (trackpad release), preserve the velocity
    // as initial spring velocity for a seamless hand-off.
    if (scroll_phase_ == 3 && std::fabs(momentum_vel_) > 1.0f) {
        // Already moving; spring will absorb it.
    } else {
        momentum_vel_ = 0.0f;
    }
    scroll_phase_ = 1;  // SPRING
    EnsureScrollTimer();
}

// Trackpad scroll: apply delta immediately (1:1) and track velocity.
void AppWindow::BeginTrackpadScroll(float delta, DWORD now) {
    // Cancel any spring/momentum — trackpad takes over.
    scroll_phase_ = 2;  // TRACKPAD
    spring_target_ = scrollY_;  // no spring while trackpad is active

    // Apply delta immediately (1:1, no animation latency).
    float prev = scrollY_;
    scrollY_ = ClampScroll(scrollY_ + delta);
    float actual = scrollY_ - prev;

    // Track velocity as exponential moving average of recent deltas.
    // Use the actual (clamped) delta so hitting an edge zeroes velocity.
    // If same direction, blend smoothly; if reversed, snap.
    if ((momentum_vel_ > 0) != (delta > 0)) {
        momentum_vel_ = actual;  // direction reversed → snap
    } else {
        momentum_vel_ = momentum_vel_ * 0.6f + actual * 0.4f;  // EMA
    }
    last_trackpad_delta_ = delta;

    // Start the timer so it can detect when trackpad events stop
    // (→ transition to momentum when idle for >80ms).
    EnsureScrollTimer();

    UpdateScrollInfo();
    Repaint();
}

// Transition from TRACKPAD idle to MOMENTUM (fingers lifted after flick).
void AppWindow::EnterMomentum() {
    if (std::fabs(momentum_vel_) < MOMENTUM_MIN_VEL) {
        scroll_phase_ = 0;  // IDLE
        StopScrollTimer();
        return;
    }
    scroll_phase_ = 3;  // MOMENTUM
    EnsureScrollTimer();
}

// Called every 16 ms by the scroll timer.
void AppWindow::OnScrollTick() {
    if (scroll_phase_ == 1) {
        // ── SPRING ──────────────────────────────────────────────
        // Semi-implicit Euler: update velocity first, then position.
        float diff = spring_target_ - scrollY_;
        float force = diff * SPRING_STIFFNESS;
        momentum_vel_ += force;
        momentum_vel_ *= (1.0f - SPRING_DAMPING);
        scrollY_ += momentum_vel_;
        UpdateScrollInfo();
        Repaint();
        if (editing_) UpdateCaretPosition();

        if (std::fabs(momentum_vel_) < SPRING_MIN_VEL && std::fabs(diff) < SPRING_SETTLE) {
            scrollY_ = spring_target_;
            StopScrollTimer();
            UpdateScrollInfo();
            Repaint();
        }

    } else if (scroll_phase_ == 2) {
        // ── TRACKPAD idle detection ────────────────────────────
        // Timer runs during trackpad input to detect when events stop.
        // If no wheel event for >80ms, fingers were lifted → momentum.
        DWORD now = GetTickCount();
        if (now - last_wheel_time_ > 80) {
            EnterMomentum();
        }

    } else if (scroll_phase_ == 3) {
        // ── MOMENTUM ───────────────────────────────────────────
        scrollY_ += momentum_vel_;
        momentum_vel_ *= MOMENTUM_FRICTION;

        // Clamp at edges — stop dead (no bounce).
        float prev = scrollY_;
        scrollY_ = ClampScroll(scrollY_);
        if (scrollY_ != prev) {
            momentum_vel_ = 0.0f;  // hit edge
        }

        UpdateScrollInfo();
        Repaint();
        if (editing_) UpdateCaretPosition();

        if (std::fabs(momentum_vel_) < MOMENTUM_MIN_VEL) {
            StopScrollTimer();
        }
    }
}

// ── Scrollbar / keyboard ──────────────────────────────────────────
void AppWindow::OnContentVScroll(HWND hwnd, int code, int pos) {
    float page = static_cast<float>(clientH_ > 0 ? clientH_ : 1);
    if (rt_) {
        D2D1_SIZE_F rtSize = rt_->GetSize();
        page = rtSize.height;
    }

    float targetY = scrollY_;
    switch (code) {
        case SB_LINEUP:        targetY = scrollY_ - 30.0f; break;
        case SB_LINEDOWN:      targetY = scrollY_ + 30.0f; break;
        case SB_PAGEUP:        targetY = scrollY_ - page;  break;
        case SB_PAGEDOWN:      targetY = scrollY_ + page;  break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            // Thumb drag: instant jump, no physics.
            StopScrollTimer();
            scrollY_ = static_cast<float>(si.nTrackPos);
            spring_target_ = scrollY_;
            UpdateScrollInfo();
            Repaint();
            UpdateCaretPosition();
            return;
        }
        case SB_TOP:     targetY = 0.0f; break;
        case SB_BOTTOM:  targetY = totalH_; break;
    }

    StartSpring(targetY);
}

// ── Mouse wheel / trackpad ────────────────────────────────────────
void AppWindow::OnContentMouseWheel(HWND hwnd, int delta) {
    DWORD now = GetTickCount();
    last_wheel_time_ = now;

    // Distinguish trackpad (small deltas) from mouse wheel (WHEEL_DELTA=120).
    // A precision trackpad sends small deltas (1-30) in rapid succession.
    // Only use delta magnitude — timing alone catches fast mouse scrolling.
    bool isTrackpad = (std::abs(delta) < WHEEL_DELTA);
    is_trackpad_ = isTrackpad;

    if (isTrackpad) {
        // ── Trackpad: 1:1 direct follow with velocity tracking ──
        float deltaScroll = -static_cast<float>(delta) * TRACKPAD_SCALE;
        BeginTrackpadScroll(deltaScroll, now);
        return;
    }

    // ── Mouse wheel: ratchet + spring ─────────────────────────
    // If we were in momentum (fingers previously lifted), cancel it.
    if (scroll_phase_ == 3) {
        momentum_vel_ = 0.0f;
        scroll_phase_ = 0;
    }

    float deltaScroll = -static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA) * WHEEL_STEP_PX;

    // Accumulate into the spring target — rapid notches build up speed.
    if (scroll_phase_ == 1) {
        spring_target_ = ClampScroll(spring_target_ + deltaScroll);
    } else {
        StartSpring(scrollY_ + deltaScroll);
    }
}

void AppWindow::RecreateRenderTarget() {
    SafeRelease(rt_);
    if (d2d_factory_ && hwnd_content_) {
        RECT rc;
        GetClientRect(hwnd_content_, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(
            (rc.right - rc.left) > 0 ? (rc.right - rc.left) : 1,
            (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : 1);
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
        rtProps.pixelFormat = D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        d2d_factory_->CreateHwndRenderTarget(
            rtProps, D2D1::HwndRenderTargetProperties(hwnd_content_, size), &rt_);
    }
}

void AppWindow::OnContentPaint(HWND hwnd) {
    if (!rt_) {
            RecreateRenderTarget();
        if (!rt_) { ValidateRect(hwnd, nullptr); return; }
    }

    // Welcome screen: no document to measure/render.
    if (welcome_mode_) {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        rt_->BeginDraw();
        D2D1_SIZE_F size = rt_->GetSize();
        rt_->Clear(D2D1::ColorF(D2D1::ColorF::White));
        welcome_.Init(dw_factory_);
        welcome_.Layout(size.width, size.height);
        welcome_.Render(rt_, size.width, size.height, welcome_hover_);
        static int welcomePaintCount = 0;
        if (welcomePaintCount < 3) {
            diaglog("OnContentPaint: welcome mode render, cards=%d\n",
                welcome_.GetCardCount());
            welcomePaintCount++;
        }
        HRESULT hr = rt_->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            RecreateRenderTarget();
            RedrawWindow(hwnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
        }
        EndPaint(hwnd, &ps);
        return;
    }

    if (renderer_inited_ && dw_factory_) {
        D2D1_SIZE_F size = rt_->GetSize();
        totalH_ = renderer_.Measure(dw_factory_, doc_, size.width, 0.0f);
        UpdateScrollInfo();
    }

    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(D2D1::ColorF::White));

    if (renderer_inited_ && dw_factory_) {
        D2D1_SIZE_F size = rt_->GetSize();
        renderer_.Render(rt_, dw_factory_, doc_, size.width, scrollY_, 0.0f, &sel_);
    }

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        RecreateRenderTarget();
        // Force immediate repaint after target recreation.
        RedrawWindow(hwnd, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    }
    EndPaint(hwnd, &ps);
    UpdateCaretPosition();
}


void AppWindow::ProcessPendingFile() {
    if (pending_file_.empty()) return;
    std::wstring path;
    path.swap(pending_file_);
    OpenFile(path);
}

void AppWindow::MarkDirty() {
    if (!dirty_) {
        dirty_ = true;
        UpdateTitleBar();
    }
}

void AppWindow::ClearDirty() {
    if (dirty_) {
        dirty_ = false;
        UpdateTitleBar();
    }
}

void AppWindow::UpdateTitleBar() {
    std::wstring title = L"MarkDownIt";
    if (!file_path_.empty()) {
        size_t slash = file_path_.find_last_of(L"\\/");
        std::wstring base = (slash != std::wstring::npos)
            ? file_path_.substr(slash + 1) : file_path_;
        title = L"MarkDownIt - " + base;
    }
    if (dirty_) title = L"*" + title;
    SetWindowTextW(hwnd_, title.c_str());
}

bool AppWindow::DoSave(const std::wstring& path) {
    // Get the buffer content as UTF-8.
    std::string content = buffer_.Text();

    // Detect and strip BOM if present on load, add it back on save.
    // Determine line endings.
    std::string out;
    if (has_bom_) {
        out += (char)0xEF;
        out += (char)0xBB;
        out += (char)0xBF;
    }
    if (use_crlf_) {
        for (size_t i = 0; i < content.size(); i++) {
            if (content[i] == 0x0A && (i == 0 || content[i - 1] != 0x0D))
                { out += (char)0x0D; out += (char)0x0A; }
            else if (content[i] != 0x0D)
                out += content[i];
        }
    } else {
        out = content;
    }

    // Atomic save: write to temp file in same directory, then rename.
    std::wstring tempPath = path + L".mdtmp";

    // Suspend the file watcher during save so our own write doesn't trigger reload.
    watcher_.Stop();

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, tempPath.c_str(), L"wb") != 0 || !fp) {
        watcher_.Start(hwnd_, file_path_);
        return false;
    }
    fwrite(out.data(), 1, out.size(), fp);
    fflush(fp);
    fclose(fp);

    // MoveFileEx with MOVEFILE_REPLACE_EXISTING for atomic replace.
    BOOL ok = MoveFileExW(tempPath.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        // Try to delete the temp file.
        DeleteFileW(tempPath.c_str());
        watcher_.Start(hwnd_, file_path_);
        return false;
    }

    // Restart watcher.
    if (!file_path_.empty()) watcher_.Start(hwnd_, file_path_);

    ClearDirty();
    return true;
}

bool AppWindow::Save() {
    if (file_path_.empty()) return SaveAs();
    return DoSave(file_path_);
}

bool AppWindow::SaveAs() {
    std::wstring path = SaveDialog();
    if (path.empty()) return false;
    file_path_ = path;
    UpdateTitleBar();
    bool ok = DoSave(path);
    if (ok) watcher_.Start(hwnd_, file_path_);
    return ok;
}

std::wstring AppWindow::SaveDialog() {
    wchar_t szFile[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Markdown (*.md)\0*.md\0All Files (*.*)\0*.*\0";
    ofn.lpstrDefExt = L"md";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (file_path_.empty()) {
        ofn.lpstrFileTitle = nullptr;
    } else {
        // Pre-fill with current path.
        wcscpy_s(szFile, MAX_PATH, file_path_.c_str());
    }
    if (GetSaveFileNameW(&ofn)) return szFile;
    return {};
}

int AppWindow::PromptSaveDiscardCancel() {
    return MessageBoxW(hwnd_,
        L"The document has unsaved changes. Save before closing?",
        L"MarkDownIt", MB_YESNOCANCEL | MB_ICONQUESTION);
}

void AppWindow::OnClose() {
    if (dirty_) {
        int result = PromptSaveDiscardCancel();
        if (result == IDCANCEL) return;
        if (result == IDYES) {
            if (!Save()) return;  // save failed or cancelled, don't close
        }
        // IDNO: discard, proceed to close
    }
    DestroyWindow(hwnd_);
}

void AppWindow::SetEdit(bool on) {
    editing_ = on;
    if (on && has_focus_ && !caret_visible_) {
        float cx, cy, ch;
        if (layout_cache_.OffsetToCaretRect(sel_.active.offset, &cx, &cy, &ch)) {
            CreateCaret(hwnd_content_, nullptr, 2, static_cast<int>(ch));
            caret_height_ = static_cast<int>(ch);
        } else {
            CreateCaret(hwnd_content_, nullptr, 2, 16);
            caret_height_ = 16;
        }
        ShowCaret(hwnd_content_);
        caret_visible_ = true;
    }
    if (!on && caret_visible_) {
        DestroyCaret();
        caret_visible_ = false;
        caret_height_ = 0;
    }
    // Invalidate the Edit toggle and all format buttons so the ribbon
    // re-queries their pressed and enabled state.
    if (g_pRibbonFramework) {
        g_pRibbonFramework->InvalidateUICommand(IDC_CMD_EDIT,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_BooleanValue);
        static const UINT toggleCmds[] = {
            IDC_CMD_BOLD, IDC_CMD_ITALIC, IDC_CMD_CODE, IDC_CMD_STRIKE,
            IDC_CMD_H1, IDC_CMD_H2, IDC_CMD_H3,
            IDC_CMD_BULLETS, IDC_CMD_NUMBERING, IDC_CMD_QUOTE,
            IDC_CMD_LINK, IDC_CMD_CLEARFORMAT,
            IDC_CMD_INDENT, IDC_CMD_OUTDENT
        };
        static const UINT enableOnlyCmds[] = {
            IDC_CMD_UNDO, IDC_CMD_REDO
        };
        for (auto cmd : toggleCmds) {
            g_pRibbonFramework->InvalidateUICommand(cmd,
                UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
            g_pRibbonFramework->InvalidateUICommand(cmd,
                UI_INVALIDATIONS_PROPERTY, &UI_PKEY_BooleanValue);
        }
        for (auto cmd : enableOnlyCmds) {
            g_pRibbonFramework->InvalidateUICommand(cmd,
                UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
        }
    }
    Repaint();
}

FormatState AppWindow::GetFormatState() const {
    FormatState fs;
    uint32_t offset = sel_.active.offset;
    const auto& text = buffer_.Text();
    if (offset > text.size()) offset = static_cast<uint32_t>(text.size());

    // Find the block containing this offset.
    for (const auto& node : doc_.nodes) {
        uint32_t blockStart = node.srcOffset;
        uint32_t blockEnd = blockStart + node.srcLength;
        if (offset < blockStart || offset > blockEnd) continue;

        // Block-level state.
        if (node.block == BlockKind::Heading) {
            fs.headingLevel = node.level;
        } else if (node.block == BlockKind::List) {
            if (node.ordered) fs.inNumbering = true;
            else fs.inBullets = true;
        } else if (node.block == BlockKind::BlockQuote) {
            fs.inQuote = true;
        }

        // Inline-level state: find the InlineBlock containing the offset.
        // The caret sits between two characters. We check the span that
        // contains the character just before the caret (if any), since
        // that is the formatting context the caret is "inside".
        uint32_t checkOffset = offset;
        if (checkOffset > blockStart && checkOffset > 0) checkOffset--;

        for (const auto& child : node.children) {
            uint32_t cs = child.srcOffset;
            uint32_t ce = cs + child.srcLength;
            if (checkOffset >= cs && checkOffset < ce) {
                if (child.strong) fs.bold = true;
                if (child.em)     fs.italic = true;
                if (child.code)   fs.code = true;
                if (child.strike) fs.strike = true;
                break;
            }
        }
        break;
    }
    return fs;
}

void AppWindow::InvalidateFormatButtons() {
    FormatState fs = GetFormatState();
    UpdateRibbonFormatState(fs);
}

void AppWindow::ToggleBold() {
    if (!editing_) return;
    ToggleInlineMarker(&buffer_, &sel_, "**");
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
}

void AppWindow::ToggleItalic() {
    if (!editing_) return;
    ToggleInlineMarker(&buffer_, &sel_, "*");
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
}

void AppWindow::ToggleStrike() {
    if (!editing_) return;
    ToggleInlineMarker(&buffer_, &sel_, "~~");
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
}

void AppWindow::ToggleCode() {
    if (!editing_) return;
    ToggleInlineMarker(&buffer_, &sel_, "`");
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
}

// Build an in-memory dialog template for the Insert Link dialog.
// This avoids needing a .rc resource file.
static std::vector<BYTE> BuildLinkDialogTemplate() {
    // Align helper: pad to 4-byte boundary.
    auto align = [](std::vector<BYTE>& buf) {
        while (buf.size() % 4 != 0) buf.push_back(0);
    };
    auto pushStr = [](std::vector<BYTE>& buf, const wchar_t* s) {
        while (*s) {
            buf.push_back(static_cast<BYTE>(*s & 0xFF));
            buf.push_back(static_cast<BYTE>(*s >> 8));
            s++;
        }
        buf.push_back(0); buf.push_back(0);  // null terminator
    };

    std::vector<BYTE> buf;
    // DLGTEMPLATE
    DWORD style = WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU
                | DS_MODALFRAME | DS_SETFONT;
    pushStr(buf, L"Segoe UI");  // menu (none... actually this is menu field)
    // Wait, DLGTEMPLATE order is: style, exStyle, cdit, x, y, cx, cy
    // Then: menu, class, title, (if DS_SETFONT: fontSize, font)
    // Let me rebuild properly.
    buf.clear();

    // DLGTEMPLATE fields (6 dwords):
    // style
    buf.push_back(style & 0xFF); buf.push_back((style>>8) & 0xFF);
    buf.push_back((style>>16) & 0xFF); buf.push_back((style>>24) & 0xFF);
    // exStyle
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0);
    // cdit = 4 (label + edit + OK + Cancel buttons)
    buf.push_back(4); buf.push_back(0); buf.push_back(0); buf.push_back(0);
    // x, y, cx, cy (in dialog units)
    // cx=200, cy=80
    WORD cx = 200, cy = 80;
    buf.push_back(10); buf.push_back(0);   // x
    buf.push_back(10); buf.push_back(0);   // y
    buf.push_back(cx & 0xFF); buf.push_back(cx >> 8);   // cx
    buf.push_back(cy & 0xFF); buf.push_back(cy >> 8);   // cy

    // menu: none (empty string)
    buf.push_back(0); buf.push_back(0);
    // class: none (empty string = default)
    buf.push_back(0); buf.push_back(0);
    // title
    pushStr(buf, L"Insert Link");
    // font (since DS_SETFONT): fontSize (WORD) + typeface (WSTR)
    WORD fontSize = 9;
    buf.push_back(fontSize & 0xFF); buf.push_back(fontSize >> 8);
    pushStr(buf, L"Segoe UI");

    align(buf);

    // --- Item 1: static text label ---
    // DLGITEMTEMPLATE: style, exStyle, x, y, cx, cy, id
    DWORD labStyle = WS_CHILD | WS_VISIBLE | SS_LEFT;
    buf.push_back(labStyle & 0xFF); buf.push_back((labStyle>>8)&0xFF);
    buf.push_back((labStyle>>16)&0xFF); buf.push_back((labStyle>>24)&0xFF);
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0); // exStyle
    buf.push_back(10); buf.push_back(0);  // x
    buf.push_back(10); buf.push_back(0);  // y
    buf.push_back(180); buf.push_back(0); // cx
    buf.push_back(12); buf.push_back(0);  // cy
    buf.push_back(1000); buf.push_back(0); // id = 1000

    // class: static
    buf.push_back(0x0082); buf.push_back(0);  // atom for STATIC
    // title
    pushStr(buf, L"Link URL:");
    // creation data: none
    buf.push_back(0); buf.push_back(0);

    align(buf);

    // --- Item 2: edit control ---
    DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP
                    | ES_AUTOHSCROLL;
    buf.push_back(editStyle & 0xFF); buf.push_back((editStyle>>8)&0xFF);
    buf.push_back((editStyle>>16)&0xFF); buf.push_back((editStyle>>24)&0xFF);
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0); // exStyle
    buf.push_back(10); buf.push_back(0);  // x
    buf.push_back(25); buf.push_back(0);  // y
    buf.push_back(180); buf.push_back(0); // cx
    buf.push_back(14); buf.push_back(0);  // cy
    buf.push_back(1001); buf.push_back(0); // id = 1001

    // class: edit
    buf.push_back(0x0081); buf.push_back(0);  // atom for EDIT
    // title: empty
    buf.push_back(0); buf.push_back(0);
    // creation data: none
    buf.push_back(0); buf.push_back(0);

    align(buf);

    // --- Item 3: OK button ---
    DWORD btnStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP
                   | BS_PUSHBUTTON | BS_DEFPUSHBUTTON;
    buf.push_back(btnStyle & 0xFF); buf.push_back((btnStyle>>8)&0xFF);
    buf.push_back((btnStyle>>16)&0xFF); buf.push_back((btnStyle>>24)&0xFF);
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0); // exStyle
    buf.push_back(140); buf.push_back(0); // x
    buf.push_back(50); buf.push_back(0);  // y
    buf.push_back(50); buf.push_back(0); // cx
    buf.push_back(14); buf.push_back(0); // cy
    buf.push_back(IDOK & 0xFF); buf.push_back(IDOK >> 8); // id = IDOK

    // class: button
    buf.push_back(0x0080); buf.push_back(0);  // atom for BUTTON
    // title
    pushStr(buf, L"OK");
    // creation data: none
    buf.push_back(0); buf.push_back(0);

    align(buf);

    // --- Item 4: Cancel button ---
    DWORD btnCancelStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP
                         | BS_PUSHBUTTON;
    buf.push_back(btnCancelStyle & 0xFF); buf.push_back((btnCancelStyle>>8)&0xFF);
    buf.push_back((btnCancelStyle>>16)&0xFF); buf.push_back((btnCancelStyle>>24)&0xFF);
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0);
    buf.push_back(80); buf.push_back(0);  // x (right of OK)
    buf.push_back(50); buf.push_back(0);  // y
    buf.push_back(50); buf.push_back(0);  // cx
    buf.push_back(14); buf.push_back(0);  // cy
    buf.push_back(IDCANCEL & 0xFF); buf.push_back(IDCANCEL >> 8);

    // class: button
    buf.push_back(0x0080); buf.push_back(0);
    // title
    pushStr(buf, L"Cancel");
    // creation data: none
    buf.push_back(0); buf.push_back(0);

    align(buf);

    return buf;
}

// Dialog procedure for the Insert Link dialog.
static INT_PTR CALLBACK LinkDialogProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    static std::wstring* pResult = nullptr;
    switch (msg) {
    case WM_INITDIALOG: {
        pResult = reinterpret_cast<std::wstring*>(lp);
        // Pre-fill with https://
        SetDlgItemTextW(hDlg, 1001, L"https://");
        // Focus the URL field and select all.
        HWND hEdit = GetDlgItem(hDlg, 1001);
        SetFocus(hEdit);
        Edit_SetSel(hEdit, 0, -1);
        return FALSE;  // we already set focus
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: {
            wchar_t buf[2048];
            GetDlgItemTextW(hDlg, 1001, buf, 2048);
            if (pResult) *pResult = buf;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// Convert wstring to UTF-8 string.
static std::string WideToUtf8(const std::wstring& ws) {
    std::string out;
    for (wchar_t ch : ws) {
        if (ch < 0x80) out += static_cast<char>(ch);
        else if (ch < 0x800) {
            out += static_cast<char>(0xC0 | (ch >> 6));
            out += static_cast<char>(0x80 | (ch & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (ch >> 12));
            out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }
    return out;
}

void AppWindow::InsertLinkCmd() {
    if (!editing_) return;
    // Build the dialog template in memory.
    auto tmpl = BuildLinkDialogTemplate();
    std::wstring url;
    INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandle(NULL),
        reinterpret_cast<DLGTEMPLATE*>(tmpl.data()),
        hwnd_,
        LinkDialogProc,
        reinterpret_cast<LPARAM>(&url));
    if (result != IDOK || url.empty()) return;
    std::string urlUtf8 = WideToUtf8(url);
    InsertLink(&buffer_, &sel_, urlUtf8);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
}

void AppWindow::ClearFormat() {
    if (!editing_) return;
    // Remove all inline markers from the selection.
    // For each marker type, toggle it off if present.
    const char* markers[] = {"**", "*", "~~", "`"};
    for (auto m : markers) {
        if (!sel_.Empty()) {
            uint32_t s = sel_.Start();
            uint32_t e = s + sel_.Length();
            const std::string& text = buffer_.Text();
            if (IsWrappedIn(text, s, e, m)) {
                uint32_t mlen = static_cast<uint32_t>(strlen(m));
                buffer_.Splice(e - mlen, mlen, "");
                buffer_.Splice(s, mlen, "");
                sel_.anchor = {s};
                sel_.active = {e - mlen * 2};
            }
        }
    }
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
}

void AppWindow::SetHeading(int level) {
    if (!editing_) return;
    // Toggle: if the caret is already in a heading of this level,
    // remove the heading (revert to paragraph).
    FormatState fs = GetFormatState();
    diaglog("SetHeading(%d): editing=%d headingLevel=%d sel.active=%u sel.anchor=%u sel.empty=%d\n",
        level, editing_, fs.headingLevel, sel_.active.offset, sel_.anchor.offset, sel_.Empty());
    if (fs.headingLevel == level) {
        SetHeadingLevel(&buffer_, &sel_, 0);
    } else {
        SetHeadingLevel(&buffer_, &sel_, level);
    }
    diaglog("SetHeading: after splice, buffer size=%zu\n", buffer_.Text().size());
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    // The ribbon button click steals focus from the content window, so the
    // deferred InvalidateRect in Repaint() may not deliver a WM_PAINT until
    // the user clicks back into the document. Force an immediate repaint.
    ForceRepaintNow();
}

void AppWindow::ToggleBullets() {
    if (!editing_) return;
    ToggleUnorderedList(&buffer_, &sel_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::ToggleNumbering() {
    if (!editing_) return;
    ToggleOrderedList(&buffer_, &sel_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::ToggleQuote() {
    if (!editing_) return;
    ToggleBlockquote(&buffer_, &sel_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::Indent() {
    if (!editing_) return;
    IndentLine(&buffer_, &sel_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::Outdent() {
    if (!editing_) return;
    OutdentLine(&buffer_, &sel_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::UndoAction() {
    if (!editing_) return;
    editor_.Undo();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::RedoAction() {
    if (!editing_) return;
    editor_.Redo();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::OnDestroy() {
    StopScrollAnimation();
    SaveWinPlacement(hwnd_);
    DestroyRibbon();
    watcher_.Stop();
    renderer_.Release();
    SafeRelease(rt_);
    SafeRelease(dw_factory_);
    SafeRelease(d2d_factory_);
    PostQuitMessage(0);
}

void AppWindow::OnRibbonHeightChanged() {
    ResizeContentWindow();
    Repaint();
}

void AppWindow::RecreateRenderer() {
    renderer_.Release();
    renderer_inited_ = false;
    EnsureRenderer();
}

void AppWindow::OpenFileDialog() {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Markdown (*.md;*.markdown)\0*.md;*.markdown\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = L"Open Markdown File";
    if (GetOpenFileNameW(&ofn)) {
        OpenFile(buf);
    }
}

void AppWindow::ZoomIn() {
    float z = renderer_.Zoom();
    z *= 1.25f;
    if (z > 4.0f) z = 4.0f;
    renderer_.SetZoom(z);
    RecreateRenderer();
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::ZoomOut() {
    float z = renderer_.Zoom();
    z /= 1.25f;
    if (z < 0.5f) z = 0.5f;
    renderer_.SetZoom(z);
    RecreateRenderer();
    UpdateScrollInfo();
    Repaint();
}

bool AppWindow::IsWrapEnabled() const {
    return renderer_.Wrap();
}

void AppWindow::ToggleWrap() {
    renderer_.SetWrap(!renderer_.Wrap());
    UpdateRibbonWrapState(renderer_.Wrap());
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::ShowWidthMenu() {
    // Show a standard Win32 popup menu with the 4 width options.
    // A Win32 TrackPopupMenu has no height limit, unlike the Ribbon
    // ApplicationMenu's DropDownButton submenu which clips and scrolls.
    HMENU hMenu = CreatePopupMenu();

    static const wchar_t* labels[4] = {
        L"Standard", L"960 px", L"1600 px", L"Full width"
    };
    int cur = settings_.contentWidthMode;

    for (int i = 0; i < 4; ++i) {
        UINT flags = MF_STRING;
        if (i == cur) flags |= MF_CHECKED;
        AppendMenuW(hMenu, flags, 3001 + i, labels[i]);
    }

    // Position the menu below the Fil (ApplicationMenu) button.
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    int x = rc.left + 10;
    int y = rc.top + static_cast<int>(g_ribbonHeight) + 5;

    int cmd = TrackPopupMenu(hMenu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        x, y, 0, hwnd_, nullptr);

    DestroyMenu(hMenu);

    if (cmd >= 3001 && cmd <= 3004) {
        SetContentWidthMode(cmd - 3001);
    }
}

void AppWindow::SelectAll() {
    sel_.anchor = {0};
    sel_.active = {static_cast<uint32_t>(buffer_.Length())};
    if (editing_) UpdateCaretPosition();
    Repaint();
}

// Context menu command IDs (must not conflict with ribbon IDs 10000+).
#define CM_UNDO     2001
#define CM_REDO     2002
#define CM_CUT      2003
#define CM_COPY     2004
#define CM_PASTE    2005
#define CM_SELALL   2006
#define CM_BOLD     2007
#define CM_ITALIC   2008
#define CM_CODE     2009
#define CM_EDIT     2010
#define CM_FIND     2011
#define CM_WRAP     2012
#define CM_ZOOMIN   2013
#define CM_ZOOMOUT  2014

void AppWindow::ShowContextMenu(int screenX, int screenY) {
    HMENU hMenu = CreatePopupMenu();

    if (editing_) {
        // ── Edit mode menu ──
        bool canUndo = undo_stack_.CanUndo();
        bool canRedo = undo_stack_.CanRedo();
        bool hasSel  = !sel_.Empty();

        AppendMenuW(hMenu, MF_STRING | (canUndo ? 0 : MF_GRAYED),
            CM_UNDO, L"Undo\tCtrl+Z");
        AppendMenuW(hMenu, MF_STRING | (canRedo ? 0 : MF_GRAYED),
            CM_REDO, L"Redo\tCtrl+Y");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        AppendMenuW(hMenu, MF_STRING | (hasSel ? 0 : MF_GRAYED),
            CM_CUT, L"Cut\tCtrl+X");
        AppendMenuW(hMenu, MF_STRING | (hasSel ? 0 : MF_GRAYED),
            CM_COPY, L"Copy\tCtrl+C");
        AppendMenuW(hMenu, MF_STRING,
            CM_PASTE, L"Paste\tCtrl+V");
        AppendMenuW(hMenu, MF_STRING, CM_SELALL, L"Select All\tCtrl+A");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        AppendMenuW(hMenu, MF_STRING, CM_BOLD,   L"Bold\tCtrl+B");
        AppendMenuW(hMenu, MF_STRING, CM_ITALIC, L"Italic\tCtrl+I");
        AppendMenuW(hMenu, MF_STRING, CM_CODE,   L"Code\tCtrl+`");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        AppendMenuW(hMenu, MF_STRING, CM_EDIT,
            L"Switch to View Mode\tCtrl+E");
    } else {
        // ── View mode menu ──
        bool hasSel = !sel_.Empty();

        AppendMenuW(hMenu, MF_STRING | (hasSel ? 0 : MF_GRAYED),
            CM_COPY, L"Copy\tCtrl+C");
        AppendMenuW(hMenu, MF_STRING, CM_SELALL, L"Select All\tCtrl+A");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING,
            CM_EDIT, L"Switch to Edit Mode\tCtrl+E");
    }

    // Common items for both modes.
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, CM_WRAP,    L"Toggle Word Wrap");
    AppendMenuW(hMenu, MF_STRING, CM_ZOOMIN,  L"Zoom In\tCtrl++");
    AppendMenuW(hMenu, MF_STRING, CM_ZOOMOUT, L"Zoom Out\tCtrl+-");

    int cmd = TrackPopupMenu(hMenu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenX, screenY, 0, hwnd_, nullptr);

    DestroyMenu(hMenu);

    // Dispatch the selected command.
    switch (cmd) {
    case CM_UNDO:    editor_.Undo();  OnBufferChanged(); break;
    case CM_REDO:    editor_.Redo();  OnBufferChanged(); break;
    case CM_CUT:
        if (!sel_.Empty()) {
            uint32_t s = sel_.Start(), len = sel_.Length();
            std::string sel_text = buffer_.Text().substr(s, len);
            ClipboardCut(hwnd_content_, sel_text);
            editor_.DeleteSelection();
            OnBufferChanged();
        }
        break;
    case CM_COPY:
        if (!sel_.Empty()) {
            uint32_t s = sel_.Start(), len = sel_.Length();
            std::string sel_text = buffer_.Text().substr(s, len);
            if (!editing_) sel_text = CleanSelectionForCopy(sel_text);
            ClipboardCopy(hwnd_content_, sel_text);
        }
        break;
    case CM_PASTE: {
        std::string text = ClipboardPaste(hwnd_content_);
        if (!text.empty()) {
            editor_.InsertText(text);
            OnBufferChanged();
        }
        break;
    }
    case CM_SELALL:  SelectAll(); break;
    case CM_BOLD:   ToggleBold();   break;
    case CM_ITALIC: ToggleItalic(); break;
    case CM_CODE:   ToggleCode();   break;
    case CM_EDIT:   ToggleEdit();   break;
    case CM_WRAP:   ToggleWrap();  break;
    case CM_ZOOMIN: ZoomIn();  break;
    case CM_ZOOMOUT: ZoomOut();   break;
    }
}

void AppWindow::ShowAbout() {
    MessageBoxW(hwnd_,
        L"MarkDownIt - Native Windows Markdown Viewer\n"
        L"Built with C++, Direct2D, and DirectWrite.\n"
        L"No Electron, no .NET runtime.\n\n"
        L"Drag a .md file onto the window or use Open.\n"
        L"F5 to reload, mouse wheel to scroll.",
        L"About MarkDownIt", MB_OK | MB_ICONINFORMATION);
}

//
// Settings (Fil menu)
//

bool AppWindow::IsMdRegistered() const {
    return settings_.fileAssoc;
}

void AppWindow::ToggleMdAssociation() {
    settings_.fileAssoc = !settings_.fileAssoc;
    // Defer all side effects (registry writes, SHChangeNotify,
    // InvalidateUICommand) via PostMessage to avoid re-entrant calls
    // inside the Ribbon's Execute callback, which causes a CTD.
    if (hwnd_content_) {
        PostMessage(hwnd_content_, WM_USER + 2, 0, 0);
    }
}

int AppWindow::GetContentWidthMode() const {
    return settings_.contentWidthMode;
}

void AppWindow::SetContentWidthMode(int mode) {
    if (mode < 0 || mode > 3) mode = 0;
    settings_.contentWidthMode = mode;
    renderer_.SetContentWidthMode(mode);
    SaveSettings(settings_);
    // Defer ALL side effects (InvalidateUICommand, repaint) via PostMessage
    // to avoid re-entrant calls inside the Ribbon Execute callback.
    layout_cache_.Clear();
    if (hwnd_content_) {
        PostMessage(hwnd_content_, WM_USER + 1, 0, 0);
    }
}

void AppWindow::InvalidateSettingsButtons() {
    if (!g_pRibbonFramework) return;
    g_pRibbonFramework->InvalidateUICommand(IDC_CMD_ASSOC_MD,
        UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Label);
    static const UINT widthCmds[] = {
        IDC_CMD_WIDTH_STD, IDC_CMD_WIDTH_960,
        IDC_CMD_WIDTH_1600, IDC_CMD_WIDTH_FULL
    };
    for (auto cmd : widthCmds) {
        g_pRibbonFramework->InvalidateUICommand(cmd,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Label);
    }
}

//
// Main window WndProc.
// Handles WM_SIZE, WM_DROPFILES, WM_DESTROY, WM_DPICHANGED.
// Does NOT handle WM_PAINT (validated by DefWindowProc, ribbon paints NC).
//
LRESULT AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:    OnCreate(hwnd);    return 0;
        case WM_DROPFILES: OnDropFiles(hwnd, (HDROP)wp); return 0;
        case WM_MOVE: {
            // Debounce-save window placement when the window is moved.
            SetTimer(hwnd_, 5, 500, nullptr);
            return 0;
        }
        case WM_SIZE: {
            int w = LOWORD(lp), h = HIWORD(lp);
            OnSize(hwnd, w, h);
            return 0;
        }
        case WM_DPICHANGED: {
            renderer_.Release();
            EnsureRenderer();
            RECT* rc = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, rc->left, rc->top,
                rc->right - rc->left, rc->bottom - rc->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            ResizeContentWindow();
            Repaint();
            return 0;
        }
        case WM_TIMER:
            if (wp == 1) {
                KillTimer(hwnd_, 1);
                ResizeContentWindow();
                Repaint();
            } else if (wp == 3) {
                // Open file from command line after init is complete.
                KillTimer(hwnd_, 3);
                ProcessPendingFile();
            } else if (wp == 5) {
                // Debounced window placement save after resize.
                KillTimer(hwnd_, 5);
                SaveWinPlacement(hwnd_);
            }
            return 0;
        case FileWatcher::WM_USER_RELOAD: OnReload(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_CLOSE:      OnClose();         return 0;
        case WM_DESTROY:   OnDestroy();   return 0;
        case WM_PAINT: {
            // Main window does not paint. The ribbon framework handles
            // its own area, and the content child handles content.
            ValidateRect(hwnd, nullptr);
            return 0;
        }
        default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

//
// Content child window WndProc.
// Handles WM_PAINT, WM_VSCROLL, WM_MOUSEWHEEL, WM_KEYDOWN, WM_SIZE.
//
LRESULT AppWindow::ContentWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:     OnContentPaint(hwnd); return 0;
        case WM_ERASEBKGND: return 1;  // D2D handles all painting
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            OnLButtonDown(hwnd, x, y);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            OnLButtonDblClk(hwnd, x, y);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            OnMouseMove(hwnd, x, y);
            return 0;
        }
        case WM_LBUTTONUP:   OnLButtonUp(hwnd);    return 0;
        case WM_CONTEXTMENU: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            ShowContextMenu(x, y);
            return 0;
        }
        case WM_CHAR:        OnChar(hwnd, static_cast<wchar_t>(wp)); return 0;
        case WM_SETFOCUS:  OnSetFocus(hwnd);   return 0;
        case WM_KILLFOCUS: OnKillFocus(hwnd);  return 0;
        case WM_VSCROLL:    OnContentVScroll(hwnd, (int)LOWORD(wp), (int)HIWORD(wp)); return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            OnContentMouseWheel(hwnd, delta);
            return 0;
        }
        case WM_KEYDOWN:
            OnKeyDown(hwnd, wp, lp);
            return 0;
        case WM_SIZE: {
            int w = LOWORD(lp), h = HIWORD(lp);
            OnContentSize(hwnd, w, h);
            return 0;
        }
        case WM_TIMER:
            if (wp == 4) {
                OnScrollTick();
            }
            return 0;
        case WM_USER + 1: {  // Deferred settings change (width, etc.)
            InvalidateSettingsButtons();
            UpdateScrollInfo();
            RedrawWindow(hwnd_content_, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
            return 0;
        }
        case WM_USER + 2: {  // Deferred .md association toggle
            if (settings_.fileAssoc) {
                wchar_t exePath[MAX_PATH] = {};
                GetModuleFileNameW(hinst_, exePath, MAX_PATH);
                RegisterMdAssociation(exePath);
            } else {
                UnregisterMdAssociation();
            }
            SaveSettings(settings_);
            InvalidateSettingsButtons();
            return 0;
        }
        default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

LRESULT CALLBACK AppWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* self = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (AppWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (AppWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK AppWindow::ContentWndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* self = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (AppWindow*)cs->lpCreateParams;
        // Content window also stores a pointer to the AppWindow.
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (AppWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (self) return self->ContentWndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}
