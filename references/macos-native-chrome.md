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

A standard toolbar (NSToolbar) in the title bar, nine items:

| Item | Symbol | Action | Also reachable |
| --- | --- | --- | --- |
| Recent | clock.arrow.circlepath | showRecents: | View menu |
| Save | template floppy | saveDocument: | File menu, Cmd+S |
| Share | square.and.arrow.up | shareDocument: | File menu |
| Undo | arrow.uturn.backward | undo: | Edit menu, Cmd+Z |
| Redo | arrow.uturn.forward | redo: | Edit menu, Shift+Cmd+Z |
| Find | magnifyingglass | findDocument: | Edit menu, Cmd+F |
| Highlight | highlighter | toggleMark: | Format menu, Shift+Cmd+H |
| Edit switch | text, Edit or Done | editMode: | View menu, checked item |
| Outline | sidebar.left | toggleOutline: | View menu |

The symbols lead, the edit switch closes, and everything else lives in
the menu bar, where macOS expects the commands anyway; the keyboard
shortcuts live on the menu items.

Recent stands first because it is navigation, not a document command:
it steps aside to the recent documents and leaves the open file where
it is, so the way back sits where a back button would. It is disabled
on that screen itself, where there is nothing to go back to. Why a text button for the switch:
the Toolbars guidance prefers symbols "except for actions like edit
that aren't well-represented by symbols". The word pairs with what
Preview and Pages show for the same switch, Edit while reading, Done
while editing.

The window wears the standard title bar. The title is the document
name, the folder rides as the subtitle, and the represented file gives
the title its icon. There is no titlebar accessory and no painted
surface anywhere in the toolbar.

The window is tabbable and prefers the tab bar. Preference alone still
leaves the bar hidden while the window has no sibling, so the shell
opens it explicitly after the window is on screen and only when
`[window tabGroup] isTabBarVisible` is false and the user has not hidden
the bar before. The bar then stays visible with a single document
instead of appearing only after a second window joins the group. The
View menu carries Show Tab Bar or Hide Tab Bar, and the shell remembers
the user's choice, so a single document can hide the bar without losing
the document. The bar is AppKit's own; the app draws no tab strip.

Most symbols are drawn by AppKit through imageWithSystemSymbolName:, so
the toolbar follows the system appearance, the user's accent colour,
increased contrast and localisation without any colour of our own.
Save is the exception: SF Symbols has no classic diskette, so it is a
small template outline drawn in the same ink and tinted by the system.
The toolbar allows customization and saves the user's set.

## The format bar belongs to the page

Editing needs formatting at the point of the text, and the menu alone
puts it a trip away. So the window wears one more native row: a format
bar under the toolbar, over the document pane only, so the outline keeps
its full height and the strip never spans the window like a second
toolbar (the Toolbars guidance allows one row, not two).

It is on screen exactly while the document can be edited, and it is
built with the system's own controls, which is why it needs no palette
of its own and follows the appearance, the accent colour and increased
contrast by construction:

| Group | Control | Commands |
| --- | --- | --- |
| Paragraph | NSPopUpButton | Body Text, Heading 1 to 6 |
| Marks | NSSegmentedControl, four segments | Bold, Italic, Strikethrough, Code |
| Insert | two symbol buttons | Link, Insert Table |
| Blocks | NSSegmentedControl, three segments | Bulleted List, Numbered List, Quote |
| Trailing | one symbol button | Clear Formatting |

Every control sends a command rather than a behaviour. Each one calls
`sendAction:to:nil`, the path a menu item with no target takes, so a
click here and a menu pick run the same selector in the same command
table. The bar carries no idea of its own about what bold means.

The caret owns the state. The shell pushes `FormatState` in on every
chrome sync, so the popup names the paragraph style and the segments
show the marks the caret holds. The segments track momentarily and take
their pressed state only from that push; a row that remembered its own
clicks would disagree with the document the first time a key moved the
caret.

The bar never takes the keyboard. Its controls refuse the first
responder, so a click leaves the reader typing in the page. A control
that accepted it would take the caret and every arrow key with it.

