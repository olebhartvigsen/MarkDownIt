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

Newest first. The first three came from the window capture the smoke
took on run 41293719752 (artifact `MarkDownIt-window-capture`), the
first time the app was photographed on the runner.

### The outline hint line clipped at the pane edge (Nov 1)
- The second empty-state line ran past the 200 DIP pane and was cut
  mid-phrase ("...your document", with "to create an outline." gone).
- Screenshot: the welcome capture, `window.png`.
- Status: fixed in this commit; the mac pane draws it as two lines now.

### The strip draws a stray trio and dead space (Nov 1)
- Three small unboxed icons (new, undo, redo) sit in the tab band left
  of the Home tab; undo and redo appear again as the large History
  items; about 150 px of blank space trails the Highlight group.
- Screenshot: the welcome capture, `window.png`.
- Status: open. The strip layout model needs a walk before a fix.

### The outline pane shows at the welcome screen (Nov 1)
- With no document open, the pane offers "No headings in this
  document.", which is text about a document that does not exist.
- Status: open; the welcome behaviour needs a decision first.

<!--
Entry format:

### Short name (date)
- What was done / what was expected / what happened.
- Screenshot: the screen and what it shows.
- Status: open | fixed in <commit> (CI run <id>)
-->
