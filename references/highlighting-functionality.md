# Text Marker / Highlighting Functionality

Status (2026-10): this document now describes the shipped marker
implementation, not only the original wish list. Sections say "not
implemented" where the build does not cover the behaviour yet. The
implementation lives in src/markers.cpp (model and sidecar),
src/renderer.cpp (drawing) and src/app.cpp (commands, shortcut and
ribbon state).

## 1. Purpose

The **Marker** functionality allows users to visually mark important passages of text while reading a Markdown document.

A marker is a **non-destructive visual annotation** applied on top of the rendered document. It allows users to return later and immediately identify passages they previously considered important.

The marker functionality MUST NOT modify the Markdown source document.

The underlying Markdown file therefore remains a normal, valid Markdown file and can be opened by any other Markdown editor or viewer without any dependency on the marker functionality.

### Core principle

> **Markers belong to the viewer, not to the Markdown document.**

The Markdown document contains the content.

The marker layer contains the user's annotations.

---

# 2. Requirements

## 2.1 Non-destructive marking

When a user marks text:

* The Markdown source MUST NOT be modified.
* Markdown syntax MUST NOT be changed.
* No HTML, Markdown comments, tags, or other marker information MUST be inserted into the document.
* The document's modification timestamp SHOULD NOT be changed solely because a marker was added or removed.
* Saving the document MUST NOT incorporate markers into the Markdown file.
* Opening the Markdown file in another application MUST display the original document without markers.

For example, given:

```markdown
# Azure Architecture

The system MUST use managed identities for authentication.
```

Marking:

> "MUST use managed identities for authentication."

MUST NOT result in:

```markdown
# Azure Architecture

The system <mark>MUST use managed identities for authentication.</mark>
```

or:

```markdown
# Azure Architecture

The system **[MARKED]** MUST use managed identities for authentication.
```

The Markdown file must remain:

```markdown
# Azure Architecture

The system MUST use managed identities for authentication.
```

---

# 3. Marker layer

Markers MUST be implemented as a separate annotation layer.

Conceptually, the application has two independent representations:

```text
┌─────────────────────────────────────┐
│ Markdown document                   │
│                                     │
│ Original source / content           │
└─────────────────────────────────────┘
                  │
                  ▼
          Markdown renderer
                  │
                  ▼
┌─────────────────────────────────────┐
│ Rendered document                   │
└─────────────────────────────────────┘
                  │
                  ▲
                  │
┌─────────────────────────────────────┐
│ Marker / annotation layer           │
│                                     │
│ User-created highlights             │
└─────────────────────────────────────┘
```

The marker layer is rendered visually over the document but is logically separate from the Markdown content.

---

# 4. Marker visibility

The current build has no visibility switch. Markers always draw while
their document is open. An earlier build had a Show markers toggle on
the ribbon; it was removed because a hidden mark is a lost mark: the
reader who returns to the document wants to see the annotations.

The ribbon still shows the layer state, but through the Mark toggle:
the button is pressed whenever the current selection overlaps a
marker. Display state and data state stay separate without a second
switch.

> **Hiding ≠ deleting** remains true as a rule: the only operation
> that removes marker data is the Mark toggle on a marked selection
> (section 6).

---

# 5. Creating a marker

## 5.1 Text selection

The primary way to create a marker is:

1. User selects text.
2. User activates the Marker command.
3. The selected text becomes visually marked.

The marker SHOULD be created from the current text selection.

The shipped activation paths:

* The Mark toggle button in the Markers group on the Home tab
* The keyboard shortcut Ctrl+Shift+H

Context menu and command palette activation are not implemented. The
Mark command is one toggle; section 6 covers the remove half.

Example:

```text
┌──────────────────────────────────────────────┐
│ The system MUST use managed identities for  │
│ authentication when accessing Azure.        │
│                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^  │
│                 selected text                │
└──────────────────────────────────────────────┘
```

After activating Marker:

```text
The system MUST use managed identities for
██████████████████████████████████████████
authentication when accessing Azure.
```

---

# 6. Removing a marker

A user MUST be able to remove an existing marker.

The Mark command removes as well as adds. The same ribbon button and
the same Ctrl+Shift+H act on the current selection:

