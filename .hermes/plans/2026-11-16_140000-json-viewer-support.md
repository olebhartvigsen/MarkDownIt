# JSON viewer support, implementation plan

> **For Hermes:** Use subagent-driven-development skill to implement this
> plan task-by-task.

**Goal:** Open a `.json` file in MarkDownIt as a read-only, pretty-printed,
syntax-highlighted document, the same way `.mmd` and `.svg` already open as
synthetic fences; add a one-click "Open in JSON Editor Online" action for
small files, which copies the buffer to the clipboard and opens
jsoneditoronline.org in the default browser.

**Architecture:** Follow the CSV precedent exactly (`csv-view.md`,
`src/csv.*`, `src/csvdocument.*`, `src/documentparse.*`). A new shared
reader (`src/json.*`) parses the bytes into a tree, a new bridge
(`src/jsondocument.*`) turns that tree into `Document` nodes the existing
code-block layout and both painters already draw, and the one kind switch
in `src/documentparse.cpp` picks the JSON path the same way it already
picks the CSV path. No platform header touches any of it. The "open
externally" action is native chrome (a command, a menu/ribbon entry, a
clipboard write, a browser launch), the same category as `cmdAssocMd` or
the Office import/export dialogs.

**Tech stack:** C++17, no vendored JSON library (the project ships
zero third-party dependencies beyond md4c/miniz/pugixml/pdfio). A JSON
reader is written by hand, the same way `settings_mac.cpp` writes JSON by
hand today.

---

## Current context / assumptions

- `FileFormat` (`src/documentfile.h/.cpp`) already has the exact shape
  needed: `isMmd`, `isSvg`, `isTable`, plus `mmdWrapped`/`svgWrapped` for
  the two kinds that get a synthetic fence. `.json` needs a third flag,
  `isJson`, and a `jsonWrapped` flag alongside it.
- `src/documentparse.cpp` is the one kind switch both ports call
  (`ParseDocument`). It currently special-cases CSV; markdown (and
  everything fenced, including `.mmd`/`.svg`) goes through `ParseMarkdown`.
  A `.json` file wrapped in a ` ```json ` fence needs **no change here at
  all** if the existing fenced-code renderer already colors or at least
  monospaces a `json` fence. That must be confirmed first (see Task 1).
- `commandstate.h`'s `Context` already carries `diagram` (the `.mmd`/`.svg`
  case: markdown formatting commands stand down inside the fence) and
  `tableDocument` (the CSV case: the whole document is read-only). JSON
  is closer to the diagram case than the table case: it is one fenced
  block, not a structural document type, so the existing `diagram`-style
  handling (or a new, analogous `jsonDocument` flag if full read-only
  behavior is wanted) covers it without new render code.
- No JSON parser exists anywhere in the codebase or third_party/. A reader
  has to be written from scratch, same as `csv.cpp` was.
- macOS document types live in `installer/macos/Info.plist`
  (`CFBundleDocumentTypes` / `UTExportedTypeDeclarations`). Windows file
  association lives in `src/fileassoc.cpp` (`kExts` array, registry-based).
  Both need a `.json` row, each guarded by a parity ledger row if one
  port's mechanism differs from the other's (it always does: registry vs.
  Info.plist is already a standing difference, see ledger line for
  `Settings store`; this is the same kind of native convention, not a new
  divergence to flag separately, but the new document type row in each
  config file should be pointed out in the ledger's existing "native
  chrome" convention sentence, or added as a one-line standing-difference
  row to be safe against `check_parity_ledger.py`).
- jsoneditoronline.org's own feature page confirms it loads JSON from
  clipboard paste, drag-and-drop, local disk pick, or a reachable URL. It
  cannot reach a file that only exists on the user's local disk unless the
  user feeds it through one of those three channels itself. **No query
  parameter exists that preloads arbitrary pasted JSON into the site**
  (checked: `?id=` only addresses JSON the site itself already stored).
  The honest integration is: copy the current buffer to the clipboard,
  open the site, tell the user to paste. A "small files" qualifier from
  the user is really a clipboard-size judgment call, not a technical cap
  the site imposes; the plan puts a soft warning above a size the
  clipboard is unpleasant to use with (see Task 9).

## Proposed approach

Two independent slices, both following the "shared first" rule:

1. **Viewing.** A `.json` file decodes like `.mmd`/`.svg`: wrapped in a
   synthetic ` ```json ` fence so the existing markdown pipeline renders
   it with no new layout code. The JSON reader additionally **re-serializes
   the parsed tree with two-space indentation** before wrapping it, so a
   minified or oddly-indented file still reads as a tree on screen. A
   parse failure falls back to showing the raw bytes verbatim in the fence,
   the same way a CSV file with an unreadable encoding still shows a
   notice instead of crashing.
