# Unified Find bar with bottom dock (Find and Find/Replace in one control)

**Status:** Outline for review. No code changes yet.
**Goal:** Ctrl+F and Ctrl+H open the same bar. The chosen function decides what is
enabled. The bar sits at the bottom of the app window as a docked toolbar strip.

## 1. What exists today

- `src/findbar.*` already has ONE window that serves both functions.
- Ctrl+F opens it in the compact form. Ctrl+H opens it with the replace row.
  `SetExpanded()` shows or hides four controls (Replace label, Replace field,
  Replace, Replace All) and resizes the window between two fixed heights:
  `kHeightFind = 80` and `kHeightReplace = 118` DIPs.
- It is a floating popup (WS_POPUP + WS_EX_TOOLWINDOW), placed near the top of
  the content area. `Reposition()` clamps it to the owner work area. `app.cpp`
  calls it from WM_SIZE and WM_MOVE.
- Controls sit at fixed pixel positions in a 420 DIP wide window
  (`LayoutControls`).
- The read-only gate already exists: `SetReplaceEnabled(editing_)` greys the
  replace controls in view mode (find-replace spec section 4).
- The bottom edge is free. `ResizeContentWindow()` sizes the content window
  from below the ribbon to the bottom of the client area.

## 2. Proposed behaviour

### 2.1 One bar, two functions

The bar gets an explicit mode. Ctrl+F selects Find. Ctrl+H selects Find/Replace.
The same controls stay on screen; the mode changes enablement only.

| Control | Find mode | Find/Replace mode | Read-only view |
|---|---|---|---|
| Find field | on | on | on |
| Previous / Next | on (matches) | on (matches) | on (matches) |
| Match case / Whole word | on | on | on |
| Replace field | off (grey) | on | off (grey) |
| Replace / Replace All | off (grey) | on | off (grey) |
| Counter / status | on | on | on |
| Close | on | on | on |

Switching mode while the bar is open reuses the same window and keeps text,
options and history (find-replace spec section 54). The read-only gate still
wins over the mode: Replace stays disabled outside edit mode, with the existing
"not available in this view" note.

### 2.2 Bottom dock

- The bar becomes a child strip of the main window. It spans the full client
  width and sits flush with the bottom window edge, like a status bar.
- While it is visible the content window shrinks by the strip height. Nothing
  hides behind the bar. Scrollbars and text wrap follow automatically.
- Esc or Close hides the strip and gives the height back to the content.
- Height and control positions scale with the window DPI (same rule as today).
- Focus rules do not change: Ctrl+F and Ctrl+H put focus in the Find field and
  select its text (find-replace spec section 28). Esc returns focus to the
  document.

### 2.3 Layout inside the strip

Full-width flow in two rows:

```
Find:    [ search text           ]  [Prev] [Next]  [Match case] [Whole word]   3 of 17   [Close]
Replace: [ replacement           ]  [Replace] [Replace All]                    0 replacements
```

Row 2 stays visible in both modes and greys out in Find mode, so nothing moves
when the mode changes. See decision 1.

Tab order keeps the find-replace spec section 45 sequence. Disabled controls
drop out of the tab cycle automatically (`MoveTabFocus` already skips disabled
controls).

## 3. Decisions needed before implementation

1. Find mode: replace controls greyed but visible (recommended: one layout, no
   jump, matches "same box, different enablement") or hidden as today?
2. Dock: shrink the content area while open (recommended, true toolbar
   behaviour) or overlay the bottom band of the content?
3. Ctrl+F while the bar is open in Find/Replace mode: switch back to Find mode
   (recommended, the function follows the shortcut) or only focus and leave the
   mode?
4. Startup: the bar starts hidden and is not persisted (recommended).

## 4. Change list (after approval)

- `src/findbar.h` / `src/findbar.cpp`
  - Add `FindBarMode` (Find, FindReplace) and `SetMode`. `Show(owner, mode)`
    replaces the expand flag.
  - Create the window as `WS_CHILD` of the owner instead of `WS_POPUP`. Drop
    `WS_EX_TOOLWINDOW`. Remove `SetActiveWindow` from `Show`.
  - Replace `Reposition()` with a dock call: the bar derives its height from the
    mode and DPI; the owner re-lays out the content window (or polls the height
    in `ResizeContentWindow`).
  - Rework `LayoutControls` for full width. Height constants stay DPI scaled.
  - `UpdateControls` merges two gates: mode, and the existing edit-mode gate
    (`replaceEnabled_`).
- `src/app.cpp`
  - `ResizeContentWindow()` reserves the strip height while the bar is visible.
  - `ShowFindReplace(replaceMode)` passes the mode. `CloseFindBar` re-lays out.
  - WM_SIZE, WM_MOVE and WM_DPICHANGED re-layout instead of `Reposition`.
- `tests/findbar_window_test.cpp` (runtime test, runs on CI)
  - Strip is a child of the owner, spans the full width, sits at the bottom.
  - Replace controls are disabled in Find mode, enabled in Find/Replace mode.
  - `SetReplaceEnabled(false)` (read-only) still disables them in Find/Replace
    mode.
  - A mode switch reuses the same HWND.
- `references/find-replace.md`
  - Record the chosen placement (docked toolbar) next to sections 26 and 27,
    plus the enablement matrix. Both sections already allow a toolbar.

## 5. Risks and notes

- WS_POPUP to WS_CHILD changes activation behaviour. That is intended: the bar
  no longer activates on its own; focus goes straight to the Find field.
- The strip takes 80 to 118 DIPs of document height while open. With decision 1
  as recommended, the height is constant after the first layout.
- CI (windows-2022) is the compile gate. The runtime test proved last round that
  it can open real windows on the runner.
- Manual check on Windows after the change: dock position, resize, DPI change,
  focus, Esc, mode switch, read-only gates, replace flow, and that zoom is
  unaffected.