* A selection that overlaps no marker receives one.
* A selection that overlaps one or more markers loses every
  overlapping marker.

A partial overlap counts: selecting half of a marked word removes the
marker that covers the word. The button shows this before the click,
because it is pressed whenever the selection overlaps any marker, so
the state the next click will change is visible first.

Removing a marker only modifies the annotation layer.

It MUST NOT modify the Markdown document.

---

# 7. Multiple markers

A document MAY contain any number of independent markers.

Example:

```text
This is an important statement.

This is another important statement.

This is normal text.

This is a third important statement.
```

The marker layer could contain:

```text
Marker 1 → first statement
Marker 2 → second statement
Marker 3 → third statement
```

Markers SHOULD be independent.

Removing one marker MUST NOT affect other markers.

---

# 8. Marker appearance

The default marker appearance SHOULD resemble a traditional text highlighter.

For example:

```text
Normal text with highlighted text inside the paragraph.
                    █████████████
```

The marker SHOULD:

* preserve the original text colour;
* preserve the original font;
* preserve bold/italic formatting;
* preserve links;
* preserve Markdown-rendered structure;
* appear behind the text rather than replacing it;
* remain visually unobtrusive.

The marker MUST NOT alter the document's typography or layout.

The shipped style is a warm yellow fill (0xFFD54A) behind the text
plus a darker line (0xB8860B) along the bottom edge of each marked
range. Marker fills sit behind search fills, so an active search
paints over the marks but never removes them.

---

# 9. Markdown-specific requirements

Because this application is a Markdown viewer, the marker operates on the **rendered document**, not directly on Markdown syntax.

For example:

```markdown
This is **important** and this is [a link](https://example.com).
```

The rendered result might be:

> This is **important** and this is a link.

A marker can cover:

```text
important and this is a link
████████████████████████████
```

without changing the Markdown source.

The implementation MUST therefore distinguish between:

1. Markdown source
2. Parsed Markdown structure
3. Rendered text
4. Marker annotations

---

# 10. Markdown syntax MUST never be marked

The marker system SHOULD operate on **visible text**, rather than raw Markdown characters.

For example:

```markdown
This is **very important** information.
```

The user sees:

> This is **very important** information.

If the user selects "very important", the marker should refer to the rendered text.

The marker system MUST NOT create an annotation such as:

```markdown
This is **<mark>very important</mark>** information.
```

because that changes the Markdown document and introduces HTML.

The shipped implementation clips the user selection to rendered text
before storing anything: LayoutCache::ClipToRendered trims hidden
Markdown syntax from both ends of the range, so a stored marker never
starts or ends on bytes the renderer does not draw.

---

# 11. Cross-element selections

Users SHOULD be able to mark text spanning multiple rendered elements where technically feasible.

For example:

```markdown
This is an important paragraph.

This is another important paragraph.
```

The user may select:

```text
important paragraph.

This is another important
```

The annotation can then consist of multiple ranges:

```text
Marker A
 ├── range in paragraph 1
 └── range in paragraph 2
```

The implementation SHOULD treat this as one logical marker even if the browser/rendering engine represents it as multiple DOM ranges.

The shipped implementation stores one byte range whose ends sit on
rendered text; drawing fills whatever part of the range renders.

---

# 12. Persistence and annotation storage

Markers MUST be persisted separately from the Markdown document.

The marker data MUST be stored as a **sidecar annotation file in the operating system's default temporary directory**.

The Markdown file itself MUST never contain marker information.

## 12.1 Temporary sidecar location

The application MUST use the operating system's standard/default temporary directory rather than creating the annotation file next to the Markdown file.

The application MUST NOT require write access to the directory containing the Markdown file solely for storing markers.

Conceptually:

```text
Markdown document:

C:\Documents\MyDocument.md

Annotation data:

%TEMP%\[application]\[document-identifier].markers.json
```

On other operating systems, the equivalent OS-provided temporary directory MUST be used.

The exact directory MUST be determined through the operating system/runtime's standard temporary-directory mechanism and MUST NOT be hard-coded.

On Windows the shipped directory is %TEMP%\MarkDownIt, created on
demand when the first marker is saved.

---

## 12.2 Annotation filename

The annotation filename MUST be based on a stable document identifier rather than simply the Markdown filename.