2. **Hand-off.** A new command, `cmdOpenInJsonEditor`, enabled only when
   the open document is a `.json` file, copies the pretty-printed text to
   the clipboard (reusing `src/clipboard.cpp`) and asks the platform shell
   to open `https://jsoneditoronline.org` in the default browser. Each
   port's app shell already knows how to open a URL (the same primitive
   `cmdAssocMd`-adjacent flows and the "about" or help links would use);
   if that primitive does not exist yet on one port, it is a thin,
   one-line OS call (`ShellExecuteW` on Windows, `NSWorkspace openURL:` on
   macOS), the kind of native-chrome code the architecture already expects
   to be written twice.

### Why a real reader instead of just showing raw bytes

A `.json` file could in principle be shown completely unparsed, as plain
fenced text: no reader needed, and the fence alone satisfies "view a JSON
file." That is rejected here for one reason: most real-world JSON that
lands next to markdown notes (API responses, build manifests, lockfiles)
is minified or inconsistently indented, and unindented JSON is not
readable in a fixed-width fence. The reader's whole job is the pretty
print; a validator and tree-folder are explicitly out of scope (see "Not
built now").

### Why not the CSV table path

CSV's table path exists because rows and columns are the natural shape of
that data and the table layout already existed. JSON's natural shape is a
tree, not a grid, and MarkDownIt owns no tree layout. Building one now
would duplicate the effort twice (CI parity, both painters, the table
guidelines doc) for a feature the user asked for as "a viewer," not an
editor with folding. The fenced-code path reuses layout and painters that
already exist on both ports; a future "collapsible tree" can be scoped
later without redoing this slice (see "Not built now").

## Step-by-step plan

### Task 1: Confirm the fenced-code render path colors unknown languages sanely

**Objective:** establish whether a ` ```json ` fence needs a new
highlighter, or whether it already renders as a monospace block through
the existing code-span/code-block path, so Task 4-6 do not guess.

**Files:** read-only investigation.
- Inspect `src/parser.cpp` for how md4c fence info strings are handled
  (language tag storage on the code block node).
- Inspect `src/layout/` and `src/renderer.cpp` /
  `src/macos/renderer_mac.cpp` for any per-language coloring (search for
  `"mermaid"`, `"svg"`, `"cpp"`, `"json"` as fence languages already
  special-cased).

**Verification:** a one-line note added to this plan's Task 4 saying
either "no language-specific highlighting exists; a `json` fence renders
in the same monospace block every other fence does" or "a highlighter
table exists at `<file>`; add a `json` row there.

**Done (2026-11-16):** no `colortext.*` or any per-language highlight
table exists anywhere in the codebase (searched for `"mermaid"`, `"cpp"`,
`"json"` as fence languages in `src/parser.cpp`, `src/layout/`, the
renderers; only `n.lang == "mermaid"` for diagram routing, no coloring).
A `json` fence renders as a plain monospace code block, same as every
other unrecognized fence language. Task 4 needs no code change.

### Task 2: Write the JSON reader (`src/json.*`)

**Objective:** parse UTF-8 JSON text into a small in-memory tree, with
caps mirroring `csv.h`'s caps so a huge file degrades the same honest way
a huge CSV file does.

**Files:**
- Create: `src/json.h`
- Create: `src/json.cpp`
- Test: `tests/json_test.cpp`

**`src/json.h` sketch:**

```cpp
// Reading a JSON file into a tree (plan: .hermes/plans/2026-11-16_140000-json-viewer-support.md).
//
// RFC 8259 only: no comments, no trailing commas, no unquoted keys. A
// standalone .json file is read, not edited, so a strict parser is the
// honest choice; a lenient one would hide a syntax error the file
// actually has.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace json {

constexpr std::size_t kMaxDepth = 64;
constexpr std::size_t kMaxBytes = 8 * 1024 * 1024;  // 8 MiB

enum class Kind { Null, Bool, Number, String, Array, Object };

struct Value {
    Kind kind = Kind::Null;
    bool boolValue = false;
    std::string numberOrStringValue;  // literal text, not re-parsed
    std::vector<std::pair<std::string, Value>> object;  // key order kept
    std::vector<Value> array;
};

struct ParseResult {
    bool ok = false;
    Value root;
    std::string error;       // "unexpected token at line 4, column 12"
    bool truncated = false;  // kMaxBytes or kMaxDepth was hit
};

ParseResult Parse(const std::string& utf8Text);

// Re-serialize a parsed tree with two-space indentation, stable key
// order, and one value per line for scalars. This is the only output
// shape the viewer ever shows.
std::string PrettyPrint(const Value& root);

}  // namespace json
```

**Step 1: write failing tests** in `tests/json_test.cpp` covering:
- an object, an array, nested structures, all six kinds
- strings with escapes (`\"`, `\\`, `\n`, `\uXXXX`)
- numbers (int, float, exponent, negative)
- `kMaxDepth` exceeded reports `truncated = true` rather than a stack
  overflow (recursive-descent parser; cap the recursion explicitly)
