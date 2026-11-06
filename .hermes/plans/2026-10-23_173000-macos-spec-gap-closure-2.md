# macOS spec-gap closure, round two

Written 2026-10-23 after the five re-audits of the reference specs at
`5eceb6782`. Round one (`.hermes/plans/2026-10-23_004116-macos-spec-gap-closure.md`)
closed the caret model, the table keyboard and paste, the find bar,
zoom, the mark and link sheet, the format toggles, the outline pane,
the office reports and the mermaid measurement. The re-audits confirm
those closures and list what is still open: nine blocking rows and
roughly seventy minor ones, against 239 compliant rows.

Every blocking row below was verified against the code in this tree
before it was accepted, including the two defects the auditors
reproduced with compiled probes (the empty-cell caret and the table
destroyed by Delete at its leading blank line).

## Blocking rows

| Id | What | Where | Fix |
|---|---|---|---|
| C2 | Copy stores raw source, hidden Markdown markers included | `src/macos/main_mac.mm:817`, `app_mac.cpp:1122` | Clip the selected range to drawn text before the clipboard write |
| AC-09 | No text drag: a press inside a selection must move it after the drag threshold, drop is one undo step, Esc cancels | `src/macos/content_view_mac.mm` mouse handlers | Drag source and drop target over the shared `MoveTextRange` |
| RG-TabEnter | Tab outside a table writes a literal tab character | `src/macos/app_mac.cpp:529` | Port the Windows handler: four spaces in a code block, indent or outdent elsewhere |
| RG-Linkakt | Nothing opens a link: `OpenInBrowser` has no caller | `src/macos/seams.h:281` | Renderer link hit test plus Cmd-click activation with the http/https/mailto allowlist |
| table-7 | A click in an empty cell leaves the cell; the caret has no rect there; the caret `Insert Table` parks is invisible | `src/macos/renderer_mac.cpp:1711`, `1945` | Clamp into the cell even when `visToSrc` is not empty, and give an empty cell a placeholder line |
| table-45 | Delete at the blank line before a table destroys the table | `src/editcontroller.cpp:486` | Add the guard the symmetric `DeleteBackward` already has |
| F10 | View Source is a dead control; the find spec's view modes clause cannot pass | `src/macos/main_mac.mm:847` | Implement the source view: draw the raw buffer and switch on the flag |
| OFF-01 | No PDF import | (absent everywhere) | Shared PDF text import over pdfio with a report of what is dropped |
| OFF-06 | PDF export is whole-document only; the writer already carries first/last page | `src/macos/main_mac.mm:716` | A scope choice that fills `PdfExportOptions` (pages or selection) |
| OFF-07 | No PDF password or permission options | `src/office/pdf_export.h` | Standard security handler in the writer plus UI |

## Batches

1. **Batch A, keyboard and clipboard core.** RG-TabEnter, C2, C3 (a
   failed clipboard write must not delete on cut), S7 (line-edge
   selectors), AC-02 (Shift+Page selectors), AC-11 (Replace shortcut
   conflict with Hide), EXT-10 (in-cell replace sanitizing), F24 and
   AC-12 (the next match after a replace), RG-Nav (desired X reset on
   edit), RG-DobbeltTrippel (double click on a blank line), RG-Format
   (seed the link dialog), F2 and F54 (Cmd+F refocus keeps the query),
   F5 (accessible names on the find bar), AC-01 (menu commands must
   reach the field editor when it has focus).
2. **Batch B, tables.** table-7, table-12 (arrows stop at the cell
   edge), table-45, table-54 and table-58 (Insert Table gated in a
   cell), table-60 and AC-07 (validate the parse, roll back a failed
   mutation), wp-H (empty-cell and blank-paragraph click tests), X2
   (the combined Tab test).
3. **Batch C, three features.** RG-Linkakt, F10 (source view), AC-09
   (text drag).
4. **Batch D, cell selection and tiers.** table-19, table-20, table-8,
   table-38 and S8 (Cmd+A tiers), AC-10 (double and triple click in
   cells, Up/Down in a column), table-52.
5. **Batch E, office.** OFF-06, OFF-25 (real error text), OFF-11
   (accept .doc), OFF-29 (report before writing), OFF-01 (PDF import
   with its report), then OFF-07 if the encryption work fits.