For example:

```text
<document-id>.markers.json
```

This avoids collisions between documents with identical filenames.

The application SHOULD NOT use the full document path directly as the filename.

For example, this SHOULD NOT be used:

```text
C:\Users\Ole\Documents\project.md.markers.json
```

Instead:

```text
%TEMP%\MyMarkdownViewer\a83f91c2.markers.json
```

The current implementation derives the name from the full document
path: the first 64 bits of an FNV-1a hash of the path, written as
sixteen hex digits, then `.markers.json`. The file also records the
plain document path and a content fingerprint, which the adoption in
section 12.8 relies on.

---

## 12.3 Example annotation file

A sidecar annotation file contains:

```json
{
  "version": 1,
  "path": "C:\\docs\\report.md",
  "fingerprint": "7d4636a59f14d0ac",
  "markers": [
    {
      "id": "m-0001",
      "start": 993,
      "end": 1041,
      "exact": "If AU already has a working LibreChat deployment",
      "prefix": "hange the choice to Open WebUI. ",
      "suffix": ", there is no evidence here that",
      "created": "2026-10-07T13:18:30Z"
    }
  ]
}
```

Field notes:

* `path` is the document path at save time.
* `fingerprint` is an FNV-1a hash of the document bytes at save
  time; it identifies the document for adoption (section 12.8).
* `start` and `end` are byte offsets into the UTF-8 document at save
  time, a cache of the last resolution.
* `exact`, `prefix` and `suffix` form the anchor (section 13). Each
  context field holds up to 32 bytes, cut at whole UTF-8 code points.
* `id` values are m-0001 style and stay unique per document.

A corrupt file is treated as "no markers": the document opens with an
empty layer and the broken file is left alone on disk.

The annotation file is completely independent of the Markdown document.

---

## 12.4 Markdown file integrity

Adding, modifying, hiding or deleting a marker MUST NOT modify the Markdown file.

For example:

```text
Before marking:

document.md
    ↓
Markdown content

After marking:

document.md
    ↓
EXACTLY THE SAME CONTENT

%TEMP%\MyMarkdownViewer\a83f91c2.markers.json
    ↓
Marker information
```

The Markdown file's:

* content
* encoding
* line endings
* formatting
* metadata
* modification timestamp

SHOULD remain unaffected by marker operations.

---

## 12.5 Temporary storage semantics

Because the annotation file is stored in the OS temporary directory, the application MUST treat marker persistence as **best-effort temporary persistence**.

The application MUST NOT assume that temporary files survive:

* OS cleanup operations;
* application data cleanup;
* user-initiated temporary-file deletion;
* system maintenance;
* application reinstallation;
* profile cleanup.

If the annotation file no longer exists, the Markdown document MUST still open normally.

The absence of the annotation file MUST be treated as:

```text
No markers available
```

and NOT as a document error.

---

## 12.6 No marker file creation in the document directory

The application MUST NOT create:

```text
document.md.markers.json
document.markers.json
.markers/
.annotations/
```

or similar files/directories next to the Markdown document unless a future explicit export function is introduced.

This ensures that the user's Markdown directory remains clean and that marker information does not accidentally become part of:

* Git repositories;
* document synchronization;
* document sharing;
* backups;
* uploads.

---

## 12.7 Marker lifecycle

When a document is opened:

1. Calculate or retrieve its stable document identifier.
2. Determine the OS default temporary directory.
3. Locate the corresponding annotation sidecar.
4. Load marker data if it exists.
5. Render the Markdown document.
6. Resolve the stored anchors against the rendered document.
7. Display the resolved markers.

When a marker is added or removed:

1. Modify the in-memory annotation model.
2. Update the temporary sidecar annotation file.
3. Do NOT save the Markdown document.

Markers also survive a same-document reload: when the buffer changes
(a reload, an edit, an undo), anchors are resolved against the fresh
text before the next paint, exactly as in the open path.

---

## 12.8 Document identity

Because the annotation file is not located beside the Markdown file, the application MUST have a reliable way to associate an annotation sidecar with its document.

A document identifier SHOULD be stable across normal document changes.

A simple content hash alone is NOT sufficient because changing the Markdown content would change the hash.

The application SHOULD therefore maintain an application-level document identity together with content information.