- `kMaxBytes` exceeded reports `truncated = true` with a partial tree
  up to that point, matching the CSV convention of showing a cut rather
  than nothing
- malformed input (`{"a":}`, trailing comma, unterminated string) returns
  `ok = false` and a line/column in `error`
- `PrettyPrint` round-trips: parse, pretty-print, parse again, same tree

**Step 2:** `g++ -std=c++17 -c tests/json_test.cpp` style run through
whatever harness `tests/gtest_lite.h` drives; expected FAIL (nothing
implemented yet).

**Step 3:** implement `src/json.cpp`: a hand-written recursive-descent
parser and a pretty-printer. No regex, no external dependency.

**Step 4:** run the suite again, expect PASS.

**Step 5:** commit (on the current branch, per AGENTS.md: no new branch
unless asked).

### Task 3: Wire `.json` into `FileFormat`

**Objective:** a `.json` path gets a synthetic ` ```json ` fence the same
way `.mmd` gets a `mermaid` fence.

**Files:** modify `src/documentfile.h`, `src/documentfile.cpp`.

```cpp
// src/documentfile.h
struct FileFormat {
    ...
    bool isSvg = false;
    bool isJson = false;        // NEW
    bool isTable = false;
    bool mmdWrapped = false;
    bool svgWrapped = false;
    bool jsonWrapped = false;   // NEW
};
```

```cpp
// src/documentfile.cpp, alongside the existing isMmd/isSvg assignment
f.isJson = !pathUtf8.empty() && PathHasExtension(pathUtf8, ".json");
...
if (f.isJson && !HasLangFence(text, "json")) {
    // Pretty-print before wrapping: a minified file should not render
    // as one unreadable line in the fence.
    json::ParseResult parsed = json::Parse(text);
    std::string body = parsed.ok ? json::PrettyPrint(parsed.root) : text;
    text = WrapLangFence("json", body);
    f.jsonWrapped = true;
}
```

Unwrap side (`EncodeFileText`) mirrors `isMmd`/`isSvg`: if the file is
reopened for saving (it should not normally be editable, see Task 7), the
fence comes back off. Since JSON stays read-only, `EncodeFileText` for
`.json` is in practice unreachable, but keeping the symmetry means a
future "allow editing" change does not have to relearn the unwrap rule.

**Step: write failing test** in whatever suite already covers
`DecodeFileText`/`EncodeFileText` for `.mmd` (`tests/documentfile_test.cpp`
if it exists; locate it before writing a new one).

**Verification:** `DecodeFileText` on a minified JSON file emits the fence
with pretty-printed body; on a malformed JSON file it emits the fence with
the raw bytes untouched inside it (no silent data loss).

### Task 4: Confirm the fenced block renders through the existing pipeline

**Objective:** make sure opening the resulting markdown (one ` ```json `
fence) draws correctly with no renderer change, per Task 1's finding.

