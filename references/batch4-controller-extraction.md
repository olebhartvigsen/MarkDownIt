# Batch 4 extraction map: the shared document controller

Where every command family lives today, what moves into
`src/documentcontroller.{h,cpp}` in Batch 4, and what CI can referee
per family. Line numbers were measured on 2026-11-09. They will drift,
so re-verify a range before you cut a move from it.

## What is already shared

The command algorithms already compile on both runners. The
duplicated part is the controller layer between them and the chrome:
edit-mode gating, pending format state, undo entry building, repaint
and invalidation. Batch 4 moves the middle layer, not the algorithms.

- Edits, composition, Enter and Backspace: `src/editcontroller.{h,cpp}`.
- Undo entries and coalescing: `src/undostack.{h,cpp}`.
- Inline toggles, links, headings, lists: `src/formatting.cpp`.
- Table algorithms: `src/tableedit.cpp` and `src/navigation.{h,cpp}`
  (`TableInsertColumn`, `TableRemoveColumn`, `TableSplitCell`,
  `TableSetColumnAlign`).
- Find state and matching: `src/searchreplace.cpp`, `src/findstate.cpp`,
  `src/findbar_model.cpp`. `FindBarListener` in `findbar_model.h` is
  already a cross-platform contract both bars speak.
- Office import and export: `src/office/`, about 4655 lines, both exe
  import/export shells wrap the same functions.

## What each platform still owns

Windows wraps everything in `src/app.cpp` (7008 lines) and `src/app.h`
(517 lines). The `AppWindow` owns the buffer, selection, editor, undo
stack, layout cache, dirty state, pending format state, find scope and
the Ctrl+A table escalation tier (`app.h:363-367`). The ribbon calls
`AppWindow` methods directly through `src/ribbon.h`, and enable state
flows through `GetFormatState` plus `InvalidateUICommand` at
`app.cpp:4079-4110` and `4759-4765`.

macOS splits the same state across `src/macos/app_mac.cpp` (1816 lines,
`app_mac.h` 491) and `src/macos/main_mac.mm` (3161 lines). The `.mm`
file owns the selectors, save dialogs, import panels and the menu
chain, including `validateMenuItem:` at `main_mac.mm:1947-1996` and
the `actionChangesTheDocument:` gate at `main_mac.mm:2001-2020`.

## What PlatformServices must carry

The service face is small. It carries the dialogs: open, save, export,
and the prompt to save or discard. It carries a clock for undo time
stamps, timer start and stop, file watch, message boxes, and focus
return when the find bar closes. Nothing else crosses.

The clipboard, settings, recents and the find bar listener already
have shared faces. The controller never sees the renderer either.
Selection and highlight news travel out through a
`SelectionChangedHandler`, and find scope asks one injected query,
`isRendered(offset)`.

## Move order

The plan order below keeps warm-ups first and the largest platform
residual last. Sizes are approximate lines that change hands.

| # | Family | Movers (Windows) | mac twins | Lines | Stays native |
|---|---|---|---|---|---|
| 1 | Command table (4.6) | `src/commands.h`, one shared id table | selector and key equivalent tables read from it | ~150 | Nothing. `check_command_coverage.py` starts failing the build instead of warning. |
| 2 | Headings and lists | `app.cpp:5702-5759` wrappers over shared `formatting.cpp` | `main_mac.mm:2899-2922`, `2982-2992` | ~130 | Nothing. Cheapest first behavior move. |
| 3 | Inline formatting and undo plumbing | `app.cpp:2236-2275`, `3888-4470`, `5032-5054`, `5680-5701`, `5760-5817`; `app.h:33-42` | `app_mac.cpp:809-1034`, `1603-1621` | ~600 | The Win32 link dialog (`app.cpp:4472-4619`), the keyboard map, ribbon forwarding. |
| 4 | Find and replace | `app.cpp:4684-4744`, `4877-5031` | `app_mac.cpp:240-376` | ~480 | The bars: `src/findbar.cpp`, `src/macos/findbar_mac.mm`. The scope query becomes `isRendered`. |
| 5 | Tables | `app.cpp:5055-5397`, `5440-5679`; `app.h:363-367` | `app_mac.cpp:403-668`, `764`, `1729-1746` | ~520 | The pickers: Win32 grid picker (`app.cpp:5106-5281`), `table_picker_mac.mm`. Algorithms already shared. |
| 6 | Open, save, import, export | `app.cpp:2430-2638`, `3592-3818`, `5854-6154` | `app_mac.cpp:154-238`, `377-402`; `main_mac.mm:2099-2286` | ~640 | Dialogs, panels, recents, file watching (`src/filewatch.cpp` has no mac twin). |

## Drift found while mapping

Four live divergences, each with file:line evidence:

1. Bold on a multi-run selection. Windows strips every bold run
   through `RemoveAllFormattingInSelection` (`app.cpp:4358-4360`). The
   mac side toggles one marker per run (`app_mac.cpp:882-888`). Pick
   one behavior, pin it with a transcript, then move the family.
2. Replace inside a table cell. The mac path sanitizes the replacement
   so a `|` cannot break the cell (`app_mac.cpp:316-336`, table spec
   rule 10). The Windows `onReplaceCurrent` splices raw text
   (`app.cpp:4901-4915`). The Windows behavior is the defect here.
3. Pending format undo entries. Windows stamps `GetTickCount64()`
   (`app.cpp:5766`), mac stamps zero (`app_mac.cpp:960`).
   `UndoStack::ShouldCoalesce` (`undostack.cpp:54`) reads 800 ms
   deltas, so the zero side only works because that entry type never
   reaches the delta branch. The controller's service clock ends this.
4. Standalone `.mmd` wrap. Windows loads and saves it
   (`app.h:313-318`, `app.cpp:2462-2562`, unwrap in `DoSave`). The mac
   side handled only the `.svg` fence. Family 6 resolved it with the
   twin: both ports decode and encode through `src/documentfile.cpp`,
   so the mac side now wraps `.mmd` too and the ledger row is gone.
   The same decode also gave the mac side the BOM strip it lacked.

One more duplication, not a divergence: the Ctrl+A table escalation
tier is written twice (`app.h:363-367`, `app_mac.h:449-452`). It
collapses into the controller with one behavior answer.

## CI referees per family

No family needs a capture gate. Each is pinned by two things, both
host independent and already building on both runners.

The first is the suite that belongs to the family: formatting,
tables, find state, office. The second is the transcript corpus from
the plan. A transcript is JSON: a start text, a list of commands fed
to the DocumentController, and the expected text, selection and undo
depth. Both runners assert the same file, and a compare job on
ubuntu-latest diffs the two results. The compare job follows the
svg-parity pattern. The inventory upload steps in
`.github/workflows/build.yml` (lines 162-168 and 249-255) already
exist, so the compare job reads them as written.

The four drift items above are the first transcripts to write, and
the first fixes they should pin.
