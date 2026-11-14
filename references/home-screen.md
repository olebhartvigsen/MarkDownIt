# The home screen

When no document is open, MarkDownIt shows a page with two ways in and the
files the reader had lately. It used to be a list of recent documents with
one button; the page is called Home now, because starting a document and
picking up an old one are the two things it is for.

## What the page holds

- A heading, `Home`.
- Two actions under it, side by side and centered as a pair:
  - **New document**, the quiet one: a card's white face and border, so the
    two read as one row of answers.
  - **Open document**, the accent one with the soft shadow. Reading is what
    the app is for, so Open is the action the page points at.
- The recent files as cards under the actions, with their preview, name and
  folder. A card for an SVG shows the picture rather than the markup.
- With an empty list, a line of text in place of the cards. The two actions
  stay, so a reader who lands here with nothing still has both ways in.

Actions are buttons and files are cards. A new document has no preview, no
name and no folder, so it would be a card of nothing in a grid of previews;
the row under the heading is where an action belongs.

## One screen for both ports

The layout, the text fitting and the drawing are shared
(`src/welcome_screen.cpp`), drawn through the render target and the text
engine seam. Each shell passes its own target and engine and answers the
presses:

| Thing | Windows (`src/app.cpp`) | macOS (`src/macos/content_view_mac.mm`) |
|-------|-------------------------|----------------------------------------|
| New document | `welcome_.HitsNewButton` in `OnLButtonDown` calls `NewDocument()` | `WelcomeNewAt` in `mouseDown:` asks `RequestNewDocument()` |
| Open document | `HitsOpenButton` calls `OpenFileDialog()` | `WelcomeOpenAt` asks `RequestOpenPanel()` |
| A card | `HitTest` then `OpenFile(path)` | `WelcomeCardAt` then `RequestOpenFile` |
| Hover | `welcome_new_hover_`, `welcome_open_hover_` | `newHover_`, `openHover_` |

The two actions are asked for before the cards, so a press on one never
opens the card underneath.

On the mac the page asks the window for a document rather than making one:
the window owns the unsaved-changes question and the title. The Windows
window does both in the same place, because its page lives in the window.

## What a new document is

An untitled buffer: no path, no watch, edit mode on so the caret is ready,
the title `Untitled`, and Save falling back to Save As because there is
nowhere to write yet. The first save gives the document its file and starts
the watch (see `file-watch.md`).

## The name

The labels say Home: the ribbon button, the View menu item, the mac toolbar
item and the string the page draws. The command keeps its historical id
(`CommandId::ViewRecent`, selector `showRecents:`), because the ids name a
place in the table and renaming one would touch every table that reads it for
no gain the reader can see.

## Tests

`tests/welcome_screen_test.cpp` holds the screen: both actions exist with a
size, the pair is centered with New on the left, the gap between them belongs
to neither, and a press on either is not a press on a card. The platform
press paths are exercised by the macOS launch smoke (`welcome-click` picks a
card and checks nothing gets selected) and by hand on Windows.