The shipped identity is the document path plus an FNV-1a fingerprint
of the document bytes stored inside the sidecar. When a document is
moved or renamed, the sidecar is filed under the hash of the old path
and no sidecar exists under the new one. The loader then scans the
annotation directory, and a sidecar is adopted when every one of these
hold: it stores the document's exact fingerprint, its recorded path no
longer exists on disk, and it contains at least one marker. The
newest such sidecar wins. Adoption never happens while the old path
remains valid, which is what keeps two copies (section 37) apart.

Conceptually:

```text
document identity
        │
        ├── current file path
        ├── content fingerprint
        └── annotation sidecar
```

The implementation SHOULD use a combination of stable identity and content-based information to make marker restoration robust when documents are modified or moved.

---

## 12.9 Cleanup

The application SHOULD periodically clean up obsolete annotation files from its temporary annotation directory.

Cleanup MUST be conservative.

An annotation file SHOULD only be considered obsolete after an appropriate retention period and when there is reasonable evidence that it is no longer associated with a document.

The application MUST NOT delete annotation data merely because the corresponding Markdown document is temporarily unavailable.

Cleanup itself is not implemented. Sidecars accumulate in the
temporary directory until the OS or the user clears it, which is
exactly the duration the temporary-storage model (section 12.5)
promises.

---

## 12.10 Optional explicit annotation export

Although normal marker persistence uses the OS temporary directory, the application MAY later provide an explicit:

**Export annotations**

function.

This could create:

```text
MyDocument.markers.json
```

in a location chosen by the user.

Such an export is separate from normal marker persistence.

The normal operation of the marker functionality MUST continue to use the OS temporary directory.

---

# 13. Text anchoring

The most important technical problem is determining **what text a marker refers to** after the document has changed.

A marker SHOULD NOT rely exclusively on:

```text
characterOffset = 1234
```

because inserting 20 characters before the marker would invalidate the position.

Instead, a marker SHOULD preferably contain a text anchor such as:

```json
{
  "selectedText": "managed identities for authentication",
  "prefix": "The system MUST use ",
  "suffix": " when accessing Azure."
}
```

A more robust representation could be:

```json
{
  "type": "text",
  "exact": "managed identities for authentication",
  "prefix": "The system MUST use ",
  "suffix": " when accessing Azure.",
  "blockPath": [2, 1],
  "startOffset": 15,
  "endOffset": 53
}
```

The exact implementation is application-specific.

The shipped anchor is the triple in section 12.3: exact text, up to 32
bytes of prefix and up to 32 bytes of suffix, all cut at whole UTF-8
code points. On resolution the store walks every occurrence of the
exact text and scores each candidate: +4 when the prefix matches, +4
when the suffix matches. The best score wins; ties go to the candidate
nearest the marker's previous start. This is how a marked phrase keeps
its mark through a paragraph shift, and how two identical copies of a
phrase do not drag a mark across the document.

---

# 14. Marker restoration

When a document is opened:

1. Load the Markdown document.
2. Parse/render the Markdown.
3. Load the associated marker data.
4. Locate each marker's text anchor in the rendered document.
5. Reconstruct the visual marker ranges.
6. Render the markers.

If a marker cannot be located with sufficient confidence, it SHOULD NOT be applied blindly.

Instead, the application SHOULD mark it as unresolved.

Example internal state:

```text
Marker 1   ✓ restored
Marker 2   ✓ restored
Marker 3   ⚠ text changed
Marker 4   ✓ restored
```

The UI MAY notify the user that some markers could not be restored.

The current build does not notify; unresolved markers simply do not
draw, and the marker data they rest on keeps them alive (the anchor
is intact, the byte positions are stale).

---

# 15. Document changes

The marker system MUST handle changes to the underlying Markdown document gracefully.

### Example

Original:

```markdown
The system MUST use managed identities for authentication.
```

Marker:

```text
managed identities
██████████████████
```

The document is later changed to:

```markdown
The system MUST use managed identities for authentication
and authorization.
```

The marker SHOULD still cover:

```text
managed identities
██████████████████
```

---

# 16. Deleted text

If the marked text is removed from the Markdown document, the corresponding marker can no longer be displayed.

The marker SHOULD then become unresolved rather than silently being reassigned to unrelated text.