**Files:** none expected to change, this is a smoke test.

**Verification:** open a `.json` test fixture end to end in a portable
build (the parser + layout path that builds without a platform header,
per AGENTS.md's "local g++ checks are fine for portable code"); confirm
the display list contains one code block with the pretty-printed lines.
If Task 1 found a per-language highlight table, add a `json` row there and
note it as a one-line addition to this task, not a new task.

### Task 5: Read-only gating in `commandstate`

**Objective:** a `.json` document behaves like a `.mmd`/`.svg` diagram
file for command gating: formatting commands that only make sense in
prose stand down, the way they already do inside a fence per the
existing `diagram` flag and `inCode` logic.

**Files:** inspect `src/commandstate.h`, `src/commandstate.cpp`,
`src/documentcontroller.h` for how `Context::diagram` is set from
`FileFormat::isMmd`/`isSvg` today, and either:

- reuse that exact path (`diagram = isMmd || isSvg || isJson`) if the
  existing gating already matches what a JSON file needs (no table
  edits, no heading/list commands, but find and copy keep working,
  editing the raw text is still permitted the way editing raw `.mmd`
  text is), or
- add a narrow `jsonDocument` flag only if JSON needs to be fully
  read-only (closer to `tableDocument`) rather than "editable as code."

**Decision point for the implementer:** the user asked for "visning"
(viewing), which argues for the `tableDocument`-style full read-only
behavior (no accidental edits to data a reader did not intend to
change), not the `.mmd`-style "it's really source code, edit away"
behavior. Default to treating `.json` like CSV: read-only, formatting
commands off, Source view available, find off for the same reason find
is off for tables (the pretty-printed text is not necessarily what is on
disk byte for byte once re-indented). Write this decision into
`references/` per Task 8 so it is not re-litigated.

**Step:** update `commands::RequirementsOf` (`src/commandstate.cpp`) so no
command is left enabled-but-broken on a JSON document; add rows to
`tests/commandstate_test.cpp` mirroring the existing CSV rows.

### Task 6: `cmdOpenInJsonEditor` command

**Objective:** a command, visible and enabled only on a `.json` document,
that copies the current buffer (the pretty-printed text, not the fenced
form) to the clipboard and opens `https://jsoneditoronline.org` in the
default browser.

**Files:**
- `src/commands.h`: add one row (id, menu name
  "Open in JSON Editor Online...", no ribbon ordinal yet, mac selector
  `openInJsonEditor`, non-mutating).
- `src/documentcontroller.h/.cpp`: the command body. It needs the
  platform's URL-open primitive; if `platformservices.h` does not expose
  one yet, add the minimal seam method (e.g.
  `AppShell::OpenExternalUrl(const std::string& url)`), implemented as a
  one-liner on each port (`ShellExecuteW(nullptr, L"open", url, ...)` on
  Windows, `[[NSWorkspace sharedWorkspace] openURL:...]` on macOS). This
  is the one piece of native chrome the feature needs; everything else is
  shared.
- `src/ribbon.xml`: add the command under whichever tab holds
  `cmdOutline`/`cmdExportPdf`-style "open elsewhere" actions (check
  `references/ribbon-menu-guidelines.md` before placing it).
- `src/macos/menu_mac.mm`: add the selector to the native menu, per
  `references/macos-native-chrome.md`.
- `references/platform-parity.md`: add the command row to the "Commands:
  ribbon to mac menu" table (`check_command_coverage.py` fails without
  it); add a standing-difference row for the `OpenExternalUrl` seam
  implementation (native per platform, same as clipboard or settings).