6. **Batch F, find minors.** F16 (a match inside a cell scrolls),
   F35 (Unicode folding), F43 (large-document search), F50 (query
   history), F53 (strings), Z15 (bare zoom keys, or a documented
   decision).
7. **Batch G, outline and mark minors.** H-5.1 (a list-item selection
   must not eat the bullet or inline delimiters), H-12.1 (the sidecar
   wording), H-8 (mark rects follow cell alignment), O-19 (scrolling
   updates the active heading), O-7 (focus returns after a jump),
   O-23 (skip the outline rebuild when headings are unchanged).
8. **Batch H, accessibility and ribbon.** table-47 (table and caret
   accessibility), F51 (announcements), AC-05 (IME marked text),
   RIB-01 (large and small icon tiers), RIB-02 (condensed groups).

## Open on both platforms, tracked but no mac-only fix

table-4, table-21, table-30 to table-32, table-34 to table-36,
table-41, table-42 (merge cells, conversions, widths, borders,
backgrounds and vertical alignment have no Markdown representation or
no design yet), night mode (nothing implements it anywhere), AC-04
(the Unicode conformance test data), the image carrying in the office
model (OFF-03, OFF-13, OFF-19), PDF/A (OFF-08), page setup kept across
import and export (OFF-16) AC-08 (a Remove Link command is missing on
Windows too), and the two ribbon layout notes (RIB-01: the icon tiers
need the Windows markup, so both platforms change in one batch; RIB-02:
the mac strip's compact and small states landed in Batch I5, the
collapse step and the Windows side stay with the shared decision).

## Method

One commit per batch on `feat/macos-port`, the shared checkers plus a
local cross-compile before each push, CI green per batch before the
next starts, and the delivery folder refreshed from the last green
run. Goldens never change.

## Status

- Batch A (keyboard and clipboard core) is implemented. Local gates
  before the push: the four checkers, the portable suite (7481
  checks, 0 failures), and an aarch64 compile of every touched app
  file with ARC and the app flags, all green. CI is the authority for
  the link stage.
- Batch B (tables) is implemented: the empty-cell clamp and its
  placeholder line, the cell wall for the arrows, the forward delete
  guard before a table, the shared row append with portable tests
  (X2), and the Insert Table gating in the menu and the strip.

- Batch C (link activation, source view, text drag) is implemented:
  a Cmd/Ctrl+click on a link opens it through the http/https/mailto
  allowlist, an internal #anchor moves the viewport instead, View
  Source draws the raw buffer so Find matches raw bytes, and a press
  inside the selection drags it as one undo entry (Esc cancels).

- Batch D (cell selection and tiers) is implemented: a double click
  takes the cell, a triple click the row, Cmd+A escalates cell, row,
  table, document with a reset whenever the caret moves, and Up/Down
  in a cell keep the column. The ranges and the tier step are shared
  helpers, covered by the portable suite.

- Batch E (office) is implemented: the PDF export asks what it should
  hold (whole document, page range, marked area), a protected export
  carries a user and an owner password plus the print, copy and change
  flags through the standard security handler, the compatibility
  report is shown before anything lands on disk, a failed import or
  export carries the importer's own message into the alert, and the
  open panel offers .doc files so a legacy document explains itself.
  The suite writes reference_protected.pdf, and CI checks it with
  pypdf: a wrong password opens nothing, the user password unlocks
  it, printing is allowed, copying and changing are refused, and the
  unlocked pages match the same golden the plain export uses.

- The PDF import (OFF-01) follows as its own step inside Batch E:
  pdf_import.cpp reads text, headings, lists, paragraphs, link
  annotations and the page breaks of a text-based PDF into the model,
  keeps the page setup of page one, warns about everything the model
  cannot hold, asks for the password of a protected file, and offers
  OCR for scanned pages through a seam. macOS fills the seam with
  Vision (Danish included), and File > Office carries Import from
  PDF....

- Batch F (find minors) is implemented: the search folds the text the
  way the spec asks (canonical decomposition plus case folding, with
  the tables generated from the Unicode data, and a match that has to
  cover whole characters), the model keeps the session's recent
  queries and the mac bar shows them behind a history button, a large
  document gets a debounced search instead of a search per keystroke,
  and the bar's strings sit in tables a translation can replace. A
  find jump into a table finds its row through the cell's own rendered
  map, the same map the hit test reads. The zoom guide records why the
  bare plus and minus keys stay out of an editor, and Command with the
  plain equals key zooms in beside them.

