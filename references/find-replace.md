# Find & Replace — Functional & UX Specification

## 1. Purpose

The word processor MUST provide a Find & Replace function that allows the user to:

* Search for text within the current document.
* Navigate between matches.
* See the number and location of matches.
* Replace the current match.
* Replace all matches.
* Optionally restrict or refine matching using search options.
* Preserve the user's document state as far as possible.
* Undo replacements using the normal undo mechanism.

The feature SHOULD behave consistently with established word processors such as Microsoft Word, LibreOffice Writer and Google Docs.

---

# 2. Keyboard shortcuts

## 2.1 Find

The standard shortcut MUST be:

| Platform      | Shortcut   |
| ------------- | ---------- |
| Windows/Linux | `Ctrl + F` |
| macOS         | `Cmd + F`  |

`Ctrl/Cmd + F` opens or focuses the Find UI.

If Find is already open, pressing the shortcut SHOULD:

1. Focus the search field.
2. Select the existing search text.

This makes it possible to immediately type a new search term.

---

## 2.2 Find and Replace

The standard shortcut MUST be:

| Platform      | Shortcut   |
| ------------- | ---------- |
| Windows/Linux | `Ctrl + H` |
| macOS         | `Cmd + H`  |

This opens the Find & Replace interface.

If the application already uses `Ctrl/Cmd + H` for another function, the application MUST provide an alternative, but `Ctrl/Cmd + H` SHOULD remain the primary shortcut where possible.

---

## 2.3 Navigate matches

While the Find interface is active:

* `Enter` SHOULD move to the next match.
* `Shift + Enter` SHOULD move to the previous match.

The UI SHOULD also expose explicit:

* Previous match button
* Next match button

with accessible labels.

---

## 2.4 Escape

`Esc` SHOULD close the Find/Replace UI.

Closing the UI MUST NOT:

* Modify the document.
* Remove the current document selection unnecessarily.
* Clear the user's clipboard.
* Change the document's scroll position more than necessary.

---

# 3. Basic Find functionality

When the user enters a search term, the application SHOULD search the entire document.

Example:

```text
Search: architecture
```

If the document contains:

```text
The architecture document describes the system architecture.
```

there are two matches.

The UI SHOULD display something equivalent to:

```text
2 of 2
```

or:

```text
2 matches
```

The exact presentation may vary.

---

# 4. Search scope

The default search scope SHOULD be the entire document.

Find MUST work in **every document view**. This includes:

* The rendered view (Read mode).
* The raw source view.
* Every other view that displays document text.

The reason is that the user decides what Find means, not the current mode.
Someone reading a rendered document must be able to search it without first
switching to editing, and someone in the source view must get the same
matches for the same query.

Replace and Replace All are different. They mutate the document, so they MUST
only be available when editing is enabled:

* In a read-only view, Replace and Replace All MUST be disabled or absent.
* Attempting to replace in a read-only view MUST be a no-op.
* Navigation, highlighting and the match counter are unaffected by this rule.

The UI SHOULD make the state visible rather than silently ignoring a click.
Disabled controls are the usual way.

The search SHOULD include:

* Body text
* Text in paragraphs
* Text in headings
* Text in tables
* Text in text boxes, where supported
* Headers and footers, where supported
* Footnotes/endnotes, where supported

However, the implementation MUST define which document structures participate in searching.

A reasonable initial implementation can limit search to the document's primary text flow.

The UI SHOULD NOT imply that unsupported document regions are searched.

---

# 5. Search behavior

## 5.1 Incremental search

Search SHOULD normally be incremental.

For example, when the user types:

```text
arch
```

the application immediately searches for:

```text
arch
```

When the user continues:

```text
architecture
```

the results update immediately.

The application SHOULD NOT require the user to press Enter to execute the search.

---

# 6. Search result highlighting

All matching occurrences SHOULD be visually highlighted while the Find UI is active.

There should be a distinction between:

### Current match

