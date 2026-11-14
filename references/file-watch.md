# Watching the open file

A document is a file someone else can change. An editor that keeps showing
the copy it read at nine o'clock is wrong the moment the file moves on disk,
which happens all the time: a build rewrites a README, a colleague commits
over a shared folder, `git checkout` swaps a branch. MarkDownIt reads the
open file again when the disk says it changed.

## What is watched

The file the page came from, and only that file. The folder is not watched,
the recent list is not watched, and an untitled document has nothing to
watch at all. A deleted file is not a changed file: the page keeps its text,
the watch stays quiet, and the next save writes the file back.

## The policy is shared

The decision lives in `src/filewatch_model.h`, so both ports answer the same
way:

- **The stamp** is the size and the modification time, in the finest unit the
  platform reports (100 ns ticks on Windows, nanoseconds on the mac). Both
  fields are compared, because a rewrite that keeps the length is still a
  rewrite, and a save inside a coarse filesystem's clock tick keeps the time.
- **The test is "differs", not "newer".** A file restored from a backup or a
  `git checkout` moves backwards in time, and that is news like any other
  change.
- **A write the app made is not news.** The window remembers the stamp it
  read and the stamp it wrote, so its own save does not come back as a
  change on disk.
- **A clean page is reloaded without asking.** The reader loses nothing.
- **A page with unsaved edits asks first.** Reading the disk again throws
  those edits away, so the window puts the question and the reader decides.
  Either answer settles that change: the next poll does not ask again.
- **An unreadable file changes nothing.** A share can go away and a writer
  can hold the file exclusively. A failed read is not a change, and the page
  stays as it is.

## The mechanism is the platform's

Both windows poll. The beat is the same, half a second, and the cost is two
`stat` calls a second, which a network share or a mounted image survives.

- Windows: `src/filewatch.cpp` runs a worker thread that calls
  `GetFileAttributesExW` and posts `WM_USER_RELOAD`. The thread is stopped
  and restarted around a save, and it is waited out rather than abandoned,
  because a worker still reading the old path would post to a stale window.
- macOS: `src/macos/filewatch_mac.cpp` reads the stamp with `stat`, and
  `MdAppDelegate` polls it from an `NSTimer` on the main run loop. The timer
  is not a file-system event API on purpose: FSEvents and kqueue both say
  little or nothing about network shares and mounted disk images, which is
  exactly where a file changes behind the app's back.

The two readers fill the same `watch::Stamp`, so no policy is written twice.
This is a standing difference in the parity ledger: the poll is native, the
answer is shared.

## What a reload keeps

The reader's place, because a reload is not a reason to lose it:

- The mode stays. An external change is not an argument about whether the
  reader is editing, so a reload never leaves edit mode.
- The caret goes back to its offset, clamped to the text that arrived.
- The scroll position goes back, after the caret has had its say.
- The marks are re-read, since the sidecar file may have moved with the
  document.
- The unsaved marker is cleared, because the page is the disk's now.

## Where the shells hook in

| Moment | Windows | macOS |
|--------|---------|-------|
| A file is opened | `LoadDocumentText` starts the watch and remembers the stamp | `OpenPath:` calls `startFileWatch:` |
| A document is saved | `DoSave` restarts the watch and remembers the stamp | `saveCurrentDocument` / `saveDocumentAs` remember the stamp |
| A new document starts | `LoadDocumentText("")` stops the watch | `OnNewDocument` stops the watch |
| The Reload command runs | `OnReload` reloads, `Reload` remembers the stamp | `OnReload` reloads and remembers the stamp |
| The disk speaks | `WM_USER_RELOAD` reaches `OnFileChangedOnDisk` | the timer reaches `fileWatchTick:` |

## Not covered

- The home screen holds no file of its own, so nothing is watched there.
  A document that is still open behind the recent cards stays watched.
- A file replaced through a symlink is watched at the link's path, which is
  what was opened. Reading through the link follows it to the target.
- Nothing merges. A change on disk replaces the page, so an editor that
  writes over the same file while the reader types is a conflict the reader
  answers, not one the app resolves.

## Verification

`tests/filewatch_model_test.cpp` holds the policy: the two-field stamp, the
own-write case, the clean reload, the question for a dirty page, the answer
that stops the question, the file that moves backwards, the file that cannot
be read, and the untitled document. It runs on both test runners and in the
portable build. The poll itself is platform code and is exercised by hand on
each platform: open a file, change it in another editor, and watch the page
follow.