The marker MAY be retained so that it can potentially be restored if the text reappears.

Possible states:

```text
ACTIVE
HIDDEN
UNRESOLVED
DELETED
```

The build implements two of them: resolved markers draw, unresolved
ones do not. There is no HIDDEN state because there is no visibility
switch (section 4), and every unresolved marker still has its anchor
stored, which is what a restoration after the text returns would need.

---

# 17. Read-only Markdown viewer

The marker functionality MUST work even when the Markdown viewer is read-only.

This is an important distinction:

```text
Document editing
        ≠
Document annotation
```

A user does not need permission to edit the Markdown document merely to create a marker.

For example:

```text
Markdown permissions:
    Read ✓
    Edit ✗

Annotation permissions:
    Create ✓
    Delete ✓
```

The architecture SHOULD therefore treat annotations as a separate permission and data model.

---

# 18. Marker state and document state

The application SHOULD distinguish between:

### Document state

```text
Markdown content
```

and:

### Annotation state

```text
Markers
```

Changing marker state MUST NOT cause the document to become "dirty".

For example:

```text
Document:       Saved
Markers:        3 active
Document dirty: No
```

After adding a marker:

```text
Document:       Saved
Markers:        4 active
Document dirty: No
```

This is important because otherwise users may incorrectly believe that their Markdown document has been modified.

---

# 19. Undo and redo

Marker operations SHOULD have their own undo/redo history.

For example:

```text
Ctrl+Z
```

after creating a marker SHOULD remove the marker rather than undoing a Markdown editing operation.

If the application has no document editing functionality, this distinction is straightforward.

If editing is later introduced, the application SHOULD maintain separate histories:

```text
Document undo stack
        +
Annotation undo stack
```

or a unified history with explicitly separated operation types.

Not implemented. Marker actions have no undo. Removing a mark through
the toggle is a deliberate, visible act on a selected range, so the
design accepted the trade; if marker undo ever becomes a need, it
belongs on a separate annotation stack, never the document one.

---

# 20. Keyboard shortcuts

The application SHOULD provide a keyboard shortcut for marking selected text.

A possible default is:

```text
Ctrl+Shift+H
```

where H represents Highlight.

However, the exact shortcut SHOULD be configurable and SHOULD avoid conflicts with existing browser/application shortcuts.

Shipped commands:

| Command        | Shortcut     |
| -------------- | ------------ |
| Mark / unmark  | Ctrl+Shift+H |

One shortcut serves both directions of the toggle. The earlier
Ctrl+Shift+Alt+H remove variant was dropped when the commands merged,
because two shortcuts that reverse each other invite mistakes.

Next marker and previous marker navigation is not implemented.

The comparatively small surface (one button, one shortcut, available
in view mode and edit mode) follows from annotating not being editing:
no command in the group needs the edit gate (section 17).

---

# 21. Context menu

When text is selected:

```text
┌─────────────────────────────┐
│ Copy                        │
│ Search                      │
│ ─────────────────────────── │
│ Mark                        │
│ ─────────────────────────── │
│ Add comment                 │
└─────────────────────────────┘
```

When the selection intersects an existing marker:

```text
┌─────────────────────────────┐
│ Copy                        │
│ Mark / unmark               │
└─────────────────────────────┘
```

Not implemented: the context menu currently has no marker entries. The
Mark toggle's pressed state carries the same information at the
ribbon.

---

# 22. Marker navigation

For documents containing many markers, the application SHOULD provide marker navigation.

Example:

```text
Marker 3 of 12

[ Previous ] [ Next ]
```

Activating **Next marker** SHOULD:

1. Locate the next marker in document order.
2. Scroll it into view.
3. Optionally provide a brief visual indication of the selected marker.

The application MAY provide a marker overview/sidebar:

```text
MARKERS
──────────────────────

01  Azure architecture
02  Authentication
03  Data classification
04  Security requirements
05  Logging
```

Clicking an item navigates directly to the marker.

---

# 23. Marker count

The UI MAY display the number of markers:

```text
🖍 7
```

or:

```text
Markers: 7
```

The count SHOULD represent markers belonging to the current document.

Not implemented: no counter is shown anywhere. The ribbon group holds
the single Mark toggle, and no statistics view exists in the app.