- Batch G (outline and mark minors) is implemented: the outline keeps
  the caret's section while the caret is on screen and follows the
  reading line once it is out of view, scrolling updates the highlight,
  a jump from the pane returns the keyboard to the document, and a
  reparse that leaves the headings alone keeps the pane's rows instead
  of rebuilding them (outline spec 7, 19 and 23). A mark or a selection
  inside a table cell follows the column alignment the painter uses
  (highlighting spec 8). The text of a list item starts past the bullet
  through one shared helper both platforms read, so the triple click and
  the backspace after it never eat the bullet (highlighting spec 5.1),
  and the spec records where the mac side keeps its marker sidecar
  (highlighting spec 12.1).

- Batch H (accessibility, input, answer to the reader) is implemented.
  An input method writes marked text through the content view, and the
  composition lives in the shared edit controller: it creates no undo
  entry before the text is confirmed, the confirmation is exactly one
  entry, and a cancelled composition puts the text and the selection
  back (text editing spec, AC-05; a system IME stays a Windows manual
  exception by the spec's own wording, the model side is covered by
  tests). The find bar announces no results, the match the navigation
  moved to, and the count Replace All replaced (find spec 44). The
  tables, their rows, header row and cells are exposed to a screen
  reader from the same geometry the painter uses (table spec 47).
  RIB-01 (large and small icon tiers) and RIB-02 (condensed groups)
  stay with the two ribbon layout notes as a decision for both
  platforms at once: the Windows ribbon is RibbonX markup whose buttons
  are all small, the mac strip mirrors it by policy (check_mac_ribbon),
  and a tier change must land on both sides in one batch, below the
  minimum window width where the condensed state would live.