The bar sits inside the document pane: the pane holds the scroll view
and the bar, the scroll view takes the height the bar leaves. Hiding it
sets the bar's height constraint to zero instead of leaving a gap.

Highlight is deliberately not in the bar. It stays the toolbar's toggle
and a Format menu item, and it works in both modes, since a mark is a
reading note as much as an editing one: its pressed state and its
command follow the selection, not the edit switch.

The context menu carries the same commands where the pointer is, with
the document commands greyed out while the page is read and Highlight
the one exception.

The QA walk has a `format-bar` state that asserts the bar is on screen
at its height while the page is editable and that every selector it
sends resolves through the responder chain, and the reading state
asserts the bar is gone. A control whose selector answers nobody is a
dead button, and no click test in the walk would see it.

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

## The zoom level names itself while it changes

Zoom has no permanent readout in the toolbar. While the level changes,
a small overlay appears at the top centre of the window for a moment
and says the new level in digits ("110%"), then leaves. It is built
from the system's own pieces:

- A rounded pill, 10 pt corners, filled with translucent black (72%)
  and a hairline of white at 12% so it also has a shape on a dark page.
  The page shows through at a little over a quarter: enough to read the
  level by, little enough to stay out of the way.
- The fill is a colour of our own rather than the system HUD material.
  The material's own tint follows the window while a label inside it
  follows the pill, and over a white page that came out as white digits
  on a light pill: invisible. Three arrangements were measured in CI
  (material in a rounded pill, material under a separate label, and a
  scrim between them) and each failed in a light window in its own way,
  so the overlay uses a fill whose colour cannot depend on the window.
- The level is in plain white on that fill, with the system's monospaced
  digit font, which keeps the number from shifting the pill as it counts
  up or down.
- A 0.09 s fade in, a 0.85 s hold, and a 0.25 s fade out.
- `hitTest:` returns nil and it never accepts the first responder: the
  overlay cannot be clicked and never takes the keyboard, so it can
  never get between the reader and the text.

Every zoom path already ends in the app's `zoomDidChange:` hook, so the
overlay needs no knowledge of which control moved the level (keys, the
toolbar, the menu or a pinch). A page swap takes it down: the overlay
belongs to the page that was zoomed, not to the recents page.

A document always opens at 100%, and a new document starts there too. The
level is a reading tool for the page in front of the reader, not a
property of the app: a file opened at 300% from the last session would
greet them with a page three times its size, and a second document would
inherit whatever the first was being read at. The reset is silent, since
the level did not change by hand and the overlay is for the reader's own
changes. The saved factor stays written, but every open starts the value
again from 100%. The Windows build keeps restoring the last factor; this
is a deliberate difference of this window.

The QA walk covers it with `zoom-hud` (the level is up and names a
higher number) and `zoom-hud-clear` (nothing is left after it has been
taken down), and tools/macos-smoke/launch_smoke.sh asserts both.

## Rules for this window

- No custom window chrome: no titlebar accessories, no painted
  surfaces in the toolbar, no colours of our own in the chrome.
- Commands live in the menu bar first; the toolbar carries the few
  that earn it.
- The document presentation follows
  osx-markdown-visual-style-guide.md as before; this file covers the
  chrome around it.
- Transient feedback (the zoom level) is an overlay on the system HUD
  material, never a permanent readout in the toolbar.
- Editing chrome sits on the page it edits: the format bar rides inside
  the document pane, is on screen only while the page can be edited,
  and is built from the system's controls over the command table.
  Nothing on it takes the keyboard.
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
- [NSVisualEffectView](https://developer.apple.com/documentation/appkit/nsvisualeffectview), materials, blending modes and the HUD material.
- [NSAnimationContext](https://developer.apple.com/documentation/appkit/nsanimationcontext), grouped, implicitly animated property changes.
- [NSSegmentedControl](https://developer.apple.com/documentation/appkit/nssegmentedcontrol), the segmented row and its tracking modes.
- [NSPopUpButton](https://developer.apple.com/documentation/appkit/nspopupbutton), the paragraph style menu.
- [TextEdit: format with fonts and styles](https://support.apple.com/guide/textedit/format-with-fonts-and-styles-txte2be20f33/mac), the format bar model this row follows.