---

# 24. Marker visibility versus marker existence

The application MUST distinguish between:

```text
Markers exist
```

and:

```text
Markers are visible
```

For example:

```text
Markers: 12
Visibility: Off
```

Turning visibility off MUST NOT delete the 12 markers.

Without a visibility switch (section 4) this distinction collapses to
existence alone: every stored marker is always drawn, and the only
way to lose one is the remove action itself.

---

# 25. Accessibility

The marker MUST NOT rely exclusively on colour.

This is important for users with colour-vision deficiencies.

The implementation SHOULD ensure sufficient contrast between:

* normal text;
* marked text;
* background;
* links;
* selected text.

If multiple marker colours are supported, the application SHOULD provide an additional visual distinction where appropriate.

A simple single marker style is preferable unless colour categories are explicitly required.

The shipped style follows this: one warm fill plus a bottom line, no
colour per category, no reliance on hue differences between markers.

---

# 26. Selection versus marker

The visual selection made by the browser and a persistent marker are different concepts.

### Selection

Temporary:

```text
User currently selected this text
```

### Marker

Persistent:

```text
User wants to remember this text
```

After the user clicks elsewhere, the selection disappears but the marker remains.

---

# 27. Hover interaction

Hovering over marked text MAY display lightweight information such as:

```text
Marked
Created: 6 Oct 2026
```

However, a tooltip SHOULD NOT obstruct reading.

The marker itself SHOULD remain visually subtle.

Not implemented. Marked text has no hover affordance: no tooltip, no
created date. The mark is the only signal.

---

# 28. Marker metadata

The initial implementation SHOULD keep marker metadata minimal.

At minimum:

```text
marker ID
document ID
text anchor
created timestamp
```

Optional metadata could include:

```text
author/user
colour
category
note
created timestamp
modified timestamp
```

The system SHOULD NOT add metadata unless it provides an actual user benefit.

---

# 29. Multiple users

If the Markdown viewer is used collaboratively, markers SHOULD be associated with a user.

Example:

```json
{
  "markerId": "m-123",
  "documentId": "doc-456",
  "userId": "user-789",
  "anchor": {
    "exact": "managed identities"
  }
}
```

The UI could then support:

```text
My markers
All markers
```

However, collaborative annotations are a separate capability and SHOULD NOT be required for the basic marker implementation.

---

# 30. Copy and paste

Copying marked text SHOULD copy only the underlying text.

For example, if:

```text
This is █████important█████ text.
```

is copied, the clipboard should contain:

```text
important
```

not marker metadata.

Unless a specific "Copy with annotations" feature is introduced, marker information MUST NOT be placed on the normal text clipboard.

---

# 31. Printing and PDF export

Marker visibility SHOULD be explicitly defined for printing/export.

Recommended behaviour:

* Screen display: markers visible when enabled.
* Normal Markdown export: markers excluded.
* Print: markers excluded by default.
* PDF export: markers excluded by default.

The shipped exporters (PDF, DOCX) never include markers. They run on
the document model, which never sees the annotation layer, so nothing
can leak into an export by accident.

If exporting marked documents is desired, it SHOULD be an explicit option:

```text
Export PDF

☐ Include markers
```

---

# 32. Search integration

Search SHOULD operate on the underlying document text, not marker metadata.

The application MAY provide:

```text
Search
[important]
```

and separately:

```text
Find marked text
```

A "Find marked text" function could navigate between markers.

Not implemented. Search and markers are separate: the find bar
queries the document text, and the marker layer reads the sidecar.
The two meet only on screen, where search fills draw over marker
fills (section 8).

---

# 33. Interaction with Markdown rendering

The marker layer SHOULD be applied after Markdown has been parsed and rendered.

Conceptually:

```text
Markdown
   │
   ▼
Markdown parser
   │
   ▼
AST
   │
   ▼
HTML / rendered document
   │
   ▼
Marker anchoring
   │
   ▼
Visual marker layer
```

The implementation SHOULD avoid modifying the original Markdown AST unless the AST is explicitly designed to support external annotations.

---

# 34. DOM implementation

For a browser-based Markdown viewer, the marker layer MAY be implemented using DOM ranges.

Conceptually:

