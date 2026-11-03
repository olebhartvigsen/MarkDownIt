# macOS Native Chrome

The mac window's command surface: what it wears, why, and which
guidance it follows.

## Why the strip went away

The mac window used to carry a command strip that mirrored the Windows
ribbon: tabs, groups, a Quick Access Toolbar in its title bar, buttons
drawn to the Windows measurements and colours. The macOS Human
Interface Guidelines name the problem in one sentence: "Avoid creating
custom window UI. System-provided windows look and behave in a way
that people understand and recognize. ... Doing so without perfectly
matching the system's look and behavior can make your app feel
broken." A hand-painted ribbon inside a mac window is exactly that, a
non-system control surface drawn next to the traffic lights.

## What the window wears now

A standard toolbar (NSToolbar) in the title bar, seven items:

| Item | Symbol | Action | Also reachable |
| --- | --- | --- | --- |
| Save | square.and.arrow.down | saveDocument: | File menu, Cmd+S |
| Share | square.and.arrow.up | shareDocument: | File menu |
| Undo | arrow.uturn.backward | undo: | Edit menu, Cmd+Z |
| Redo | arrow.uturn.forward | redo: | Edit menu, Shift+Cmd+Z |
| Find | magnifyingglass | findDocument: | Edit menu, Cmd+F |
| Edit switch | text, Edit or Done | editMode: | View menu, checked item |
| Outline | sidebar.left | toggleOutline: | View menu |

The symbols lead, the edit switch closes, and everything else lives in
the menu bar, where macOS expects the commands anyway; the keyboard
shortcuts live on the menu items. Why a text button for the switch:
the Toolbars guidance prefers symbols "except for actions like edit
that aren't well-represented by symbols". The word pairs with what
Preview and Pages show for the same switch, Edit while reading, Done
while editing.

The window wears the standard title bar. The title is the document
name, the folder rides as the subtitle, and the represented file gives
the title its icon. There is no titlebar accessory and no painted
surface anywhere in the toolbar.

The window is tabbable and prefers the tab bar, so the bar stays
visible with a single document instead of appearing only after a
second window joins the group. The bar is AppKit's own; the app draws
no tab strip.

Every symbol is drawn by AppKit through imageWithSystemSymbolName:, so
the toolbar follows the system appearance, the user's accent colour,
increased contrast and localisation without any colour of our own.
The toolbar allows customization and saves the user's set.

## The outline pane follows the Sidebars guidance

The outline keeps its own scrolling pane inside the split view, now
wearing the system's sidebar material and semantics:

- a vibrancy backdrop (NSVisualEffectView, sidebar material) instead
  of a painted background;
- the active row marked the way sidebars mark selection, with the
  system selection colour and rounded corners;
- row text in the system label colours, disclosure chevrons in the
  secondary label colour;
- show and hide from the toolbar's Outline switch and from the View
  menu, which is what the Sidebars guidance asks for ("include a
  show/hide button or add Show Sidebar and Hide Sidebar commands to
  your app's View menu").

The guidance also notes that sidebar icons take the user's accent
colour and should keep it; the pane's marks use system colours for
that reason.

## The find bar wears standard controls

The find bar stays at the bottom of the window, and its controls are
the system's own: a search field with its magnifier and clear button,
a plain replace field, a segmented control with the system's chevrons
for the match steps, and standard push buttons for Replace and
Replace All. Its behaviour is unchanged; find-replace.md remains the
spec for that.

## The Settings window keeps the pane's size out

The Settings window is a standard titled window with a small form: a
section header and the outline switch. It has no document width row,
and the View menu has no Wider Content command, because the pane
always fits the window with the margin the padding leaves. The width
modes are a Windows idea and stay there; on the mac the mode is
pinned to the full width at launch, and a width value in an old
settings file is not consulted.

## Rules for this window

- No custom window chrome: no titlebar accessories, no painted
  surfaces in the toolbar, no colours of our own in the chrome.
- Commands live in the menu bar first; the toolbar carries the few
  that earn it.
- The document presentation follows
  osx-markdown-visual-style-guide.md as before; this file covers the
  chrome around it.
- tools/check_mac_chrome.py enforces all of it in CI.
- The Windows window keeps its ribbon and the rules in
  ribbon-menu-guidelines.md; the two files describe different
  windows.

## Sources

- [Windows](https://developer.apple.com/design/human-interface-guidelines/windows), the "avoid custom window UI" rule.
- [Toolbars](https://developer.apple.com/design/human-interface-guidelines/toolbars), deliberate items, system symbols, reduced backgrounds, customization.
- [Sidebars](https://developer.apple.com/design/human-interface-guidelines/sidebars), disclosure controls, hide and show, accent colour.
- [The menu bar](https://developer.apple.com/design/human-interface-guidelines/the-menu-bar), where app commands live.
- [Designing for macOS](https://developer.apple.com/design/human-interface-guidelines/designing-for-macos/), the general principles the visual style guide already cites.