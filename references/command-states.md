# Command states: when a command can act

The command table (`src/commands.h`) says what each command is. This
document covers the other half: when each command is available. One
answer decides it, and four surfaces read that answer, the Windows
ribbon, the mac menu bar, the mac toolbar and the mac format bar. A
command that cannot act is greyed out, never hidden, so the surface keeps
its shape and the reader learns where things live.

The answer lives in `src/commandstate.h` and `src/commandstate.cpp`:

- `commands::Context` holds the facts the answer needs, filled from the
  document by `FromDocument` and from the window by the shell.
- `commands::RequirementsOf` names what each command needs: a document,
  edit mode, markdown, a selection, a query, undo history, headings, a
  table with room for the operation, or a zoom step left in the range.
- `commands::Enabled` combines the two.

The tests are `tests/commandstate_test.cpp`. They fail when a command is
added to the table without a requirements row, and when a mutating
command forgets the edit-mode gate.

## The three axes

The surface. The welcome screen holds no document, so every command that
edits, saves, finds or zooms has nothing to act on. New, Open, the two
imports, Recent, the tab bar and the app commands stay live, because they
are ways to get a document or about the app rather than about a page.

The mode. In view mode a command that changes the document stands down,
Save included: nothing can have changed, so a live Save button would be a
button that does nothing. Reading keeps working, which is copy, select
all, find, the marker, the zoom, the outline, the width and the export.

The file. Three kinds of document reach the editor.

| Kind | What the buffer holds | What it can take |
|------|------------------------|------------------|
| Markdown (.md, .markdown, .mdown, .mkd) | the file | everything, except inside a code block |
| Mermaid (.mmd, .mermaid) | the bare source, wrapped in a mermaid fence | text editing, find, the clipboard, save, zoom, no markdown formatting |
| SVG (.svg) | the bare XML, wrapped in an svg fence | the same as Mermaid |

Inside a markdown document the caret can also sit in a fenced code block,
an indented code block or a diagram. That place has no markdown around it
either, so the same gate applies: a caret inside code takes the markdown
commands away.

## The matrix

| Command group | Welcome | View mode | Edit, markdown | Edit, code or diagram |
|----------------|---------|-----------|----------------|------------------------|
| New, Open, Import Word, Import PDF | yes | yes | yes | yes |
| Recent, Show Tab Bar, About, Settings, Help | yes | yes | yes | yes |
| Reload, Save As, Close, Share, Export | no | yes | yes | yes |
| Save | no | no | yes | yes |
| Undo, Redo | no | no | with history | with history |
| Paste | no | no | yes | yes |
| Cut | no | no | with a selection | with a selection |
| Copy, Select All | no | with a selection | yes | yes |
| Bold, Italic, Strikethrough, Code, Link | no | no | yes | no |
| Body Text, Heading 1 to 6, lists, Quote, Indent, Outdent | no | no | yes | no |
| Clear Formatting | no | no | with a selection | no |
| Insert Table | no | no | outside a table | no |
| Add or remove a row or column, Split Cell, Align, Delete Table | no | no | inside a table, where the operation is valid | no |
| Find | no | yes | yes | yes |
| Find Next, Find Previous | no | with a query | with a query | with a query |
| Replace | no | no | yes | yes |
| Highlight | no | with a selection | with a selection | with a selection |
| Edit Mode switch, Source View | no | yes | yes | yes |
| Outline | no | with headings | with headings | with headings |
| Zoom In, Zoom Out | no | inside the range | inside the range | inside the range |
| Actual Size, Wrap, Width | no | yes | yes | yes |

Two rows deserve a note. Save As stays live in view mode because writing
a copy does not touch the document that is open. The marker is not a
document edit: it lives in a sidecar file, so it works while reading,
which is the whole point of marking a page you are reading.

## Why a fence and a diagram file are one case

A standalone `.mmd` or `.svg` file is bare source. The document model
wraps it in a fence so the normal pipeline can render it, and the file
type is remembered, so saving writes the source back without the fence.
That means the whole buffer of such a document is code, and the markdown
commands have nothing to act on anywhere in it.

The same is true for a fence inside a markdown document. The rule is
therefore written once, in `OffsetInCodeLikeBlock` (`src/dom.h`), and the
code block answer widens to the two fence lines themselves: a caret on
the opening backticks sits in the block the same way a caret in the
middle of the code does. The soft break, the caret placement and the
command gate all read that one function.

## What stays native

A few things are chrome rather than commands, and the shells keep them:

- The width entries carry their checkmark on the label. That is a label
  pass, not an enable decision, and it stays in `src/ribbon.cpp`.
- The Table Tools dropdown has no command row. Its enabled state follows
  its first item, which is the same answer the menu items inside it give.
- AppKit's own items (full screen, the window menu, services, hide and
  quit) carry no command row and keep their default.

## Adding a command

1. Add the row to `src/commands.h`, with the ribbon command number from
   `src/ribbon.xml` (`ribbonId`) and the mac selector.
2. Add the requirements to `RequirementsOf` in `src/commandstate.cpp`.
   A command with no row answers "unknown" and is greyed everywhere, and
   the test fails on it.
3. Both shells then show the right state without a change: the ribbon
   because it looks the row up by the command number it gets from the
   framework, the mac because the menu bar, the toolbar and the format
   bar all ask the same context.
4. Run `python3 tools/check_command_coverage.py`. It cross-checks the
   ribbon command numbers against `ribbon.xml`, and it fails when a shell
   grows a list of its own again.
5. Add a case to `tests/commandstate_test.cpp` when the command brings a
   new kind of precondition.

## Verification

The portable suite builds the whole command state on Linux, so the
answer can be checked without a window:

```
g++ -std=c++17 -Wall -Wextra -I tests -I src -I third_party/md4c \
    tests/test_main.cpp tests/commandstate_test.cpp src/commandstate.cpp \
    src/documentcontroller.cpp src/parser.cpp ... third_party/md4c/md4c.c
```

On the two real ports the same answer shows up as greyed ribbon buttons
and greyed menu items, and the launch smoke on macOS checks the toolbar
and the format bar in the states it can drive.