```javascript
const range = document.createRange();

range.setStart(startNode, startOffset);
range.setEnd(endNode, endOffset);
```

Possible implementation approaches include:

* CSS Custom Highlight API;
* DOM overlay;
* generated annotation elements;
* SVG/canvas overlay;
* other rendering-layer techniques.

The preferred implementation SHOULD minimise changes to the rendered document DOM.

This application is a native Direct2D/DirectWrite renderer without a
DOM. The marker layer is a pair of brushes applied behind the block
text through four call sites (source blocks, code, tables and the
plain render path), reusing the same layout-to-byte mapping that the
search fills use.

Where browser support permits it, a browser-native highlighting mechanism can be advantageous because the annotation can remain separate from the document's semantic content.

---

# 35. Separation from Markdown semantics

The marker MUST NOT become part of the semantic document.

For example, accessibility tooling SHOULD ideally encounter:

```text
The system MUST use managed identities for authentication.
```

rather than:

```text
The system [marked start] MUST use managed identities [marked end].
```

The marker is a presentation/annotation layer, not document content.

---

# 36. Security and privacy

Marker data may itself contain information about what a user considers important.

Therefore, if marker data is stored remotely, it SHOULD be subject to appropriate access controls.

For the temporary local implementation, the application SHOULD follow the operating system's normal temporary-file security model.

The system SHOULD consider:

* user ownership;
* document access;
* marker access;
* deletion;
* retention;
* backup;
* audit requirements.

---

# 37. File lifecycle

When a Markdown document is:

### Renamed

The marker data SHOULD continue to be associated with the document using a stable document identifier rather than only its filename.

### Moved

Markers SHOULD continue to work.

### Copied

The application's behaviour MUST be defined.

Recommended behaviour:

```text
document.md
    ↓ copy
document-copy.md
```

The copy SHOULD either:

1. receive a new document identity and no markers; or
2. explicitly clone the markers.

The application SHOULD NOT accidentally share markers between unrelated document copies.

Because the sidecar identity is the document path (section 12.8), a
copy made in the file system (a new path, identical bytes) has no
sidecar until you mark something in it. If you then update the copy,
its sidecar is a fresh one, not the original's. The reverse also
holds: adopting a sidecar requires the stored path to be gone from
disk, so an existing copy never adopts the original side just by
sharing bytes.

---

# 38. Version control

The Markdown file SHOULD remain completely compatible with Git and other version-control systems.

A user can therefore commit:

```text
document.md
```

without committing marker information.

Because marker information is stored in the OS temporary directory, it SHOULD NOT normally appear in the Markdown repository at all.

The marker system MUST NOT pollute Markdown diffs.

---

# 39. Offline behaviour

If the application supports offline use, markers SHOULD remain available offline.

Marker creation SHOULD NOT require network connectivity because the annotation sidecar is stored locally in the OS temporary directory.

---

# 40. Failure behaviour

If marker data cannot be loaded:

* The Markdown document MUST still open normally.
* The document MUST NOT be modified.
* The application SHOULD notify the user that markers could not be loaded where appropriate.
* Failure to load annotations MUST NOT prevent reading the document.

The Markdown document always takes precedence over annotation data.

---

# 41. Performance

Marker rendering SHOULD have minimal impact on document performance.

The implementation SHOULD be able to handle at least:

* hundreds of markers in a document;
* long documents;
* large Markdown files;
* multiple visible markers in the same viewport.

Marker processing SHOULD be incremental where necessary.

The application SHOULD avoid rebuilding the entire rendered document when a single marker is added or removed.

---

# 42. Acceptance criteria

The implementation is considered correct when all of the following are true.

## Basic functionality

* [x] User can select text.
* [x] User can create a marker from selected text.
* [x] Marked text is visually distinguishable.
* [x] User can remove a marker (the Mark toggle removes on overlap).
* [x] Multiple markers can exist.
* [x] Markers can be restored when the document is reopened and the temporary annotation data still exists.
* [x] Marked text renders in view mode and in edit mode.

## Non-destructive behaviour

* [ ] Creating a marker does not modify Markdown source.
* [ ] Removing a marker does not modify Markdown source.
* [ ] Saving the document does not write marker information to the Markdown file.
* [ ] Markdown syntax remains unchanged.
* [ ] Git diffs do not contain marker changes.
* [ ] Another Markdown viewer displays the original document normally.

