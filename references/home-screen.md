# The home screen

When no document is open, MarkDownIt shows a page with two ways in and the
files the reader had lately. It used to be a list of recent documents with
one button; the page is called Home now, because starting a document and
picking up an old one are the two things it is for.

## What the page holds

The page wears the site's shape: a dark band at the top over a light
ground, the way the project page opens.

**The band is one row.** The lockup sits at the left edge of the content
column and the two actions at the right end of it, so the band reads as a
masthead with its controls and not as a leaflet with a button under it.

- **The mark.** Drawn from the icon's own geometry
  (`docs/markdownit-icon.svg`, the same 256-unit box): the disc, the M and
  the arrow down. On the band the disc is white and the M and the arrow are
  the site's teal, the icon's two colours the other way round, which is what
  reads on a dark ground.
- **The name**, `MarkDownIt`, beside the mark, and **the line the site
  leads with**, "A word processor for Markdown files.", under the name from
  the name's left edge, in the announce bar's mint. The two lines and the
  mark are one block, centred on the band.
- **Two actions** at the band's right end, each a 34 point square with a
  glyph and no words, so the row stays a masthead:
  - **New document**, the quiet one: the site's outline button, a white
    hairline on the band and next to no fill, holding the add sign.
  - **Open document**, the solid one and the rightmost of the pair: the
    site's `.btn-solid`, bright teal with the dark ink on it, holding a
    folder. Reading is what the app is for, so this is the action the page
    points at.

  The two glyphs are drawn from primitives, so both ports draw the same
  marks; because a glyph alone has to name itself, each shell hangs a
  tooltip on its rect (`NSToolTipOwner` on the mac, a `TOOLTIPS_CLASS`
  control on Windows), and the words live there.

Under the band:

- A label, `Recent files`, at the grid's left edge, so label and cards read
  as one column.
- The recent files as cards: white on the mint ground, with the site's
  radius and hairline, their preview, name and folder. A card for an SVG
  shows the picture rather than the markup.
- No find bar. The bar belongs to a document, so it stands down when this
  page comes up: a search field over the recent files would search nothing.
- With an empty list, a line of text in place of the cards, at the column's
  left edge. The two actions stay, so a reader who lands here with nothing
  still has both ways in.

Actions are buttons and files are cards. A new document has no preview, no
name and no folder, so it would be a card of nothing in a grid of previews;
the band's row is where an action belongs.

## One content column

Every element starts at the same left edge: the mark, the label and the
first card, and the band's own row ends where the last card of a row ends.
The column is the window width minus `kSideMargin` on each side, capped at
`kContentMax` and centred, so a wide window keeps the column and lets the
band and the ground run full width.

The cards share the column out evenly: as many columns as fit at a `kCardW`
minimum, then the width is divided, so the last card ends at the column's
right edge. A card whose width changed is fitted again, because the preview
wraps to the card it is drawn in and not to the one it had.

The band has two cases, and `Layout` picks between them:

- **One row** when the pair fits beside the lockup (`kInlineRowMin`, 340
  points of column): the lockup left, the actions right, the band 84 points.
- **Two rows** below that: the actions drop under the lockup at the left
  edge and the band grows to 122. The 480 point window the mac window can
  be narrowed to lands here.

The tagline stands down on its own when the column is narrower than
`kTaglineMin` (380), because under that it would run beneath the pair. The
name then carries the row alone, and the row stays one row.

The vertical rhythm is one number after another in `welcome_screen.cpp`:
`kBandHeight` (84, or `kBandHeightStacked`, 122), the row tops
`kRowMarkTop` (25) and `kStackedMarkTop` (20) for the mark with
`kMarkSize` (34), the baselines of the name and the tagline, `kRowButtonTop`
(25) and `kStackedButtonTop` (66) for the pair, and `kLabelLift` (34) and
`kGridLift` (50) for the label and the first row of cards, read from the
band that was laid out. Move one and the ones under it move with it; the
tests probe the line where the cards start.

## Why the band is a masthead and not a hero

The site opens on the same gradient, from the navy `#0B1E22` to the deep
teal `#0F5E57`, with its content on the soft mint `#F4F8F7` and white cards
with a `#E2E8E8` hairline. Carrying the band into the app makes the window
and the page it comes from read as one product, and it gives the mark a dark
ground to sit on.

What the start screens of other document apps do, and what this page
follows:

- **The work comes first.** VS Code, Xcode and GitHub Desktop open with
  the actions and the recent items, and keep identity to a line. A start
  screen is read for the way back into a file, so the brand takes one
  strip (`kBandHeight`) and the files take everything under it. The first
  version of this page used a 186 point hero, with the actions under the
  brand, and pushed the files off a small window; one row of 84 points is
  the masthead that leaves them the room.
- **Documents are recognised by their content.** The developer apps list
  file names because a name is enough there. A markdown page is recognised
  by its first lines, so the files stay cards with a preview.
- **An empty state is a block with a next step.** The guidance from
  NN/g, Carbon and PatternFly agrees: say what is missing, give the action
  that fills it, and align it with the rest of the page instead of
  centring a polite sentence in the middle. The hint under the label does
  that, and the two actions above it are the next step.
- **One left edge.** Carbon asks for empty-state elements to be aligned as
  a block; the same rule holds for a page that is not empty. The brand,
  the actions, the label and the grid all start at `ContentLeft`, and a
  centred stack of three things was what made the first version feel like
  a leaflet.

The render seam fills shapes and does not interpolate a gradient, so the
band is laid down in strips and the pool of teal under it in overlapping
ellipses. Neither is a texture or an image; both scale with the window.

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
item and the string the page draws. On the mac the toolbar item wears the
`house` symbol and the View menu item the same image, so the way back to
the page reads the same in both places; the Windows ribbon carries labels
and no images, for every command. The command keeps its historical id
(`CommandId::ViewRecent`, selector `showRecents:`), because the ids name a
place in the table and renaming one would touch every table that reads it for
no gain the reader can see.

## Leaving a document for Home

Home is not an overlay over the open document: it is the page the app
shows when there is no document, which is why the document commands are
greyed there. Going home therefore leaves the document, and unsaved
changes are asked about first with the same Save/Discard/Cancel question
Open and Close ask. Cancel puts the document back on screen untouched.
The document itself stays loaded behind the page, the way it did before
the question existed, so the walk back through the recent files does not
have to reload it and a later Save cannot reach text the writer threw
away (Save has nothing to act on from this page).

## Tests

`tests/welcome_screen_test.cpp` holds the screen: the lockup puts the name
beside the mark and the tagline under the name, the disc is white with the
teal letter on it, the band is a dark strip across the full width with light
ink on it, the pair sits at the band's right end with New on the left of
Open and the gap between them belonging to neither, the band is one row on a
wide window, gives up the tagline at 700 and stacks at 500, the grid hangs
from the same left edge and takes its columns from the width, the label sits
at the grid's edge, and a press on a button is not a press on a card. The platform press paths are exercised by the macOS launch smoke
(`welcome-click` picks a card and checks nothing gets selected) and by hand
on Windows.