The match currently selected/navigated to.

Example:

```text
The system architecture defines...
           ^^^^^^^^^^^^
```

The current match SHOULD have a stronger visual indication than other matches.

### Other matches

Other matches SHOULD receive a less prominent highlight.

This distinction is important because the user needs to know which match will be affected by **Replace**.

---

# 7. Current match selection

When a match becomes the current match:

1. The document MUST scroll it into view.
2. The matched text SHOULD become the active selection or otherwise clearly identified.
3. The match MUST become the target of `Replace`.
4. The caret/selection SHOULD correspond to the complete match.

For example, searching:

```text
architecture
```

should select:

```text
architecture
```

rather than only positioning the caret before it.

---

# 8. Search navigation

Search navigation MUST wrap around the document.

For example:

```text
Match 1
Match 2
Match 3
```

If the user is at Match 3 and presses Next:

```text
Match 1
```

becomes active.

Similarly, pressing Previous from Match 1 SHOULD navigate to Match 3.

The UI SHOULD communicate this through the match counter.

Example:

```text
3 of 3
```

followed by Next results in:

```text
1 of 3
```

---

# 9. Search starting position

When Find is opened, the initial search position SHOULD depend on the current selection/caret.

Recommended behavior:

### If text is selected

The selected text SHOULD be copied into the Find field.

For example, if the user selects:

```text
Azure AI
```

and presses `Ctrl + F`, the Find field should contain:

```text
Azure AI
```

The first search result SHOULD preferably be the selected occurrence or the next occurrence.

### If no text is selected

The search SHOULD begin from the current caret position.

If no caret exists, search SHOULD begin at the start of the document.

---

# 10. Case sensitivity

The Find & Replace interface SHOULD support:

**Match case**

Example:

Search:

```text
Azure
```

With Match case enabled:

```text
Azure
```

matches.

```text
azure
```

does not.

With Match case disabled:

```text
Azure
azure
AZURE
```

all match.

Default:

**Match case = Off**

---

# 11. Whole-word matching

The interface SHOULD provide:

**Match whole word**

Example:

Search:

```text
cat
```

With whole-word matching enabled:

```text
cat
```

matches.

But:

```text
catalog
concatenate
copycat
```

do not.

The definition of a "word" SHOULD be based on Unicode-aware word boundaries rather than simply ASCII spaces.

---

# 12. Search direction

The implementation SHOULD support:

* Search forward
* Search backward

The default direction SHOULD be forward.

Navigation buttons should make direction explicit:

```text
↑ Previous
↓ Next
```

or equivalent icons with accessible labels.

---

# 13. Replace interface

The Find & Replace UI SHOULD contain two fields:

```text
Find:
[ architecture              ]

Replace with:
[ solution architecture     ]
```

And actions:

```text
Previous    Next
Replace     Replace All
```

The exact UI can differ, but the functionality MUST be available.

---

# 14. Replace

**Replace** MUST replace only the current match.

Example:

Document:

```text
The architecture is good.
The architecture is flexible.
```

Find:

```text
architecture
```

Replace:

```text
design
```

After pressing Replace:

```text
The design is good.
The architecture is flexible.
```

The second occurrence SHOULD then become the current match.

This allows the user to review replacements individually.

---

# 15. Replace All

**Replace All** MUST replace every matching occurrence within the search scope.

Example:

```text
Find: architecture
Replace: design
```

Result:

```text
The design is good.
The design is flexible.
```

The operation SHOULD report the number of replacements.

Example:

> 14 replacements made.

---

# 16. Replace All confirmation

For potentially destructive operations, the UI SHOULD provide confirmation where appropriate.

For example:

```text
Replace all 147 occurrences of "architecture" with "design"?

[Cancel] [Replace All]
```

This is particularly useful when:

* There are many matches.
* The replacement is non-trivial.
* Regex replacement is enabled.

However, a simple Replace All operation with a clearly visible result count MAY be performed immediately if the application follows a standard word-processor interaction model.

