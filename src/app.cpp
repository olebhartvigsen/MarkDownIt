#include "app.h"
#include "resource.h"
#include "ribbon.h"
#include "fileassoc.h"
#include "parser.h"
#include "navigation.h"
#include "office/markdown_bridge.h"
#include "office/docx_import.h"
#include "office/docx_export.h"
#include "office/pdf_export.h"
#include <commdlg.h>
#include <windowsx.h>

#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#include <d2d1.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>
#include <utility>
#include <cctype>
#include <cwctype>
#include <cstdint>
#include "crash_trace.h"







// Defined below, near the table commands. The key handler needs it too, so it
// is declared here rather than moving the definition up.
static const Node* FindContainingTable(const Document& doc, uint32_t offset);

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
                continue; // skip, already separated
            out += ' ';
        }
    }
    return out;
}

static std::wstring ImeUtf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), wide.data(), needed);
    return wide;
}

static std::string ImeWideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        needed = WideCharToMultiByte(CP_UTF8, 0, text.data(),
            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    }
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string AppWindow::SelectionForClipboard() const {
    const std::string& source = buffer_.Text();
    uint32_t start = std::min(sel_.Start(), static_cast<uint32_t>(source.size()));
    uint32_t end = std::min(start + sel_.Length(), static_cast<uint32_t>(source.size()));
    if (start >= end) return {};

    // Source view shows the raw Markdown, so the clipboard gets it byte
    // for byte. Running the rendered-view filters over it turned soft
    // line breaks into spaces and dropped syntax the user could plainly
    // see on screen, so a copy from source mode no longer matched what
    // was displayed.
    if (source_view_) return source.substr(start, end - start);

    // WYSIWYG selection is exported as visible text. Markdown delimiters,
    // list prefixes and fence markers are not user-visible characters and
    // must not leak into CF_UNICODETEXT or the lossless application format.
    std::string visible;
    for (uint32_t at = start; at < end;) {
        uint32_t next = NextGraphemeBoundary(source, at);
        if (next <= at) break;
        bool rendered = layout_cache_.OffsetIsRendered(at);
        bool lineBreak = source[at] == '\n' || source[at] == '\r';
        if (rendered || lineBreak)
            visible.append(source, at, next - at);
        at = next;
    }
    if (visible.empty() && layout_cache_.Blocks().empty()) {
        // Raw source view and documents whose layout is not ready still
        // need normal clipboard behavior rather than silently copying none.
        visible = source.substr(start, end - start);
    }
    return editing_ ? visible : CleanSelectionForCopy(visible);
}