## Annotation storage

* [x] Marker data is stored separately from the Markdown file.
* [x] Marker data is stored in the OS default temporary directory.
* [x] The application does not create marker files next to the Markdown document.
* [x] The annotation filename is based on a stable document identifier (path hash).
* [x] Loss of the temporary annotation file does not prevent opening the Markdown document.
* [x] Marker data is treated as temporary/best-effort persistence.
* [x] A moved or renamed document adopts its sidecar from the previous path (section 12.8).

## Toggle state

* [x] The Mark button is pressed whenever the selection overlaps a marker.
* [x] A partial overlap also reads as pressed.
* [x] The same click or shortcut that adds a marker removes it again.
* [x] The toggle state refreshes on selection changes in both view and edit mode.

The earlier Visibility block (hide/show markers) was retired with the
switch itself. Nothing can hide a marker now, so "hiding does not
delete" holds vacuously.

## Document changes

* [x] Markers can survive reasonable text changes (anchor scoring, section 13).
* [x] Markers do not silently attach themselves to unrelated text (context scoring).
* [x] Deleted marker text is handled safely (unresolved, kept in data).
* [ ] Unresolvable markers are identifiable in the UI: not implemented, they only vanish from view.

## Rendering

* [ ] Markers do not change document layout.
* [ ] Markdown formatting remains intact.
* [ ] Links remain functional.
* [ ] Bold/italic/code formatting remains intact.
* [ ] Marker rendering does not introduce Markdown syntax.
* [ ] Marker rendering does not become part of the document semantics.

## Clipboard/export

* [x] Copying marked text copies normal text.
* [x] Markdown export excludes markers.
* [x] Normal document save excludes markers.
* [x] Print/export behaviour is explicitly defined (never includes markers).

---

# 43. Recommended architecture

The recommended architecture is:

```text
                    ┌────────────────────┐
                    │   document.md      │
                    │                    │
                    │   Markdown only    │
                    └─────────┬──────────┘
                              │
                              ▼
                    ┌────────────────────┐
                    │ Markdown Parser    │
                    └─────────┬──────────┘
                              │
                              ▼
                    ┌────────────────────┐
                    │ Rendered Document  │
                    └─────────┬──────────┘
                              │
                    ┌─────────▼──────────┐
                    │ Marker Engine      │
                    │                    │
                    │ resolve anchors    │
                    │ create ranges      │
                    │ render markers     │
                    └─────────┬──────────┘
                              │
                              ▼
                    ┌────────────────────┐
                    │ Visible document   │
                    │ + marker layer     │
                    └────────────────────┘


                    ┌────────────────────┐
                    │ OS TEMP directory  │
                    │                    │
                    │ *.markers.json     │
                    │                    │
                    │ Annotation data    │
                    └────────────────────┘
```

The critical architectural rule is:

```text
                    ┌──────────────┐
                    │ Markdown     │
                    │ content      │
                    └──────┬───────┘
                           │
                           │ READ
                           ▼
                    ┌──────────────┐
                    │ Viewer       │
                    └──────┬───────┘
                           ▲
                           │
                    ┌──────┴───────┐
                    │ Marker layer │
                    └──────────────┘
```

**Never:**

```text
Marker → modifies → Markdown
```

**Always:**

```text
Markdown ───────────────► Viewer
                            ▲
                            │
Markers ───────────────────┘
```

The Markdown file is the **source of truth for document content**.

The temporary sidecar is the **source of truth for the user's visual annotations**.

---

# 44. Future extensibility

The marker system SHOULD be designed so that markers can later support additional annotation types without changing the Markdown document.

For example:

```text
Marker
Comment
Bookmark
Note
Question
Task
```

could all use the same underlying annotation mechanism:

```json
{
  "type": "highlight",
  "anchor": {},
  "metadata": {}
}
```

This allows the application to evolve from a simple highlighter into a more general **document annotation layer**.

The important architectural decision is therefore not merely to implement a highlighter, but to establish a clear separation between:

**Document content**

and

**User annotations**.

That separation ensures that Markdown remains portable, standards-compatible and safe to use with other Markdown applications.
