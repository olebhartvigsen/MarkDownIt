// Runtime test for the find bar's Win32 plumbing. Windows only: the bar is
// built from real windows, and the break this guards against lived exactly
// where the portable tests cannot look.
//
// The bar is a plain CreateWindowExW window, not a dialog. Its window
// procedures recover the FindBar instance from GWLP_USERDATA, a slot every
// window has. Stored in a dialog-only index instead, the pointer becomes
// unreadable, every subclassed control falls through to DefWindowProc, and
// DefWindowProc paints no button face, no static text and no edit content:
// the bar opens as an empty rectangle. This test drives the real messages
// and fails on that failure mode.

#ifdef _WIN32

#include "gtest_lite.h"

#include <windows.h>

#include <iostream>

#include "findbar.h"

namespace {

LRESULT CALLBACK OwnerWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The bare counterpart of the app window: an owner the bar can attach to.
HWND CreateOwnerWindow() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = &OwnerWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MarkDownItFindBarRuntimeTestOwner";
    RegisterClassW(&wc);
    return CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 320,
                           240, nullptr, nullptr, GetModuleHandleW(nullptr),
                           nullptr);
}

}  // namespace

TEST(FindBarWindow, ControlsExistAndMessagesReachTheBar) {
    HWND owner = CreateOwnerWindow();
    if (!owner) {
        // Skipped only in a session that cannot create windows at all.
        std::cout << "  (skipped: this session cannot create windows)\n";
        return;
    }

    FindBar bar;
    int closes = 0;
    int searches = 0;
    FindBarListener listener;
    listener.onClose = [&closes]() { ++closes; };
    listener.onSearchChanged = [&searches]() { ++searches; };
    bar.SetListener(listener);

    EXPECT_TRUE(bar.Show(owner, false));
    HWND bar_wnd = bar.Handle();
    EXPECT_TRUE(bar_wnd != nullptr);
    if (!bar_wnd) {
        DestroyWindow(owner);
        return;
    }

    // The instance pointer must be readable from the bar window and from
    // every child. When a window procedure reads null there, it stops doing
    // its job: no painting, no input and no commands.
    const LONG_PTR instance = GetWindowLongPtrW(bar_wnd, GWLP_USERDATA);
    EXPECT_TRUE(instance != 0);
    for (int id = kFindBarFindEdit; id <= kFindBarStatus; ++id) {
        HWND child = GetDlgItem(bar_wnd, id);
        EXPECT_TRUE(child != nullptr);
        if (child) {
            EXPECT_TRUE(GetWindowLongPtrW(child, GWLP_USERDATA) == instance);
        }
    }

    // Typing must reach the state through the real EDIT control and the
    // child subclass chain, and report a search change to the owner.
    HWND find_edit = GetDlgItem(bar_wnd, kFindBarFindEdit);
    EXPECT_TRUE(find_edit != nullptr);
    if (find_edit) {
        SendMessageW(find_edit, WM_CHAR, L'a', 0);
        SendMessageW(find_edit, WM_CHAR, L'b', 0);
    }
    EXPECT_TRUE(bar.GetSearchText() == L"ab");
    EXPECT_TRUE(searches >= 1);

    // Tab must walk the bar's own controls. Focus is not asserted here: an
    // unattended session may not hold activation, but the walk runs anyway.
    if (find_edit) SendMessageW(find_edit, WM_KEYDOWN, VK_TAB, 0);

    // Escape from the Find field closes the bar and tells the owner.
    if (find_edit) SendMessageW(find_edit, WM_KEYDOWN, VK_ESCAPE, 0);
    EXPECT_TRUE(closes == 1);
    EXPECT_FALSE(bar.IsVisible());

    // A click on Close posts WM_COMMAND to the bar itself.
    EXPECT_TRUE(bar.Show(owner, false));
    HWND close_button = GetDlgItem(bar.Handle(), kFindBarClose);
    EXPECT_TRUE(close_button != nullptr);
    if (close_button) {
        SendMessageW(bar.Handle(), WM_COMMAND,
                     MAKEWPARAM(kFindBarClose, BN_CLICKED),
                     reinterpret_cast<LPARAM>(close_button));
    }
    EXPECT_TRUE(closes == 2);
    EXPECT_FALSE(bar.IsVisible());

    bar.Destroy();
    DestroyWindow(owner);
}

#endif  // _WIN32