- Batch I (the reference audit's gaps, closed) is implemented. A
  read-only audit of every reference document against the mac sources
  at HEAD found twelve gaps on the mac side and no Windows
  regression; each one is now closed:
  - The edit mode exists. One flag on the App gates every mutation
    entry point (typing, composition, paste, replace, every delete,
    both breaks, the paragraph break, undo, redo, the format toggles,
    the clear format command, cut, the text drag and every table
    rewrite), the find bar's replace half follows it, the caret hides
    in read mode, and the mode is in the menu (Edit Mode, Cmd+E) and
    on the strip as the toggle the markup already lists. Everything
    that only reads, finds, zooms, selects, copies or marks stays
    available, and the mutating commands grey out the way the Windows
    ribbon greys them (text editing spec, state and commands, AC-06,
    error and write protection).
  - A pasted block is escaped the way the Windows build escapes it, so
    pasted markdown syntax stays literal text; the raw copy still
    feeds the spreadsheet table detector, and an in-cell paste is
    sanitized after the escape (text editing spec, clipboard paste,
    AC-05). Control characters never reach the buffer through typing
    or paste.
  - Up and Down with a selection collapse to Start and End, and Home
    and End use the selection's first and last line, the Windows
    rules (spec navigation, AC-02).
  - The Windows alias keys Ctrl+Insert, Shift+Insert, Shift+Delete
    and Ctrl+Y reach the same commands their Cmd forms reach
    (spec AC-01).
  - The drag autoscroll step scales with the pointer distance with the
    Windows floor and ceiling instead of a constant step (spec mouse
    drag), and a drag that starts inside a table cell never leaves
    that cell (table spec 20).
  - Undo and redo bring the restored caret back into view through the
    same follow an edit uses (spec undo, state after undo).
  - The zoom buttons dim at the ends of the range (zoom guide).
  - The find strip docks at the bottom of the window. It sat above the
    document before, while the find spec names one strip position: the
    bottom of the application window (find spec 27).
  - The strip has three layout states now: full, compact and small,
    chosen by the window width, so the right hand groups stay
    reachable instead of being clipped on a narrow window (ribbon
    guide 8, RIB-02 on the mac side; the icon tier question of RIB-01
    still needs the Windows markup and stays a shared decision).
  The strip carries the edit toggle now (38 commands) and the menu 55
  commands, both enforced by the checkers, and eight mac sources
  compile against the 11.3 SDK in the local gate.

- Crash fix (the user's report): the shipped mac build died inside
  md4c's callbacks when editing reparsed the document. Root cause
  found with an ASan fuzz of the app's exact edit-then-reparse flow:
  ParseMarkdown parsed in place, so a reparse into the same Document
  kept the previous parse's nodes while the callbacks' offset table
  started empty, and the first text run read past the table. The
  Windows build carried the same overrun behind its structured
  exception guard, which turned it into a silent failed parse. The
  parse now builds into a fresh Document and swaps it in on success,
  a failed parse leaves the previous document fully in place, and two
  regression tests (Reparse.IntoTheSameDocumentStartsFresh,
  Reparse.TitleFollowsTheReparse) pin both halves. Verified with the
  ASan edit fuzz (30k rounds) and the parser smoke suite (399 checks,
  0 failures) before the commit.

- Batch J, after the second audit round (deleg_f0e21924, three read-only
  audits at 677cb1e): 222+ compliant rows across the nine remaining
  reference docs; the mac gaps it found were mostly the ones Batch I had
  already closed. Three survived and are closed here: the Insert Table
  strip button was enabled only inside a table (the menu was right); the
  rest of the table group ignored the shared capability table, so buttons
  the guards refuse stayed live; and the find bar's Replace and All titles
  were the only hard-coded strings left. The strip now asks
  TableCapabilitiesFor for every row/column/split command, Insert Table
  enables outside a table, and the whole table group follows read mode.
  Checkers green: 38 strip commands, 55 menu commands. Office and mermaid
  rows were fully compliant; the office gaps in that audit (page setup,
  PDF/A, image payload, section breaks, field warnings) are open on both
  platforms and tracked for one shared batch.



### Batch K: what the shipped build showed (ce6764f, and the italic fix fb727f3)

A run of the shipped build settled four of the visual gaps: the caret
blinked its flag but nothing repainted (`ContentView::Invalidate()` was
a stub), so the blink, focus and defocus paths repaint now; the document
and the chrome draw with the platform's own faces instead of the
bundled Selawik and Cascadia Mono (the port plan's font note); the
ribbon labels draw in the control-size system font in wider cells; and
the window stayed light while the palette was fixed, so a dark-mode Mac
could not mix appearances. The italic path learned that Core Text can
answer the slant trait with the upright face: the engine asks for the
italic through the symbolic trait first and shears the font itself
(tan 12 degrees) when the answer is unchanged, so an italic run is
never drawn upright (fb727f3).

### Batch L: the mac visual style guide (7307732)

references/osx-markdown-visual-style-guide.md landed as a standing
requirement, and the display now follows it: the palette resolves from
AppKit's semantic colours, so the document belongs to Light Mode and
Dark Mode (the forced light appearance is gone) and the outline pane
refreshes when the appearance changes; code draws in the system's
fixed-pitch face through the platform API; headings and inline strong
spans draw semibold, the weight the Windows heading formats use; and
task list items draw a checkbox in the marker gutter (the parser
records the check state and drops the "[ ]" from the rendered text; the
shape carries the state, not the colour). The mermaid diagrams keep
mermaid's own default-theme colours on purpose. The parser smoke suite
gains a task list mapping test; the text engine test follows the
fixed-pitch mapping.


### Batch M: the standard mac furniture (b37aed5)

One more runthrough against the style guide and the macOS conventions:
the app menu gains Services, Hide Others and Show All, and every item
that opens a panel carries the Apple ellipsis; File gains Open Recent
(filled from the same list the welcome screen shows) and Reload gets
Command-R; Edit gains the standard Find submenu with Find Next and
Find Previous carried out by the delegate; Edit Mode moves to View and
Enter Full Screen joins it (with the system's own key, and the item
renames itself the way the system's does); a Window menu and a Help
menu join the bar and are handed to NSApp; the Settings command opens
a real window of standard controls, with the width mode and the
outline switch going through the same commands the View menu uses; the
title bar becomes a document title bar (display name, proxy icon,
edited dot); and the strip's tooltips read the shortcuts in the Apple
spelling (Command-Option-F for replace, because Command-H hides the
app; Outline gains the Command-Shift-O the tooltip names).

The launch smoke caught a crash on the way: the palette refresh fired
during view setup, before the outline pane existed, and the handler
reached a still nil pane. The handler is wired where the pane is, and
it checks before it calls.