---

# 17. Undo behavior

Replace All MUST be treated as **one logical undo operation**.

For example:

1. User performs Replace All.
2. 127 occurrences are changed.
3. User presses `Ctrl + Z`.

The entire Replace All operation MUST be undone.

It MUST NOT require 127 separate undo operations.

Likewise:

```text
Replace
Replace
Replace
```

may be represented as individual undoable operations.

---

# 18. Formatting preservation

Basic Find & Replace SHOULD preserve the formatting of the matched text unless explicitly configured otherwise.

Example:

Original:

```text
The architecture is important.
```

where `architecture` is bold.

Replacing:

```text
architecture
```

with:

```text
design
```

should normally result in:

```text
The design is important.
```

with `design` retaining the formatting associated with the replaced text.

The implementation MUST define how formatting is handled when the replacement string differs in length.

---

# 19. Paragraph and line boundaries

The search implementation SHOULD be capable of handling text across normal document boundaries where appropriate.

At minimum, the implementation should clearly define whether searches can cross:

* Paragraph boundaries
* Table cells
* Text boxes
* Sections

For a basic implementation, matching SHOULD normally be constrained to the same logical text node/paragraph unless multiline search is explicitly supported.

---

# 20. Empty search

If the Find field is empty:

* No matches SHOULD be shown.
* Next/Previous SHOULD be disabled.
* Replace SHOULD be disabled.
* Replace All SHOULD be disabled.

The application MUST NOT interpret an empty search as "match everything."

---

# 21. No matches

If no matches exist, the UI SHOULD clearly communicate this.

Example:

```text
No matches
```

The navigation and replacement actions SHOULD be disabled.

The document MUST NOT be modified.

---

# 22. Search text not found after replacement

An important edge case occurs when:

```text
Find: architecture
Replace: design
```

and the current occurrence is replaced.

The implementation SHOULD continue searching for the next occurrence of the original search string.

It MUST NOT accidentally search for the replacement string unless explicitly instructed.

---

# 23. Replace All and overlapping matches

The search engine MUST define how overlapping matches are handled.

Example:

```text
aaaa
```

Search:

```text
aa
```

Normal word-processor behavior is non-overlapping matching:

```text
[aa][aa]
```

rather than:

```text
[aa]
 [aa]
  [aa]
```

Replace All therefore produces two replacements.

---

# 24. Search normalization

The implementation SHOULD use Unicode-aware text comparison.

This is particularly important for languages such as Danish and other European languages.

The implementation SHOULD define behavior for:

* Unicode normalization
* Combining characters
* Accented characters
* Case folding
* Ligatures
* Unicode word boundaries

For example, visually equivalent Unicode representations SHOULD ideally behave consistently.

---

# 25. Danish characters

The search MUST correctly support:

```text
æ
ø
å
Æ
Ø
Å
```

when searching Danish text.

Case-insensitive search should treat:

```text
Å
å
```

as equivalent.

---

# 26. Search UI design

A recommended compact Find & Replace panel:

```text
┌──────────────────────────────────────────┐
│ Find and Replace                    ✕   │
├──────────────────────────────────────────┤
│ Find                                     │
│ [ architecture                    ]     │
│                                          │
│ Replace with                             │
│ [ design                         ]       │
│                                          │
│ [←] [→]                                  │
│                                          │
│ ☐ Match case                             │
│ ☐ Whole word                             │
│                                          │
│ [ Replace ]       [ Replace All ]       │
│                                          │
│ 4 of 17                                  │
└──────────────────────────────────────────┘
```

A Find-only mode can be simplified:

```text
┌──────────────────────────────────────┐
│ Find                              ✕ │
│ [ architecture              ]       │
│                              4 of 17│
│       ↑                 ↓            │
└──────────────────────────────────────┘
```

---

# 27. UI placement

The implementation can use:

* A floating search bar
* A sidebar
* A toolbar
* A modal/dialog

