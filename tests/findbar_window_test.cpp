// Runtime test for the find bar's Win32 plumbing. Windows only: the bar is
// built from real windows, and the breaks this guards against lived exactly
// where the portable tests cannot look.
//
// The bar is a child strip docked at the bottom of its owner window. Its
// window procedures recover the FindBar instance from GWLP_USERDATA, a slot
// every window has, and the strip spans the owner's full client width. This
// test drives the real messages and fails on the failure modes that broke
// the bar before: an unreadable instance pointer (every control falls to
// DefWindowProc and paints nothing) and a mode gate that leaks.

#ifdef _WIN32

#include "gtest_lite.h"

#include <windows.h>

#include <iostream>

#include "findbar.h"

namespace {

LRESULT CALLBACK OwnerWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The bare counterpart of the app window: an owner the bar can dock into.
HWND CreateOwnerWindow() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = &OwnerWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MarkDownItFindBarRuntimeTestOwner";
    RegisterClassW(&wc);
    return CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 900,
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

    // The strip is a child window now, so it counts as visible only while
    // its owner is visible. The real owner is a shown application window,
    // so the test shows its owner too.
    ShowWindow(owner, SW_SHOW);

    FindBar bar;
    int closes = 0;
    int searches = 0;
    FindBarListener listener;
    listener.onClose = [&closes]() { ++closes; };
    listener.onSearchChanged = [&searches]() { ++searches; };
    bar.SetListener(listener);

    EXPECT_TRUE(bar.Show(owner, FindBarMode::Find));
    EXPECT_TRUE(bar.IsVisible());
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
    // Every control must sit inside the strip: the dock layout must never
    // push a control out of the window (or, at this width, on top of
    // another one).
    RECT bar_client = {};
    GetClientRect(bar_wnd, &bar_client);
    // The Find and the Replace field share a left edge (both sit after the
    // same 76 DIP label column).
    RECT find_rect = {};
    RECT replace_rect = {};
    GetWindowRect(GetDlgItem(bar_wnd, kFindBarFindEdit), &find_rect);
    GetWindowRect(GetDlgItem(bar_wnd, kFindBarReplaceEdit), &replace_rect);
    EXPECT_TRUE(find_rect.left == replace_rect.left);
    for (int id = kFindBarFindEdit; id <= kFindBarStatus; ++id) {
        HWND child = GetDlgItem(bar_wnd, id);
        EXPECT_TRUE(child != nullptr);
        if (child) {
            EXPECT_TRUE(GetWindowLongPtrW(child, GWLP_USERDATA) == instance);
            RECT cr = {};
            GetWindowRect(child, &cr);
            MapWindowPoints(HWND_DESKTOP, bar_wnd,
                            reinterpret_cast<POINT*>(&cr), 2);
            EXPECT_TRUE(cr.left >= 0 && cr.top >= 0);
            EXPECT_TRUE(cr.right <= bar_client.right &&
                        cr.bottom <= bar_client.bottom);
        }
    }

    // The strip is a child of the owner, spans the full client width and
    // sits flush with the bottom edge.
    EXPECT_TRUE(GetParent(bar_wnd) == owner);
    RECT oc = {};
    GetClientRect(owner, &oc);
    RECT br = {};
    GetWindowRect(bar_wnd, &br);
    MapWindowPoints(HWND_DESKTOP, owner, reinterpret_cast<POINT*>(&br), 2);
    EXPECT_TRUE(br.left == 0 && br.right == oc.right);
    EXPECT_TRUE(br.bottom == oc.bottom && br.top == oc.bottom - bar.HeightPx());

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

    // Find mode greys the replace half but keeps it on screen, so the strip
    // layout is the same in both modes (the reviewed plan's first decision).
    EXPECT_TRUE(bar.Mode() == FindBarMode::Find);
    HWND replace_edit = GetDlgItem(bar_wnd, kFindBarReplaceEdit);
    HWND replace_btn = GetDlgItem(bar_wnd, kFindBarReplace);
    HWND replace_all = GetDlgItem(bar_wnd, kFindBarReplaceAll);
    EXPECT_TRUE(replace_edit != nullptr);
    EXPECT_TRUE(replace_btn != nullptr);
    EXPECT_TRUE(replace_all != nullptr);
    EXPECT_FALSE(IsWindowEnabled(replace_edit));
    EXPECT_FALSE(IsWindowEnabled(replace_btn));
    EXPECT_FALSE(IsWindowEnabled(replace_all));

    // With matches present the mode gate still holds: a Find session never
    // arms the replace buttons.
    bar.SetReplaceEnabled(true);
    bar.SetMatchCount(1, 2, FindStatus::Found);
    EXPECT_FALSE(IsWindowEnabled(replace_edit));
    EXPECT_FALSE(IsWindowEnabled(replace_btn));
    EXPECT_FALSE(IsWindowEnabled(replace_all));

    // Find/Replace mode with editing allowed arms the half, on the same
    // window and the same controls.
    bar.SetMode(FindBarMode::FindReplace);
    EXPECT_TRUE(bar.Handle() == bar_wnd);
    EXPECT_TRUE(IsWindowEnabled(replace_edit));
    EXPECT_TRUE(IsWindowEnabled(replace_btn));
    EXPECT_TRUE(IsWindowEnabled(replace_all));

    // Back to Find: greyed again, matches or not.
    bar.SetMode(FindBarMode::Find);
    EXPECT_FALSE(IsWindowEnabled(replace_edit));
    EXPECT_FALSE(IsWindowEnabled(replace_btn));

    // The read-only gate wins over the replace mode (guide 4).
    bar.SetReplaceEnabled(false);
    bar.SetMode(FindBarMode::FindReplace);
    EXPECT_FALSE(IsWindowEnabled(replace_edit));
    EXPECT_FALSE(IsWindowEnabled(replace_btn));
    EXPECT_FALSE(IsWindowEnabled(replace_all));

    // Leave the bar where the rest of the test expects it.
    bar.SetReplaceEnabled(true);
    bar.SetMatchCount(0, 0, FindStatus::Empty);
    bar.SetMode(FindBarMode::Find);

    // Tab must walk the bar's own controls. Focus is not asserted here: an
    // unattended session may not hold activation, but the walk runs anyway.
    if (find_edit) SendMessageW(find_edit, WM_KEYDOWN, VK_TAB, 0);

    // Escape from the Find field closes the bar and tells the owner.
    if (find_edit) SendMessageW(find_edit, WM_KEYDOWN, VK_ESCAPE, 0);
    EXPECT_TRUE(closes == 1);
    EXPECT_FALSE(bar.IsVisible());

    // A click on Close posts WM_COMMAND to the bar itself.
    EXPECT_TRUE(bar.Show(owner, FindBarMode::Find));
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