- `src/commandstate.cpp`: `RequirementsOf` gets a new case: enabled only
  when the file kind is JSON (whatever flag Task 5 settled on).

**Step 1:** write the failing `commandstate_test.cpp` case: the command is
disabled on a markdown document, disabled on the welcome screen, enabled
on an open `.json` document.

**Step 2:** run, confirm FAIL.

**Step 3:** implement the command body and the gating.

**Step 4:** run, confirm PASS.

**Step 5:** commit.

### Task 7: Size warning before the hand-off

**Objective:** honor "small files" from the request: pasting a huge
clipboard payload into a browser text editor is unpleasant, so warn
rather than silently truncate.

**Files:** `src/documentcontroller.cpp` (the command body from Task 6).

**Behavior:** if the pretty-printed text exceeds a constant (suggest
`kClipboardWarnBytes = 2 * 1024 * 1024`, 2 MiB, chosen as "clearly past
comfortable paste size" rather than a jsoneditoronline.org limit, since
the site imposes none that is documented), show the platform's existing
confirm-style dialog ("This file is N MB; pasting that much JSON into a
browser tab may be slow to continue. Copy to clipboard anyway?") before
copying. Reuse whatever dialog primitive `cmdExportDocx`'s "overwrite?"
or similar confirmations already use; do not invent a new dialog type.

**Verification:** a unit test on the pure decision function
(`ShouldWarnBeforeJsonEditorCopy(size_t bytes) -> bool`), kept
platform-free so it tests the same on both runners.

### Task 8: File association and document type registration

**Objective:** `.json` opens in MarkDownIt from Explorer/Finder the way
`.md`/`.mmd`/`.svg` already do, without taking over the system's default
JSON handler if the user has one (JSON is a far more commonly
system-associated extension than `.mmd`, so default registration here
needs more care than the existing silent takeover of `.mmd`/`.svg`).

**Files:**
- `src/fileassoc.cpp`: add `L".json"` to `kExts`. Re-check
  `RegisterFileAssociation`'s existing "save the previous default, offer
  restore on unregister" logic (already present per the earlier grep);
  confirm it is invoked for `.json` too so an uninstall or a disable of
  the association hands JSON back to whatever opened it before
  (Explorer's JSON association is commonly Notepad or a code editor;
  clobbering it silently is a worse regression than doing nothing).
- `installer/macos/Info.plist`: add a `CFBundleDocumentTypes` entry with
  `LSHandlerRank` `Alternate` (not `Owner`, mirroring the existing `.md`
  and `.svg` entries, so MarkDownIt offers itself in "Open With" without
  becoming the forced default) and `LSItemContentTypes` of
  `public.json`.
- `references/platform-parity.md`: this is covered by the existing
  "Window chrome"/"Settings store" convention of native per-platform
  registration; a new row is only needed if the Windows and macOS
  mechanisms for *this specific extension* diverge in a way the general
  convention does not already cover (they don't: it's the same ProgID / Info.plist
  mechanism every other extension already uses). Skip a new ledger row
  unless `check_parity_ledger.py` disagrees; verify by running the
  checker locally (`tools/check_alignment.sh`).

**Verification:** Explorer/Finder show MarkDownIt in "Open with" for a
`.json` file after a local install; this is not verifiable from WSL (no
MSVC, no Xcode locally, per the Sandbox gotcha in AGENTS.md), so the
checker is the real gate and manual Explorer/Finder verification happens
on the CI-built artifact per the usual build-swap-verify loop.

**Done (2026-11-16):** `.json` added to `src/fileassoc.cpp`'s `kExts`
(the same registration/unregister/restore-previous-default path `.mmd`
and `.svg` already go through, no new code needed). Added a
`JSON Document` entry to `installer/macos/Info.plist`'s
`CFBundleDocumentTypes` with `LSHandlerRank` `Alternate` and
`LSItemContentTypes` `public.json`, mirroring the existing SVG entry.
`tools/check_alignment.sh` stays green with no new ledger row needed
(same native-registration convention every other extension uses).

### Task 9: Write `references/json-view.md`

**Objective:** document the feature the way `csv-view.md` documents CSV,
so a later contributor finds the design decisions instead of
re-deriving them: the pretty-print choice, the read-only decision from
Task 5, the caps from `json.h`, the "no query-param hand-off exists"
finding, and the size-warning constant.

**Files:** create `references/json-view.md`, modeled section-for-section
on `references/csv-view.md` (why the same engine is reused, the caps
table, read-only and what that greys, where the pieces live, not built
yet).

**Step:** add a row for it to the `references/` table in AGENTS.md's code
map (the "references/" table near the end of the file already lists
`csv-view.md`; add `json-view.md` alongside it in the same edit, keeping
AGENTS.md itself in sync, since the project treats that file as binding).

### Task 10: Tests registered on both targets

**Objective:** `tests/json_test.cpp` and any new `commandstate_test.cpp`
cases build on both the Windows and macOS test targets, per AGENTS.md's
"Tests" section.

**Files:** `CMakeLists.txt`, both test target source lists;
`cmake/sources.cmake` for the new `src/json.*` and `src/jsondocument.*` (if
Task 2's tree is kept as its own translation unit; folding the bridge
logic into `documentfile.cpp` directly is also acceptable and avoids a
near-empty bridge file, decide based on actual line count once Task 2 is
done).

**Verification:** `tools/check_sources_listed.py` and
`tools/check_test_inventory.py` both pass locally via
`tools/check_alignment.sh`.

## Files likely to change

| File | Change |
|---|---|
| `src/json.h`, `src/json.cpp` (new) | the JSON reader and pretty-printer |
| `tests/json_test.cpp` (new) | reader unit tests |
| `src/documentfile.h`, `src/documentfile.cpp` | `isJson`/`jsonWrapped`, the fence wrap/unwrap |
| `src/commandstate.h`, `src/commandstate.cpp` | the JSON read-only gating decision from Task 5 |
| `tests/commandstate_test.cpp` | new gating rows, the `cmdOpenInJsonEditor` enablement rows |
| `src/commands.h` | the new command row |
| `src/documentcontroller.h`, `src/documentcontroller.cpp` | the command body, the size-warning decision function |
| `src/platform/seams.h` (maybe) | `AppShell::OpenExternalUrl`, only if no equivalent exists yet |
| `src/windows/*`, `src/macos/*` (native shells) | the one-line URL-open call per port |
| `src/ribbon.xml` | the ribbon entry |
| `src/macos/menu_mac.mm` | the mac menu entry |
| `src/fileassoc.cpp` | `.json` added to `kExts` |
| `installer/macos/Info.plist` | the `.json` document type |
| `references/platform-parity.md` | the command row (required), a standing-difference row if the checker demands one |
| `references/json-view.md` (new) | the feature writeup |
| `AGENTS.md` | the `references/` table gets a `json-view.md` row |
| `cmake/sources.cmake`, `CMakeLists.txt` | the new source files and test registration |

## Tests / validation

- `tests/json_test.cpp`: parser correctness, caps, pretty-print
  round-trip, malformed-input error reporting.
- `tests/documentfile_test.cpp` (existing suite, extended): `.json` wrap
  and unwrap, minified input pretty-printed, malformed input passed
  through raw.
- `tests/commandstate_test.cpp` (existing suite, extended): JSON read-only
  gating, `cmdOpenInJsonEditor` enablement by file kind.
- A pure, platform-free unit test for the size-warning threshold function.
- `tools/check_alignment.sh` end to end before any push, per AGENTS.md.
- The real compile gate is CI on both platforms, per the build-swap-verify
  loop (`wsl-windows-native-dev` skill); nothing here builds or runs
  locally beyond the portable g++ checks.

## Risks, tradeoffs and open questions

- **Read-only vs. editable-as-code.** Task 5 picks "read-only like CSV"
  as the default; if the user actually wants to hand-edit JSON text (not
  just view it), the gating should instead mirror `.mmd`/`.svg`
  (editable source inside a fence). Flag this choice back to the user
  before Task 5 ships; it changes several other tasks' gating rows.
- **No vendored JSON library.** Writing a hand-rolled parser is
  consistent with the project's zero-dependency rule, but it is real
  parser work with its own edge cases (surrogate pairs in `\u` escapes,
  deeply nested arrays, numbers at the edge of `double` precision). Scope
  it to RFC 8259 only and lean on the test suite, the same discipline
  `csv.cpp` already applies to RFC 4180.
  Consider porting md4c's or pugixml's string-escaping helpers for the
  `\uXXXX` path rather than writing UTF-16 surrogate handling fresh. pugixml
  deals in XML not JSON; it saves nothing here and is skipped.
- **No officially supported jsoneditoronline.org URL hand-off.** Confirmed
  by inspecting their own feature page: load/save happens via clipboard,
  drag-and-drop, local file pick, or a URL the site can itself fetch over
  the network. A local file the app has open is not network-reachable, so
  the clipboard-and-open-browser path is the only honest implementation;
  do not build or promise a query-string bridge like the GitHub issue
  "josdejong/jsoneditor#334" author wished for; it does not exist, and
  it was not ever delivered after that issue was opened.
- **Windows file association for `.json` is riskier than for `.mmd`.**
  Almost every developer machine already has a `.json` handler (VS Code,
  Notepad++, or similar). Registering `Alternate` rank, not `Owner`, and
  preserving the previous default (Task 8) is the mitigation; test the
  unregister-restores-previous path specifically, since a bug there
  silently reassigns a system-wide extension on uninstall.
- **Pretty-print changes bytes on screen vs. bytes on disk.** Since JSON
  stays read-only (Task 5's default), this is cosmetic only: Source view
  (per the existing CSV precedent) should show the raw file bytes, not
  the pretty-printed form, so a reader checking an actual formatting bug
  in the file is not shown a reformatted lie. Confirm this is how CSV's
  Source view already behaves (`csv-view.md` says "Source view stays on:
  it shows the bytes the file holds") and mirror it exactly for JSON.

## Not built now (explicitly out of scope, YAGNI)

- A collapsible tree view, folding objects/arrays (closer to a real JSON
  editor than "a viewer"; would need new layout code, a new painter, and
  a parity ledger entry of its own; revisit only if the user asks for it
  after using the fence-based viewer).
- JSON Schema validation or linting.
- Editing JSON with structural awareness (insert key, reorder array,
  etc.), the JSON analogue of `tableedit.*`.
- Any attempt at a direct URL-based hand-off to jsoneditoronline.org;
  confirmed not to exist (see Risks).
- Saving a `.json` file from inside MarkDownIt; the file is a viewer, the
  same stance `csv-view.md` takes for CSV ("Saving... waits for a quoting
  model that can tell..."; JSON's analogous blocker is "waits for an
  editable tree model," not attempted here).

## Execution handoff

Ready to execute with `subagent-driven-development`: one subagent per
task above, spec-compliance review against this plan, then a code-quality
pass, before moving to the next task. Task 5's open decision (read-only
vs. editable) should be confirmed with the user before that task starts,
since it changes the shape of several later tasks.