A floating search bar is particularly suitable for Find.

Find & Replace SHOULD preferably be expandable from the Find UI rather than requiring a completely separate interface.

For example:

```text
Ctrl + F

┌──────────────────────────────┐
│ Find: architecture      4/17│
│                         ↑ ↓ │
│ More options ▼              │
└──────────────────────────────┘
```

Clicking **More options** expands:

```text
Replace with:
[ design ]

☐ Match case
☐ Whole word

[Replace] [Replace All]
```

**MarkDownIt uses a docked bar.** One strip at the bottom of the
application window serves both functions. `Ctrl + F` opens it in Find mode
and `Ctrl + H` in Find & Replace mode. The strip keeps one layout in both
modes: the Replace field and the Replace buttons stay on screen and grey out
in Find mode, and Replace and Replace All stay disabled in read-only views
(section 4) whatever the mode is. The strip spans the full window width and
shrinks the document area while it is open.

---

# 28. Focus management

When Find opens:

1. Focus MUST move to the Find field.
2. Existing selected text SHOULD be populated.
3. The text SHOULD be selected so typing immediately replaces it.

When Find closes:

* Focus SHOULD return to the document/editor.
* The document selection SHOULD remain sensible.

If the user navigated to a match, closing Find SHOULD leave the caret/selection at that match.

---

# 29. Mouse interaction

The user MUST be able to:

* Click the Find field.
* Edit search text.
* Click Previous.
* Click Next.
* Click Replace.
* Click Replace All.
* Toggle search options.
* Close the Find interface.

Clicking a search result in the result list, if one exists, SHOULD navigate directly to that match.

---

# 30. Selection behavior

When the current match is selected and the user clicks elsewhere in the document:

The implementation SHOULD define whether the Find session:

### Option A — continues from the new caret position

This is generally intuitive.

### Option B — maintains the existing search position

This can be useful for controlled navigation.

For a word processor, **Option A is recommended**.

The next search operation should begin relative to the new caret position.

---

# 31. Editing while Find is open

The document MUST remain editable while Find is open.

If the user changes document content:

```text
Find: architecture
```

the search results SHOULD update automatically.

The implementation MUST invalidate stale match positions.

It MUST NOT retain character offsets that are no longer valid after an edit.

---

# 32. Concurrent editing

If the application supports collaborative editing, Find & Replace MUST account for remote document changes.

Match positions SHOULD be represented using document positions/ranges that can be remapped after edits rather than fixed character offsets.

Replace All SHOULD operate against a consistent document version.

---

# 33. Search performance

For small documents, searching can occur synchronously.

For large documents, search SHOULD be incremental/asynchronous.

The UI MUST remain responsive while searching.

For very large documents:

```text
Searching…
```

may be displayed temporarily.

Search results SHOULD be updated progressively if appropriate.

---

# 34. Large documents

The implementation SHOULD avoid creating a DOM/text representation of the entire document solely for searching if the document can become very large.

A suitable architecture is:

```text
Document Model
      │
      ▼
Text Extraction Layer
      │
      ▼
Search Engine
      │
      ├── Match ranges
      ├── Match count
      └── Current match
              │
              ▼
        Editor Selection
```

The search engine SHOULD return document-relative ranges rather than UI coordinates.

---

# 35. Search results model

An implementation could represent a match as:

```text
Match {
    start: DocumentPosition
    end: DocumentPosition
    text: string
}
```

The search system SHOULD NOT primarily operate on screen coordinates.

The editor is responsible for converting document positions into visual positions.

---

# 36. Replacement algorithm

A safe replacement operation should conceptually be:

```text
1. Execute search.
2. Identify current match.
3. Resolve current match against the latest document version.
4. Verify the matched text still corresponds to the search criteria.
5. Replace the exact range.
6. Update document model.
7. Recalculate/invalidate affected matches.
8. Select or navigate to the next match.
```

For Replace All:

```text
1. Execute search.
2. Collect all matching ranges.
3. Sort ranges from document start to end.
4. Validate ranges against the same document version.
5. Apply replacements from end → start.
6. Create one undo transaction.
7. Recalculate search results.
8. Report replacement count.
```

Applying replacements from the end toward the beginning avoids invalidating earlier offsets.

---

# 37. Replace All safety

The implementation MUST prevent accidental repeated replacement.

For example:

```text
Find: cat
Replace: dog
```

After replacement, the new text `dog` MUST NOT itself be searched and replaced again during the same Replace All operation.

The operation must operate on the original set of matches.

---

# 38. Replacement containing the search string

Example:

```text
Find: cat
Replace: catfish
```

The operation must terminate after replacing the original matches.

It MUST NOT repeatedly match the newly inserted `cat`.

---

# 39. Special characters

The Find field MUST support normal text containing:

* Quotes
* Apostrophes
* Parentheses
* Hyphens
* Unicode characters
* Emoji
* Numbers
* Punctuation

These characters MUST be treated as literal search characters unless a special search mode such as Regex is explicitly enabled.

---

# 40. Regular expressions

Regex SHOULD be treated as an **advanced optional feature**, not part of the basic Find & Replace implementation.

If implemented, the UI could expose:

```text
☐ Regular expression
```

When enabled, the search engine MUST clearly communicate that the search syntax has changed.

Example:

```text
Find:
\b[A-Z][a-z]+\b
```

The application SHOULD validate invalid regular expressions and display a clear error rather than silently returning zero results.

---

# 41. Regex replacement

If regex is supported, replacement MAY support capture groups.

Example:

```text
Find:
(\w+), (\w+)

Replace:
$2 $1
```

This is an advanced feature and should be implemented separately from the basic search engine.

---

# 42. Match formatting

An advanced implementation MAY support:

* Find formatting
* Replace formatting
* Font
* Font size
* Bold
* Italic
* Underline
* Text color
* Paragraph formatting

This should not be required for the basic implementation.

---

# 43. Search history

The application SHOULD remember recently used search strings during the current session.

A dropdown could show:

```text
Recent searches

architecture
Azure
AI
```

The implementation MAY persist search history between sessions.

If it does, privacy considerations SHOULD be taken into account.

---

# 44. Accessibility

The Find & Replace functionality MUST be keyboard accessible.

All controls MUST have accessible names.

For example:

```text
Find input
Replace input
Previous match
Next match
Match case
Match whole word
Replace
Replace all
Close
```

Screen readers SHOULD be informed when:

* A search produces no results.
* The current match changes.
* Replace All completes.
* An invalid search expression is entered.

Example announcement:

> "Match 4 of 17."

or:

> "No matches found."

---

# 45. Keyboard-only workflow

The complete workflow SHOULD be possible without a mouse:

```text
Ctrl + H
```

→ Find field

```text
type search
Tab
type replacement
Tab
Tab
Enter
```

The exact tab order MUST be logical.

Recommended tab order:

1. Find
2. Replace
3. Previous
4. Next
5. Search options
6. Replace
7. Replace All
8. Close

---

# 46. Internationalization

All user-visible strings MUST be localizable.

Do not hard-code:

```text
Replace All
No matches
Match case
```

inside application logic.

The search engine itself should remain language-independent.

---

# 47. Right-to-left languages

The UI SHOULD support RTL document languages.

The search mechanism itself should operate on logical text order rather than visual order.

Navigation should follow the logical document sequence.

---

# 48. Tables

The implementation MUST define behavior for tables.

A reasonable implementation:

```text
Table
 ├── Cell 1
 ├── Cell 2
 ├── Cell 3
 └── Cell 4
```

should search cell text in document order.

Replace All SHOULD be able to replace text inside cells if tables are included in the search scope.

---

# 49. Headers and footers

If headers and footers are searchable, they SHOULD be searched as separate document regions.

