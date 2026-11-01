# macOS QA: the checklist and the issue log

The port is verified structurally: it compiles on the runner, starts,
and passes its tests. None of that sees what a person sees. This file
is the shared instrument for closing that gap.

## How an issue gets found and closed

1. It comes from a screenshot of the real app: one the person testing
   takes, or the capture the launch smoke uploads as
   `MarkDownIt-window-capture` on every CI run.
2. It is written down here as one entry: what was done, what was
   expected, what happened.
3. It gets fixed, the fix lands through a CI run, and the delivery zip
   is refreshed. The entry then says which commit fixed it.

## The checklist

Walk each screen once and note anything that reads as wrong. One line
per item is enough ("H1 too small", "Replace button dead in read
mode"); a screenshot of the screen makes it exact.

### The window and its chrome
- Title bar: the document name, the proxy icon, the edited dot after a
  change, "Untitled" for a new document.
- Resize, zoom, minimize; the layout reflows without clipping.
- Enter Full Screen and back; the menu item renames itself.
- The About panel and the Settings window (Command-,): controls act.

### The menu bar
- Each menu opens and its items act: File, Edit, View, Format, Table,
  Window, Help.
- Shortcuts work: Command-N/O/S/F/R/Z, Command-Option-F, Command-/.
- Items grey out when they should (no document, read mode).
- Open Recent fills, and an entry opens the file.
- Services, Hide Others, Show All, the window list, Help.

### The command strip
- Tab labels, icons, labels under the icons; nothing clipped at tab
  edges.
- A toggle shows pressed state (Bold on a bold selection, Edit in edit
  mode).
- Tooltips name the Apple shortcut (Command-F, not Ctrl-F).
- A narrow window reflows the strip without overlap.

### The document
- Heading sizes step down clearly; H6 reads muted.
- Paragraph rhythm: more space before a section than between
  paragraphs.
- Code blocks: monospaced, boxed, readable.
- Tables: header row, alternating rows, borders.
- Blockquotes: bar plus indented muted text.
- Task lists: a checkbox with the state; `[ ]` never shown as text.
- Links blue and underlined; inline code tinted.
- Images and mermaid diagrams draw inside the column.
- Read mode hides the markers; edit mode shows the source.
- Selection highlight and the caret (shape, blink) behave.
- Both appearances: light and dark.

### The find bar
- Opens with Command-F, and Command-Option-F for replace; docks at the
  bottom edge with the document above it, nothing covered.
- Enter and Shift-Enter walk matches; the counter updates.
- The replace buttons are live only in edit mode.
- Close (Escape, the arrow at the right) returns the space.

### The outline pane
- Lists the headings; clicking one scrolls the document there.
- The active row follows the document while scrolling.
- Empty state shows both lines.
- The divider drags and the pane hides/shows from the menu.

### Files and editing
- A file opens from Finder, from the terminal, and from Open Recent.
- Typing needs the Edit toggle; the toggle is reachable from the
  strip and the View menu.
- Save, Save As, and the edited dot clearing on save.
- Undo and redo step through edits.
- Copy and paste text; dragging a selection moves it.
- Find/replace as above.
- Zoom in, out, and back to actual size.
- docx import and export; PDF export; the compatibility report.

## The issue log

Newest first. The first entries came from the window capture the smoke
took on run 41293719752; the rest from the six state captures of the QA
walk (artifact `MarkDownIt-window-capture`, run 41294080796), the same
loop that now runs on every build.

### The outline row clipped a long heading (Nov 1)
- "The smallest heading" drew cut mid-word at the pane edge ("The
  smallest hea"), with no ellipsis.
- Screenshot: `qa/doc.png` (light), also visible in `qa/dark.png`.
- Status: fixed in this commit. The row is trimmed to the width the
  pane can give it and ends in an ellipsis; the Windows pane puts a
  trailing ellipsis sign on its row layout for the same result.
- Checked in the next capture (run 41294774955): the long rows end in
  an ellipsis and nothing runs off the pane.

### The outline expanders drew as squares (Nov 1)
- Rows with a subtree showed a small filled square in the arrow zone,
  the same shape in both states, where the pane's own comment says
  triangle.
- Screenshot: `qa/doc.png` and `qa/dark.png`.
- Status: fixed in this commit. A filled triangle points right while
  the subtree is folded and down while it is open, the shape Windows
  and the Finder use.
- Checked in the same run: every row with a subtree carries a
  triangle, down while open, and leaf rows carry none.

### Blank lines push blocks apart (Nov 1)
- The showcase capture shows about 90 px between the "Lists and
  tasks" heading and its first bullet, and about 65 px between the
  bullet list and the numbered list. The metrics allow for roughly
  30 px (10 + 16 and 4 + 16 plus line leading).
- Cause: every blank line is a virtual empty paragraph in the node
  stream (parser.cpp marks them), and both renderers give it a full
  line's height so the caret has a place to sit. In the read view
  that line is dead space on top of the block gaps.
- Screenshot: `qa/doc.png`; measured pixel rows in the bullet
  section (bands 448-465, 559-571, 614-626, 693-705).
- Status: fixed in this commit. The blank line keeps a full line in
  edit mode, so the caret's place stays, and renders as a thin strip
  while reading, so blocks around a list sit at their normal distance.
  Both renderers follow the mode; a mac renderer test holds the two
  heights apart.

### The outline hint line clipped at the pane edge (Nov 1)
- The second empty-state line ran past the 200 DIP pane and was cut
  mid-phrase ("...your document", with "to create an outline." gone).
- Screenshot: the welcome capture, `window.png`.
- Status: fixed in this commit; the mac pane draws it as two lines now.

### The strip dropped a size step and left dead space (Nov 1)
- The command strip always drew its items icon-only inside oversized
  group boxes, with a wide dead zone to the right. Each group puts its
  caption label in the same subview list as its buttons, and both the
  width calculation and the group framing counted the caption as one
  more command, so every group was one command too wide and the full
  (icon and label) layout never fit any window.
- Screenshot: the welcome capture, `window.png`.
- Status: fixed in this commit. Only buttons count now, so a 1000 px
  window draws the full layout with correctly sized groups. The three
  small icons at the top left are the Quick Access Toolbar, which
  mirrors the Windows caption strip on purpose, and stay.
- Checked in the next captures (runs 41294189793 and 41294774955):
  every button carries its label and the groups are as wide as their
  buttons.

### The outline pane shows at the welcome screen (Nov 1)
- With no document open, the pane offers "No headings in this
  document.", which is text about a document that does not exist.
- Status: open; the welcome behaviour needs a decision first.

### Dark mode drew the light appearance's ink (Nov 1)
- The dark capture showed the dark page with the document text still
  in the light theme's near-black ink, unreadable; the strip and the
  outline pane had adapted. Root cause: the renderer bakes the palette
  into the laid-out runs, and ContentView::RefreshTheme re-set the
  palette without laying the document out again.
- Fixed: RefreshTheme lays the document out again and invalidates.
  Pinned by tests/renderer_mac_test.cpp
  (RecoloursTheRunsWhenTheThemeChanges) and by the capture-parity tool,
  which now fails any captured state whose document pane shows almost
  no text, the way a dark-on-dark document does.

### The parity pipeline caught, in order (Nov 1)
- Windows could not open its startup file: every Windows capture showed
  a Could not open file dialog. The hand-stripped quote handling did
  not survive the CI launcher; the canonical Windows parser now hands
  over the plain path, the argument is traced, and the next captures
  show the document rendered (trace: startup file arg=[D:\a\...]).
- The Windows document kept a full empty line next to every list on
  first open: the renderer keeps the editing heights by default so the
  export goldens stay put, and the app only pushed its mode when the
  user toggled edit. The startup now states the mode beside the width
  push. Evidence: the Windows doc capture went from 9 to 12 text bands,
  matching the mac's 12.
- The mac drew 24 px line pitch where Windows draws 31: the Windows
  renderer scales the font's natural line box (DirectWrite reports it)
  while the mac scaled the bare em. The LineHeight seam, built with
  exactly those DirectWrite-equivalent numbers, was never wired into
  ApplyLineSpacing; it now is, with the em as fallback. What remains is
  the fonts' own difference (SF asks for less leading than Segoe UI),
  which is the part that cannot be removed.
- The blinking caret made the outline capture differ from the edit
  capture by 15 px at random: both walks now park the caret off, and
  stop the mac blink timer, right before they announce a state. The
  comparison tool was recalibrated in the same pass: the text rows are
  paired by index and the gap after each row is compared, which holds
  the two fonts' honest 3-5 px difference steady, and the source view
  is measured by pitch because its monospace wrap points differ by
  design.
- Kept as by-design differences: fonts and their wrap points, the
  window chrome, the outline pane's active-row tint, and the pane
  width (Windows sizes it to its items, the mac remembers a width).

### Cross-platform capture parity (Nov 1)
- The Windows build photographs itself in the same named states as the
  mac build (doc, find, edit, outline, source). With
  MARKDOWNIT_QA_STATES set, src/app.cpp walks the states on a timer,
  writes "qa state ready: <name> rect=x,y,w,h" into the log named by
  MARKDOWNIT_QA_LOG, and closes itself at the end;
  tools/windows-smoke/capture.ps1 polls that log and takes a screen
  capture per state. The mac walk (src/macos/main_mac.mm) announces
  the same rectangle for its pane, and tools/macos-smoke/launch_smoke.sh
  records both the pictures and the rectangles into build-mac/qa.
- tools/compare_platform_captures.py crops each pair to its document
  pane, scales both sides to one width, and compares the text band
  count, the large gaps between the bands and the ink share. Fonts and
  window chrome differ by design, so the comparison is structural: a
  state fails when a block went missing or moved past the tolerances
  (2 bands, 12 px gaps, 8 px first row). The tool runs the same way
  locally: point --macos and --windows at two capture directories.
- The capture-parity job in .github/workflows/build.yml runs the
  comparison as a fixed step whenever both platform builds passed; the
  side-by-side and overlay pictures (mac ink red, Windows ink green)
  land as the MarkDownIt-capture-parity artifact on every run.
- dark and settings are mac-only states; the report names them and
  skips them.

<!--
Entry format:

### Short name (date)
- What was done / what was expected / what happened.
- Screenshot: the screen and what it shows.
- Status: open | fixed in <commit> (CI run <id>)
-->