AppWindow::~AppWindow() {
    SafeRelease(d2d_ctx5_);
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

    // WS_CLIPCHILDREN is a window style, never a class style (a class
    // style register attempt rejects it). On the window it keeps the
    // chrome fill out of the ribbon's and the content's areas.
    hwnd_ = CreateWindowExW(
        WS_EX_ACCEPTFILES, kClassName, L"MarkDownIt",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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

        // The saved width must also be visible in the File menu. The
        // framework drops a Label invalidation for a command whose
        // view does not exist yet, and the File menu realizes its
        // items lazily at the first open, so this timer alone does
        // not carry the checkmark on the very first popup (live
        // trace: startup timer 6 fired, yet the first popup build
        // issued no Label query). The primary first-open marking is
        // the Enabled-query push in ribbon.cpp's UpdateProperty; this
        // timer stays as the deferred invalidation for the realized
        // case, which a width change also uses. Harmless when
        // dropped.
        if (hwnd_) {
            SetTimer(hwnd_, 6, 350, nullptr);
        }
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
    renderer_.SetD2DDeviceContext5(d2d_ctx5_);
    layout_cache_.SetSourceText(&buffer_.Text());
    renderer_.SetSourceText(&buffer_.Text());
}

void AppWindow::InitEditor() {
    editor_ = EditController(&buffer_, &sel_);
    editor_.SetUndoStack(&undo_stack_);
}

void AppWindow::OnKeyDown(HWND hwnd, WPARAM vk, LPARAM lp) {
    if (!has_focus_) return;

    // A navigation action ends pending typing-format state. The state only
    // applies while the caret remains at the insertion point.
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
        vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT) {
        pending_bold_set_ = false;
        pending_italic_set_ = false;
        pending_run_active_ = false;
        pending_run_suffix_bytes_ = 0;
    }

    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

    // Zoom shortcuts. Handled before the view-mode suppression below so
    // they work in both view and edit mode and never touch the editor:
    //   Ctrl+0 / Ctrl+Numpad0 -> reset to 100%
    //   Ctrl+Plus / Ctrl+=    -> zoom in (VK_OEM_PLUS is the = key on US
    //                              and the + key on ISO/Danish layouts)
    //   Ctrl+Minus / Ctrl+-   -> zoom out
    // Every branch returns early: no scrolling, no text insertion, and no
    // selection change from these keys.
    if (ctrl && (vk == VK_OEM_PLUS || vk == VK_ADD)) {
        ZoomIn();
        return;
    }
    if (ctrl && (vk == VK_OEM_MINUS || vk == VK_SUBTRACT)) {
        ZoomOut();
        return;
    }
    if (ctrl && !shift && (vk == 0x30 || vk == VK_NUMPAD0)) {
        ResetZoom();
        return;
    }

    // Toggle edit mode with Ctrl+E
    if (ctrl && !shift && vk == 0x45) {
        SetEdit(!editing_);
        return;
    }

    // Marker shortcut, available in every mode like the clipboard
    // aliases: annotating a document is not editing it (marker guide 17).
    //   Ctrl+Shift+H -> mark the selection, or unmark when it is marked
    if (ctrl && shift && vk == 0x48) {  // H
        ToggleMarkSelection();
        return;
    }

    // Standard Windows clipboard aliases. Keep these before the main
    // switch so they work in the same modes as Ctrl+C/X/V.
    if (vk == VK_INSERT && ctrl && !shift) {
        if (!sel_.Empty()) {
            uint32_t s = sel_.Start();
            uint32_t len = sel_.Length();
            std::string sel_text = SelectionForClipboard();
            ClipboardCopy(hwnd_content_, sel_text);
        }
        return;
    }
    if (vk == VK_INSERT && shift && !ctrl) {
        std::string text = ClipboardPaste(hwnd_content_);
        // Same as Ctrl+V: view mode enters edit mode on a real paste.
        if (!text.empty() && !editing_) SetEdit(true);
        // Same in-cell paste rule as Ctrl+V: strip row syntax.
        if (!text.empty() && IsOffsetInTable(doc_, sel_.active.offset))
            text = SanitizePasteForTableCell(text);
        if (!text.empty()) {
            pending_run_active_ = false;
            pending_run_suffix_bytes_ = 0;
            editor_.InsertText(text);
            OnBufferChanged();
        }
        return;
    }
    if (vk == VK_DELETE && shift && !ctrl) {
        if (!sel_.Empty()) {
            // Same as Ctrl+X, including entering edit mode from view mode.
            if (!editing_) SetEdit(true);
            std::string sel_text = SelectionForClipboard();
            if (ClipboardCut(hwnd_content_, sel_text)) {
                editor_.DeleteSelection();
                OnBufferChanged();
            }
        }
        return;
    }

    if (ctrl && !shift && vk == 0x46) {
        ShowFindReplace(false);
        return;
    }
    if (ctrl && !shift && vk == 0x48) {
        ShowFindReplace(true);
        return;
    }
    if (ctrl && !shift && vk == 0x4E) {
        NewDocument();
        return;
    }

    // Any direct keyboard action can move or replace the caret. The find
    // bar asks for the caret offset every time it navigates, so a moved
    // caret needs no bookkeeping here.

    // In view mode, suppress editing keys but allow navigation, selection,
    // and the clipboard family (Ctrl+A/C/X/V). View mode has no caret, so
    // cut and paste enter edit mode in their handlers; plain typing stays
    // blocked in OnChar.
    if (!editing_) {
        bool isNavigation = (vk == VK_LEFT || vk == VK_RIGHT ||
            vk == VK_UP || vk == VK_DOWN ||
            vk == VK_HOME || vk == VK_END ||
            vk == VK_PRIOR || vk == VK_NEXT);
        bool isCopy = (ctrl && vk == 0x43); // Ctrl+C
        bool isCut = (ctrl && !shift && vk == 0x58); // Ctrl+X
        bool isPaste = (ctrl && !shift && vk == 0x56); // Ctrl+V
        bool isSelectAll = (ctrl && vk == 0x41); // Ctrl+A
        // Shift extends navigation (selection) but nothing else: Shift with
        // any non-navigation key (Backspace, Tab, letter combos) would
        // otherwise fall through to the edit handlers below and mutate the
        // buffer while in view mode.
        if (!isNavigation && !isCopy && !isCut && !isPaste && !isSelectAll)
            return;
    }

    if (vk != 0x41) last_selectall_tier_ = 0;  // any other key ends the Ctrl+A escalation run

    switch (vk) {
        case VK_LEFT: {
            uint32_t newOffset = (!shift && !sel_.Empty())
                ? sel_.Start()
                : (ctrl ? MoveWordLeft(buffer_, sel_.active.offset)
                        : MoveLeft(buffer_, sel_.active.offset, &layout_cache_));
            // Cell boundary rule (plan 12): the caret may not leave its
            // cell with Left/Right; Tab owns structural cell movement.
            TableCellRef fromCell;
            if (TableCellAtOffset(doc_, buffer_.Text(), sel_.active.offset, &fromCell) &&
                !fromCell.separatorRow && !ctrl) {
                if (newOffset < fromCell.srcOffset)
                    newOffset = fromCell.srcOffset;
                else if (newOffset > fromCell.srcEnd)
                    newOffset = fromCell.srcEnd;
            }
            desiredX_ = -1.0f;
            if (!shift) editor_.BreakUndoCoalesce();
            if (shift) sel_.active = {newOffset};
            else sel_.Collapse({newOffset});
            UpdateCaretPosition();
            Repaint();
            break;
        }
        case VK_RIGHT: {
            uint32_t newOffset = (!shift && !sel_.Empty())
                ? sel_.End()
                : (ctrl ? MoveWordRight(buffer_, sel_.active.offset)
                        : MoveRight(buffer_, sel_.active.offset, &layout_cache_));
            // Cell boundary rule (plan 12): the caret may not leave its
            // cell with Left/Right; Tab owns structural cell movement.
            TableCellRef fromCell;
            if (TableCellAtOffset(doc_, buffer_.Text(), sel_.active.offset, &fromCell) &&
                !fromCell.separatorRow && !ctrl) {
                if (newOffset > fromCell.srcEnd)
                    newOffset = fromCell.srcEnd;
                else if (newOffset < fromCell.srcOffset)
                    newOffset = fromCell.srcOffset;
            }
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
                lineHeight = sz.height / 40.0f; // rough estimate
                if (lineHeight < 16.0f) lineHeight = 16.0f;
            }
            uint32_t newOffset = (!shift && !sel_.Empty())
                ? sel_.Start()
                : MoveVertical(layout_cache_, sel_.active.offset,
                               -1, &desiredX_, scrollY_, lineHeight);
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
            // Inside a table, Down walks rows and keeps the column
            // (plan Task 16b step 5). Outside one, it stays a visual line.
            uint32_t newOffset = 0;
            bool handled = false;
            const Node* tbl = FindContainingTable(doc_, sel_.active.offset);
            if (tbl && sel_.Empty()) {
                const uint32_t tblEnd = tbl->srcOffset + tbl->srcLength;
                handled = TableVerticalMove(buffer_.Text(), tbl->srcOffset,
                                            tblEnd, sel_.active.offset,
                                            1, &newOffset);
            }
            if (!handled) {
                newOffset = (!shift && !sel_.Empty())
                    ? sel_.End()
                    : MoveVertical(layout_cache_, sel_.active.offset,
                                   1, &desiredX_, scrollY_, lineHeight);
            }
            if (shift) sel_.active = {newOffset};
            else sel_.Collapse({newOffset});
            UpdateCaretPosition();
            Repaint();
            break;
        }
        case VK_HOME: {
            uint32_t newOffset;
            if (!shift && !sel_.Empty()) {
                newOffset = sel_.Start();
            } else if (ctrl) {
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
            if (!shift && !sel_.Empty()) {
                newOffset = sel_.End();
            } else if (ctrl) {
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
            float page = rt_ ? rt_->GetSize().height : static_cast<float>(clientH_);
            float lineHeight = 20.0f;
            float caretX = desiredX_;
            float caretY = scrollY_;
            float caretH = lineHeight;
            if (layout_cache_.OffsetToCaretRect(sel_.active.offset,
                                                &caretX, &caretY, &caretH)) {
                lineHeight = std::max(1.0f, caretH);
                if (desiredX_ < 0.0f) desiredX_ = caretX;
                caretX = desiredX_;
            }
            float amount = std::max(lineHeight, page - lineHeight);
            float targetScroll = ClampScroll(scrollY_ + amount);
            if (editing_) {
                uint32_t newOff = layout_cache_.PointToOffset(
                    caretX >= 0.0f ? caretX : 100.0f, caretY + amount);
                if (newOff != UINT32_MAX) {
                    if (shift) sel_.active = {newOff};
                    else sel_.Collapse({newOff});
                }
            }
            StartSpring(targetScroll);
            if (editing_) UpdateCaretPosition();
            Repaint();
            break;
        }
        case VK_PRIOR: {  // Page Up
            float page = rt_ ? rt_->GetSize().height : static_cast<float>(clientH_);
            float lineHeight = 20.0f;
            float caretX = desiredX_;
            float caretY = scrollY_;
            float caretH = lineHeight;
            if (layout_cache_.OffsetToCaretRect(sel_.active.offset,
                                                &caretX, &caretY, &caretH)) {
                lineHeight = std::max(1.0f, caretH);
                if (desiredX_ < 0.0f) desiredX_ = caretX;
                caretX = desiredX_;
            }
            float amount = std::max(lineHeight, page - lineHeight);
            float targetScroll = ClampScroll(scrollY_ - amount);
            if (editing_) {
                uint32_t newOff = layout_cache_.PointToOffset(
                    caretX >= 0.0f ? caretX : 100.0f, caretY - amount);
                if (newOff != UINT32_MAX) {
                    if (shift) sel_.active = {newOff};
                    else sel_.Collapse({newOff});
                }
            }
            StartSpring(targetScroll);
            if (editing_) UpdateCaretPosition();
            Repaint();
            break;
        }
        case VK_F5:
            // OnReload (not Reload): prompts before discarding unsaved edits.
            OnReload();
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
        case 0x37:  // Ctrl+7/Ctrl+Shift+7 = ordered list
            if (ctrl && shift) {
                ToggleOrderedList(&buffer_, &sel_, &undo_stack_);
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case 0x38:  // Ctrl+8/Ctrl+Shift+8 = unordered list
            if (ctrl && shift) {
                ToggleUnorderedList(&buffer_, &sel_, &undo_stack_);
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case VK_OEM_PERIOD:  // Ctrl+Shift+. = blockquote
            if (ctrl && shift) {
                ToggleBlockquote(&buffer_, &sel_, &undo_stack_);
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            }
            break;
        case VK_TAB: {
            // Markdown tables keep each cell on one source line. Tab moves
            // between cells instead of inserting indentation or changing the
            // table prefix. Shift+Tab moves in the opposite direction.
            uint32_t tableOffset = 0;
            if (IsOffsetInTable(doc_, sel_.active.offset) ||
                (!sel_.Empty() && IsOffsetInTable(doc_, sel_.Start()))) {
                // Tab with a selection replaces the selection like a
                // normal editor: collapse to the cell boundary target.
                if (MoveTableCell(doc_, sel_.active.offset, shift, &tableOffset)) {
                    sel_.Collapse({tableOffset});
                    editor_.BreakUndoCoalesce();
                    desiredX_ = -1.0f;
                    UpdateCaretPosition();
                    Repaint();
                } else if (!shift) {
                    // Tab from the last cell appends a blank row with the
                    // active column count and puts the caret in its first
                    // cell. One undo entry removes row and caret change.
                    const std::string& text = buffer_.Text();
                    for (size_t ti = 0; ti < doc_.nodes.size(); ++ti) {
                        const Node& node = doc_.nodes[ti];
                        if (node.block != BlockKind::Table) continue;
                        const uint32_t tblStart = node.srcOffset;
                        const uint32_t tblEnd = tblStart + node.srcLength;
                        if (sel_.active.offset < tblStart ||
                            sel_.active.offset > tblEnd) continue;
                        const int cols =
                            static_cast<int>(TableColumnCount(doc_, ti));
                        if (cols <= 0) break;
                        std::string row = TableBlankRow(cols);
                        const uint32_t insertPos = tblEnd;
                        const bool neededBreak =
                            insertPos > 0 && text[insertPos - 1] != '\n';
                        if (neededBreak) row.insert(0, "\n");
                        SpliceWithUndo(insertPos, 0, row);
                        editor_.BreakUndoCoalesce();
                        // The caret belongs in the new row's first cell,
                        // which starts after its leading pipe. The row text
                        // begins with the optional separator break, so the
                        // cell is one further in than that break.
                        sel_.Collapse({insertPos + (neededBreak ? 2u : 1u)});
                        desiredX_ = -1.0f;
                        UpdateCaretPosition();
                        OnBufferChanged();
                        ForceRepaintNow();
                        break;
                    }
                }
                break;
            }
            // In fenced code, Tab is literal four-space indentation. Outside
            // code blocks, retain the Markdown list indentation behavior.
            bool inCodeBlock = false;
            for (const auto& node : doc_.nodes) {
                if ((node.block == BlockKind::CodeBlock ||
                     node.block == BlockKind::MermaidFlowchart ||
                     node.block == BlockKind::MermaidPie ||
                     node.block == BlockKind::MermaidSequence) &&
                    sel_.active.offset >= node.srcOffset &&
                    sel_.active.offset <= node.srcOffset + node.srcLength) {
                    inCodeBlock = true;
                    break;
                }
            }
            if (inCodeBlock && !shift) {
                editor_.InsertText("    ");
            } else if (shift && inCodeBlock) {
                const std::string& source = buffer_.Text();
                const size_t newline = sel_.active.offset == 0
                    ? std::string::npos
                    : source.rfind('\n', sel_.active.offset - 1);
                const uint32_t lineStart = newline == std::string::npos
                    ? 0 : static_cast<uint32_t>(newline + 1);
                uint32_t spaces = 0;
                while (lineStart + spaces < source.size() && spaces < 4 &&
                       source[lineStart + spaces] == ' ') ++spaces;
                if (spaces == 4) {
                    editor_.ReplaceTextRange(lineStart, 4, "", EditType::Other);
                }
                editor_.BreakUndoCoalesce();
            } else {
                IndentLine(&buffer_, &sel_, &undo_stack_);
                editor_.BreakUndoCoalesce();
            }
            OnBufferChanged();
            break;
        }
        case 0x42:  // Ctrl+B = bold
            if (ctrl && !shift) {
                ToggleBold();
            }
            break;
        case 0x41:  // Ctrl+A = select all
            if (ctrl && !shift) {
                SelectAll();
            }
            break;
        case 0x49:  // Ctrl+I = italic
            if (ctrl && !shift) {
                ToggleItalic();
            }
            break;
        case 0x4B:  // Ctrl+K = link dialog
            if (ctrl && !shift) {
                InsertLinkCmd();
            }
            break;
        case 0x58:  // Ctrl+X = cut, Ctrl+Shift+X = strikethrough
            if (ctrl && shift) {
                ToggleInlineMarker(&buffer_, &sel_, "~~", &undo_stack_);
                editor_.BreakUndoCoalesce();
                OnBufferChanged();
            } else if (ctrl && !shift) {
                // An empty selection is a no-op and must not switch modes.
                if (!sel_.Empty()) {
                    // Cut is an edit, so view mode enters edit mode first:
                    // there is no caret there to cut against.
                    if (!editing_) SetEdit(true);
                    std::string sel_text = SelectionForClipboard();
                    if (ClipboardCut(hwnd_content_, sel_text)) {
                        editor_.DeleteSelection();
                        OnBufferChanged();
                    }
                }
            }
            break;
        case VK_OEM_3:  // Ctrl+` = inline code (backtick key)
            if (ctrl && !shift) {
                ToggleInlineMarker(&buffer_, &sel_, "`", &undo_stack_);
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
            // SelectionForClipboard already applies the view-mode cleanup
            // (soft line breaks become spaces, paragraph and forced breaks
            // survive), so the text is used exactly as returned.
            if (ctrl && !shift) {
                if (!sel_.Empty()) {
                    ClipboardCopy(hwnd_content_, SelectionForClipboard());
                }
            }
            break;

        case 0x56:  // Ctrl+V = paste
            if (ctrl && !shift) {
                std::string text = ClipboardPaste(hwnd_content_);
                // Paste is an edit and view mode has no caret to paste at,
                // so switch to edit mode. Done after reading the clipboard
                // so an empty or unsupported clipboard stays a no-op and
                // leaves the mode alone.
                if (!text.empty() && !editing_) SetEdit(true);
                if (!text.empty()) {
                    // Tables are single-line Markdown syntax: a paste
                    // inside a cell must never introduce pipes or row
                    // breaks (nested tables are not expressible here).
                    if (IsOffsetInTable(doc_, sel_.active.offset))
                        text = SanitizePasteForTableCell(text);
                    if (!text.empty()) {
                        pending_run_active_ = false;
                        pending_run_suffix_bytes_ = 0;
                        editor_.InsertText(text);
                        OnBufferChanged();
                    }
                }
            }
            break;
        case 0x5A:  // Ctrl+Z = undo
            if (ctrl && !shift) {
                pending_run_active_ = false;
                pending_run_suffix_bytes_ = 0;
                pending_bold_set_ = false;
                pending_italic_set_ = false;
                ApplyUndo(false);
            } else if (ctrl && shift) {
                pending_run_active_ = false;
                pending_run_suffix_bytes_ = 0;
                pending_bold_set_ = false;
                pending_italic_set_ = false;
                ApplyUndo(true);
            }
            break;
        case 0x59:  // Ctrl+Y = redo
            if (ctrl) {
                pending_run_active_ = false;
                pending_run_suffix_bytes_ = 0;
                pending_bold_set_ = false;
                pending_italic_set_ = false;
                ApplyUndo(true);
            }
            break;
        case VK_BACK: {
            pending_run_active_ = false;
            pending_run_suffix_bytes_ = 0;
            const size_t lengthBefore = buffer_.Length();
            if (ctrl) {
                editor_.DeleteWordBackward();
            } else if (IsOffsetInTable(doc_, sel_.active.offset)) {
                // Inside a cell the boundary rule replaces the global
                // one: the cell's left pipe is never consumed.
                editor_.DeleteBackwardInCell(doc_);
            } else {
                editor_.DeleteBackward(&doc_);
            }
            if (buffer_.Length() != lengthBefore) OnBufferChanged();
            break;
        }
        case VK_DELETE: {
            pending_run_active_ = false;
            pending_run_suffix_bytes_ = 0;
            const size_t lengthBefore = buffer_.Length();
            if (ctrl) {
                editor_.DeleteWordForward();
            } else if (IsOffsetInTable(doc_, sel_.active.offset)) {
                // Inside a cell the closing pipe is never consumed.
                editor_.DeleteForwardInCell(doc_);
            } else {
                editor_.DeleteForward(&doc_);
            }
            if (buffer_.Length() != lengthBefore) OnBufferChanged();
            break;
        }
        case VK_RETURN:
            pending_run_active_ = false;
            pending_run_suffix_bytes_ = 0;
            if ((shift ? editor_.InsertSoftBreak(doc_)
                       : editor_.InsertParagraphBreak(doc_))) {
                OnBufferChanged();
                // Force immediate visual update after Enter; don't wait
                // for the debounced reparse timer.
                ForceRepaintNow();
            }
            break;
        default:
            DefWindowProcW(hwnd, WM_KEYDOWN, vk, lp);
            break;
    }
}

// Remove entries from the recent list whose file is gone from disk.
// GetFileAttributesW is the cheap existence probe: it fails for
// missing files, directories and unreadable paths alike, and the
// INVALID_FILE_ATTRIBUTES check covers all three. Missing entries are
// dropped from the in-memory list and the settings are rewritten right
// away, so the registry never re-offers a card whose click can only
// fail.
void AppWindow::PruneMissingRecentFiles() {
    if (settings_.recentFiles.empty()) return;
    const size_t before = settings_.recentFiles.size();
    std::vector<RecentFile> kept;
    kept.reserve(settings_.recentFiles.size());
    for (const RecentFile& entry : settings_.recentFiles) {
        if (entry.path.empty()) continue;  // defensive: no path, no card
        const DWORD attrs = GetFileAttributesW(entry.path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;  // gone from disk (or never was a file)
        }
        kept.push_back(entry);
    }
    if (kept.size() != before) {
        settings_.recentFiles = std::move(kept);
        SaveSettings(settings_);
    }
}

void AppWindow::LoadSampleDoc() {
    // Instead of showing a placeholder text, show the welcome screen
    // with recent files cards.
    welcome_mode_ = true;
    if (dw_factory_) {
        welcome_.Init(dw_factory_);
        // A recent list that offers dead paths is worse than an empty
        // one: the card says the file is there, the click says it is
        // not. Remove missing files before display, and keep the
        // registry in step so they do not come back next session.
        PruneMissingRecentFiles();
        welcome_.SetRecentFiles(settings_.recentFiles);
    }
    if (rt_) {
        D2D1_SIZE_F sz = rt_->GetSize();
        welcome_.Layout(sz.width, sz.height);
    }
    // Clear any document content
    doc_ = Document{};
    buffer_.SetText("");
    layout_cache_.Clear();
    renderer_.ClearSvgCache();
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
    if (reparse_timer_) return; // already scheduled
    reparse_pending_ = true;
    reparse_timer_ = SetTimer(hwnd_content_, 2, 150, nullptr);
}

void AppWindow::ToggleSourceView() {
    source_view_ = !source_view_;
    // Keep the current edit state; source view does not force edit mode.
    // Invalidate the ribbon toggle state.
    if (g_pRibbonFramework) {
        g_pRibbonFramework->InvalidateUICommand(IDC_CMD_SOURCE,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_BooleanValue);
    }
    scrollY_ = 0.0f;
    StopScrollAnimation();
    layout_cache_.Clear();
    renderer_.ClearSvgCache();
    ForceRepaintNow();
    if (hwnd_content_) SetFocus(hwnd_content_);
    // Find works in both views, but WHICH text counts as rendered differs:
    // in source view every byte is on screen, so the same query can return
    // different matches than in the rendered view. Recompute (guide 4).
    if (find_bar_.IsVisible()) RefreshFindResults(false);
}

void AppWindow::OnReparseTimer() {
    reparse_pending_ = false;
    if (reparse_timer_) { KillTimer(hwnd_content_, reparse_timer_); reparse_timer_ = 0; }
    // A reparse follows every document change; the stored anchors are
    // resolved against the new text so markers follow edited documents
    // (marker guide 15).
    ResolveMarkerAnchors();
    // In source view, we don't need to reparse the markdown; the
    // raw text is displayed directly. Just rebuild the layout cache.
    if (source_view_) {
        layout_cache_.Clear();
    renderer_.ClearSvgCache();
        UpdateScrollInfo();
        ForceRepaintNow();
        return;
    }
    doc_ = Document{};
    ParseMarkdown(buffer_.Text(), doc_);
    layout_cache_.Clear();
    renderer_.ClearSvgCache();
    UpdateScrollInfo();
    // Force synchronous repaint so the layout cache is rebuilt before
    // UpdateCaretPosition runs. Repaint() is async (InvalidateRect) and
    // would leave the cache empty, causing OffsetToCaretRect to fail
    // and the caret to disappear.
    ForceRepaintNow();
}

void AppWindow::OnBufferChanged() {
    MarkDirty();
    InvalidateFormatButtons();
    OnFindDocumentChanged();
    layout_cache_.Clear();
    renderer_.ClearSvgCache();
    ScheduleReparse();
}

// The document changed underneath an open Find bar. Every stored match
// offset is stale now, so the search is recomputed rather than kept, and
// no stale position is ever used for a later replacement (guide 31).
void AppWindow::OnFindDocumentChanged() {
    if (!find_bar_.IsVisible()) return;
    RefreshFindResults(false);
}

void AppWindow::ScrollCaretIntoView(float caretY, float caretH) {
    if (!rt_) return;
    float viewH = rt_->GetSize().height;
    // Margin so the caret never sits glued to the very edge.
    float margin = 8.0f;
    if (caretY - margin < scrollY_) {
        // Caret above the viewport: scroll up.
        scrollY_ = ClampScroll(caretY - margin);
        StopScrollAnimation();
        UpdateScrollInfo();
        Repaint();
    } else if (caretY + caretH + margin > scrollY_ + viewH) {
        // Caret below the viewport: scroll down.
        scrollY_ = ClampScroll(caretY + caretH + margin - viewH);
        StopScrollAnimation();
        UpdateScrollInfo();
        Repaint();
    }
}

void AppWindow::UpdateCaretPosition() {
    if (!has_focus_ || !hwnd_content_) return;
    if (!editing_) {
        // No caret in view mode, but the selection still changed and the
        // marker toggle follows it, so feed the ribbon the new state.
        InvalidateMarkerToggleUI();
        return;
    }
    InvalidateFormatButtons();

    // Hide the caret when text is selected; only show it when the
    // selection is collapsed to a single point (no active selection).
    if (!sel_.Empty()) {
        if (caret_visible_) { HideCaret(hwnd_content_); caret_visible_ = false; }
        // Still scroll to follow the moving (active) end of a
        // shift-extended selection; but only when the offset actually
        // changed since the last follow, so wheel scrolling is never
        // fought by caret-follow.
        float sx, sy, sh;
        if (sel_.active.offset != last_scroll_offset_ &&
            layout_cache_.OffsetToCaretRect(sel_.active.offset, &sx, &sy, &sh)) {
            last_scroll_offset_ = sel_.active.offset;
            ScrollCaretIntoView(sy, sh);
        }
        return;
    }

    float x, y, h;
    if (layout_cache_.OffsetToCaretRect(sel_.active.offset, &x, &y, &h)) {
        // Keep the caret inside the viewport after keyboard navigation.
        // Only when the offset changed since the last follow: wheel and
        // trackpad scrolling move scrollY_ under a stationary caret,
        // and re-scrolling here would fight the user's scroll.
        if (sel_.active.offset != last_scroll_offset_) {
            last_scroll_offset_ = sel_.active.offset;
            ScrollCaretIntoView(y, h);
        }
        // Convert DIPs to physical pixels for the caret.
        float dpix = static_cast<float>(dpi_) / 96.0f;
        int cx = static_cast<int>(x * dpix);
        int cy = static_cast<int>((y - scrollY_) * dpix);
        // Recreate the caret if its height changed (e.g. cursor moved
        // from body text to a heading or vice versa).
        int newH = static_cast<int>(h * dpix);
        if (newH < 1) newH = 1;
        if (caret_height_ != newH) {
            DestroyCaret();
            caret_visible_ = false; // caret destroyed; must re-show
            CreateCaret(hwnd_content_, nullptr, 2, newH);
            caret_height_ = newH;
        }
        SetCaretPos(cx, cy);
        if (!caret_visible_) { ShowCaret(hwnd_content_); caret_visible_ = true; }
    } else {
        // OffsetToCaretRect failed (offset not in any block or block has
        // no layout). Hide the caret so it doesn't appear at a stale
        // position from a previous click.
        if (caret_visible_) { HideCaret(hwnd_content_); caret_visible_ = false; }
    }
}

std::string AppWindow::FindLinkAtOffset(uint32_t offset) const {
    // The parser records rendered link text plus its trailing caret boundary
    // for hit testing, not the Markdown delimiters around the link.
    for (const auto& n : doc_.nodes) {
        for (const auto& ib : n.children) {
            if (ib.kind != InlineKind::Link) continue;
            const uint32_t end = ib.srcOffset + ib.srcLength;
            if (offset >= ib.srcOffset && offset < end)
                return ib.url;
            if (offset == end && !layout_cache_.OffsetIsRendered(offset))
                return ib.url;
        }
        if (n.block != BlockKind::Table) continue;
        for (const auto& row : n.rows) {
            for (const auto& cell : row.cells) {
                for (const auto& link : cell.links) {
                    const uint32_t end = link.srcOffset + link.srcLength;
                    if (offset >= link.srcOffset && offset < end)
                        return link.url;
                    if (offset == end &&
                        !layout_cache_.OffsetIsRendered(offset))
                        return link.url;
                }
            }
        }
    }
    return {};
}

static void AppendCodePointUtf8(std::string& out, char32_t cp) {
    if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static std::string SlugifyAnchor(const std::string& value) {
    std::string result;
    bool pendingHyphen = false;
    for (size_t i = 0; i < value.size();) {
        const unsigned char b = static_cast<unsigned char>(value[i]);
        if (b < 0x80) {
            const char c = static_cast<char>(b);
            if (std::isspace(b) != 0) {
                pendingHyphen = !result.empty();
                ++i;
                continue;
            }
            if (std::ispunct(b) != 0 && c != '_' && c != '-') {
                ++i;
                continue;
            }
            if (pendingHyphen && !result.empty() && result.back() != '-') {
                result.push_back('-');
            }
            pendingHyphen = false;
            result.push_back((c >= 'A' && c <= 'Z') ?
                             static_cast<char>(c - 'A' + 'a') : c);
            ++i;
            continue;
        }
        uint32_t length = (b & 0xE0) == 0xC0 ? 2 :
                          (b & 0xF0) == 0xE0 ? 3 :
                          (b & 0xF8) == 0xF0 ? 4 : 1;
        if (i + length > value.size()) length = 1;
        if (pendingHyphen && !result.empty() && result.back() != '-') {
            result.push_back('-');
        }
        pendingHyphen = false;
        result.append(value, i, length);
        i += length;
    }
    while (!result.empty() && result.back() == '-') result.pop_back();
    return result;
}

void AppWindow::OpenLink(const std::string& url) {
    if (url.empty()) return;

    // Internal anchor link: #section or #heading-text
    if (url[0] == '#') {
        const std::string anchor = SlugifyAnchor(url.substr(1));
        for (const auto& n : doc_.nodes) {
            if (n.block != BlockKind::Heading) continue;
            std::string headingText;
            for (const auto& ib : n.children) {
                for (char32_t cp : ib.text) AppendCodePointUtf8(headingText, cp);
            }
            if (SlugifyAnchor(headingText) == anchor) {
                int blkIdx = layout_cache_.BlockForOffset(n.srcOffset);
                if (blkIdx >= 0) {
                    const auto& blocks = layout_cache_.Blocks();
                    StartSpring(blocks[blkIdx].y - 50.0f);
                }
                return;
            }
        }
        return;
    }

    // External link: open in default browser via ShellExecuteW.
    // Only allow http/https/mailto schemes; a crafted document could
    // otherwise invoke arbitrary URI handlers (file:, search-ms:, ...).
    std::string lowerUrl;
    for (char c : url) lowerUrl += (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
    bool safeScheme =
        lowerUrl.rfind("http://", 0) == 0 ||
        lowerUrl.rfind("https://", 0) == 0 ||
        lowerUrl.rfind("mailto:", 0) == 0;
    if (!safeScheme) return;

    // Convert UTF-8 URL to UTF-16.
    std::wstring wideUrl;
    for (size_t i = 0; i < url.size(); ) {
        unsigned char c = static_cast<unsigned char>(url[i]);
        if (c < 0x80) {
            wideUrl += static_cast<wchar_t>(c);
            i += 1;
        } else if (c < 0xC0) {
            i += 1; // continuation byte, skip
        } else if (c < 0xE0) {
            if (i + 1 < url.size()) {
                wchar_t ch = ((c & 0x1F) << 6) |
                    (static_cast<unsigned char>(url[i+1]) & 0x3F);
                wideUrl += ch;
                i += 2;
            } else { i += 1; }
        } else if (c < 0xF0) {
            if (i + 2 < url.size()) {
                uint32_t cp = ((c & 0x0F) << 12) |
                    ((static_cast<unsigned char>(url[i+1]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(url[i+2]) & 0x3F);
                wideUrl += static_cast<wchar_t>(cp);
                i += 3;
            } else { i += 1; }
        } else {
            if (i + 3 < url.size()) {
                uint32_t cp = ((c & 0x07) << 18) |
                    ((static_cast<unsigned char>(url[i+1]) & 0x3F) << 12) |
                    ((static_cast<unsigned char>(url[i+2]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(url[i+3]) & 0x3F);
                // Surrogate pair
                cp -= 0x10000;
                wideUrl += static_cast<wchar_t>(0xD800 + (cp >> 10));
                wideUrl += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
                i += 4;
            } else { i += 1; }
        }
    }
    ShellExecuteW(hwnd_, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void AppWindow::OnLButtonDown(HWND hwnd, int x, int y) {
    SetFocus(hwnd);
    // A click moves or extends the caret. Navigation continues from the new
    // caret position, which is the recommended behaviour for a word
    // processor (guide 30), and the bar reads it live.
    diag::Trace("OnLButtonDown enter");

    // Welcome screen: clicking a card opens that file.
    if (welcome_mode_) {
        D2D1_SIZE_F sz = rt_ ? rt_->GetSize() : D2D1::SizeF(800, 600);
        welcome_.Layout(sz.width, sz.height);
        float scale = 96.0f / static_cast<float>(dpi_);
        int idx = welcome_.HitTest(static_cast<float>(x) * scale,
                                    static_cast<float>(y) * scale);
        if (idx >= 0) {
            std::wstring path = welcome_.GetPath(idx);
            if (!path.empty()) {
                OpenFile(path);
            }
        }
        return;
    }

    last_selectall_tier_ = 0;
    SetCapture(hwnd);
    float scale = 96.0f / static_cast<float>(dpi_);
    float docX = static_cast<float>(x) * scale;
    float docY = static_cast<float>(y) * scale + scrollY_;

    // Ensure the layout cache is populated. The cache may be empty
    // if OnBufferChanged cleared it and WM_PAINT hasn't fired yet.
    if (layout_cache_.Blocks().empty()) {
        ForceRepaintNow();
    }

    bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    pending_bold_set_ = false;
    pending_italic_set_ = false;
    pending_run_active_ = false;
    pending_run_suffix_bytes_ = 0;
    word_dragging_ = false;
    paragraph_dragging_ = false;
    selection_dragging_ = false;
    text_drag_candidate_ = false;
    text_dragging_ = false;
    text_drag_start_ = 0;
    text_drag_length_ = 0;
    text_drag_last_x_ = x;
    text_drag_last_y_ = y;
    text_drag_down_x_ = x;
    text_drag_down_y_ = y;
    // The anchor stays where it is; only the active end moves. This
    // works for text clicks, margin clicks, and empty-space clicks.
    // Link-click behavior is suppressed when Shift is held.
    if (shiftDown) {
        // First, try to get a text offset at the click position.
        uint32_t offset = layout_cache_.PointToOffsetAtOrAfterBlock(docX, docY);
        if (offset != UINT32_MAX) {
            // Shift+click on text: extend selection to this offset.
            margin_selecting_ = false;
            sel_.active = {offset};
        } else {
            // Shift+click in the margin: extend selection to the
            // entire visual line at this y position.
            int blkIdx = layout_cache_.FindBlockAtY(docY);
            if (blkIdx >= 0) {
                uint32_t lineStart = 0, lineEnd = 0;
                float lineTopRel = 0.0f;
                bool gotLine = layout_cache_.GetLineRangeAtY(
                    blkIdx, docY, &lineStart, &lineEnd, &lineTopRel);
                if (gotLine) {
                    // Extend the selection to include this line.
                    // If the anchor is before the line, extend active
                    // to lineEnd; if after, extend to lineStart.
                    uint32_t anchorOff = sel_.anchor.offset;
                    if (anchorOff <= lineStart) {
                        sel_.active = {lineEnd};
                    } else {
                        sel_.active = {lineStart};
                    }
                } else {
                    const auto& blocks = layout_cache_.Blocks();
                    const auto& bl = blocks[blkIdx];
                    uint32_t anchorOff = sel_.anchor.offset;
                    if (anchorOff <= bl.srcOffset) {
                        sel_.active = {bl.srcOffset + bl.srcLength};
                    } else {
                        sel_.active = {bl.srcOffset};
                    }
                }
            } else {
                // Shift+click in empty space: snap to nearest end.
                sel_.active = {sel_.active.offset};
            }
        }
        if (editing_) UpdateCaretPosition();
        Repaint();
        return;
    }

    // Triple-click detection: if this click follows a double-click
    // within the system double-click time and at roughly the same
    // y position, treat it as a triple-click that selects the full
    // paragraph/block containing the clicked position.
    DWORD now = GetTickCount();
    DWORD dblClickTime = GetDoubleClickTime();
    if (click_count_ >= 2 &&
        (now - last_click_time_) <= dblClickTime &&
        abs(y - last_click_y_) < 5) {
        click_count_ = 0; // reset
        // Triple-click: select the full block/paragraph. Inside a table
        // cell the paragraph equivalent is the ROW (plan 19): the block
        // is the whole table, which only Ctrl+A tier 3 expands to.
        {
            uint32_t off = layout_cache_.PointToOffsetAtOrAfterBlock(docX, docY);
            if (off != UINT32_MAX) {
                TableCellRef cellRef;
                if (TableCellAtOffset(doc_, buffer_.Text(), off, &cellRef) &&
                    !cellRef.separatorRow && cellRef.rowIndex != SIZE_MAX) {
                    const Node& node = doc_.nodes[cellRef.tableIndex];
                    const TableRow& row = node.rows[cellRef.rowIndex];
                    uint32_t rowStart = UINT32_MAX;
                    uint32_t rowEnd = 0;
                    for (const TableCell& c : row.cells) {
                        rowStart = std::min(rowStart, c.srcOffset);
                        rowEnd = std::max(rowEnd, c.srcEnd);
                    }
                    const uint32_t textLen =
                        static_cast<uint32_t>(buffer_.Text().size());
                    if (rowEnd > rowStart) {
                        sel_.anchor = {rowStart};
                        sel_.active = {std::min(rowEnd, textLen)};
                        margin_selecting_ = false;
                        paragraph_dragging_ = true;
                        selection_dragging_ = false;
                        last_selectall_tier_ = 2;
                        last_selectall_caret_ = std::min(rowEnd, textLen);
                        if (editing_) UpdateCaretPosition();
                        Repaint();
                        return;
                    }
                }
            }
        }
        int blkIdx = layout_cache_.FindBlockAtY(docY);
        if (blkIdx < 0) {
            // Try via the offset.
            uint32_t off = layout_cache_.PointToOffsetAtOrAfterBlock(docX, docY);
            if (off != UINT32_MAX)
                blkIdx = layout_cache_.BlockForOffset(off);
        }
        if (blkIdx >= 0) {
            uint32_t blockStart = 0, blockEnd = 0;
            if (!layout_cache_.GetRenderedBlockRange(blkIdx,
                                                     &blockStart, &blockEnd)) {
                return;
            }
            sel_.anchor = {blockStart};
            sel_.active = {blockEnd};
            margin_selecting_ = false;
            paragraph_dragging_ = true;
            selection_dragging_ = false;
            paragraph_anchor_block_ = blkIdx;
            if (editing_) UpdateCaretPosition();
            Repaint();
            return;
        }
    }
    // Track click count for triple-click detection.
    if (click_count_ >= 2 ||
        (now - last_click_time_) > dblClickTime ||
        abs(y - last_click_y_) >= 5) {
        click_count_ = 1;
    } else {
        click_count_++;
    }
    last_click_time_ = now;
    last_click_y_ = y;

    // Link click handling: clicking a link opens it in both view and
    // edit mode. In edit mode, if the click is NOT on a link, the
    // caret is placed as usual.
    {
        uint32_t linkOffset = layout_cache_.PointToOffsetAtOrAfterBlock(docX, docY);
        if (linkOffset != UINT32_MAX) {
            std::string url = FindLinkAtOffset(linkOffset);
            if (!url.empty()) {
            if (!editing_ || (GetKeyState(VK_CONTROL) & 0x8000)) {
                OpenLink(url);
                return;
            }
            }
        }
    }

    uint32_t offset = layout_cache_.PointToOffsetAtOrAfterBlock(docX, docY);
    diag::TraceFmt("DOWN doc=(%.1f,%.1f) clicks=%d off=%u", docX, docY,
                   click_count_, offset);

    // Table projection (plan 51): identify the cell first, then the
    // caret inside it. Whitespace, cell padding and the gap between the
    // text and the pipes never move the caret to a neighbor cell.
    if (offset == UINT32_MAX) {
        // Missed all text blocks: if the click's y is inside the band of
        // a cached table cell row, project into that row's nearest cell.
        // Pick the cell block whose x-span contains the click; a click
        // in the padding beside a cell stays in that cell (plan 51).
        int rowBlock = layout_cache_.HitTestBlock(docX, docY);
        for (float dy = 1.0f; dy <= 6.0f && rowBlock < 0; dy += 1.0f) {
            rowBlock = layout_cache_.FindNearestBlockInRow(docX, docY - dy);
            if (rowBlock < 0)
                rowBlock = layout_cache_.FindNearestBlockInRow(docX,
                                                               docY + dy);
        }
        if (rowBlock >= 0) {
            const auto& bl = layout_cache_.Blocks()[
                static_cast<size_t>(rowBlock)];
            TableCellRef cellRef;
            if (TableCellAtOffset(doc_, buffer_.Text(), bl.srcOffset, &cellRef) &&
                !cellRef.separatorRow) {
                const std::string& text = buffer_.Text();
                offset = (docX <= (bl.x + bl.width * 0.5f))
                    ? std::min(cellRef.srcOffset,
                               static_cast<uint32_t>(text.size()))
                    : std::min(cellRef.srcEnd,
                               static_cast<uint32_t>(text.size()));
            }
        }
    } else {
        // A resolved offset can still sit on hidden cell syntax (the
        // closing pipe). Keep the caret inside the containing cell.
        TableCellRef cellRef;
        if (TableCellAtOffset(doc_, buffer_.Text(), offset, &cellRef) &&
            !cellRef.separatorRow) {
            const std::string& text = buffer_.Text();
            if (offset < cellRef.srcOffset)
                offset = std::min(cellRef.srcOffset,
                                  static_cast<uint32_t>(text.size()));
            else if (offset > cellRef.srcEnd)
                offset = std::min(cellRef.srcEnd,
                                  static_cast<uint32_t>(text.size()));
        }
    }

    // Check if the click is in the left margin (no text block hit at x).
    // If so, select the visual line at that y position (like Word).
    if (offset == UINT32_MAX) {
        // Click missed all text blocks; in the left margin.
        // Find the block at this y and select the single visual line.
        int blkIdx = layout_cache_.FindBlockAtY(docY);
        if (blkIdx >= 0) {
            uint32_t lineStart = 0, lineEnd = 0;
            float lineTopRel = 0.0f;
            bool gotLine = layout_cache_.GetLineRangeAtY(blkIdx, docY,
                    &lineStart, &lineEnd, &lineTopRel);
            if (gotLine) {
                sel_.anchor = {lineStart};
                sel_.active = {lineEnd};
            } else {
                if (!layout_cache_.GetRenderedBlockRange(blkIdx,
                                                         &lineStart, &lineEnd)) {
                    return;
                }
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
        // A press inside an existing selection starts an internal text drag.
        // Keep the selection until movement crosses the system threshold.
        if (editing_ && !sel_.Empty() && offset >= sel_.Start() &&
            offset < sel_.End()) {
            text_drag_candidate_ = true;
            text_drag_start_ = sel_.Start();
            text_drag_length_ = sel_.Length();
        } else {
            sel_.Collapse({offset});
            selection_dragging_ = true;
        }
    } else {
        // Click landed on empty space (no text block, no margin line).
        // Clear any active selection.
        sel_.Collapse({sel_.active.offset});
    }
    // Only update caret position in edit mode; in view mode we have no caret.
    if (editing_) UpdateCaretPosition();
    diag::Trace("OnLButtonDown end");
    Repaint();
}

void AppWindow::OnLButtonDblClk(HWND hwnd, int x, int y) {
    SetFocus(hwnd);
    if (welcome_mode_) return; // single-click handles welcome screen clicks
    // Track for triple-click: double-click counts as the 2nd click.
    click_count_ = std::max(click_count_, 2);
    last_click_time_ = GetTickCount();
    last_click_y_ = y;
    float scale = 96.0f / static_cast<float>(dpi_);
    float docX = static_cast<float>(x) * scale;
    float docY = static_cast<float>(y) * scale + scrollY_;
    uint32_t offset = layout_cache_.PointToOffsetAtOrAfterBlock(docX, docY);
    if (offset == UINT32_MAX) return;

    const std::string& text = buffer_.Text();
    if (offset > text.size()) return;
    // Inside a table cell a double-click selects the whole cell
    // (plan 19 tier 1); word granularity is for prose only.
    TableCellRef cellRef;
    if (TableCellAtOffset(doc_, buffer_.Text(), offset, &cellRef) &&
        !cellRef.separatorRow && cellRef.srcEnd > cellRef.srcOffset) {
        sel_.anchor = {cellRef.srcOffset};
        sel_.active = {std::min(cellRef.srcEnd,
                                static_cast<uint32_t>(text.size()))};
        word_anchor_start_ = cellRef.srcOffset;
        word_anchor_end_ = cellRef.srcEnd;
        word_anchor_caret_ = offset;
        word_dragging_ = true;
        paragraph_dragging_ = false;
        last_selectall_tier_ = 1;
        last_selectall_caret_ = static_cast<uint32_t>(text.size()) < cellRef.srcEnd
            ? static_cast<uint32_t>(text.size()) : cellRef.srcEnd;
        SetCapture(hwnd);
        if (editing_) UpdateCaretPosition();
        Repaint();
        return;
    }
    // Use the same Unicode word boundaries as Ctrl+Left/Ctrl+Right.
    // This operates on grapheme boundaries, not UTF-8 bytes.
    uint32_t start = 0;
    uint32_t end = 0;
    WordSpanAt(buffer_, offset, &start, &end);
    if (start == end && offset < text.size()) {
        end = NextGraphemeBoundary(text, offset);
    }
    while (start < end && !layout_cache_.OffsetIsRendered(start)) {
        uint32_t next = NextGraphemeBoundary(text, start);
        if (next <= start) break;
        start = next;
    }
    while (end > start && end <= text.size() &&
           !layout_cache_.OffsetIsRendered(
               PrevGraphemeBoundary(text, end))) {
        uint32_t prev = PrevGraphemeBoundary(text, end);
        if (prev >= end) break;
        end = prev;
    }

    diag::TraceFmt("DBLCLK off=%u span=[%u,%u) txtlen=%zu",
                   offset, start, end, text.size());
    sel_.anchor = {start};
    sel_.active = {end};
    word_anchor_start_ = start;
    word_anchor_end_ = end;
    word_anchor_caret_ = offset;
    word_dragging_ = true;
    paragraph_dragging_ = false;
    SetCapture(hwnd);
    if (editing_) UpdateCaretPosition();
    Repaint();
}

void AppWindow::OnMouseMove(HWND hwnd, int x, int y) {
    // Welcome screen: track hover for card highlight.
    if (welcome_mode_) {
        D2D1_SIZE_F sz = rt_ ? rt_->GetSize() : D2D1::SizeF(800, 600);
        welcome_.Layout(sz.width, sz.height);
        float scale = 96.0f / static_cast<float>(dpi_);
        int newHover = welcome_.HitTest(static_cast<float>(x) * scale,
            static_cast<float>(y) * scale);
        if (newHover != welcome_hover_) {
            welcome_hover_ = newHover;
            Repaint();
        }
        return;
    }

    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
    float scale = 96.0f / static_cast<float>(dpi_);
    int hitY = y;
    if (y < 0 || y > clientH_) {
        float distance = y < 0 ? static_cast<float>(-y)
                               : static_cast<float>(y - clientH_);
        float step = std::min(96.0f, std::max(24.0f, distance * 0.5f));
        if (y < 0) StartSpring(scrollY_ - step);
        else StartSpring(scrollY_ + step);
        hitY = std::max(0, std::min(clientH_, y));
    } else if (text_dragging_) {
        StopScrollAnimation();
    }
    float docX = static_cast<float>(x) * scale;
    float docY = static_cast<float>(hitY) * scale + scrollY_;

    // Ensure the layout cache is populated during drag operations.
    if (layout_cache_.Blocks().empty()) {
        ForceRepaintNow();
    }

    text_drag_last_x_ = x;
    text_drag_last_y_ = y;
    if (text_drag_candidate_ && !text_dragging_) {
        int dx = x - text_drag_down_x_;
        int dy = y - text_drag_down_y_;
        const int dragX = std::max(1, static_cast<int>(
            GetSystemMetrics(SM_CXDRAG) * dpi_ / 96));
        const int dragY = std::max(1, static_cast<int>(
            GetSystemMetrics(SM_CYDRAG) * dpi_ / 96));
        if (std::abs(dx) >= dragX || std::abs(dy) >= dragY) {
            text_dragging_ = true;
            word_dragging_ = false;
            paragraph_dragging_ = false;
            selection_dragging_ = false;
        }
    }
    if (text_dragging_) return;

    if (paragraph_dragging_ && paragraph_anchor_block_ >= 0) {
        int currentBlock = layout_cache_.FindBlockAtY(docY);
        if (currentBlock >= 0) {
            int first = std::min(paragraph_anchor_block_, currentBlock);
            int last = std::max(paragraph_anchor_block_, currentBlock);
            uint32_t firstStart = 0, ignoredEnd = 0;
            uint32_t ignoredStart = 0, lastEnd = 0;
            if (!layout_cache_.GetRenderedBlockRange(first, &firstStart,
                                                     &ignoredEnd) ||
                !layout_cache_.GetRenderedBlockRange(last, &ignoredStart,
                                                     &lastEnd)) {
                return;
            }
            if (currentBlock >= paragraph_anchor_block_) {
                sel_.anchor = {firstStart};
                sel_.active = {lastEnd};
            } else {
                sel_.anchor = {lastEnd};
                sel_.active = {firstStart};
            }
            if (editing_) UpdateCaretPosition();
            Repaint();
        }
        return;
    }

    uint32_t offset = layout_cache_.PointToOffsetAtOrAfterBlock(docX, docY);
    if (word_dragging_) {
        if (offset != UINT32_MAX) {
            if (offset >= word_anchor_caret_) {
                sel_.anchor = {word_anchor_start_};
                sel_.active = {MoveWordRight(buffer_, offset)};
            } else {
                sel_.anchor = {word_anchor_end_};
                sel_.active = {MoveWordLeft(buffer_, offset)};
            }
            if (editing_) UpdateCaretPosition();
            Repaint();
        }
        return;
    }

    if (margin_selecting_ && margin_anchor_block_ >= 0) {
        // Extending a margin selection by visual lines.
        // Find the block and visual line at the current y position.
        int blkIdx = layout_cache_.FindBlockAtY(docY);
        if (blkIdx >= 0) {
            uint32_t curStart = 0, curEnd = 0;
            bool gotLine = layout_cache_.GetLineRangeAtY(
                blkIdx, docY, &curStart, &curEnd);
            if (!gotLine) {
                if (!layout_cache_.GetRenderedBlockRange(blkIdx,
                                                         &curStart, &curEnd)) {
                    return;
                }
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

    offset = layout_cache_.PointToOffsetAtOrAfterBlock(docX, docY);
    if (offset != UINT32_MAX) {
        // Cell-boundary rule extended to drag (plan 18): when the
        // selection's fixed end sits inside a cell, the dragged end
        // never leaves that cell — text selection may not escape the
        // cell walls the way a click cannot.
        const uint32_t fixedEnd = sel_.anchor.offset <= offset
            ? sel_.anchor.offset : sel_.active.offset;
        TableCellRef dragCell;
        if (TableCellAtOffset(doc_, buffer_.Text(), fixedEnd, &dragCell) &&
            !dragCell.separatorRow) {
            const uint32_t textLen =
                static_cast<uint32_t>(buffer_.Text().size());
            if (offset < dragCell.srcOffset)
                offset = std::min(dragCell.srcOffset, textLen);
            else if (offset > dragCell.srcEnd)
                offset = std::min(dragCell.srcEnd, textLen);
        }
        sel_.active = {offset};
    }
    if (editing_) UpdateCaretPosition();
    Repaint();
}

void AppWindow::FinishTextDrag() {
    if (!text_dragging_ || !editing_) return;
    float scale = 96.0f / static_cast<float>(dpi_);
    float docX = static_cast<float>(text_drag_last_x_) * scale;
    float docY = static_cast<float>(text_drag_last_y_) * scale + scrollY_;
    uint32_t drop = layout_cache_.PointToOffsetAtOrAfterBlock(docX, docY);
    if (drop == UINT32_MAX) return;
    drop = layout_cache_.NormalizeToRenderedCaret(drop);
    if (drop == UINT32_MAX) return;

    // Table guard (plan 57): a drop inside a cell may not carry table
    // syntax — pipes or line breaks would corrupt the row, and a nested
    // table is unrepresentable. Fold the segment for cell drops;
    // anything that would still break the line is rejected outright.
    TableCellRef dropRef;
    const bool dropInTable = TableCellAtOffset(doc_, buffer_.Text(), drop,
                                               &dropRef);
    const bool dropInCell = dropInTable && !dropRef.separatorRow;
    std::string moved;
    uint32_t newStart = 0;
    if (!MoveTextRange(buffer_.Text(), text_drag_start_, text_drag_length_,
                       drop, &moved, &newStart)) return;

    uint32_t segmentLen = text_drag_length_;
    if (dropInTable && !dropInCell) {
        // The delimiter row is table syntax, not editable text: any
        // drop onto it is refused.
        return;
    }
    if (dropInCell) {
        std::string segment = moved.substr(newStart, segmentLen);
        if (segment.find('|') != std::string::npos) return;  // reject
        segment = SanitizePasteForTableCell(segment);
        if (segment.size() != segmentLen) {
            moved.replace(newStart, segmentLen, segment);
            segmentLen = static_cast<uint32_t>(segment.size());
        }
    }

    Selection before = sel_;
    std::string original = buffer_.Text();
    buffer_.Splice(0, static_cast<uint32_t>(original.size()), moved);
    Selection after;
    after.anchor = {newStart};
    after.active = {newStart + segmentLen};
    sel_ = after;

    UndoEntry entry{};
    entry.offset = 0;
    entry.removed = std::move(original);
    entry.inserted = std::move(moved);
    entry.selBefore = before;
    entry.selAfter = after;
    entry.timestamp = GetTickCount64();
    entry.type = EditType::Other;
    undo_stack_.Push(entry);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::OnLButtonUp(HWND hwnd) {
    FinishTextDrag();
    // The selection just settled; the marker toggle follows it. This
    // covers view mode, where no caret update runs to do the same.
    InvalidateMarkerToggleUI();
    ReleaseCapture();
    margin_selecting_ = false;
    margin_anchor_block_ = -1;
    word_dragging_ = false;
    paragraph_dragging_ = false;
    selection_dragging_ = false;
    text_drag_candidate_ = false;
    text_dragging_ = false;
    text_drag_start_ = 0;
    text_drag_length_ = 0;
    paragraph_anchor_block_ = -1;
}

// Create the system caret at the selection's active end and show it.
// This runs whenever focus or edit mode turns the caret on (SetEdit,
// OnSetFocus). When the layout is not ready yet, typically an empty
// document before its first paint, a 16 DIP caret stands in; the next
// UpdateCaretPosition after the paint resizes and repositions it.
// The flags always mirror reality: caret_visible_ true means a shown
// caret exists, so a focus loss that destroys the caret clears them.
bool AppWindow::EnsureCaretVisible() {
    if (!has_focus_ || !hwnd_content_) return false;
    // While a selection is shown, the caret is the selection's active
    // end, not a separate blinking line (cursor-blinking guide), so
    // focus changes never turn one on over a selection.
    if (!sel_.Empty()) return false;
    float x = 0.0f, y = 0.0f, h = 0.0f;
    const bool haveRect =
        layout_cache_.OffsetToCaretRect(sel_.active.offset, &x, &y, &h);
    if (!haveRect) h = 16.0f;  // fallback height, top of the viewport
    const float dpix = static_cast<float>(dpi_) / 96.0f;
    int height = static_cast<int>(h * dpix);
    if (height < 1) height = 1;
    CreateCaret(hwnd_content_, nullptr, 2, height);
    caret_height_ = height;
    if (haveRect) {
        SetCaretPos(static_cast<int>(x * dpix),
                    static_cast<int>((y - scrollY_) * dpix));
    } else {
        SetCaretPos(0, 0);  // defined origin until the layout exists
    }
    ShowCaret(hwnd_content_);
    caret_visible_ = true;
    return true;
}

void AppWindow::OnSetFocus(HWND hwnd) {
    has_focus_ = true;
    // The caret is visible while the editor has focus (cursor-blinking
    // guide), and only exists in edit mode: view mode has no caret.
    if (!editing_ || caret_visible_) return;
    EnsureCaretVisible();
}

void AppWindow::OnKillFocus(HWND hwnd) {
    OnImeEndComposition();
    has_focus_ = false;
    // The caret disappears with focus (cursor-blinking guide). The
    // flags reset with it: a later re-focus must recreate the caret
    // through EnsureCaretVisible instead of trusting stale state, and
    // UpdateCaretPosition must not skip recreation because an old
    // height still matches.
    DestroyCaret();
    caret_visible_ = false;
    caret_height_ = 0;
}

void AppWindow::OnImeComposition(LPARAM lp) {
    if (!editing_ || !hwnd_content_) return;
    HIMC context = ImmGetContext(hwnd_content_);
    if (!context) return;

    auto readComposition = [&](DWORD flag) -> std::wstring {
        LONG bytes = ImmGetCompositionStringW(context, flag, nullptr, 0);
        if (bytes <= 0) return {};
        std::wstring value(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
        ImmGetCompositionStringW(context, flag, value.data(), bytes);
        return value;
    };

    bool hasResult = (lp & GCS_RESULTSTR) != 0;
    std::string result;
    if (hasResult) {
        result = ImeWideToUtf8(readComposition(GCS_RESULTSTR));
    }
    std::string preedit;
    if (lp & GCS_COMPSTR) {
        preedit = ImeWideToUtf8(readComposition(GCS_COMPSTR));
    }

    if (!ime_composing_) {
        ime_selection_before_ = sel_;
        uint32_t sourceLength = static_cast<uint32_t>(buffer_.Text().size());
        ime_source_start_ = std::min(sel_.Start(), sourceLength);
        uint32_t replaceLength = std::min(sel_.Length(),
                                          sourceLength - ime_source_start_);
        ime_replaced_text_ = buffer_.Text().substr(
            ime_source_start_, replaceLength);
        if (replaceLength != 0) {
            buffer_.Splice(ime_source_start_, replaceLength, {});
            sel_.Collapse({ime_source_start_});
        }
        ime_composing_ = true;
    }

    if (!ime_preedit_.empty()) {
        buffer_.Splice(ime_source_start_,
            static_cast<uint32_t>(ime_preedit_.size()), {});
        ime_preedit_.clear();
    }

    if (hasResult) {
        buffer_.Splice(ime_source_start_, 0, result);
        sel_.Collapse({ime_source_start_ + static_cast<uint32_t>(result.size())});

        UndoEntry entry{};
        entry.offset = ime_source_start_;
        entry.removed = ime_replaced_text_;
        entry.inserted = result;
        entry.selBefore = ime_selection_before_;
        entry.selAfter = sel_;
        entry.timestamp = GetTickCount64();
        entry.type = EditType::Insert;
        undo_stack_.Push(entry);

        ime_composing_ = false;
        ime_replaced_text_.clear();
        ime_preedit_.clear();
        OnBufferChanged();
    } else {
        ime_preedit_ = preedit;
        if (!ime_preedit_.empty()) {
            buffer_.Splice(ime_source_start_, 0, ime_preedit_);
            sel_.Collapse({ime_source_start_ +
                static_cast<uint32_t>(ime_preedit_.size())});
        } else {
            sel_.Collapse({ime_source_start_});
        }
        bool wasDirty = dirty_;
        OnBufferChanged();
        if (!wasDirty) dirty_ = false;
    }
    ImmReleaseContext(hwnd_content_, context);
}

void AppWindow::OnImeEndComposition() {
    if (!ime_composing_) return;
    if (!ime_preedit_.empty()) {
        buffer_.Splice(ime_source_start_,
            static_cast<uint32_t>(ime_preedit_.size()), {});
    }
    buffer_.Splice(ime_source_start_, 0, ime_replaced_text_);
    sel_ = ime_selection_before_;
    ime_composing_ = false;
    ime_preedit_.clear();
    ime_replaced_text_.clear();
    bool wasDirty = dirty_;
    OnBufferChanged();
    if (!wasDirty) dirty_ = false;
}

void AppWindow::OnChar(HWND hwnd, wchar_t ch) {
    if (!has_focus_) return;

    // In view mode, no character input is accepted.
    // Edit mode is entered explicitly via the Edit button or Ctrl+E.
    if (!editing_) return;

    // Keep one continuous Markdown span while the caret remains at the end
    // of text typed with pending formatting. Reusing the same closing markers
    // avoids producing **a****b****c** for a continuous input sequence.
    auto ApplyPendingTypingFormat = [&](const std::string& value) -> void {
        std::string prefix;
        std::string suffix;
        if (pending_bold_set_ && pending_bold_) {
            prefix += "**";
            suffix = "**" + suffix;
        }
        if (pending_italic_set_ && pending_italic_) {
            prefix += "*";
            suffix = "*" + suffix;
        }

        if (value.empty()) {
            pending_run_active_ = false;
            pending_run_suffix_bytes_ = 0;
            return;
        }
        if (suffix.empty()) {
            editor_.InsertText(value);
            pending_run_active_ = false;
            pending_run_suffix_bytes_ = 0;
            return;
        }

        const std::string& source = buffer_.Text();
        const uint32_t caret = sel_.active.offset;
        const uint32_t suffixBytes = static_cast<uint32_t>(suffix.size());
        const bool canExtend = pending_run_active_ && sel_.Empty() &&
            pending_run_suffix_bytes_ == suffixBytes &&
            caret >= suffixBytes &&
            source.compare(caret - suffixBytes, suffixBytes, suffix) == 0;

        if (canExtend) {
            editor_.ReplaceTextRange(caret - suffixBytes, suffixBytes,
                                      value + suffix, EditType::Insert);
        } else {
            editor_.InsertText(prefix + value + suffix);
        }
        pending_run_active_ = true;
        pending_run_suffix_bytes_ = suffixBytes;
        return;
    };

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
        ins = EscapeForInsert(buffer_, sel_.active.offset, ins,
                              IsOffsetInTable(doc_, sel_.active.offset));
        ApplyPendingTypingFormat(ins);
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
    ins2 = EscapeForInsert(buffer_, sel_.active.offset, ins2,
                           IsOffsetInTable(doc_, sel_.active.offset));
    ApplyPendingTypingFormat(ins2);
    if (!ins2.empty() && !ins2.empty()) CheckAutoformat(&buffer_, &sel_, ins2[0]);
    OnBufferChanged();
}

// Standalone diagram-file support (.mmd = mermaid source, .svg =
// plain SVG). Neither is markdown on disk: the buffer shows the
// content wrapped in a fence (```mermaid / ```svg) so the regular
// markdown pipeline parses and renders it as a diagram; saving
// unwraps the fence again, leaving the file on disk unchanged.

// Case-insensitive check whether the path ends with the extension
// (including the dot, e.g. L".mmd").
static bool EndsWithExtension(const std::wstring& path,
                               const wchar_t* ext) {
    const size_t n = wcslen(ext);
    if (path.size() < n) return false;
    const wchar_t* tail = path.c_str() + path.size() - n;
    for (size_t i = 0; i < n; i++) {
        if (towlower(tail[i]) != towlower(ext[i])) return false;
    }
    return true;
}

// Does the text already contain a ```<lang> fence? Used both to
// detect user-provided fences in a wrapped file and (indirectly)
// to keep the wrap idempotent.
static bool HasLangFence(const std::string& text,
                         const std::string& lang) {
    size_t i = 0;
    while (i < text.size()) {
        size_t eol = text.find('\n', i);
        std::string line = text.substr(i,
            (eol == std::string::npos ? text.size() : eol) - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Accept leading indentation of up to 3 spaces (CommonMark).
        size_t b = 0;
        while (b < line.size() && b < 3 && line[b] == ' ') b++;
        if (line.compare(b, 3, "```") == 0) {
            std::string info = line.substr(b + 3);
            if (!info.empty() && info[0] == ' ') info.erase(0, info.find_first_not_of(" \t"));
            if (info == lang) return true;
        }
        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    return false;
}

// Wrap bare source of the given language in a fence that the
// markdown parser promotes to a rendered block.
static std::string WrapLangFence(const std::string& lang,
                                 const std::string& text) {
    std::string out = "```" + lang + "\n";
    out += text;
    if (out.empty() || out.back() != '\n') out += '\n';
    if (text.empty()) out += '\n';  // keep the code block non-empty
    out += "```\n";
    return out;
}

// Strip the synthetic fence added by WrapLangFence. Removes at
// most one opening fence right at the top and a matching closing
// fence at the end, so content that legitimately contains other
// fences stays intact when the wrap was skipped.
static std::string UnwrapLangFence(const std::string& lang,
                                   const std::string& text) {
    const std::string openingFence = "```" + lang;
    const size_t fl = openingFence.size();
    size_t b = 0;
    while (b < text.size() && text[b] == '\n') b++;
    bool opening = (text.size() >= b + fl &&
        text.compare(b, fl, openingFence) == 0 &&
        (b + fl == text.size() || text[b + fl] == '\n' ||
         text[b + fl] == '\r'));
    if (!opening) return text;
    size_t i = b + fl;
    if (i < text.size() && (text[i] == '\n' || text[i] == '\r')) i++;
    if (i < text.size() && text[i - 1] == '\r' && text[i] == '\n') i++;
    size_t j = text.find("```", i);
    // Closing fence must sit at a line start to be ours.
    while (j != std::string::npos) {
        if (j == 0 || text[j - 1] == '\n') break;
        j = text.find("```", j + 1);
    }
    if (j == std::string::npos) return text;
    size_t e = j + 3;
    if (e < text.size() && text[e] == '\r') e++;
    if (e < text.size() && text[e] == '\n') e++;
    std::string inner = text.substr(i, j - i);
    // Empty file: wrap added one blank line; drop it on unwrap so
    // an empty file stays empty.
    if (inner == "\n") return std::string();
    return inner;
}

void AppWindow::OpenFile(const std::wstring& path) {
    // Exit edit mode: destroy caret, invalidate ribbon state.
    if (editing_) {
        SetEdit(false);
    }
    welcome_mode_ = false; // leaving welcome screen

    // Track this file in the recent list.
    AddRecentFile(settings_, path);
    SaveSettings(settings_);


    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open()) {
        MessageBoxW(hwnd_, L"Could not open file", L"MarkDownIt", MB_ICONWARNING);
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string raw = ss.str();
    // Same load path as the .docx import: BOM, line endings, the .mmd/.svg
    // fence, parse, buffer swap, view reset, watcher.
    LoadDocumentText(raw, path);
}

// Load UTF-8 markdown into the editor: the sequence OpenFile has always run
// after it read a file (BOM strip, line endings, the .mmd/.svg fence, parse,
// buffer swap, view reset, watcher). The .docx import loads its converted
// markdown the same way. path is the file the text came from; an empty path
// means the text has no file on disk yet, so the document is untitled,
// nothing is watched and Save falls back to Save As. The caller ends edit
// mode and tracks recent files; this function only loads.
void AppWindow::LoadDocumentText(const std::string& raw, const std::wstring& path) {
    welcome_mode_ = false;  // a document is in the editor


    // Detect BOM (UTF-8 BOM: EF BB BF)
    has_bom_ = (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF);
    std::string utf8 = has_bom_ ? raw.substr(3) : raw;

    // Standalone Mermaid file (.mmd): the source is bare diagram
    // commands. Wrap it in a ```mermaid fence so the normal markdown
    // pipeline parses and renders it as a diagram. DoSave unwraps
    // the fence again so the file on disk keeps its original form.
    is_mmd_ = !path.empty() && EndsWithExtension(path, L".mmd");
    is_svg_ = !path.empty() && EndsWithExtension(path, L".svg");
    // A ```svg fence is a picture only in a standalone .svg file. In a .md
    // file the author wrote that fence as code, so it renders as code.
    renderer_.SetStandaloneSvg(is_svg_);
    mmd_wrapped_ = false;
    if (is_mmd_ && !HasLangFence(utf8, "mermaid")) {
        utf8 = WrapLangFence("mermaid", utf8);
        mmd_wrapped_ = true;
    }
    svg_wrapped_ = false;
    if (is_svg_ && !HasLangFence(utf8, "svg")) {
        utf8 = WrapLangFence("svg", utf8);
        svg_wrapped_ = true;
    }

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
    find_bar_.state().Invalidate();
    renderer_.ClearSearchMatches();
    layout_cache_.Clear();
    renderer_.ClearSvgCache();
    // Update source text pointers (buffer may have been reallocated).
    layout_cache_.SetSourceText(&buffer_.Text());
    renderer_.SetSourceText(&buffer_.Text());

    std::wstring title = L"MarkDownIt";
    if (!path.empty()) {
        size_t slash = path.find_last_of(L"\\/");
        std::wstring base = (slash != std::wstring::npos)
            ? path.substr(slash + 1) : path;
        if (!base.empty()) title = L"MarkDownIt - " + base;
    }
    SetWindowTextW(hwnd_, title.c_str());

    UpdateScrollInfo();
    // Load the marker sidecar for this document and resolve its anchors
    // against the fresh text (marker guide 12.7).
    LoadMarkersForDocument();
    // Force render target recreation; the D2D hwnd target can become
    // invalid after the GetOpenFileNameW modal dialog closes.
    SafeRelease(rt_);
    RedrawWindow(hwnd_content_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);

    // An untitled buffer has no file to watch.
    if (!path.empty()) {
        watcher_.Start(hwnd_, path);
    } else {
        watcher_.Stop();
    }
}

void AppWindow::Reload() {
    if (file_path_.empty()) return;
    find_bar_.state().Invalidate();
    renderer_.ClearSearchMatches();
    std::ifstream f(file_path_.c_str(), std::ios::binary);
    if (!f.is_open()) return;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string raw = ss.str();

    // Same load path as OpenFile: strip BOM and detect line endings,
    // otherwise a saved reload writes a double BOM.
    has_bom_ = (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF);
    std::string utf8 = has_bom_ ? raw.substr(3) : raw;
    // Re-apply the .mmd fence wrap (matches OpenFile: the disk file
    // is bare mermaid, the buffer shows it fenced).
    mmd_wrapped_ = false;
    if (is_mmd_ && !HasLangFence(utf8, "mermaid")) {
        utf8 = WrapLangFence("mermaid", utf8);
        mmd_wrapped_ = true;
    }
    svg_wrapped_ = false;
    if (is_svg_ && !HasLangFence(utf8, "svg")) {
        utf8 = WrapLangFence("svg", utf8);
        svg_wrapped_ = true;
    }
    use_crlf_ = (utf8.find("\x0D\x0A") != std::string::npos);

    float savedY = scrollY_;
    doc_ = Document{};
    ParseMarkdown(utf8, doc_);
    buffer_.SetText(utf8);
    // The buffer pointer may have been reallocated; stale layouts, SVG
    // documents and decoded bitmaps are all bound to the old content.
    layout_cache_.Clear();
    renderer_.ClearSvgCache();
    layout_cache_.SetSourceText(&buffer_.Text());
    renderer_.SetSourceText(&buffer_.Text());
    undo_stack_.Clear();
    UpdateScrollInfo();
    if (scrollY_ > savedY) scrollY_ = savedY;
    UpdateScrollInfo();
    // A reload re-reads the file but keeps the document identity, so the
    // marker sidecar is re-loaded and re-resolved like a fresh open.
    LoadMarkersForDocument();
    
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
        if (result != IDNO) return; // IDYES = keep mine
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

    // Load settings FIRST; LoadSampleDoc (below) needs recentFiles.
    settings_ = LoadSettings();

    // Zoom is a global view setting, not per-document state: restore
    // the factor persisted by the previous session so it carries across
    // launches and across documents within a session. This must happen
    // BEFORE EnsureRenderer (below) because Renderer::Init bakes the
    // current zoom into every text format it creates; ApplyZoom
    // recreates the renderer for the same reason on live zoom changes.
    renderer_.SetZoom(settings_.zoomFactor);

    D2D1_FACTORY_OPTIONS opts = {};
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        &opts,
        reinterpret_cast<void**>(&d2d_factory_));
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
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS,
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
    dpi_ = GetWindowDpi(hwnd_);
    rt_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));

    // Try to get ID2D1DeviceContext5 for SVG support (Windows 10 Creators Update+).
    // If QI fails, the app runs without SVG; diagrams fall back to code blocks.
    if (rt_) {
        HRESULT qi = rt_->QueryInterface(
            __uuidof(ID2D1DeviceContext5),
            reinterpret_cast<void**>(&d2d_ctx5_));
        if (FAILED(qi)) {
            d2d_ctx5_ = nullptr;
        }
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

    // Horizontal range: the content column's width at the current zoom
    // against the viewport width. Content narrower than the viewport
    // means no horizontal scrolling at all, which is what keeps the page
    // centred at low zoom.
    float clientWDip = static_cast<float>(clientW_);
    if (rt_) {
        D2D1_SIZE_F rtSize = rt_->GetSize();
        clientWDip = rtSize.width;
    }
    // The scroll range has to describe the column as actually drawn, not
    // the uncapped width mode value: an over-wide range lets the user
    // scroll into empty space past the end of the content (zoom guide
    // section 4).
    const float contentW = renderer_.ContentWidthDip(clientWDip);
    float maxScrollX = 0.0f;
    if (contentW > clientWDip && clientWDip > 0.0f) {
        maxScrollX = contentW - clientWDip;
    }
    if (scrollX_ < 0.0f) scrollX_ = 0.0f;
    if (scrollX_ > maxScrollX) scrollX_ = maxScrollX;

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = static_cast<int>(totalH_);
    si.nPage  = static_cast<UINT>(clientHDip > 0 ? clientHDip : 1);
    si.nPos   = static_cast<int>(scrollY_);
    SetScrollInfo(hwnd_content_, SB_VERT, &si, TRUE);

    SCROLLINFO sih = {};
    sih.cbSize = sizeof(sih);
    sih.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    sih.nMin   = 0;
    sih.nMax   = static_cast<int>(contentW);
    sih.nPage  = static_cast<UINT>(clientWDip > 0.0f ? clientWDip : 1);
    sih.nPos   = static_cast<int>(scrollX_);
    SetScrollInfo(hwnd_content_, SB_HORZ, &sih, TRUE);
}

void AppWindow::ResizeContentWindow() {
    if (!hwnd_ || !hwnd_content_) return;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    int contentY = static_cast<int>(g_ribbonHeight);
    int contentH = rc.bottom - contentY;

    // The docked find strip claims its height while it is open, so no line
    // of text hides behind the bar. Hiding the bar gives the height back.
    if (find_bar_.IsVisible() && find_bar_.IsCreated()) {
        const int stripH = find_bar_.HeightPx();
        if (stripH < contentH) contentH -= stripH;
    }
    if (contentH < 1) contentH = 1;

    SetWindowPos(hwnd_content_, nullptr,
        0, contentY,
        rc.right - rc.left, contentH,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // The strip is part of this layout, so it is re-docked here rather than
    // from a message handler of its own.
    find_bar_.Dock();
}

void AppWindow::OnSize(HWND hwnd, int width, int height) {
    ResizeContentWindow();
    // Debounce-save window placement so bounds persist even if the app
    // crashes or is killed (not just on clean shutdown).
    SetTimer(hwnd_, 5, 500, nullptr);
}

void AppWindow::OnContentSize(HWND hwnd, int width, int height) {
    clientH_ = height;
    clientW_ = width;
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
// 1. SPRING - mouse wheel, arrow keys, scrollbar line/page clicks.
//    spring_target_ is the rest position; each tick applies:
//       force   = (target − pos) × stiffness
//       vel    += force − damping × vel
//       pos    += vel
//    Critically damped → smooth ease-out, multi-notch accumulation.
//
// 2. TRACKPAD - precision touchpad / high-frequency wheel.
//    Each WM_MOUSEWHEEL event applies its delta immediately (1:1)
//    and records an exponential moving average of recent velocity.
//    The timer runs but does nothing while events arrive.
//    When events stop for >80 ms, → MOMENTUM.
//
// 3. MOMENTUM - fingers lifted, scroll glides with friction.
//    pos += vel; vel *= friction (0.955 per 16 ms tick).
//    Stops when |vel| < 0.5 or hit an edge.
//
// SB_THUMBTRACK (scrollbar drag) bypasses all physics; instant jump.
// ─────────────────────────────────────────────────────────────────

// Spring constants (tuned for ~300 ms settle with critical damping).
static const float SPRING_STIFFNESS = 0.12f; // per 16ms tick (stiffer = faster settle)
static const float SPRING_DAMPING   = 0.45f;  // per 16ms tick (higher = less oscillation)
static const float SPRING_SETTLE    = 0.5f;   // stop when within 0.5px of target
static const float SPRING_MIN_VEL   = 0.3f;   // stop when velocity below this

// Mouse wheel: pixels per notch (WHEEL_DELTA = 120).
static const float WHEEL_STEP_PX = 120.0f;

// Trackpad: scale raw delta to screen DIPs.
static const float TRACKPAD_SCALE = 0.75f;

// Momentum friction: vel *= FRICTION each 16ms tick.
// 0.955 ≈ 3.5% velocity loss per tick ≈ ~2 s glide from 20 px/tick.
static const float MOMENTUM_FRICTION = 0.955f;
static const float MOMENTUM_MIN_VEL  = 0.5f;    // stop threshold (px/tick)

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
    scroll_phase_ = 0; // IDLE
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
    scroll_phase_ = 1; // SPRING
    EnsureScrollTimer();
}

// Trackpad scroll: apply delta immediately (1:1) and track velocity.
void AppWindow::BeginTrackpadScroll(float delta, DWORD now) {
    // Cancel any spring/momentum; trackpad takes over.
    scroll_phase_ = 2; // TRACKPAD
    spring_target_ = scrollY_; // no spring while trackpad is active

    // Apply delta immediately (1:1, no animation latency).
    float prev = scrollY_;
    scrollY_ = ClampScroll(scrollY_ + delta);
    float actual = scrollY_ - prev;

    // Track velocity as exponential moving average of recent deltas.
    // Use the actual (clamped) delta so hitting an edge zeroes velocity.
    // If same direction, blend smoothly; if reversed, snap.
    if ((momentum_vel_ > 0) != (delta > 0)) {
        momentum_vel_ = actual; // direction reversed → snap
    } else {
        momentum_vel_ = momentum_vel_ * 0.6f + actual * 0.4f; // EMA
    }

    // Start the timer so it can detect when trackpad events stop
    // (→ transition to momentum when idle for >80ms).
    EnsureScrollTimer();

    UpdateScrollInfo();
    Repaint();
}

// Transition from TRACKPAD idle to MOMENTUM (fingers lifted after flick).
void AppWindow::EnterMomentum() {
    if (std::fabs(momentum_vel_) < MOMENTUM_MIN_VEL) {
        scroll_phase_ = 0; // IDLE
        StopScrollTimer();
        return;
    }
    scroll_phase_ = 3; // MOMENTUM
    EnsureScrollTimer();
}

// Called every 16 ms by the scroll timer.
void AppWindow::OnScrollTick() {
    // Continue semantic selection while the pointer is held outside the
    // content viewport. WM_MOUSEMOVE is not guaranteed to repeat there.
    if ((margin_selecting_ || word_dragging_ || paragraph_dragging_ ||
         selection_dragging_ || text_dragging_) &&
        GetCapture() == hwnd_content_ &&
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(hwnd_content_, &pt);
        if (pt.y < 0 || pt.y > clientH_)
            OnMouseMove(hwnd_content_, pt.x, pt.y);
    }

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

        // Clamp at edges; stop dead (no bounce).
        float prev = scrollY_;
        scrollY_ = ClampScroll(scrollY_);
        if (scrollY_ != prev) {
            momentum_vel_ = 0.0f; // hit edge
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
        case SB_PAGEUP:        targetY = scrollY_ - page; break;
        case SB_PAGEDOWN:      targetY = scrollY_ + page; break;
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

// Horizontal scrolling. No spring: a horizontal drag maps straight to an
// offset, and the vertical spring is tuned for wheel momentum, which would
// feel wrong when dragging a scrollbar thumb sideways.
void AppWindow::OnContentHScroll(HWND hwnd, int code, int pos) {
    (void)pos;
    float page = static_cast<float>(clientW_ > 0 ? clientW_ : 1);
    if (rt_) {
        D2D1_SIZE_F rtSize = rt_->GetSize();
        page = rtSize.width;
    }

    float targetX = scrollX_;
    switch (code) {
        case SB_LINEUP:        targetX = scrollX_ - 40.0f; break;
        case SB_LINEDOWN:      targetX = scrollX_ + 40.0f; break;
        case SB_PAGEUP:        targetX = scrollX_ - page * 0.9f; break;
        case SB_PAGEDOWN:      targetX = scrollX_ + page * 0.9f; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_HORZ, &si);
            scrollX_ = static_cast<float>(si.nTrackPos);
            if (scrollX_ < 0.0f) scrollX_ = 0.0f;
            UpdateScrollInfo();
            Repaint();
            UpdateCaretPosition();
            return;
        }
        case SB_LEFT:  targetX = 0.0f; break;
        case SB_RIGHT: targetX = renderer_.ContentWidthDip(page); break;
    }

    if (targetX < 0.0f) targetX = 0.0f;
    scrollX_ = targetX;
    UpdateScrollInfo();
    Repaint();
    UpdateCaretPosition();
}

// ── Mouse wheel / trackpad ────────────────────────────────────────
void AppWindow::OnContentMouseWheel(HWND hwnd, int delta) {
    DWORD now = GetTickCount();
    last_wheel_time_ = now;

    // Distinguish trackpad (small deltas) from mouse wheel (WHEEL_DELTA=120).
    // A precision trackpad sends small deltas (1-30) in rapid succession.
    // Only use delta magnitude; timing alone catches fast mouse scrolling.
    bool isTrackpad = (std::abs(delta) < WHEEL_DELTA);

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

    // Accumulate into the spring target; rapid notches build up speed.
    if (scroll_phase_ == 1) {
        spring_target_ = ClampScroll(spring_target_ + deltaScroll);
    } else {
        StartSpring(scrollY_ + deltaScroll);
    }
}

void AppWindow::RecreateRenderTarget() {
    SafeRelease(d2d_ctx5_);
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
        if (rt_) {
            rt_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
            HRESULT qi = rt_->QueryInterface(
                __uuidof(ID2D1DeviceContext5),
                reinterpret_cast<void**>(&d2d_ctx5_));
            if (FAILED(qi)) {
                d2d_ctx5_ = nullptr;
            }
        }
    }
    // The renderer holds its own copy of the device context (for SVG and
    // the diagram paths). Republish it, or it keeps pointing at the
    // released context (use-after-free on the next SVG paint). Cached
    // SVG documents and inline bitmaps were created against the old
    // target, so drop them too.
    renderer_.SetD2DDeviceContext5(d2d_ctx5_);
    renderer_.ClearSvgCache();
}

void AppWindow::UpdateDpi() {
    UINT newDpi = GetWindowDpi(hwnd_);
    if (newDpi == dpi_ && rt_) return;
    dpi_ = newDpi;
    if (rt_) {
        rt_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
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
        if (source_view_) {
            totalH_ = renderer_.MeasureSourceView(
                dw_factory_, buffer_.Text(), size.width, 0.0f);
        } else {
            totalH_ = renderer_.Measure(dw_factory_, doc_, size.width, 0.0f);
        }
        UpdateScrollInfo();
    }

    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(D2D1::ColorF::White));

    if (renderer_inited_ && dw_factory_) {
        D2D1_SIZE_F size = rt_->GetSize();
        if (source_view_) {
            // RenderSourceView doesn't clear the cache itself.
            layout_cache_.Clear();
            renderer_.RenderSourceView(rt_, dw_factory_, buffer_.Text(),
                size.width, scrollY_, 0.0f, scrollX_, &sel_);
        } else {
            renderer_.Render(rt_, dw_factory_, doc_, size.width, scrollY_,
                0.0f, scrollX_, &sel_);
        }
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

    // Standalone .mmd documents are edited inside a synthetic
    // ```mermaid fence (see OpenFile). The file on disk is bare
    // mermaid source, so unwrap the fence before writing.
    if (is_mmd_ && mmd_wrapped_) {
        content = UnwrapLangFence("mermaid", content);
    }
    if (is_svg_ && svg_wrapped_) {
        content = UnwrapLangFence("svg", content);
    }

    // Detect and strip BOM if present on load, add it back on save.
    // Determine line endings.
    std::string out;
    if (has_bom_) {
        out += (char)0xEF;
        out += (char)0xBB;
        out += (char)0xBF;
    }
    if (use_crlf_) {
        // Convert any line ending to CRLF while preserving existing CRLF:
        // CR followed by LF is passed through as a pair, a lone LF (or a
        // lone CR at end of input) becomes CRLF.
        for (size_t i = 0; i < content.size(); i++) {
            if (content[i] == 0x0D) {
                out += (char)0x0D; out += (char)0x0A;
                if (i + 1 < content.size() && content[i + 1] == 0x0A) i++;
            } else if (content[i] == 0x0A) {
                out += (char)0x0D; out += (char)0x0A;
            } else {
                out += content[i];
            }
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
    if (fwrite(out.data(), 1, out.size(), fp) != out.size()) {
        // Partial write (disk full etc.): do not rename over the target.
        fclose(fp);
        DeleteFileW(tempPath.c_str());
        watcher_.Start(hwnd_, file_path_);
        return false;
    }
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
    ofn.lpstrFilter = L"Markdown (*.md)\0*.md\0Mermaid (*.mmd)\0*.mmd\0SVG (*.svg)\0*.svg\0All Files (*.*)\0*.*\0";
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

// Save dialog for an export: the same shape as SaveDialog, with the
// export's own filter, extension and a default name derived from the
// current document. OFN_OVERWRITEPROMPT makes the dialog ask before an
// existing file is replaced.
std::wstring AppWindow::ExportSaveDialog(const wchar_t* filter, const wchar_t* defExt,
                                        const std::wstring& defaultName) {
    wchar_t szFile[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!defaultName.empty() && defaultName.size() < static_cast<size_t>(MAX_PATH)) {
        wcscpy_s(szFile, MAX_PATH, defaultName.c_str());
    }
    if (GetSaveFileNameW(&ofn)) return szFile;
    return {};
}

// Default target for an export of the current document: the document's own
// path with the export's extension instead of the markdown one, so the
// dialog opens beside the file the user is editing. A document with no
// file on disk gets "document" plus the extension.
std::wstring AppWindow::ExportDefaultName(const wchar_t* ext) const {
    if (!file_path_.empty()) {
        // Replace the markdown extension, and only that one: a dot in a
        // directory name stays part of the path.
        std::wstring base = file_path_;
        const size_t dot = base.find_last_of(L'.');
        const size_t slash = base.find_last_of(L"\\/");
        if (dot != std::wstring::npos &&
            (slash == std::wstring::npos || dot > slash)) {
            base = base.substr(0, dot);
        }
        if (!base.empty()) return base + ext;
    }
    return L"document" + std::wstring(ext);
}

int AppWindow::PromptSaveDiscardCancel() {
    return MessageBoxW(hwnd_,
        L"The document has unsaved changes. Save before continuing?",
        L"MarkDownIt", MB_YESNOCANCEL | MB_ICONQUESTION);
}

void AppWindow::OnClose() {
    if (dirty_) {
        int result = PromptSaveDiscardCancel();
        if (result == IDCANCEL) return;
        if (result == IDYES) {
            if (!Save()) return; // save failed or cancelled, don't close
        }
        // IDNO: discard, proceed to close
    }
    DestroyWindow(hwnd_);
}

void AppWindow::NewDocument() {
    if (dirty_) {
        int result = PromptSaveDiscardCancel();
        if (result == IDCANCEL) return;
        if (result == IDYES) {
            if (!Save()) return; // save failed or cancelled, keep document
        }
        // IDNO: discard changes, continue with the new document
    }
    // Exit edit mode like OpenFile does: destroys the caret and
    // invalidates the ribbon format state.
    if (editing_) SetEdit(false);
    // Empty path means untitled: nothing watched, Save falls back to
    // Save As, title shows no file name (LoadDocumentText handles all
    // of this: buffer swap, view reset, watcher, title).
    LoadDocumentText("", L"");
    // A new document is for writing; enter edit mode so the caret is
    // ready without a trip to the Edit toggle first.
    SetEdit(true);
}

void AppWindow::SetEdit(bool on) {
    editing_ = on;
    // Find works in every view, but Replace only makes sense where the
    // document can change. Re-gate whenever edit mode flips, so a bar left
    // open across the toggle greys the two buttons out (guide 4).
    find_bar_.SetReplaceEnabled(editing_);
    if (on && has_focus_ && !caret_visible_) EnsureCaretVisible();
    if (!on && caret_visible_) {
        DestroyCaret();
        caret_visible_ = false;
        caret_height_ = 0;
    }
    // A ribbon button click steals focus to the ribbon; keyboard input
    // (arrows, PgUp/PgDn, typing) would then go to the ribbon instead
    // of the document. Always return focus to the content window.
    if (hwnd_content_) SetFocus(hwnd_content_);
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
        // Table commands are gated on edit mode AND caret context, so the
        // ribbon must re-ask for them whenever either changes.
        static const UINT tableCmds[] = {
            IDC_CMD_INSERT_TABLE, IDC_CMD_TABLE_TOOLS,
            IDC_CMD_ADD_ROW, IDC_CMD_ADD_ROW_ABOVE, IDC_CMD_REMOVE_ROW,
            IDC_CMD_ADD_COLUMN, IDC_CMD_ADD_COLUMN_LEFT,
            IDC_CMD_REMOVE_COLUMN, IDC_CMD_REMOVE_TABLE,
            IDC_CMD_ALIGN_LEFT, IDC_CMD_ALIGN_CENTER, IDC_CMD_ALIGN_RIGHT
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
        for (auto cmd : tableCmds) {
            g_pRibbonFramework->InvalidateUICommand(cmd,
                UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
        }
        // The marker toggle follows the caret: pressed whenever the
        // selection touches a marker. The BooleanValue query is keyed on
        // the selection, and this path runs on every selection change.
        g_pRibbonFramework->InvalidateUICommand(IDC_CMD_MARK,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
        g_pRibbonFramework->InvalidateUICommand(IDC_CMD_MARK,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_BooleanValue);
    }
    Repaint();
}

FormatState AppWindow::GetFormatState() const {
    FormatState fs;
    const auto& text = buffer_.Text();
    if (text.empty()) return fs;

    uint32_t caret = sel_.active.offset;
    if (caret > text.size()) caret = static_cast<uint32_t>(text.size());

    // Helper lambda: check table cell inline formatting.
    // For a table node, iterate all rows/cells and check if the caret
    // or selection overlaps any CellInlineSpan (converting u16 indices
    // to source byte offsets via u16ToSrc). Also scan the raw source
    // text for markdown markers as a fallback.
    auto checkTableCells = [&](const Node& node, uint32_t chkStart, uint32_t chkEnd) {
        // Path 1: CellInlineSpan via u16ToSrc mapping.
        for (const auto& row : node.rows) {
            for (const auto& cell : row.cells) {
                if (cell.u16ToSrc.empty() || cell.inlineSpans.empty()) continue;
                uint32_t cellSrcStart = cell.srcOffset;
                uint32_t cellSrcEnd = cell.u16ToSrc.back() + 1;
                if (chkEnd <= cellSrcStart || chkStart >= cellSrcEnd) continue;

                for (const auto& span : cell.inlineSpans) {
                    if (span.u16Start >= cell.u16ToSrc.size()) continue;
                    uint32_t spanSrcStart = cell.u16ToSrc[span.u16Start];
                    uint32_t spanSrcEnd;
                    if (span.u16End < cell.u16ToSrc.size())
                        spanSrcEnd = cell.u16ToSrc[span.u16End];
                    else
                        spanSrcEnd = cellSrcEnd;

                    // Check overlap with [chkStart, chkEnd).
                    if (spanSrcEnd <= chkStart || spanSrcStart >= chkEnd) continue;
                    if (span.bold)   fs.bold = true;
                    if (span.italic) fs.italic = true;
                    if (span.code)   fs.code = true;
                    if (span.strike) fs.strike = true;
                }
            }
        }

        // Path 2: raw source text marker scan (fallback).
        // Scan the source text around chkStart for markdown markers.
        // This catches formatting that u16ToSrc mapping might miss.
        // Find cell boundaries (pipe chars or newlines).
        uint32_t pos = (chkStart + chkEnd) / 2; // use midpoint
        if (pos >= text.size()) return;
        uint32_t left = pos;
        while (left > 0 && text[left - 1] != '|' && text[left - 1] != '\n')
            left--;
        uint32_t right = pos;
        while (right < text.size() && text[right] != '|' && text[right] != '\n')
            right++;
        // For each marker type, scan left for opening, right for closing.
        auto findFmt = [&](char ch, uint32_t mlen) -> bool {
            // Scan left from `pos` for `mlen` consecutive `ch` chars.
            for (uint32_t i = pos; i >= left + mlen; i--) {
                bool match = true;
                for (uint32_t k = 0; k < mlen; k++)
                    if (text[i - 1 - k] != ch) { match = false; break; }
                if (match) {
                    // Check that these aren't part of a longer run (e.g.
                    // *** is both bold+italic, not just bold).
                    // Also check the char before the run to distinguish
                    // ** (bold) from * (italic).
                    if (mlen == 1 && i >= left + 2 && text[i - 2] == ch)
                        continue; // part of a longer ** run, skip for single-*
                    // Found opening. Scan right for closing of same length.
                    for (uint32_t j = pos; j + mlen <= right; j++) {
                        bool match2 = true;
                        for (uint32_t k = 0; k < mlen; k++)
                            if (text[j + k] != ch) { match2 = false; break; }
                        if (match2) return true;
                    }
                    return false;
                }
                if (i == 0) break;
            }
            return false;
        };
        if (findFmt('*', 2)) fs.bold = true;
        if (findFmt('~', 2)) fs.strike = true;
        if (findFmt('`', 1)) fs.code = true;
        if (!fs.bold && findFmt('*', 1)) fs.italic = true;
    };

    // Collapsed caret: check any inline span whose [cs, ce) range
    // contains the caret position, using CLOSED intervals (cs <= P
    // AND P <= ce). This detects formatting at both the start and end
    // of a span, matching how Word and VS Code behave. Also detect
    // spans that end right before or start right after the caret
    // (adjacent), so the button stays active at boundaries.
    if (sel_.Empty()) {
        uint32_t lo = (caret > 0) ? caret - 1 : 0;
        uint32_t hi = (caret < text.size()) ? caret + 1 : caret;

        for (const auto& node : doc_.nodes) {
            uint32_t bs = node.srcOffset;
            uint32_t be = bs + node.srcLength;
            if (caret < bs || caret > be) continue;

            if (node.block == BlockKind::Heading) {
                fs.headingLevel = node.level;
            } else if (node.block == BlockKind::List) {
                if (node.ordered) fs.inNumbering = true;
                else fs.inBullets = true;
            } else if (node.block == BlockKind::BlockQuote) {
                fs.inQuote = true;
            }

            // Regular inline spans.
            for (const auto& child : node.children) {
                uint32_t cs = child.srcOffset;
                uint32_t ce = cs + child.srcLength;
                bool hit = (cs <= caret && caret <= ce);
                if (!hit) {
                    if (ce == lo) hit = true;      // span ends right before caret
                    if (cs == hi) hit = true;      // span starts right after caret
                }
                if (hit) {
                    if (child.strong) fs.bold = true;
                    if (child.em)     fs.italic = true;
                    if (child.code)   fs.code = true;
                    if (child.strike) fs.strike = true;
                }
            }

            // Table cell spans.
            if (node.block == BlockKind::Table) {
                checkTableCells(node, caret, caret + 1);
            }
        }
        if (pending_bold_set_) fs.bold = pending_bold_;
        if (pending_italic_set_) fs.italic = pending_italic_;
        return fs;
    }

    // Non-empty selection: trim whitespace, then check ALL blocks and
    // ALL inline blocks that overlap the trimmed range. Do NOT stop
    // after the first block, since the selection may span paragraphs.
    uint32_t start = sel_.Start();
    uint32_t end = start + sel_.Length();
    if (end > text.size()) end = static_cast<uint32_t>(text.size());

    while (start < end &&
           (text[start] == ' ' || text[start] == '\t' ||
            text[start] == '\n' || text[start] == '\r'))
        start++;
    while (end > start &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' ||
            text[end - 1] == '\n' || text[end - 1] == '\r'))
        end--;

    if (start >= end) {
        start = sel_.Start();
        end = start + 1;
        if (end > text.size()) end = static_cast<uint32_t>(text.size());
    }

    for (const auto& node : doc_.nodes) {
        uint32_t bs = node.srcOffset;
        uint32_t be = bs + node.srcLength;
        if (end <= bs || start >= be) continue;

        if (node.block == BlockKind::Heading) {
            fs.headingLevel = node.level;
        } else if (node.block == BlockKind::List) {
            if (node.ordered) fs.inNumbering = true;
            else fs.inBullets = true;
        } else if (node.block == BlockKind::BlockQuote) {
            fs.inQuote = true;
        }

        for (const auto& child : node.children) {
            uint32_t cs = child.srcOffset;
            uint32_t ce = cs + child.srcLength;
            if (cs >= end || ce <= start) continue;
            if (child.strong) fs.bold = true;
            if (child.em)     fs.italic = true;
            if (child.code)   fs.code = true;
            if (child.strike) fs.strike = true;
        }

        // Table cell spans.
        if (node.block == BlockKind::Table) {
            checkTableCells(node, start, end);
        }
    }
    return fs;
}

void AppWindow::InvalidateFormatButtons() {
    FormatState fs = GetFormatState();
    UpdateRibbonFormatState(fs);
    // Invalidate Undo/Redo enabled state so the buttons reflect whether
    // there is undo/redo history available.
    if (g_pRibbonFramework) {
        g_pRibbonFramework->InvalidateUICommand(IDC_CMD_UNDO,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
        g_pRibbonFramework->InvalidateUICommand(IDC_CMD_REDO,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
        // Table commands are enabled per caret context, so a caret move
        // into or out of a table must re-ask for their enabled state.
        static const UINT tableCmds[] = {
            IDC_CMD_INSERT_TABLE, IDC_CMD_TABLE_TOOLS,
            IDC_CMD_ADD_ROW, IDC_CMD_ADD_ROW_ABOVE, IDC_CMD_REMOVE_ROW,
            IDC_CMD_ADD_COLUMN, IDC_CMD_ADD_COLUMN_LEFT,
            IDC_CMD_REMOVE_COLUMN, IDC_CMD_REMOVE_TABLE,
            IDC_CMD_ALIGN_LEFT, IDC_CMD_ALIGN_CENTER, IDC_CMD_ALIGN_RIGHT
        };
        for (auto cmd : tableCmds) {
            g_pRibbonFramework->InvalidateUICommand(cmd,
                UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
        }
        // The marker toggle follows the caret: pressed whenever the
        // selection touches a marker. The BooleanValue query is keyed on
        // the selection, and this path runs on every selection change.
        g_pRibbonFramework->InvalidateUICommand(IDC_CMD_MARK,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
        g_pRibbonFramework->InvalidateUICommand(IDC_CMD_MARK,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_BooleanValue);
    }
}

// Remove ALL inline formatting of a specific type from the selection.
// Used by ToggleBold/Italic/Strike/Code when the format is detected as
// active; instead of removing just the first matching span, this
// iterates all spans and removes all markers of that type.
void AppWindow::RemoveAllFormattingInSelection(bool wantStrong, bool wantEm,
                                                bool wantCode, bool wantStrike,
                                                uint32_t mlen) {
    if (doc_.nodes.empty()) return;
    const std::string& text = buffer_.Text();
    uint32_t selStart = sel_.Start();
    uint32_t selEnd = selStart + sel_.Length();
    if (selEnd > text.size()) selEnd = static_cast<uint32_t>(text.size());

    // Collect all formatting spans of the requested type.
    struct Removal {
        uint32_t markerStart, markerEnd, contentStart, contentEnd;
    };
    std::vector<Removal> removals;

    char mc = '*';
    if (wantCode) mc = '`';
    if (wantStrike) mc = '~';

    for (const auto& node : doc_.nodes) {
        uint32_t bs = node.srcOffset;
        uint32_t be = bs + node.srcLength;
        if (selEnd <= bs || selStart >= be) continue;

        // Path 1: regular inline spans (node.children).
        for (const auto& child : node.children) {
            if (wantStrong  && !child.strong)  continue;
            if (wantEm      && !child.em)      continue;
            if (wantCode    && !child.code)    continue;
            if (wantStrike  && !child.strike)  continue;

            uint32_t cs = child.srcOffset;
            uint32_t ce = cs + child.srcLength;
            if (ce <= cs) continue;
            if (ce <= selStart || cs >= selEnd) continue;

            uint32_t leftRun = 0;
            while (cs > leftRun && text[cs - leftRun - 1] == mc)
                leftRun++;
            uint32_t rightRun = 0;
            while (ce + rightRun < text.size() && text[ce + rightRun] == mc)
                rightRun++;

            if (leftRun >= mlen && rightRun >= mlen) {
                removals.push_back({cs - leftRun, ce + rightRun, cs, ce});
            }
        }

        // Path 2: table cells; scan raw source text for markers.
        // Table cells don't create node.children (parser returns early),
        // so we must scan the source text directly.
        if (node.block == BlockKind::Table) {
            // For each cell, find marker pairs overlapping the selection.
            // Scan the source text within each cell for the requested marker.
            auto scanCellMarkers = [&](uint32_t cellLeft, uint32_t cellRight) {
                // Find all marker pairs of type `mc` with length `mlen`
                // within [cellLeft, cellRight) that overlap [selStart, selEnd).
                uint32_t i = cellLeft;
                while (i + mlen <= cellRight) {
                    // Check for `mlen` consecutive `mc` chars at position i.
                    bool isMarker = true;
                    for (uint32_t k = 0; k < mlen; k++) {
                        if (text[i + k] != mc) { isMarker = false; break; }
                    }
                    if (!isMarker) { i++; continue; }
                    // Skip if this is part of a longer run (e.g. ** is also
                    // two * chars). For bold (mlen=2), skip if the char before
                    // is also * (would be *** or more).
                    if (i > cellLeft && text[i - 1] == mc) {
                        i++; continue;
                    }
                    uint32_t openStart = i;
                    uint32_t contentStart = i + mlen;
                    // Find closing marker of same length.
                    uint32_t j = contentStart;
                    uint32_t closePos = UINT32_MAX;
                    while (j + mlen <= cellRight) {
                        bool isClose = true;
                        for (uint32_t k = 0; k < mlen; k++) {
                            if (text[j + k] != mc) { isClose = false; break; }
                        }
                        if (isClose) {
                            // Skip if this close is part of a longer run
                            if (j + mlen < cellRight && text[j + mlen] == mc) {
                                j++; continue;
                            }
                            closePos = j;
                            break;
                        }
                        j++;
                    }
                    if (closePos == UINT32_MAX) { i = contentStart; continue; }
                    uint32_t contentEnd = closePos;
                    uint32_t markerEnd = closePos + mlen;
                    // Check if this marker pair overlaps the selection.
                    if (markerEnd > selStart && openStart < selEnd) {
                        removals.push_back({openStart, markerEnd,
                                           contentStart, contentEnd});
                    }
                    i = markerEnd;
                }
            };
            // Iterate cells to find their boundaries.
            for (const auto& row : node.rows) {
                for (const auto& cell : row.cells) {
                    if (cell.srcOffset == 0) continue;
                    // Cell content range in source text.
                    uint32_t cellLeft = cell.srcOffset;
                    uint32_t cellRight = cellLeft;
                    for (char32_t cp : cell.text) {
                        cellRight += (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                                     (cp <= 0xFFFF) ? 3 : 4;
                    }
                    // Extend cellRight to include gap characters (markers
                    // after content like ** that were gap-filled).
                    // Use u16ToSrc if available.
                    if (!cell.u16ToSrc.empty()) {
                        cellRight = cell.u16ToSrc.back() + 1;
                    }
                    if (selEnd <= cellLeft || selStart >= cellRight) continue;
                    scanCellMarkers(cellLeft, cellRight);
                }
            }
        }
    }

    // Sort rightmost first and merge overlapping.
    std::sort(removals.begin(), removals.end(),
              [](const Removal& a, const Removal& b) {
                  return a.markerStart > b.markerStart;
              });
    std::vector<Removal> merged;
    for (const auto& r : removals) {
        if (!merged.empty() && r.markerEnd >= merged.back().markerStart) {
            merged.back().markerStart = std::min(merged.back().markerStart, r.markerStart);
            merged.back().markerEnd = std::max(merged.back().markerEnd, r.markerEnd);
            merged.back().contentStart = std::min(merged.back().contentStart, r.contentStart);
            merged.back().contentEnd = std::max(merged.back().contentEnd, r.contentEnd);
        } else {
            merged.push_back(r);
        }
    }

    Selection selBefore = sel_;
    int32_t totalDelta = 0; // net chars removed (markers removed - content kept)
    for (const auto& r : merged) {
        std::string content = buffer_.Text().substr(r.contentStart, r.contentEnd - r.contentStart);
        std::string removed = buffer_.Text().substr(r.markerStart, r.markerEnd - r.markerStart);
        buffer_.Splice(r.markerStart, r.markerEnd - r.markerStart, content);
        UndoEntry entry{};
        entry.offset = r.markerStart;
        entry.removed = removed;
        entry.inserted = content;
        entry.selBefore = selBefore;
        entry.selAfter = sel_;
        entry.type = EditType::Other;
        undo_stack_.Push(entry);
        totalDelta += static_cast<int32_t>(removed.size()) - static_cast<int32_t>(content.size());
    }
    // Keep the selection covering the same text (now without markers).
    // Adjust the end by the net delta of removed marker chars.
    uint32_t newEnd = (selEnd > static_cast<uint32_t>(totalDelta))
                      ? selEnd - static_cast<uint32_t>(totalDelta) : selStart;
    sel_.anchor = {selStart};
    sel_.active = {newEnd};
}

// If the caret or selection is inside a formatted span (matching the
// given flags), expand the selection to include the full span content
// plus the surrounding markdown markers. After this, ToggleInlineMarker
// can detect the markers via IsWrappedIn and remove them.
bool AppWindow::ExpandSelectionToFormatSpan(bool wantStrong, bool wantEm,
                                             bool wantCode, bool wantStrike,
                                             uint32_t mlen) {
    if (doc_.nodes.empty()) return false;
    const auto& text = buffer_.Text();
    uint32_t caret = sel_.active.offset;
    if (caret > text.size()) caret = static_cast<uint32_t>(text.size());
    uint32_t selStart = sel_.Start();
    uint32_t selEnd = selStart + sel_.Length();

    for (const auto& node : doc_.nodes) {
        for (const auto& child : node.children) {
            if (wantStrong && !child.strong) continue;
            if (wantEm    && !child.em)     continue;
            if (wantCode   && !child.code)   continue;
            if (wantStrike && !child.strike) continue;

            uint32_t cs = child.srcOffset;
            uint32_t ce = cs + child.srcLength;
            if (ce <= cs) continue; // skip empty spans

            // Check if caret/selection overlaps this span.
            bool hit = false;
            if (sel_.Empty()) {
                hit = (cs <= caret && caret <= ce);
            } else {
                hit = (cs < selEnd && ce > selStart);
            }
            if (!hit) continue;

            // Verify markers exist outside the content.
            if (cs < mlen || ce + mlen > text.size()) continue;
            bool ok = true;
            char mc = (wantCode) ? '`' : '*';
            if (wantStrike) mc = '~';
            if (wantEm && !wantStrong) mc = '*';
            for (uint32_t i = 0; i < mlen && ok; i++) {
                if (text[cs - mlen + i] != mc) ok = false;
                if (text[ce + i] != mc)       ok = false;
            }
            if (!ok) continue;

            // Expand selection to include the markers.
            sel_.anchor = {cs - mlen};
            sel_.active = {ce + mlen};
            return true;
        }
    }
    return false;
}

void AppWindow::ToggleBold() {
    if (!editing_) return;
    if (sel_.Empty()) {
        bool beforeBold = pending_bold_;
        bool beforeItalic = pending_italic_;
        bool beforeBoldSet = pending_bold_set_;
        bool beforeItalicSet = pending_italic_set_;
        FormatState fs = GetFormatState();
        bool current = pending_bold_set_ ? pending_bold_ : fs.bold;
        pending_bold_ = !current;
        pending_bold_set_ = true;
        pending_run_active_ = false;
        pending_run_suffix_bytes_ = 0;
        RecordPendingFormatUndo(beforeBold, beforeItalic,
                                beforeBoldSet, beforeItalicSet);
        editor_.BreakUndoCoalesce();
        InvalidateFormatButtons();
        return;
    }
    FormatState fs = GetFormatState();
    if (fs.bold && !sel_.Empty()) {
        // Selection contains bold text: remove ALL bold markers.
        RemoveAllFormattingInSelection(true, false, false, false, 2);
    } else if (fs.bold) {
        // Caret is in bold text: expand to span and remove.
        ExpandSelectionToFormatSpan(true, false, false, false, 2);
        ToggleInlineMarker(&buffer_, &sel_, "**", &undo_stack_);
    } else {
        // No bold: add markers.
        ToggleInlineMarker(&buffer_, &sel_, "**", &undo_stack_);
    }
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::ToggleItalic() {
    if (!editing_) return;
    if (sel_.Empty()) {
        bool beforeBold = pending_bold_;
        bool beforeItalic = pending_italic_;
        bool beforeBoldSet = pending_bold_set_;
        bool beforeItalicSet = pending_italic_set_;
        FormatState fs = GetFormatState();
        bool current = pending_italic_set_ ? pending_italic_ : fs.italic;
        pending_italic_ = !current;
        pending_italic_set_ = true;
        pending_run_active_ = false;
        pending_run_suffix_bytes_ = 0;
        RecordPendingFormatUndo(beforeBold, beforeItalic,
                                beforeBoldSet, beforeItalicSet);
        editor_.BreakUndoCoalesce();
        InvalidateFormatButtons();
        return;
    }
    FormatState fs = GetFormatState();
    if (fs.italic && !sel_.Empty()) {
        RemoveAllFormattingInSelection(false, true, false, false, 1);
    } else if (fs.italic) {
        ExpandSelectionToFormatSpan(false, true, false, false, 1);
        ToggleInlineMarker(&buffer_, &sel_, "*", &undo_stack_);
    } else {
        ToggleInlineMarker(&buffer_, &sel_, "*", &undo_stack_);
    }
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::ToggleStrike() {
    if (!editing_) return;
    FormatState fs = GetFormatState();
    if (fs.strike && !sel_.Empty()) {
        RemoveAllFormattingInSelection(false, false, false, true, 2);
    } else if (fs.strike) {
        ExpandSelectionToFormatSpan(false, false, false, true, 2);
        ToggleInlineMarker(&buffer_, &sel_, "~~", &undo_stack_);
    } else {
        ToggleInlineMarker(&buffer_, &sel_, "~~", &undo_stack_);
    }
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::ToggleCode() {
    if (!editing_) return;
    // If the selection spans one or more entire lines (paragraphs),
    // toggle a fenced code block (``` ```). Otherwise, toggle inline code (`).
    if (!sel_.Empty()) {
        const std::string& text = buffer_.Text();
        uint32_t start = sel_.Start();
        uint32_t end = start + sel_.Length();
        // Check if the selection starts at the beginning of a line and
        // ends at the end of a line (or at a newline character).
        bool startsAtLineStart = (start == 0 || text[start - 1] == '\n');
        bool endsAtLineEnd = (end >= text.size() || text[end] == '\n');
        // Also handle selections that end right before the newline
        // (e.g. the last visible character, not including the newline).
        if (end > 0 && end <= text.size() && end < text.size() && text[end] != '\n') {
            // Check if the rest of the line is only whitespace
            uint32_t check = end;
            while (check < text.size() && text[check] != '\n') {
                if (text[check] != ' ' && text[check] != '\t') break;
                check++;
            }
            if (check >= text.size() || text[check] == '\n')
                endsAtLineEnd = true;
        }
        if (startsAtLineStart && endsAtLineEnd) {
            ToggleCodeBlock(&buffer_, &sel_, &undo_stack_);
            editor_.BreakUndoCoalesce();
            OnBufferChanged();
            ForceRepaintNow();
            return;
        }
    }
    // Default: inline code (single backticks)
    {
        FormatState fs = GetFormatState();
        if (fs.code && !sel_.Empty()) {
            RemoveAllFormattingInSelection(false, false, true, false, 1);
        } else if (fs.code) {
            ExpandSelectionToFormatSpan(false, false, true, false, 1);
            ToggleInlineMarker(&buffer_, &sel_, "`", &undo_stack_);
        } else {
            ToggleInlineMarker(&buffer_, &sel_, "`", &undo_stack_);
        }
    }
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
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
        buf.push_back(0); buf.push_back(0); // null terminator
    };

    std::vector<BYTE> buf;
    // DLGTEMPLATE
    DWORD style = WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU
                | DS_MODALFRAME | DS_SETFONT;
    pushStr(buf, L"Segoe UI"); // menu (none... actually this is menu field)
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
    // cdit = 4 (label + edit + OK + Cancel buttons). WORD field.
    buf.push_back(4); buf.push_back(0);
    // x, y, cx, cy (in dialog units)
    // cx=200, cy=80
    WORD cx = 200, cy = 80;
    buf.push_back(10); buf.push_back(0);  // x
    buf.push_back(10); buf.push_back(0);  // y
    buf.push_back(cx & 0xFF); buf.push_back(cx >> 8);  // cx
    buf.push_back(cy & 0xFF); buf.push_back(cy >> 8);  // cy

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
    buf.push_back(10); buf.push_back(0); // x
    buf.push_back(10); buf.push_back(0); // y
    buf.push_back(180); buf.push_back(0); // cx
    buf.push_back(12); buf.push_back(0); // cy
    buf.push_back(1000); buf.push_back(0); // id = 1000

    // class: static
    buf.push_back(0x0082); buf.push_back(0); // atom for STATIC
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
    buf.push_back(10); buf.push_back(0); // x
    buf.push_back(25); buf.push_back(0); // y
    buf.push_back(180); buf.push_back(0); // cx
    buf.push_back(14); buf.push_back(0); // cy
    buf.push_back(1001); buf.push_back(0); // id = 1001

    // class: edit
    buf.push_back(0x0081); buf.push_back(0); // atom for EDIT
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
    buf.push_back(50); buf.push_back(0); // y
    buf.push_back(50); buf.push_back(0); // cx
    buf.push_back(14); buf.push_back(0); // cy
    buf.push_back(IDOK & 0xFF); buf.push_back(IDOK >> 8); // id = IDOK

    // class: button
    buf.push_back(0x0080); buf.push_back(0); // atom for BUTTON
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
    buf.push_back(80); buf.push_back(0); // x (right of OK)
    buf.push_back(50); buf.push_back(0); // y
    buf.push_back(50); buf.push_back(0); // cx
    buf.push_back(14); buf.push_back(0); // cy
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
        return FALSE; // we already set focus
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


// Find & Replace session.
//
// The bar is modeless, so all of this runs against a live, editable
// document (find guide 31). The split of responsibility is the point of
// guide 62: the bar owns search state and visual state, this file owns
// document state and undo. Neither one reaches into the other.

// Scope filter. Decides which candidate ranges take part in the search,
// which is how Find stays inside what the user can actually see and
// edit (guide 4). Two rules:
//   * In the rendered view, only RENDERED text takes part. Hidden
//     Markdown syntax (the asterisks of bold, the pipes of a table) is
//     not text the reader sees, so searching it would report matches
//     that are not on screen.
//   * When Find is scoped to the selection, only bytes inside it.
// A candidate the filter rejects is skipped, not consumed, so a later
// overlapping match is still found.
bool AppWindow::FindScopeFilter(uint32_t start, uint32_t length,
                                void* context) {
    const FindScopeContext* scope =
        static_cast<const FindScopeContext*>(context);
    if (!scope || !scope->app) return true;
    if (scope->withinSelection) {
        // A match must lie wholly inside the selection, so Replace All can
        // never touch a byte outside it (guide 51).
        if (start < scope->selectionStart) return false;
        if (start + length > scope->selectionEnd) return false;
    }
    // In source view every byte of the buffer is on screen, so the layout
    // cache says nothing useful and would wrongly reject text.
    if (scope->app->IsSourceView()) return true;
    return scope->app->layout_cache_.RangeIsRendered(start, length);
}

// Turn a match into a document selection: select exactly the matched bytes
// and bring it into view (guide 7). The whole match is selected, not just
// the caret parked in front of it, so the user sees what Replace will hit.
void AppWindow::ApplyFindSelection(uint32_t offset, uint32_t length) {
    if (length == 0) return;
    sel_.anchor = {offset};
    sel_.active = {offset + length};
    UpdateCaretPosition();
    // A paint is needed before the caret rect exists, and the scroll that
    // follows needs the rebuilt layout cache.
    ForceRepaintNow();
    float cx = 0.0f, cy = 0.0f, ch = 0.0f;
    if (layout_cache_.OffsetToCaretRect(sel_.active.offset, &cx, &cy, &ch))
        ScrollCaretIntoView(cy, ch);
    Repaint();
}

// Hand the current match list to the renderer. Document byte offsets all
// the way through, so zoom, scroll and relayout cannot change which text is
// marked (guide 64).
void AppWindow::UpdateFindHighlight() {
    if (!find_bar_.IsVisible() || find_bar_.state().Matches().empty()) {
        renderer_.ClearSearchMatches();
        return;
    }
    renderer_.SetSearchMatches(&find_bar_.state().Matches(),
                               find_bar_.state().CurrentIndex());
}

// Re-run the search against the live document and publish the result.
void AppWindow::RefreshFindResults(bool adoptCurrentMatch) {
    FindReplaceState& state = find_bar_.state();
    state.Refresh(buffer_.Text(), &AppWindow::FindScopeFilter,
                  &find_scope_);
    if (adoptCurrentMatch && state.HasQuery())
        state.AdoptMatchAt(sel_.active.offset);
    find_bar_.SyncFromState();
    UpdateFindHighlight();
    TextMatch current{};
    if (state.CurrentMatch(&current))
        ApplyFindSelection(current.start, current.length);
    else
        Repaint();
}

// Marker layer commands. A marker annotates the rendered document; it is
// stored in a temp sidecar, never in the Markdown file, and marker changes
// never mark the document dirty (marker guide 12, 17 and 18).

std::string AppWindow::MarkerDocumentPath() const {
    if (file_path_.empty()) return std::string();
    return ImeWideToUtf8(file_path_);
}

// Re-query the ribbon for the marker toggle state. Called wherever the
// selection or the marker set changes and no caret update will follow:
// view-mode selections never run UpdateCaretPosition, and the toggle
// command itself executes while the ribbon button is pressed.
void AppWindow::InvalidateMarkerToggleUI() {
    if (!g_pRibbonFramework) return;
    g_pRibbonFramework->InvalidateUICommand(IDC_CMD_MARK,
        UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
    g_pRibbonFramework->InvalidateUICommand(IDC_CMD_MARK,
        UI_INVALIDATIONS_PROPERTY, &UI_PKEY_BooleanValue);
}

void AppWindow::RefreshMarkerRanges() {
    marker_ranges_.clear();
    for (const auto& m : marker_store_.Markers()) {
        if (!m.resolved || m.end <= m.start) continue;
        marker_ranges_.push_back(
            {m.start, static_cast<uint32_t>(m.end - m.start)});
    }
    renderer_.SetMarkers(&marker_ranges_, true);
}

void AppWindow::ResolveMarkerAnchors() {
    marker_store_.Resolve(buffer_.Text());
    RefreshMarkerRanges();
}

void AppWindow::LoadMarkersForDocument() {
    marker_ranges_.clear();
    const std::string path = MarkerDocumentPath();
    if (!path.empty()) {
        marker_store_.LoadFor(path, buffer_.Text());
    }
    ResolveMarkerAnchors();
}

void AppWindow::SaveMarkers() {
    const std::string path = MarkerDocumentPath();
    if (!path.empty()) marker_store_.Save(path, buffer_.Text());
}

bool AppWindow::SelectionHasMarker() const {
    if (sel_.Empty()) return false;
    const uint32_t s = sel_.Start();
    const uint32_t e = s + sel_.Length();
    for (const auto& m : marker_store_.Markers()) {
        if (m.end > s && m.start < e) return true;
    }
    return false;
}

void AppWindow::ToggleMarkSelection() {
    // One button, one shortcut: a selection that touches a marker
    // unmarks it, any other selection marks it. A partially marked
    // selection also reads as marked, so the button shows pressed.
    diag::TraceFmt("MARKTOG enter sel=[%u,%u) len=%u", sel_.Start(),
                   sel_.Length());
    if (SelectionHasMarker()) {
        diag::Trace("MARKTOG branch remove");
        RemoveMarkerAtSelection();
    } else {
        diag::Trace("MARKTOG branch mark");
        MarkSelection();
    }
    InvalidateMarkerToggleUI();
}

void AppWindow::MarkSelection() {
    diag::TraceFmt("MARKSEL enter sel=[%u,%u)", sel_.Start(),
                   sel_.Length());
    if (sel_.Empty()) {
        diag::Trace("MARKSEL empty");
        return;
    }
    uint32_t lo = 0;
    uint32_t hi = 0;
    if (!layout_cache_.ClipToRendered(sel_.Start(), sel_.Length(), &lo, &hi)) {
        diag::Trace("MARKSEL clip failed");
        return;  // the selection covers no rendered text
    }
    diag::TraceFmt("MARKSEL clip=[%u,%u)", lo, hi);
    if (!marker_store_.Add(buffer_.Text(), lo, hi)) {
        diag::Trace("MARKSEL add rejected");
        return;
    }
    diag::Trace("MARKSEL add ok");
    SaveMarkers();
    RefreshMarkerRanges();
    Repaint();
}

void AppWindow::RemoveMarkerAtSelection() {
    if (sel_.Empty()) {
        diag::Trace("MARKRM sel empty");
        return;
    }
    const size_t removed = marker_store_.RemoveIntersecting(
        sel_.Start(), sel_.Start() + sel_.Length());
    diag::TraceFmt("MARKRM removed=%zu", removed);
    if (removed == 0) return;
    SaveMarkers();
    RefreshMarkerRanges();
    Repaint();
}

// Pull the live view and edit state into the bar. Called on every Show and
// whenever the view changes, because the bar deliberately does not remember
// either (guide 4).
void AppWindow::SyncFindBarFromDocument() {
    // Find works in every view. Replace only makes sense where the document
    // can change, so it is gated on edit mode and the bar greys the two
    // buttons out rather than silently ignoring a click.
    find_bar_.SetReplaceEnabled(editing_);
}

// Wire the bar's callbacks once. The bar never touches the buffer or the
// undo stack itself; everything it wants applied comes through here.
void AppWindow::WireFindBar() {
    FindBarListener listener;
    listener.caretOffset = [this]() { return sel_.active.offset; };

    listener.onSearchChanged = [this]() {
        // Incremental search: the bar reports a new query, the result is
        // recomputed right away without an Enter (guide 5).
        RefreshFindResults(true);
    };

    listener.onSelectionChanged = [this](uint32_t offset, uint32_t length) {
        ApplyFindSelection(offset, length);
        UpdateFindHighlight();
    };

    listener.onReplaceCurrent = [this](uint32_t offset, uint32_t length) {
        if (!editing_ || length == 0) return;
        const Selection before = sel_;
        const std::string replacement = ImeWideToUtf8(find_bar_.GetReplaceText());
        // The editor path records one undo entry and leaves the inserted
        // text selected, like every other replacement in the app.
        editor_.ReplaceTextRange(offset, length, replacement, EditType::Other,
                                 &before);
        editor_.BreakUndoCoalesce();
        OnBufferChanged();
        // Continue from where the replacement ended, still searching for the
        // ORIGINAL query, never for the text just inserted (guide 22).
        RefreshFindResults(false);
        find_bar_.SelectMatchAt(sel_.active.offset);
    };

    listener.onReplaceAll = [this](const std::wstring& query,
                                   const std::wstring& replacement,
                                   const FindStateOptions&) {
        if (!editing_) return;
        FindReplaceState& state = find_bar_.state();
        // The callback hands over wide text and the state speaks UTF-8, so
        // convert at the boundary. SetReplaceText cannot change which
        // ranges match, so the existing match list stays valid.
        state.SetReplaceText(ImeWideToUtf8(replacement));
        std::string result;
        const size_t count = state.BuildReplacedDocument(buffer_.Text(),
                                                         &result);
        // Zero means nothing matched. Not applying it keeps an empty
        // Replace All from marking the document dirty (guide 59).
        if (count == 0) {
            find_bar_.SetStatusText(L"No matches");
            return;
        }
        const std::string original = buffer_.Text();
        Selection before = sel_;
        buffer_.Splice(0, static_cast<uint32_t>(original.size()), result);
        sel_.Collapse({0});
        UndoEntry entry{};
        entry.offset = 0;
        entry.removed = original;
        entry.inserted = std::move(result);
        entry.selBefore = before;
        entry.selAfter = sel_;
        entry.timestamp = GetTickCount64();
        // ONE undo entry for the whole sweep, so a single Ctrl+Z restores
        // the document (guide 17).
        entry.type = EditType::Other;
        undo_stack_.Push(entry);
        editor_.BreakUndoCoalesce();
        OnBufferChanged();
        RefreshFindResults(false);
        wchar_t message[128] = {};
        _snwprintf_s(message, _TRUNCATE, L"%zu replacements made.", count);
        find_bar_.SetStatusText(message);
        ForceRepaintNow();
    };

    listener.onClose = [this]() {
        CloseFindBar();
    };

    find_bar_.SetListener(std::move(listener));
}

void AppWindow::CloseFindBar() {
    renderer_.ClearSearchMatches();
    find_bar_.state().Invalidate();
    find_bar_.Hide();
    // The strip is gone; give its height back to the content.
    ResizeContentWindow();
    if (hwnd_content_) SetFocus(hwnd_content_);
    Repaint();
}

void AppWindow::ShowFindReplace(bool replaceMode) {
    if (welcome_mode_) return;
    WireFindBar();

    // Ctrl+F on an already open bar focuses the field and selects its text
    // so a new term can be typed straight away (guide 2.1).
    if (find_bar_.IsVisible()) {
        find_bar_.SetReplaceEnabled(editing_);
        // The function follows the shortcut: Ctrl+H turns the replace half
        // on, Ctrl+F drops back to Find mode. Same window, same text.
        find_bar_.SetMode(replaceMode ? FindBarMode::FindReplace
                                      : FindBarMode::Find);
        find_bar_.FocusAndSelectSearchText();
        return;
    }

    SyncFindBarFromDocument();

    // The layout cache decides what counts as rendered text, so it has to
    // exist before the first search (guide 4).
    ForceRepaintNow();

    // Seed the query from the selection, otherwise keep the last one
    // (guide 9 and 54). A single-line selection is the useful case: a
    // multi-line one would fill the field with newlines.
    if (find_bar_.state().SearchText().empty() && !sel_.Empty() &&
        sel_.Length() < 512) {
        const std::string selected =
            buffer_.Text().substr(sel_.Start(), sel_.Length());
        if (selected.find('\n') == std::string::npos) {
            find_bar_.SetSearchText(ImeUtf8ToWide(selected));
        }
    }

    find_scope_.app = this;
    find_scope_.withinSelection = false;
    find_scope_.selectionStart = sel_.Start();
    find_scope_.selectionEnd = sel_.Start() + sel_.Length();

    if (!find_bar_.Show(hwnd_, replaceMode ? FindBarMode::FindReplace
                                           : FindBarMode::Find)) return;

    // The strip now claims its height from the content area.
    ResizeContentWindow();

    // The strip took over the band the content window vacated. Paint its
    // face once, right away, so no document pixels linger there until a
    // natural invalidate arrives.
    if (HWND bar_wnd = find_bar_.Handle()) {
        RedrawWindow(bar_wnd, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }

    RefreshFindResults(true);
}

void AppWindow::InsertLinkCmd() {
    if (!editing_) return;
    // Ctrl+K with an empty caret may open the dialog, but it must not create
    // an empty link because the base editor specification requires a
    // non-empty visible selection for link formatting.
    bool hadSelection = !sel_.Empty();
    // Build the dialog template in memory.
    auto tmpl = BuildLinkDialogTemplate();
    std::wstring url;
    INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandle(NULL),
        reinterpret_cast<DLGTEMPLATE*>(tmpl.data()),
        hwnd_,
        LinkDialogProc,
        reinterpret_cast<LPARAM>(&url));
    if (result != IDOK || url.empty() || !hadSelection) return;
    std::string urlUtf8 = WideToUtf8(url);
    InsertLink(&buffer_, &sel_, urlUtf8, &undo_stack_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::SpliceWithUndo(uint32_t offset, uint32_t length,
                                   const std::string& replacement) {
    std::string removed = buffer_.Text().substr(
        std::min(offset, static_cast<uint32_t>(buffer_.Text().size())),
        std::min(length, static_cast<uint32_t>(
            buffer_.Text().size() - std::min(offset,
                static_cast<uint32_t>(buffer_.Text().size())))));
    Selection selBefore = sel_;
    buffer_.Splice(offset, length, replacement);
    Selection selAfter = sel_;
    UndoEntry entry{};
    entry.offset = offset;
    entry.removed = removed;
    entry.inserted = replacement;
    entry.selBefore = selBefore;
    entry.selAfter = selAfter;
    entry.timestamp = GetTickCount64();
    entry.type = EditType::Insert;
    undo_stack_.Push(entry);
}

// Table support: insert table, add/remove row/column
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Word-style grid table picker: a popup showing cells you hover to select
// the table size, then click to insert.
// ---------------------------------------------------------------------------

struct TableGridPicker {
    static constexpr int kMaxCols = 10;
    static constexpr int kMaxRows = 8;
    static constexpr int kCellSize = 16;   // logical pixels per cell
    static constexpr int kMargin = 6;      // padding around grid
    static constexpr int kLabelH = 22;     // bottom label "3 x 3 Table"

    int selCols = 3;
    int selRows = 3;
    // Geometry in physical pixels, scaled from the logical constants
    // above by the content window DPI when the popup is created. Paint
    // and mouse hit-testing both read these so they cannot drift apart.
    int cell = kCellSize;
    int margin = kMargin;
    int labelH = kLabelH;
    HWND hPopup = nullptr;
    HWND hParent = nullptr;
    bool tracking = false;
};

static TableGridPicker g_gridPicker;

static LRESULT CALLBACK GridPickerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);

        int n = TableGridPicker::kMaxCols;
        int m = TableGridPicker::kMaxRows;
        int cs = g_gridPicker.cell;
        int margin = g_gridPicker.margin;
        int labelH = g_gridPicker.labelH;

        // Draw background
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        // Draw grid cells
        for (int row = 0; row < m; ++row) {
            for (int col = 0; col < n; ++col) {
                int x = margin + col * cs;
                int y = margin + row * cs;
                RECT cell = {x, y, x + cs - 1, y + cs - 1};

                if (col < g_gridPicker.selCols && row < g_gridPicker.selRows) {
                    // Selected cell: blue fill + white border
                    FillRect(hdc, &cell, GetSysColorBrush(COLOR_HIGHLIGHT));
                } else {
                    // Unselected: light border only
                    FrameRect(hdc, &cell, GetSysColorBrush(COLOR_BTNSHADOW));
                }
            }
        }

        // Draw label at bottom: "Rx C Table"
        RECT labelRc = {
            0,
            margin + m * cs + 4,
            rc.right,
            margin + m * cs + 4 + labelH
        };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        wchar_t label[64];
        // +1 to rows because Word counts header as part of the visible rows;
        // but for markdown, selRows = data rows (header is extra). Show
        // total rows including header.
        int totalRows = g_gridPicker.selRows + 1;
        swprintf_s(label, 64, L"%d x %d Table", totalRows, g_gridPicker.selCols);
        DrawTextW(hdc, label, -1, &labelRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        int cs = g_gridPicker.cell;
        int margin = g_gridPicker.margin;
        int col = (x - margin) / cs + 1;
        int row = (y - margin) / cs + 1;
        if (col < 1) col = 1;
        if (row < 1) row = 1;
        if (col > TableGridPicker::kMaxCols) col = TableGridPicker::kMaxCols;
        if (row > TableGridPicker::kMaxRows) row = TableGridPicker::kMaxRows;

        // For markdown, row 0 is the header which is always included.
        // The grid shows data rows (excluding header). So the visual
        // selection maps: selRows = row (data rows), selCols = col.
        if (col != g_gridPicker.selCols || row != g_gridPicker.selRows) {
            g_gridPicker.selCols = col;
            g_gridPicker.selRows = row;
            InvalidateRect(h, nullptr, TRUE);
            UpdateWindow(h);
        }

        // Track mouse leave
        if (!g_gridPicker.tracking) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            TrackMouseEvent(&tme);
            g_gridPicker.tracking = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        g_gridPicker.tracking = false;
        // Close the popup when the mouse leaves
        DestroyWindow(h);
        g_gridPicker.hPopup = nullptr;
        return 0;
    }
    case WM_LBUTTONDOWN: {
        // User clicked: insert this table size
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        int cs = g_gridPicker.cell;
        int margin = g_gridPicker.margin;
        int col = (x - margin) / cs + 1;
        int row = (y - margin) / cs + 1;
        if (col < 1) col = 1;
        if (row < 1) row = 1;
        if (col > TableGridPicker::kMaxCols) col = TableGridPicker::kMaxCols;
        if (row > TableGridPicker::kMaxRows) row = TableGridPicker::kMaxRows;
        g_gridPicker.selCols = col;
        g_gridPicker.selRows = row;
        DestroyWindow(h);
        g_gridPicker.hPopup = nullptr;

        // Post a message to the parent to insert the table
        PostMessage(g_gridPicker.hParent, WM_APP + 1,
                    static_cast<WPARAM>(col),
                    static_cast<LPARAM>(row));
        return 0;
    }
    case WM_KILLFOCUS: {
        // Close if focus is lost (user clicked elsewhere or pressed Escape)
        if (g_gridPicker.hPopup) {
            DestroyWindow(h);
            g_gridPicker.hPopup = nullptr;
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (wp == VK_ESCAPE) {
            DestroyWindow(h);
            g_gridPicker.hPopup = nullptr;
        }
        return 0;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}

static void RegisterGridPickerClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = GridPickerProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"MarkDownItGridPicker";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
    registered = true;
}

void AppWindow::InsertTableCmd() {
    if (!editing_) return;

    // Show the Word-style grid picker popup.
    RegisterGridPickerClass();
    g_gridPicker.hParent = hwnd_content_ ? hwnd_content_ : hwnd_;
    g_gridPicker.selCols = 3;
    g_gridPicker.selRows = 3;
    g_gridPicker.tracking = false;

    // Scale the popup from logical pixels to physical pixels so the grid
    // keeps its intended size on a high-DPI display. Paint and hit-testing
    // read these same fields.
    const float dpix = static_cast<float>(dpi_ > 0 ? dpi_ : 96) / 96.0f;
    g_gridPicker.cell = MulDiv(TableGridPicker::kCellSize, dpi_, 96);
    g_gridPicker.margin = MulDiv(TableGridPicker::kMargin, dpi_, 96);
    g_gridPicker.labelH = MulDiv(TableGridPicker::kLabelH, dpi_, 96);
    if (g_gridPicker.cell < 4) g_gridPicker.cell = 4;
    if (g_gridPicker.margin < 2) g_gridPicker.margin = 2;
    if (g_gridPicker.labelH < 8) g_gridPicker.labelH = 8;

    int w = g_gridPicker.margin * 2 +
            g_gridPicker.cell * TableGridPicker::kMaxCols;
    int h = g_gridPicker.margin * 2 +
            g_gridPicker.cell * TableGridPicker::kMaxRows +
            g_gridPicker.labelH;

    // Anchor the popup to the caret: its left edge lines up with the
    // insertion point and it opens just below the caret line. Opening at a
    // fixed window offset put the grid far to the left of the text the
    // table is inserted at.
    POINT pt = {0, 0};
    bool anchored = false;
    if (hwnd_content_) {
        float cx = 0.0f, cy = 0.0f, ch = 0.0f;
        if (layout_cache_.OffsetToCaretRect(sel_.active.offset, &cx, &cy, &ch)) {
            POINT c = {static_cast<int>(cx * dpix),
                       static_cast<int>((cy - scrollY_) * dpix)};
            ClientToScreen(hwnd_content_, &c);
            pt = c;
            pt.y += std::max(1, static_cast<int>(ch * dpix));
            anchored = true;
        }
    }
    if (!anchored) {
        // No laid-out caret yet (empty document, or the layout cache has
        // not run): fall back to the content window's top-left.
        RECT rc;
        HWND host = hwnd_content_ ? hwnd_content_ : hwnd_;
        GetWindowRect(host, &rc);
        pt.x = rc.left + 20;
        pt.y = rc.top + 10;
    }

    // Keep the whole popup on the monitor. Clamp against the work area so
    // the grid never opens off-screen or under the taskbar.
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        if (pt.x + w > mi.rcWork.right)  pt.x = mi.rcWork.right - w;
        if (pt.y + h > mi.rcWork.bottom) pt.y = mi.rcWork.bottom - h;
        if (pt.x < mi.rcWork.left)   pt.x = mi.rcWork.left;
        if (pt.y < mi.rcWork.top)    pt.y = mi.rcWork.top;
    }

    g_gridPicker.hPopup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"MarkDownItGridPicker", L"",
        WS_POPUP | WS_VISIBLE,
        pt.x, pt.y, w, h,
        hwnd_, nullptr, GetModuleHandle(NULL), nullptr);

    SetFocus(g_gridPicker.hPopup);
    SetCapture(g_gridPicker.hPopup);
}

// Called from ContentWndProc when WM_APP+1 is received (grid picker
// sent the selected rows x cols).
void AppWindow::InsertTableFromGrid(int cols, int rows) {
    if (!editing_) return;
    // Nested tables are not supported document structure: a table may
    // never be created inside a table (menu, picker, paste, drag alike).
    if (IsOffsetInTable(doc_, sel_.active.offset)) return;

    int dataRows = rows;

    std::string table;
    // Header
    for (int c = 0; c < cols; ++c) {
        table += "| Column ";
        table += std::to_string(c + 1);
        table += " ";
    }
    table += "|\n";
    // Separator
    for (int c = 0; c < cols; ++c) {
        table += "|--------";
    }
    table += "|\n";
    // Data rows
    for (int r = 0; r < dataRows; ++r) {
        for (int c = 0; c < cols; ++c) {
            table += "|        ";
        }
        table += "|\n";
    }

    // Insert at cursor position. If there is a selection, replace it.
    uint32_t insertPos = sel_.active.offset;
    if (!sel_.Empty()) {
        insertPos = sel_.Start();
        SpliceWithUndo(insertPos, sel_.Length(), "");
        sel_.Collapse({insertPos});
    }

    // Ensure table starts on a new line.
    const std::string& txt = buffer_.Text();
    if (insertPos > 0 && txt[insertPos - 1] != '\n') {
        SpliceWithUndo(insertPos, 0, "\n");
        insertPos += 1;
    }
    std::string insertText = table;
    if (insertPos + table.size() < txt.size() &&
        txt[insertPos + table.size()] != '\n') {
        insertText += "\n";
    }

    SpliceWithUndo(insertPos, 0, insertText);
    editor_.BreakUndoCoalesce();

    // Caret goes to the first header cell's content (after its leading
    // pipe), not past the whole table.
    uint32_t firstCellOffset = insertPos + 1;  // past the leading '|'
    sel_.Collapse({firstCellOffset});

    OnBufferChanged();
    ForceRepaintNow();
}

// True when the caret sits inside a Markdown table. A selection counts as
// in-table when its anchor side is in a table, matching the rule the Tab
// handler uses to decide whether Tab is structural navigation.
bool AppWindow::CaretInTable() const {
    if (IsOffsetInTable(doc_, sel_.active.offset)) return true;
    if (!sel_.Empty() && IsOffsetInTable(doc_, sel_.Start())) return true;
    return false;
}

// Find the table node containing the cursor offset.
// Returns nullptr if cursor is not inside a table.
static const Node* FindContainingTable(const Document& doc, uint32_t offset) {
    for (const auto& n : doc.nodes) {
        if (n.block != BlockKind::Table) continue;
        uint32_t start = n.srcOffset;
        uint32_t end = start + n.srcLength;
        // Half-open span: `end` is the first byte after the table, so a
        // caret resting there is not inside this table.
        if (offset >= start && offset < end)
            return &n;
    }
    return nullptr;
}

// Find the source range of a specific row in the table's source text.
// A row is one line within the table block. Also return the column
// index that the cursor is in.
struct TableLocation {
    uint32_t rowStart;  // source offset of row's first character
    uint32_t rowEnd;    // source offset past the row's newline (or end of table)
    int columnIndex;    // 0-based column the cursor is in
    int rowIndex;       // 0-based row index
    int numCols;         // number of columns
    int numRows;         // number of rows
};

static bool LocateInTable(const std::string& text, const Node* node,
                          uint32_t offset, TableLocation& loc) {
    if (!node) return false;
    uint32_t tblStart = node->srcOffset;
    uint32_t tblEnd = tblStart + node->srcLength;
    if (offset < tblStart || offset > tblEnd) return false;

    // Walk lines within the table source.
    int rowIdx = 0;
    uint32_t lineStart = tblStart;
    int numCols = 0;
    for (const auto& r : node->rows) {
        if (static_cast<int>(r.cells.size()) > numCols)
            numCols = static_cast<int>(r.cells.size());
    }

    for (uint32_t i = tblStart; i <= tblEnd; ++i) {
        if (i == tblEnd || text[i] == '\n') {
            uint32_t lineEnd = i; // not including newline
            if (offset >= lineStart && offset <= (i == tblEnd ? i : i + 1)) {
                // Found the row. Count pipes to determine column.
                int col = 0;
                for (uint32_t j = lineStart; j < offset && j < lineEnd; ++j) {
                    if (text[j] == '|') ++col;
                }
                // Adjust: first | is col 0 start
                col = col > 0 ? col - 1 : 0;
                if (col >= numCols) col = numCols - 1;

                loc.rowStart = lineStart;
                loc.rowEnd = (i == tblEnd) ? i : i + 1;
                loc.columnIndex = col;
                loc.rowIndex = rowIdx;
                loc.numCols = numCols;
                loc.numRows = static_cast<int>(node->rows.size());
                return true;
            }
            if (i < tblEnd) lineStart = i + 1;
            ++rowIdx;
        }
    }
    return false;
}

TableCapabilities AppWindow::CaretTableCapabilities() const {
    // Resolve the caret against the same LocateInTable the commands use, so
    // an enabled ribbon button always corresponds to a command that runs.
    const Node* tbl = FindContainingTable(doc_, sel_.active.offset);
    if (!tbl) return TableCapabilities();
    TableLocation loc;
    if (!LocateInTable(buffer_.Text(), tbl, sel_.active.offset, loc))
        return TableCapabilities();
    return TableCapabilitiesFor(loc.rowIndex, loc.numCols);
}

bool AppWindow::AddTableRow(bool below) {
    if (!editing_) return false;
    const Node* tbl = FindContainingTable(doc_, sel_.active.offset);
    if (!tbl) return false;

    const std::string& text = buffer_.Text();
    TableLocation loc;
    if (!LocateInTable(text, tbl, sel_.active.offset, loc)) return false;

    // The header line and the dash delimiter are what make this a table.
    // Inserting above the header, or between header and delimiter, would
    // leave a block md4c no longer parses as a table.
    const TableCapabilities caps =
        TableCapabilitiesFor(loc.rowIndex, loc.numCols);
    if (below ? !caps.addRowBelow : !caps.addRowAbove) return false;

    // Build a new row string with the same number of columns.
    std::string newRow = "|";
    for (int c = 0; c < loc.numCols; ++c) {
        newRow += "        |";
    }
    newRow += "\n";

    // Below: insert right after the current row's end.
    // Above: insert at the current row's start.
    uint32_t insertPos = below ? loc.rowEnd : loc.rowStart;
    if (below && insertPos < text.size() && text[insertPos] == '\n')
        insertPos += 1;

    // A table at the end of the document may have no trailing newline, in
    // which case loc.rowEnd is already the text end and the row would be
    // appended onto the last row's line. Start the new row on its own line.
    std::string prefix;
    if (insertPos > 0 && text[insertPos - 1] != '\n') prefix = "\n";

    SpliceWithUndo(insertPos, 0, prefix + newRow);
    editor_.BreakUndoCoalesce();

    // Below keeps the shipped caret (start of the row after the new
    // one). Above places the caret inside the new row's first cell,
    // one position past its leading pipe.
    const uint32_t caretBase = insertPos +
        static_cast<uint32_t>(prefix.size() + newRow.size());
    if (below) {
        sel_.Collapse({caretBase});
    } else {
        sel_.Collapse({insertPos + static_cast<uint32_t>(prefix.size()) + 1});
    }
    OnBufferChanged();
    ForceRepaintNow();
    return true;
}

void AppWindow::RemoveTable() {
    if (!editing_) return;
    const Node* tbl = FindContainingTable(doc_, sel_.active.offset);
    if (!tbl) return;

    const std::string& text = buffer_.Text();
    const uint32_t start = tbl->srcOffset;
    const uint32_t end = start + tbl->srcLength;

    // Where does the block after the table begin (pre-splice offsets)?
    // The table's trailing newline run is inside its span, so the next
    // block's source starts at `end` plus any extra blank lines that
    // follow (the run that belongs to the table was consumed with it).
    uint32_t following = end;
    {
        const uint32_t tail = static_cast<uint32_t>(text.size());
        uint32_t nl = end;
        while (nl < tail && text[nl] == '\n') ++nl;
        // Only trust a following block when a non-empty run points at
        // one; otherwise the caret rests on the removal site.
        bool found = false;
        if (nl < tail) {
            for (const Node& n : doc_.nodes) {
                if (n.block != BlockKind::Table && n.srcOffset == nl) {
                    following = nl;
                    found = true;
                    break;
                }
            }
        }
        if (!found) following = start;
    }

    // A table is always its own paragraph-separated block, so removing
    // its complete source span keeps the surrounding blank lines valid.
    // The table's length is known before the splice, so `following` (a
    // pre-splice offset) must be shifted left by that length before it is
    // used as a post-splice caret offset.
    const uint32_t removed = end - start;
    SpliceWithUndo(start, removed, "");
    editor_.BreakUndoCoalesce();

    uint32_t caret = following - removed;
    const uint32_t newLen = static_cast<uint32_t>(buffer_.Text().size());
    if (caret > newLen) caret = newLen;
    sel_.Collapse({caret});
    OnBufferChanged();
    ForceRepaintNow();
}

bool AppWindow::RemoveTableRow() {
    if (!editing_) return false;
    const Node* tbl = FindContainingTable(doc_, sel_.active.offset);
    if (!tbl) return false;

    const std::string& text = buffer_.Text();
    TableLocation loc;
    if (!LocateInTable(text, tbl, sel_.active.offset, loc)) return false;

    // The caret's physical line sits on the header (line 0) or the
    // delimiter (line 1): those lines carry the table's structure and
    // are never removed by this command, whatever the table size.
    if (!TableCapabilitiesFor(loc.rowIndex, loc.numCols).removeRow) {
        return false;
    }

    // Remove the row's source text (from lineStart to rowEnd).
    uint32_t start = loc.rowStart;
    uint32_t end = loc.rowEnd;
    SpliceWithUndo(start, end - start, "");
    editor_.BreakUndoCoalesce();

    sel_.Collapse({start});
    OnBufferChanged();
    ForceRepaintNow();
    return true;
}

bool AppWindow::AddTableColumn(bool right) {
    if (!editing_) return false;
    const Node* tbl = FindContainingTable(doc_, sel_.active.offset);
    if (!tbl) return false;

    const std::string& text = buffer_.Text();
    TableLocation loc;
    if (!LocateInTable(text, tbl, sel_.active.offset, loc)) return false;

    const uint32_t tblStart = tbl->srcOffset;
    const uint32_t tblEnd = tblStart + tbl->srcLength;

    // The rewrite covers the whole table span and lands as a single splice,
    // so one Ctrl+Z removes the whole column (spec section 46).
    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableInsertColumn(text, tblStart, tblEnd, loc.columnIndex, right,
                           sel_.active.offset, &rebuilt, &caret)) {
        return false;
    }

    SpliceWithUndo(tblStart, tblEnd - tblStart, rebuilt);
    editor_.BreakUndoCoalesce();
    sel_.Collapse({caret});
    OnBufferChanged();
    ForceRepaintNow();
    return true;
}

bool AppWindow::RemoveTableColumn() {
    if (!editing_) return false;
    const Node* tbl = FindContainingTable(doc_, sel_.active.offset);
    if (!tbl) return false;

    const std::string& text = buffer_.Text();
    TableLocation loc;
    if (!LocateInTable(text, tbl, sel_.active.offset, loc)) return false;

    if (!TableCapabilitiesFor(loc.rowIndex, loc.numCols).removeColumn)
        return false; // don't remove the last column

    const uint32_t tblStart = tbl->srcOffset;
    const uint32_t tblEnd = tblStart + tbl->srcLength;

    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableRemoveColumn(text, tblStart, tblEnd, loc.columnIndex,
                           sel_.active.offset, &rebuilt, &caret)) {
        return false;
    }

    SpliceWithUndo(tblStart, tblEnd - tblStart, rebuilt);
    editor_.BreakUndoCoalesce();
    sel_.Collapse({caret});
    OnBufferChanged();
    ForceRepaintNow();
    return true;
}

bool AppWindow::SetTableColumnAlign(TableAlignMark mark) {
    if (!editing_) return false;
    const Node* tbl = FindContainingTable(doc_, sel_.active.offset);
    if (!tbl) return false;

    const std::string& text = buffer_.Text();
    TableLocation loc;
    if (!LocateInTable(text, tbl, sel_.active.offset, loc)) return false;

    const uint32_t tblStart = tbl->srcOffset;
    const uint32_t tblEnd = tblStart + tbl->srcLength;

    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableSetColumnAlign(text, tblStart, tblEnd, loc.columnIndex, mark,
                             sel_.active.offset, &rebuilt, &caret)) {
        return false;
    }

    // One splice, so one table operation is one undo step.
    SpliceWithUndo(tblStart, tblEnd - tblStart, rebuilt);
    editor_.BreakUndoCoalesce();
    sel_.Collapse({caret});
    OnBufferChanged();
    ForceRepaintNow();
    return true;
}

// Split the caret's cell in two (table guidelines section 22). Markdown has
// no syntax for a merged cell, so there is no merge to pair this with.
bool AppWindow::SplitTableCell() {
    if (!editing_) return false;
    const Node* tbl = FindContainingTable(doc_, sel_.active.offset);
    if (!tbl) return false;

    const std::string& text = buffer_.Text();
    TableLocation loc;
    if (!LocateInTable(text, tbl, sel_.active.offset, loc)) return false;

    if (!TableCapabilitiesFor(loc.rowIndex, loc.numCols).splitCell)
        return false; // header and delimiter rows carry the table's shape

    const uint32_t tblStart = tbl->srcOffset;
    const uint32_t tblEnd = tblStart + tbl->srcLength;

    std::string rebuilt;
    uint32_t caret = 0;
    if (!TableSplitCell(text, tblStart, tblEnd, loc.columnIndex,
                        loc.rowIndex, 2, sel_.active.offset,
                        &rebuilt, &caret)) {
        return false;
    }

    // One splice for the whole table, so one Ctrl+Z undoes the whole split
    // rather than one row (guidelines section 46).
    SpliceWithUndo(tblStart, tblEnd - tblStart, rebuilt);
    editor_.BreakUndoCoalesce();
    sel_.Collapse({caret});
    OnBufferChanged();
    ForceRepaintNow();
    return true;
}

void AppWindow::ClearFormat() {
    if (!editing_) return;
    if (sel_.Empty()) return; // Need a selection to clear formatting.

    uint32_t origStart = sel_.Start();
    uint32_t origEnd = origStart + sel_.Length();
    const std::string& text = buffer_.Text();
    if (origEnd > text.size()) origEnd = static_cast<uint32_t>(text.size());

    // ── Collect ALL inline formatting spans that overlap the selection ──
    // For each span, record the marker region to remove. We'll splice
    // in reverse order so earlier removals don't shift later offsets.
    struct MarkerRemoval {
        uint32_t markerStart; // start of left markers
        uint32_t markerEnd;   // end of right markers
        uint32_t contentStart; // start of content (after left markers)
        uint32_t contentEnd;  // end of content (before right markers)
    };
    std::vector<MarkerRemoval> removals;

    for (const auto& node : doc_.nodes) {
        // Only look at blocks overlapping the selection.
        uint32_t bs = node.srcOffset;
        uint32_t be = bs + node.srcLength;
        if (origEnd <= bs || origStart >= be) continue;

        for (const auto& child : node.children) {
            // Only collect spans with formatting.
            if (!child.strong && !child.em && !child.code && !child.strike)
                continue;

            uint32_t cs = child.srcOffset;
            uint32_t ce = cs + child.srcLength;
            if (ce <= cs) continue; // skip empty spans

            // Check overlap with the selection.
            if (ce <= origStart || cs >= origEnd) continue;

            // Determine the marker char and count the run.
            // Bold and italic both use '*'. Strike uses '~'. Code uses '`'.
            // For combined bold+italic (***), the marker run includes all *.
            char mc = '*';
            if (child.code)   mc = '`';
            if (child.strike) mc = '~';

            // Scan left for consecutive marker chars.
            uint32_t leftRun = 0;
            while (cs > leftRun && text[cs - leftRun - 1] == mc)
                leftRun++;
            // Scan right for consecutive marker chars.
            uint32_t rightRun = 0;
            while (ce + rightRun < text.size() && text[ce + rightRun] == mc)
                rightRun++;

            if (leftRun > 0 && rightRun > 0) {
                removals.push_back({cs - leftRun, ce + rightRun, cs, ce});
            }
        }
    }

    // Sort in REVERSE order (rightmost first) so splices don't shift offsets.
    std::sort(removals.begin(), removals.end(),
              [](const MarkerRemoval& a, const MarkerRemoval& b) {
                  return a.markerStart > b.markerStart;
              });

    // Remove duplicate/overlapping removals (e.g. bold+italic share ***).
    // If two removals overlap, keep only the wider one.
    std::vector<MarkerRemoval> merged;
    for (const auto& r : removals) {
        if (!merged.empty() && r.markerEnd >= merged.back().markerStart) {
            // Overlaps with previous; merge by taking the wider range.
            merged.back().markerStart = std::min(merged.back().markerStart, r.markerStart);
            merged.back().markerEnd = std::max(merged.back().markerEnd, r.markerEnd);
            merged.back().contentStart = std::min(merged.back().contentStart, r.contentStart);
            merged.back().contentEnd = std::max(merged.back().contentEnd, r.contentEnd);
        } else {
            merged.push_back(r);
        }
    }

    // Splice: for each removal, replace the marker region with just content.
    Selection selBefore = sel_;
    int32_t totalDelta = 0;
    for (const auto& r : merged) {
        std::string content = buffer_.Text().substr(r.contentStart, r.contentEnd - r.contentStart);
        std::string removed = buffer_.Text().substr(r.markerStart, r.markerEnd - r.markerStart);
        buffer_.Splice(r.markerStart, r.markerEnd - r.markerStart, content);
        UndoEntry entry{};
        entry.offset = r.markerStart;
        entry.removed = removed;
        entry.inserted = content;
        entry.selBefore = selBefore;
        entry.selAfter = sel_;
        entry.type = EditType::Other;
        undo_stack_.Push(entry);
        totalDelta += static_cast<int32_t>(removed.size()) - static_cast<int32_t>(content.size());
    }
    // Keep selection covering the same text (now without markers).
    uint32_t newEnd = (origEnd > static_cast<uint32_t>(totalDelta))
                      ? origEnd - static_cast<uint32_t>(totalDelta) : origStart;
    sel_.anchor = {origStart};
    sel_.active = {newEnd};


    // Remove BLOCK-level formatting: headings, lists, blockquotes.
    FormatState fs = GetFormatState(); // re-read after inline changes
    if (fs.headingLevel > 0) {
        SetHeadingLevel(&buffer_, &sel_, 0, &undo_stack_);
    }
    if (fs.inBullets)  ToggleUnorderedList(&buffer_, &sel_, &undo_stack_);
    if (fs.inNumbering) ToggleOrderedList(&buffer_, &sel_, &undo_stack_);
    if (fs.inQuote)    ToggleBlockquote(&buffer_, &sel_, &undo_stack_);

    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::SetHeading(int level) {
    if (!editing_) return;
    // Toggle: if the caret is already in a heading of this level,
    // remove the heading (revert to paragraph).
    FormatState fs = GetFormatState();
    if (fs.headingLevel == level) {
        SetHeadingLevel(&buffer_, &sel_, 0, &undo_stack_);
    } else {
        SetHeadingLevel(&buffer_, &sel_, level, &undo_stack_);
    }
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    // The ribbon button click steals focus from the content window, so the
    // deferred InvalidateRect in Repaint() may not deliver a WM_PAINT until
    // the user clicks back into the document. Force an immediate repaint.
    ForceRepaintNow();
}

void AppWindow::ToggleBullets() {
    if (!editing_) return;
    ToggleUnorderedList(&buffer_, &sel_, &undo_stack_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::ToggleNumbering() {
    if (!editing_) return;
    ToggleOrderedList(&buffer_, &sel_, &undo_stack_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::ToggleQuote() {
    if (!editing_) return;
    ToggleBlockquote(&buffer_, &sel_, &undo_stack_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::Indent() {
    if (!editing_) return;
    IndentLine(&buffer_, &sel_, &undo_stack_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::Outdent() {
    if (!editing_) return;
    OutdentLine(&buffer_, &sel_, &undo_stack_);
    editor_.BreakUndoCoalesce();
    OnBufferChanged();
    ForceRepaintNow();
}

void AppWindow::RecordPendingFormatUndo(bool beforeBold, bool beforeItalic,
                                         bool beforeBoldSet, bool beforeItalicSet) {
    UndoEntry entry{};
    entry.offset = sel_.active.offset;
    entry.selBefore = sel_;
    entry.selAfter = sel_;
    entry.timestamp = GetTickCount64();
    entry.type = EditType::PendingFormat;
    entry.hasPendingFormat = true;
    entry.pendingBold = beforeBold;
    entry.pendingItalic = beforeItalic;
    entry.pendingBoldSet = beforeBoldSet;
    entry.pendingItalicSet = beforeItalicSet;
    entry.afterPendingBold = pending_bold_;
    entry.afterPendingItalic = pending_italic_;
    entry.afterPendingBoldSet = pending_bold_set_;
    entry.afterPendingItalicSet = pending_italic_set_;
    undo_stack_.Push(entry);

    // Store the after-state in the same entry by pushing it through the
    // redo side is not possible with the public stack API. The current
    // state is deterministic from the before-state and this toggle, so
    // ApplyUndo/ApplyRedo recomputes the opposite state for this entry.
}

void AppWindow::ApplyUndo(bool redo) {
    UndoEntry entry{};
    bool changed = redo ? editor_.Redo(&entry) : editor_.Undo(&entry);
    if (!changed) return;
    if (entry.hasPendingFormat) {
        if (redo) {
            pending_bold_ = entry.afterPendingBold;
            pending_italic_ = entry.afterPendingItalic;
            pending_bold_set_ = entry.afterPendingBoldSet;
            pending_italic_set_ = entry.afterPendingItalicSet;
        } else {
            pending_bold_ = entry.pendingBold;
            pending_italic_ = entry.pendingItalic;
            pending_bold_set_ = entry.pendingBoldSet;
            pending_italic_set_ = entry.pendingItalicSet;
        }
        pending_run_active_ = false;
        pending_run_suffix_bytes_ = 0;
    }
    OnBufferChanged();
    InvalidateFormatButtons();
    ForceRepaintNow();
}

void AppWindow::UndoAction() {
    if (!editing_) return;
    ApplyUndo(false);
}

void AppWindow::RedoAction() {
    if (!editing_) return;
    ApplyUndo(true);
}



void AppWindow::OnDestroy() {
    // The bar is an owned popup: tear its windows down before this window
    // disappears, so nothing is left pointing at a dead owner.
    find_bar_.Destroy();
    StopScrollAnimation();
    SaveWinPlacement(hwnd_);
    DestroyRibbon();
    watcher_.Stop();
    renderer_.Release();
    SafeRelease(d2d_ctx5_);
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
    // EnsureRenderer above re-inits because renderer_inited_ was reset first,
    // but if the release/init round-trip is skipped (inited stayed false on
    // init failure), still republish the current device context:
    // RecreateRenderTarget may have swapped d2d_ctx5_ since the last init.
    renderer_.SetD2DDeviceContext5(d2d_ctx5_);
}

void AppWindow::OpenFileDialog() {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Markdown (*.md;*.markdown;*.mmd;*.svg)\0*.md;*.markdown;*.mmd;*.svg\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = L"Open Markdown File";
    if (GetOpenFileNameW(&ofn)) {
        OpenFile(buf);
    }
}

//
// Word / PDF interop (ribbon: Home tab, Office group).
//
// The conversion itself lives in src/office/: office::DocxImport reads a
// .docx package, office::DocxExport writes one, office::PdfExport renders
// one, and office::MarkdownToOfficeModel / office::OfficeModelToMarkdown
// translate between the document model and the markdown in the editor.
// These handlers only read and write files, load the editor and report what
// the conversion could not carry.
//

// UTF-8 (the office layer's string type) to UTF-16 for the Win32 dialogs.
static std::wstring OfficeUtf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), wide.data(), needed);
    return wide;
}

// The compatibility report as dialog text: one "feature: detail" line per
// dropped or approximated feature. Empty when there was nothing to report.
static std::wstring CompatReportText(const office::CompatReport& report) {
    std::wstring text;
    for (const office::CompatWarning& warning : report.warnings) {
        text += L"- ";
        text += OfficeUtf8ToWide(warning.feature);
        if (!warning.detail.empty()) {
            text += L": ";
            text += OfficeUtf8ToWide(warning.detail);
        }
        text += L"\n";
    }
    return text;
}

// Read a whole file into a string. False when it cannot be opened.
static bool ReadAllBytes(const std::wstring& path, std::string& out) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Write a produced file the way DoSave writes the markdown source: a temp
// file beside the target, then an atomic replace. The save dialog has
// already asked before an existing file is replaced (OFN_OVERWRITEPROMPT),
// and only that dialog can name the target, so an export never writes over
// the document it was made from.
static bool WriteExportBytes(const std::wstring& path, const std::string& bytes) {
    std::wstring tempPath = path + L".mdtmp";
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, tempPath.c_str(), L"wb") != 0 || !fp) return false;
    if (fwrite(bytes.data(), 1, bytes.size(), fp) != bytes.size()) {
        fclose(fp);
        DeleteFileW(tempPath.c_str());
        return false;
    }
    fflush(fp);
    fclose(fp);
    if (!MoveFileExW(tempPath.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return true;
}

// Import a Word .docx package as markdown in the editor. The dialog takes
// .docx; a legacy .doc or an encrypted package reaches the importer and its
// error message says so instead of crashing.
void AppWindow::ImportWordDocx() {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Word documents (*.docx)\0*.docx\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = L"Import Word (.docx)";
    if (!GetOpenFileNameW(&ofn)) return;

    const wchar_t* const caption = L"MarkDownIt - Import Word (.docx)";
    const std::wstring path = buf;

    std::string bytes;
    if (!ReadAllBytes(path, bytes)) {
        MessageBoxW(hwnd_, L"Could not read the selected file.", caption,
                    MB_ICONWARNING);
        return;
    }

    office::DocModel model;
    office::CompatReport report;
    std::string error;
    if (!office::DocxImport(bytes, model, report, error)) {
        // Empty, unreadable, legacy .doc and encrypted packages all land
        // here with the importer's own description of the file.
        std::wstring message = L"Could not import the Word document.";
        if (!error.empty()) {
            message += L"\n\n";
            message += OfficeUtf8ToWide(error);
        }
        MessageBoxW(hwnd_, message.c_str(), caption, MB_ICONWARNING);
        return;
    }

    // The document as markdown; the report gains everything markdown has no
    // form for (a page break, for instance).
    const std::string markdown = office::OfficeModelToMarkdown(model, report);

    // Same load path as a document read from disk. The markdown has no file
    // of its own yet: untitled and dirty, so Save asks for a name and
    // closing asks whether to save.
    if (editing_) SetEdit(false);
    LoadDocumentText(markdown, std::wstring());
    MarkDirty();

    const std::wstring notes = CompatReportText(report);
    if (!notes.empty()) {
        std::wstring message = L"The document was imported with these "
                               L"compatibility notes:\n\n";
        message += notes;
        MessageBoxW(hwnd_, message.c_str(), caption, MB_ICONINFORMATION);
    }
}

// Export the current document as Word .docx. The save dialog is always in
// the way, so the markdown source is never overwritten by an export.
void AppWindow::ExportWordDocx() {
    // Danish caption, same words as the ribbon label. The e-acute is
    // written as the wide escape L"\x00e9" (U+00E9), so this source
    // file stays plain ASCII: the build has no /utf-8 flag, and a raw
    // UTF-8 byte in a wide literal would be read in the ANSI codepage.
    const wchar_t* const caption = L"MarkDownIt - Eksport" L"\x00e9" L"r Word (.docx)";

    if (welcome_mode_) {
        MessageBoxW(hwnd_, L"No document is open. Open a markdown file first.",
                    caption, MB_ICONWARNING);
        return;
    }
    // The document's markdown is the buffer content, the same text Save
    // writes (see DoSave).
    const std::string markdown = buffer_.Text();
    if (markdown.empty()) {
        MessageBoxW(hwnd_, L"The document is empty. Nothing to export.",
                    caption, MB_ICONWARNING);
        return;
    }

    office::DocModel model;
    office::CompatReport report;
    std::string error;
    if (!office::MarkdownToOfficeModel(markdown, model, report, error)) {
        std::wstring message = L"Could not convert the document for the export.";
        if (!error.empty()) {
            message += L"\n\n";
            message += OfficeUtf8ToWide(error);
        }
        MessageBoxW(hwnd_, message.c_str(), caption, MB_ICONWARNING);
        return;
    }

    std::string bytes;
    if (!office::DocxExport(model, bytes, report, error) || bytes.empty()) {
        std::wstring message = L"Could not write the Word document.";
        if (!error.empty()) {
            message += L"\n\n";
            message += OfficeUtf8ToWide(error);
        }
        MessageBoxW(hwnd_, message.c_str(), caption, MB_ICONWARNING);
        return;
    }

    // The save dialog is the only way an export target is chosen, so an
    // export never writes over the markdown document it was made from, and
    // the dialog itself (OFN_OVERWRITEPROMPT) asks before replacing a file.
    const std::wstring path = ExportSaveDialog(
        L"Word documents (*.docx)\0*.docx\0All Files\0*.*\0", L"docx",
        ExportDefaultName(L".docx"));
    if (path.empty()) return;  // cancelled

    if (!WriteExportBytes(path, bytes)) {
        MessageBoxW(hwnd_, L"Could not write the file. The export was not saved.",
                    caption, MB_ICONWARNING);
        return;
    }

    const std::wstring notes = CompatReportText(report);
    if (!notes.empty()) {
        std::wstring message = L"The document was exported to:\n";
        message += path;
        message += L"\n\nCompatibility notes:\n\n";
        message += notes;
        MessageBoxW(hwnd_, message.c_str(), caption, MB_ICONINFORMATION);
    }
}

// Export the current document as PDF, the whole document, and report the
// page count the exporter returns.
void AppWindow::ExportPdf() {
    const wchar_t* const caption = L"MarkDownIt - Eksport" L"\x00e9" L"r PDF";

    if (welcome_mode_) {
        MessageBoxW(hwnd_, L"No document is open. Open a markdown file first.",
                    caption, MB_ICONWARNING);
        return;
    }
    const std::string markdown = buffer_.Text();
    if (markdown.empty()) {
        MessageBoxW(hwnd_, L"The document is empty. Nothing to export.",
                    caption, MB_ICONWARNING);
        return;
    }

    office::DocModel model;
    office::CompatReport report;
    std::string error;
    if (!office::MarkdownToOfficeModel(markdown, model, report, error)) {
        std::wstring message = L"Could not convert the document for the export.";
        if (!error.empty()) {
            message += L"\n\n";
            message += OfficeUtf8ToWide(error);
        }
        MessageBoxW(hwnd_, message.c_str(), caption, MB_ICONWARNING);
        return;
    }

    // The whole document: the model carries the page setup.
    office::PdfExportOptions options;
    options.all_pages = true;
    options.first_page = 1;
    options.last_page = 0;

    std::string bytes;
    int page_count = 0;
    if (!office::PdfExport(model, options, bytes, report, error, &page_count) ||
        bytes.empty()) {
        std::wstring message = L"Could not write the PDF file.";
        if (!error.empty()) {
            message += L"\n\n";
            message += OfficeUtf8ToWide(error);
        }
        MessageBoxW(hwnd_, message.c_str(), caption, MB_ICONWARNING);
        return;
    }

    // Same contract as the Word export: the dialog names the target.
    const std::wstring path = ExportSaveDialog(
        L"PDF documents (*.pdf)\0*.pdf\0All Files\0*.*\0", L"pdf",
        ExportDefaultName(L".pdf"));
    if (path.empty()) return;  // cancelled

    if (!WriteExportBytes(path, bytes)) {
        MessageBoxW(hwnd_, L"Could not write the file. The export was not saved.",
                    caption, MB_ICONWARNING);
        return;
    }

    std::wstring message = L"The document was exported to:\n";
    message += path;
    wchar_t pageText[32] = {};
    swprintf_s(pageText, 32, L"\n\nPages: %d", page_count);
    message += pageText;
    const std::wstring notes = CompatReportText(report);
    if (!notes.empty()) {
        message += L"\n\nCompatibility notes:\n\n";
        message += notes;
    }
    MessageBoxW(hwnd_, message.c_str(), caption, MB_ICONINFORMATION);
}

// Apply a new zoom factor with a stable anchor: the document point
// under the mouse cursor (or the viewport center when the cursor is
// elsewhere) stays at the same screen position. All layout measures
// scale linearly with zoom, so a doc point at scrollY_ + anchorY sits
// at (scrollY_ + anchorY) * newZoom/oldZoom after the change; solve
// for the scroll offset that puts it back under the anchor.
void AppWindow::ApplyZoom(float newZoom, float focusYdip, float focusXdip) {
    float oldZoom = renderer_.GetZoom();
    renderer_.SetZoom(newZoom);  // clamps to kMinZoom..kMaxZoom
    const float newZoomClamped = renderer_.GetZoom();
    if (newZoomClamped == oldZoom) return;

    // Persist the new factor as a global view setting (see OnCreate):
    // the last zoom used is restored on the next launch, and all
    // documents in this session share it. ResetZoom() persists 1.0,
    // so a reset stays at 100% after a restart too.
    settings_.zoomFactor = renderer_.GetZoom();
    SaveSettings(settings_);

    // The focus point comes from the caller: the pointer for a wheel
    // gesture, the viewport centre for a button or a keyboard shortcut.
    // Probing the cursor here instead made a ribbon click anchor on an
    // arbitrary point whenever the mouse happened to be over the text.
    const float anchorY = focusYdip;
    const float anchorX = focusXdip;

    // Stop any in-flight scroll animation so it cannot fight the jump.
    StopScrollAnimation();

    // Re-create the text formats at the new zoom so Measure and Render
    // agree; clear zoom-dependent caches (metrics, hit-test rects, SVG).
    RecreateRenderer();
    layout_cache_.Clear();
    renderer_.ClearSvgCache();

    if (oldZoom > 0.001f) {
        const float k = newZoomClamped / oldZoom;
        // Same formula the zoom guide gives, applied per axis:
        // t' = a - s'((a - t) / s), written to keep the factor explicit.
        scrollY_ = (scrollY_ + anchorY) * k - anchorY;
        scrollX_ = (scrollX_ + anchorX) * k - anchorX;
        if (scrollX_ < 0.0f) scrollX_ = 0.0f;
    }

    // Re-measure at the new zoom so the clamp below uses the new total
    // height; UpdateScrollInfo clamps scrollY_ into [0, maxScroll].
    if (renderer_inited_ && dw_factory_ && rt_) {
        D2D1_SIZE_F size = rt_->GetSize();
        if (source_view_) {
            totalH_ = renderer_.MeasureSourceView(
                dw_factory_, buffer_.Text(), size.width, 0.0f);
        } else {
            totalH_ = renderer_.Measure(dw_factory_, doc_, size.width, 0.0f);
        }
    }
    UpdateScrollInfo();
    Repaint();

    // Re-query the zoom buttons. Without this they keep whatever enabled
    // state they were given at startup, so they stay live at the maximum
    // and enabled below the minimum.
    if (g_pRibbonFramework) {
        g_pRibbonFramework->InvalidateUICommand(IDC_CMD_ZOOMIN,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
        g_pRibbonFramework->InvalidateUICommand(IDC_CMD_ZOOMOUT,
            UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
    }
}

// Viewport centre, in content-window DIPs. A button or keyboard zoom uses
// this as its focus point; a wheel gesture uses the pointer instead.
static float CenterYDip(ID2D1RenderTarget* rt) {
    if (!rt) return 0.0f;
    return rt->GetSize().height * 0.5f;
}
static float CenterXDip(ID2D1RenderTarget* rt) {
    if (!rt) return 0.0f;
    return rt->GetSize().width * 0.5f;
}

void AppWindow::ZoomIn() {
    ApplyZoom(zoom::Step(renderer_.GetZoom(), true),
              CenterYDip(rt_), CenterXDip(rt_));
}

void AppWindow::ZoomOut() {
    ApplyZoom(zoom::Step(renderer_.GetZoom(), false),
              CenterYDip(rt_), CenterXDip(rt_));
}

void AppWindow::ResetZoom() {
    ApplyZoom(renderer_.kDefaultZoom, CenterYDip(rt_), CenterXDip(rt_));
}

bool AppWindow::CanZoomIn() const {
    return renderer_.GetZoom() < renderer_.kMaxZoom - 1e-4f;
}

bool AppWindow::CanZoomOut() const {
    return renderer_.GetZoom() > renderer_.kMinZoom + 1e-4f;
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

void AppWindow::SelectAll() {

    // Progressive selection tiers when the caret sits in a table cell
    // (plan 19): cell, then row, then whole table, then whole document.
    // The run escalates only while the caret has not moved between
    // presses; the tier resets when any other key or click intervenes.
    if (sel_.active.offset != last_selectall_caret_) {
        last_selectall_tier_ = 0;
    }
    TableCellRef cell;
    // Source view shows the Markdown text, not rendered cells, so the
    // cell/row/table escalation has nothing to select there. Ctrl+A is
    // the whole buffer on screen, in one press.
    const bool inTable = !source_view_ &&
        TableCellAtOffset(doc_, buffer_.Text(), sel_.active.offset, &cell);
    const uint32_t textLen = static_cast<uint32_t>(buffer_.Text().size());
    const bool activeCell = inTable && !cell.separatorRow &&
                            cell.srcEnd > cell.srcOffset;
    if (activeCell && last_selectall_tier_ < 4) {
        const Node& node = doc_.nodes[cell.tableIndex];
        if (last_selectall_tier_ == 0) {
            // Tier 1: the current cell.
            last_selectall_tier_ = 1;
            sel_.anchor = {cell.srcOffset};
            sel_.active = {std::min(cell.srcEnd, textLen)};
            last_selectall_caret_ = std::min(cell.srcEnd, textLen);
            if (editing_) UpdateCaretPosition();
            Repaint();
            return;
        }
        if (last_selectall_tier_ == 1 && cell.rowIndex < node.rows.size()) {
            // Tier 2: the entire row.
            const TableRow& row = node.rows[cell.rowIndex];
            uint32_t rowStart = UINT32_MAX;
            uint32_t rowEnd = 0;
            for (const TableCell& c : row.cells) {
                rowStart = std::min(rowStart, c.srcOffset);
                rowEnd = std::max(rowEnd, c.srcEnd);
            }
            if (rowEnd > rowStart) {
                last_selectall_tier_ = 2;
                sel_.anchor = {rowStart};
                sel_.active = {std::min(rowEnd, textLen)};
                last_selectall_caret_ = std::min(rowEnd, textLen);
                if (editing_) UpdateCaretPosition();
                Repaint();
                return;
            }
        }
        if (last_selectall_tier_ < 3) {
            // Tier 3: the whole table.
            last_selectall_tier_ = 3;
            sel_.anchor = {node.srcOffset};
            sel_.active = {std::min(node.srcOffset + node.srcLength, textLen)};
            last_selectall_caret_ = std::min(node.srcOffset + node.srcLength, textLen);
            if (editing_) UpdateCaretPosition();
            Repaint();
            return;
        }
    }
    last_selectall_tier_ = 0;
    last_selectall_caret_ = sel_.active.offset;

    sel_.anchor = {0};
    sel_.active = {static_cast<uint32_t>(buffer_.Length())};
    if (!editing_) InvalidateMarkerToggleUI();
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
#define CM_DELTABLE 2015

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

        if (IsOffsetInTable(doc_, sel_.active.offset)) {
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, CM_DELTABLE, L"Delete Table");
        }
        AppendMenuW(hMenu, MF_STRING, CM_EDIT,
            L"Switch to View Mode\tCtrl+E");
    } else {
        // ── View mode menu ──
        bool hasSel = !sel_.Empty();

        AppendMenuW(hMenu, MF_STRING | (hasSel ? 0 : MF_GRAYED),
            CM_COPY, L"Copy\tCtrl+C");
        // Cut and paste are offered here as well as in edit mode: both
        // switch to edit mode, since neither has a caret to work from in
        // view mode. Cut stays greyed with no selection, like the spec
        // requires for an empty selection.
        AppendMenuW(hMenu, MF_STRING | (hasSel ? 0 : MF_GRAYED),
            CM_CUT, L"Cut\tCtrl+X");
        AppendMenuW(hMenu, MF_STRING, CM_PASTE, L"Paste\tCtrl+V");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
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
    case CM_UNDO:    ApplyUndo(false); break;
    case CM_REDO:    ApplyUndo(true); break;
    case CM_CUT:
        if (!sel_.Empty()) {
            // Same rule as Ctrl+X: a cut needs a caret, so view mode
            // enters edit mode here too.
            if (!editing_) SetEdit(true);
            std::string sel_text = SelectionForClipboard();
            if (ClipboardCut(hwnd_content_, sel_text)) {
                editor_.DeleteSelection();
                OnBufferChanged();
            }
        }
        break;
    case CM_COPY:
        if (!sel_.Empty()) {
            uint32_t s = sel_.Start(), len = sel_.Length();
            std::string sel_text = SelectionForClipboard();
            ClipboardCopy(hwnd_content_, sel_text);
        }
        break;
    case CM_PASTE: {
        std::string text = ClipboardPaste(hwnd_content_);
        // Same as Ctrl+V: enter edit mode on a real paste.
        if (!text.empty() && !editing_) SetEdit(true);
        // Same in-cell paste rule as Ctrl+V: strip row syntax.
        if (!text.empty() && IsOffsetInTable(doc_, sel_.active.offset))
            text = SanitizePasteForTableCell(text);
        if (!text.empty()) {
            editor_.InsertText(text);
            OnBufferChanged();
        }
        break;
    }
    case CM_DELTABLE: RemoveTable(); break;
    case CM_SELALL:  SelectAll(); break;
    case CM_BOLD:   ToggleBold();  break;
    case CM_ITALIC: ToggleItalic(); break;
    case CM_CODE:   ToggleCode();  break;
    case CM_EDIT:   ToggleEdit();  break;
    case CM_WRAP:   ToggleWrap(); break;
    case CM_ZOOMIN: ZoomIn(); break;
    case CM_ZOOMOUT: ZoomOut();  break;
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
    if (hwnd_) {
        SetTimer(hwnd_, 6, 350, nullptr);
    }
    if (hwnd_content_) {
        PostMessage(hwnd_content_, WM_USER + 2, 0, 0);
    }
}

int AppWindow::GetContentWidthMode() const {
    return settings_.contentWidthMode;
}

void AppWindow::SetContentWidthMode(int mode) {
    diag::Trace("SetContentWidthMode enter");
    if (mode < 0 || mode > 3) mode = 0;
    settings_.contentWidthMode = mode;
    renderer_.SetContentWidthMode(mode);
    SaveSettings(settings_);
    // Defer ALL side effects (InvalidateUICommand, repaint) via PostMessage
    // to avoid re-entrant calls inside the Ribbon Execute callback.
    layout_cache_.Clear();
    renderer_.ClearSvgCache();
    diag::Trace("SetContentWidthMode caches cleared");
    // Defer the invalidation past the closing popup. (The Fil-click CTD was
    // later root-caused to UpdateProperty returning SysAllocString'd
    // VT_LPWSTR labels that PropVariantClear frees with CoTaskMemFree,
    // corrupting the heap; ribbon.cpp now allocates labels with a
    // CoTaskMemAlloc copy (SetCmdLabel). The 350 ms timer remains as a
    // cheap guarantee
    // that invalidation never runs while a popup is animating closed.)
    // Timer id 6 (free: 1/3/5 are taken on hwnd_).
    if (hwnd_) {
        SetTimer(hwnd_, 6, 350, nullptr);
    }
    if (hwnd_content_) {
        PostMessage(hwnd_content_, WM_USER + 1, 0, 0);
    }
    diag::Trace("SetContentWidthMode posted");
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
        case WM_CREATE:    OnCreate(hwnd);   return 0;
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
            UpdateDpi();
            // RecreateRenderer resets renderer_inited_ before re-init;
            // EnsureRenderer alone would early-return and leave the
            // renderer destroyed (blank content until a zoom change).
            RecreateRenderer();
            RECT* rc = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, rc->left, rc->top,
                rc->right - rc->left, rc->bottom - rc->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            ResizeContentWindow();
            Repaint();
            diag::TraceFmt("WM_DPICHANGED dpi=%d findvis=%d", dpi_,
                           find_bar_.IsVisible() ? 1 : 0);
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
            } else if (wp == 6) {
                // Deferred Ribbon button invalidation: the ApplicationMenu
                // popup has fully closed by now, so invalidating the label
                // properties cannot corrupt the framework anymore (CTD fix).
                KillTimer(hwnd_, 6);
                diag::Trace("timer6 InvalidateSettingsButtons begin");
                InvalidateSettingsButtons();
                diag::Trace("timer6 InvalidateSettingsButtons done");
            }
            return 0;
        case FileWatcher::WM_USER_RELOAD: OnReload(); return 0;
        case WM_ERASEBKGND: {
            // Fill the client so any uncovered band, for example while the
            // find strip re-docks after a monitor or DPI switch, reads as
            // chrome instead of as a black hole. WS_CLIPCHILDREN keeps the
            // fill out of the children's areas.
            HDC dc = reinterpret_cast<HDC>(wp);
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
            return 1;
        }
        case WM_CLOSE:      OnClose();        return 0;
        case WM_DESTROY:   OnDestroy();  return 0;
        case WM_PAINT: {
            // The main window owns no visuals of its own, the ribbon and
            // the content child paint themselves, but it still paints the
            // chrome fill above so uncovered bands never show black.
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(hwnd, &ps);
            if (dc) {
                RECT rc = {};
                GetClientRect(hwnd, &rc);
                FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
            }
            EndPaint(hwnd, &ps);
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
        case WM_APP + 1: {
            // Grid table picker sent col (wParam) and row (lParam).
            ReleaseCapture();
            InsertTableFromGrid(static_cast<int>(wp),
                                static_cast<int>(lp));
            return 0;
        }
        case WM_PAINT:     OnContentPaint(hwnd); return 0;
        case WM_ERASEBKGND: return 1; // D2D handles all painting
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
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            margin_selecting_ = false;
            margin_anchor_block_ = -1;
            word_dragging_ = false;
            paragraph_dragging_ = false;
            selection_dragging_ = false;
            text_drag_candidate_ = false;
            text_dragging_ = false;
            text_drag_start_ = 0;
            text_drag_length_ = 0;
            paragraph_anchor_block_ = -1;
            return 0;
        case WM_LBUTTONUP:   OnLButtonUp(hwnd);   return 0;
        case WM_CONTEXTMENU: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            ShowContextMenu(x, y);
            return 0;
        }
        case WM_IME_COMPOSITION:
            OnImeComposition(lp);
            return 0;
        case WM_IME_ENDCOMPOSITION:
            OnImeEndComposition();
            return 0;
        case WM_CHAR:        OnChar(hwnd, static_cast<wchar_t>(wp)); return 0;
        case WM_SETFOCUS:  OnSetFocus(hwnd);  return 0;
        case WM_KILLFOCUS: OnKillFocus(hwnd); return 0;
        case WM_VSCROLL:    OnContentVScroll(hwnd, (int)LOWORD(wp), (int)HIWORD(wp)); return 0;
        case WM_HSCROLL:    OnContentHScroll(hwnd, (int)LOWORD(wp), (int)HIWORD(wp)); return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            // Ctrl+wheel zooms instead of scrolling; ApplyZoom anchors on
            // the point under the cursor. Deltas accumulate in
            // wheel_zoom_acc_ so precision trackpads (small deltas) also
            // step once per 120 WHEEL_DELTA notch. Returns early: no
            // scrolling path runs for the same event.
            if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
                // WM_MOUSEWHEEL packs the cursor position in screen
                // coordinates; the zoom has to keep whatever content point
                // sits under it in the same place on screen.
                float focusX = CenterXDip(rt_);
                float focusY = CenterYDip(rt_);
                POINT pt = {};
                if (hwnd_content_ && GetCursorPos(&pt)) {
                    ScreenToClient(hwnd_content_, &pt);
                    RECT rc = {};
                    GetClientRect(hwnd_content_, &rc);
                    if (pt.x >= 0 && pt.y >= 0 &&
                        pt.x < rc.right && pt.y < rc.bottom) {
                        // Every layout measure is in DIPs, so the anchor
                        // must be too. At 150% scaling the raw client
                        // pixels overshoot and the point under the cursor
                        // drifts away from where it was.
                        const float dip = 96.0f / static_cast<float>(dpi_);
                        focusX = static_cast<float>(pt.x) * dip;
                        focusY = static_cast<float>(pt.y) * dip;
                    }
                }
                wheel_zoom_acc_ += static_cast<float>(delta);
                while (wheel_zoom_acc_ >= WHEEL_DELTA) {
                    ApplyZoom(zoom::Step(renderer_.GetZoom(), true),
                              focusY, focusX);
                    wheel_zoom_acc_ -= WHEEL_DELTA;
                }
                while (wheel_zoom_acc_ <= -WHEEL_DELTA) {
                    ApplyZoom(zoom::Step(renderer_.GetZoom(), false),
                              focusY, focusX);
                    wheel_zoom_acc_ += WHEEL_DELTA;
                }
                return 0;
            }
            wheel_zoom_acc_ = 0.0f;
            // Shift+wheel scrolls sideways. The guide asks for this where
            // horizontal scrolling exists, which it now does at high zoom.
            // Natural direction is preserved: shift+wheel down still moves
            // content down, it just moves the window right.
            if (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) {
                float shift = -static_cast<float>(delta) / 40.0f;
                scrollX_ += shift;
                if (scrollX_ < 0.0f) scrollX_ = 0.0f;
                UpdateScrollInfo();
                Repaint();
                UpdateCaretPosition();
                return 0;
            }
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
            } else if (wp == 2) {
                // Reparse debounce timer for large documents
                // (armed by ScheduleReparse at >= 100 KB).
                KillTimer(hwnd_content_, 2);
                reparse_timer_ = 0;
                OnReparseTimer();
            }
            return 0;
        case WM_USER + 1: {  // Deferred settings change (width, etc.)
            diag::Trace("WM_USER+1 enter");
            // Invalidations moved to timer 6 (see SetContentWidthMode);
            // only scroll + repaint remain here.
            UpdateScrollInfo();
            diag::Trace("WM_USER+1 scroll info updated");
            RedrawWindow(hwnd_content_, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
            diag::Trace("WM_USER+1 repaint done");
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
            // Invalidations now run on timer 6 once the Ribbon popup is
            // fully closed; direct invalidation here corrupts the framework.
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