The current match should automatically navigate to the relevant page/header/footer.

Example:

```text
Match 12 of 15
```

may be inside a header on page 4.

The editor should scroll/navigate appropriately.

---

# 50. Footnotes and endnotes

If supported, footnotes/endnotes SHOULD participate in search.

The UI SHOULD make it clear that matches can occur there.

The current match navigation should move the user to the relevant footnote.

---

# 51. Search within selection

An advanced but highly useful feature is:

**Find in selection**

Example:

```text
Search:
architecture

Scope:
○ Entire document
○ Current selection
```

If enabled, Find and Replace MUST only operate within the selected range.

Replace All MUST NOT modify content outside that selection.

This is especially useful for large documents.

---

# 52. Search result count

The match count SHOULD update dynamically.

Examples:

```text
1 of 1
```

```text
7 of 42
```

```text
No matches
```

The count should represent the current search configuration, including:

* Search text
* Case sensitivity
* Whole-word mode
* Regex mode
* Search scope

---

# 53. Match count and performance

For extremely large documents, calculating the exact total may be expensive.

The UI MAY temporarily display:

```text
Searching…
```

and then:

```text
17 matches
```

The application MUST NOT display an incorrect count merely to make the UI appear responsive.

---

# 54. Closing behavior

Closing Find & Replace MUST NOT discard document edits.

Search state MAY be discarded.

The document's normal undo history MUST remain intact.

Reopening Find MAY restore:

* Last search
* Last replacement
* Last options

This is recommended.

---

# 55. Clipboard behavior

Find & Replace MUST NOT interfere with the system clipboard.

Copying the search text into the Find field must not unexpectedly overwrite clipboard contents.

Likewise, opening Find should not alter clipboard state.

---

# 56. Browser/editor integration

If the word processor is web-based, the implementation SHOULD avoid relying exclusively on the browser's native `Ctrl + F`.

The application should intercept:

```text
Ctrl + F
```

and provide its own document-aware search.

This is important because browser Find generally:

* Cannot replace text.
* Does not understand the editor's document model.
* May search UI text rather than document text.
* Cannot reliably navigate structured document content.

---

# 57. Native browser Find

If the application deliberately allows browser Find, it SHOULD provide a clear distinction between:

**Application Find**

and:

**Browser Find**

The application Find should take precedence when the editor has focus.

---

# 58. Undo/redo integration

Every replacement MUST integrate with the editor's existing transaction/undo system.

Recommended model:

```text
Find operation
    ↓
No undo entry

Replace
    ↓
One undo transaction

Replace All
    ↓
One undo transaction
```

Simply navigating between matches MUST NOT create undo entries.

Typing into the Find field MUST NOT create document undo entries.

---

# 59. Document dirty state

Find itself MUST NOT mark the document as modified.

Replace and Replace All MUST mark the document as modified.

Example:

```text
Open document
Ctrl + F
search
navigate
close
```

→ document remains clean.

But:

```text
Replace All
```

→ document becomes dirty.

---

# 60. Recommended minimum implementation

For a first implementation, I would define the MVP as:

### Required

* `Ctrl + F`
* `Ctrl + H`
* Find field
* Replace field
* Incremental search
* Current-match highlighting
* All-match highlighting
* Next
* Previous
* Wrap-around navigation
* Match counter
* Replace
* Replace All
* Case-sensitive search
* Whole-word search
* Empty-search handling
* No-match handling
* Undo
* Keyboard navigation
* Unicode support
* Danish characters
* Accessibility labels
* Document editing while search is open
* Find in every view (read-only and source view)
* Replace disabled outside edit mode

### Recommended shortly after

* Find in selection
* Search history
* Regex
* Formatting-aware search
* Headers/footers
* Footnotes/endnotes
* Search across document structures
* Advanced replacement expressions

---

# 61. Recommended state model

A clean implementation could maintain a state similar to:

```text
FindReplaceState

searchText
replaceText

caseSensitive
wholeWord
regexEnabled

scope
    document
    selection

matches[]
currentMatchIndex

isSearching
matchCount

searchStartPosition
```

The state should be independent of the UI.

The UI should consume the state rather than implementing search logic itself.

---

# 62. High-level architecture

A robust implementation can be structured as:

```text
                    ┌────────────────────┐
                    │ Find/Replace UI    │
                    └─────────┬──────────┘
                              │
                              ▼
                    ┌────────────────────┐
                    │ Find/Replace       │
                    │ Controller         │
                    └─────────┬──────────┘
                              │
               ┌──────────────┼──────────────┐
               ▼              ▼              ▼
        ┌────────────┐ ┌─────────────┐ ┌─────────────┐
        │ Search     │ │ Navigation  │ │ Replacement │
        │ Engine     │ │ Manager     │ │ Engine      │
        └─────┬──────┘ └──────┬──────┘ └──────┬──────┘
              │               │               │
              └───────────────┼───────────────┘
                              ▼
                    ┌────────────────────┐
                    │ Document Model     │
                    └────────────────────┘
                              │
                              ▼
                    ┌────────────────────┐
                    │ Undo/Redo Manager  │
                    └────────────────────┘
```

The key architectural principle is:

> **Search should operate on document positions, not rendered screen positions.**

That makes Find & Replace work correctly with scrolling, zooming, pagination, tables, reflow, and different rendering implementations.

---

# 63. Acceptance criteria

An implementation can be tested against the following minimum scenarios.

### Basic search

Given:

```text
Azure is a cloud platform.
Azure provides many services.
```

When the user presses `Ctrl + F` and enters `Azure`:

* Two matches are found.
* Both are highlighted.
* The first match is current.
* The counter shows `1 of 2`.

### Navigation

Pressing Next:

```text
1 of 2 → 2 of 2 → 1 of 2
```

Pressing Previous:

```text
1 of 2 → 2 of 2
```

### Replace

Given:

```text
Azure is powerful.
Azure is flexible.
```

Find:

```text
Azure
```

Replace:

```text
Microsoft Azure
```

Pressing Replace once produces:

```text
Microsoft Azure is powerful.
Azure is flexible.
```

### Replace All

Pressing Replace All produces:

```text
Microsoft Azure is powerful.
Microsoft Azure is flexible.
```

and one `Ctrl + Z` restores the original document.

### No results

Searching:

```text
xyz123
```

shows:

```text
No matches
```

and Replace/Replace All are disabled.

### Case sensitivity

Search:

```text
azure
```

with Match case OFF → matches `Azure`, `azure`, `AZURE`.

With Match case ON → only `azure`.

### Whole word

Search:

```text
AI
```

with Whole word ON should match:

```text
AI
```

but not:

```text
AIP
```

### Editing

While Find is open, editing the document must update search results and must not leave stale match positions.

### View modes

Find must work in the read-only rendered view and in the raw source view, with the same matches for the same query. Replace and Replace All must be disabled in a read-only view and must do nothing if invoked there.

### Shortcut behavior

```text
Ctrl + F → Find
Ctrl + H → Find & Replace
Esc → close
Enter → next
Shift + Enter → previous
```

---

## 64. One important design principle

The most important distinction for implementation is between **search state**, **document state**, and **visual state**:

```text
SEARCH STATE
"What text am I looking for?"
        │
        ▼
DOCUMENT STATE
"Where does that text currently exist?"
        │
        ▼
VISUAL STATE
"Where should the editor scroll and highlight it?"
```

These should not be conflated.

In particular, **zoom, scrolling, pagination, layout changes, and window resizing must not affect the logical Find result**. They only affect how the current match is rendered and brought into view.

This also makes Find & Replace compatible with the kind of zoom/scroll behavior you were defining earlier: the search engine should work entirely in document coordinates, while the editor's viewport/scroll system handles how the selected match is presented to the user.
