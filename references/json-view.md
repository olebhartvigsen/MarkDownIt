# JSON file viewing

A standalone `.json` file opens as a read-only, pretty-printed,
syntax-highlighted document, the same stance `.csv` takes
(`references/csv-view.md`): "visning" means a viewer, not an editor.

## Reading

`src/json.h` / `src/json.cpp` holds a small RFC 8259 parser (`json::Parse`),
a two-space `json::PrettyPrint`, a `json::Compact` re-serializer with no
limits beyond `kMaxDepth = 64` and `kMaxBytes = 8 MiB`, and `json::Tokenize`,
a second pass over already-printed text that splits it into coloured spans
(key, string, number, true/false/null literal, or punctuation). No
comments, no trailing commas, no unquoted keys: a strict parser, because
this is a viewer for well-formed data, not a lenient one for hand-edited
config.

`src/documentfile.cpp` detects a `.json` extension, pretty-prints the
parsed tree and wraps it in a synthetic fence, the same pattern `.mmd` and
`.svg` already use:

```` text
```json
{
  "a": 1
}
```
````

A malformed file falls back to its raw bytes inside the fence, so the
viewer never hides what is actually on disk. A file the user already wraps
in a `json` fence by hand keeps that fence untouched (`jsonWrapped`).

On save, an untouched buffer round-trips to its original compact form
(`json::Compact`), not the pretty-printed view: the file on disk is not
rewritten just because the viewer opened it.

## Syntax colour

A ```` ```json ```` fence (the synthetic one a `.json` file opens into, or
one a user wrote by hand) is tokenized by `json::Tokenize` and drawn as
several coloured runs instead of the one plain-ink run every other fence
gets (`src/layout/code_layout.cpp`). Four palette tokens carry the
colours (`Palette::jsonKey`, `jsonString`, `jsonNumber`, `jsonLiteral`
in `src/theme.h`), each a vivid, clearly separated hue: blue object
keys, green string values, orange numbers, red `true`/`false`/`null`
literals, so a key and its value (and a string from a number) read
apart at a glance; punctuation and whitespace keep `textPrimary`, the
same ink the block's border and every other fence already use. The
split only changes which colour rides each run: wrapping, line breaking
and the font stay exactly what `LayoutCodeBlock` already did, and every
byte of the fence's text is still covered by exactly one run, in order,
so a non-json fence (including a `.csv` table's own code paths) is
untouched.

## Read-only

A `.json` document follows the same gate `InTableDocument()` already
follows for CSV (`commandstate.h`'s `jsonDocument` context flag): the
writing commands, the markdown formatting and the table edits all stand
down. There is no second mode to switch to.

## Open in JSON Editor Online

"Open in JSON Editor Online" (`CommandId::OpenInJsonEditor`, ribbon id
10803, mac selector `openInJsonEditor:`) is the one command that needs a
`.json` document (`commandstate.cpp`'s `kJsonEditor` requirements). It
copies the pretty-printed buffer (`DocumentController::JsonEditorPayload`,
fence stripped) to the clipboard and opens
`https://jsoneditoronline.org/` in the default browser. Past
`json::kClipboardWarnBytes` (2 MiB), it asks before doing either: no
API exists to prefill the site from a URL, so the hand-off is
copy-then-navigate, and a large paste in a browser tab is worth a second
thought.

## What is shared, what is native

| Area | Shared | Native |
|------|--------|--------|
| Parsing, pretty-print, compact | `src/json.*` | none |
| Fence wrap/unwrap, round-trip on save | `src/documentfile.*` | none |
| Read-only gate | `commandstate.h`/`.cpp` (`jsonDocument`, `needsJson`) | none |
| Command table row | `src/commands.h` | ribbon entry (`ribbon.xml`, `ribbon.cpp`), mac menu item (`menu_mac.mm`, `main_mac.mm`) |
| Clipboard write, browser open | `json::ShouldWarnBeforeJsonEditorCopy` | `ClipboardCopy`/`ShellExecuteW` (Windows), `platform::Clipboard`/`platform::OpenExternalUrl` (macOS) |

No file-type association is added for `.json` (the same choice already
made for `.csv`): too many other applications own that extension system
wide. The file opens through File > Open, a command line argument, or
drag and drop.
