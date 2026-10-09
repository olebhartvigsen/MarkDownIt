# Outline View – Reference Specification

## 1. Purpose

The **Outline View** is an integrated navigation panel in a word processor that provides a structural overview of the current document.

The Outline View displays a hierarchical table of contents based on the document's active headings. It allows the user to:

* See the document's structure at a glance.
* Identify the section currently being viewed.
* Navigate directly to any heading.
* Understand the hierarchy and relationship between sections.
* Quickly move between major sections of a document.

The Outline View is part of the active document window and does not open as a separate window.

---

## 2. Position and Layout

The Outline View is displayed as a **vertical panel/frame on the left side of the document window**.

Conceptually:

```text
┌──────────────────────────────────────────────────────────────┐
│ Document title / toolbar                                    │
├────────────────┬─────────────────────────────────────────────┤
│ OUTLINE        │                                             │
│                │  Document content                           │
│  Introduction  │                                             │
│  1. Background │                                             │
│    1.1 Scope   │                                             │
│    1.2 Goals   │                                             │
│  2. Analysis   │                                             │
│    2.1 Method  │                                             │
│    2.2 Results │                                             │
│  3. Conclusion │                                             │
│                │                                             │
└────────────────┴─────────────────────────────────────────────┘
```

The panel:

* MUST be visually integrated into the document window.
* MUST occupy a dedicated area on the left side.
* MUST NOT appear as an independent floating window by default.
* MUST remain available while editing the document.
* MUST scroll independently from the document content.
* SHOULD have a resizable width.
* SHOULD remember its open/closed state for the document or application.

---

## 3. Source of the Outline

The Outline View is generated from the document's **active headings**.

A heading is considered active when it has a heading structure/style recognized by the word processor, such as:

* Heading 1
* Heading 2
* Heading 3
* Heading 4
* etc.

The outline MUST reflect the document's actual heading hierarchy rather than simply displaying visually formatted text.

For example:

```text
Heading 1
    Heading 2
        Heading 3
    Heading 2
Heading 1
```

MUST produce:

```text
Heading 1
    Heading 2
        Heading 3
    Heading 2
Heading 1
```

---

## 4. Hierarchical Structure

The Outline View MUST visually communicate heading levels.

Each heading level SHOULD be represented through indentation.

Example:

```text
1 Introduction
    1.1 Background
        1.1.1 Historical context
        1.1.2 Current situation
    1.2 Purpose

2 Analysis
    2.1 Methodology
    2.2 Findings
```

The indentation MUST make the parent/child relationship between headings clear.

The implementation SHOULD support an arbitrary number of heading levels supported by the document model.

---

## 5. Heading Text

Each outline item MUST display the text of its corresponding heading.

The displayed text SHOULD match the heading text in the document.

If the heading contains formatting, inline elements, fields, or other rich content, the Outline View SHOULD display a simplified representation appropriate for navigation.

The outline SHOULD NOT display the entire contents of the heading if the heading contains unusually large or complex content.

---

## 6. Current Location / Active Heading

The Outline View MUST indicate which heading corresponds to the user's current position in the document.

When the text cursor is located within a section, the corresponding heading MUST be visually highlighted in the Outline View.

Example:

```text
Introduction
1 Background
    1.1 History
    1.2 Current situation   ← highlighted
2 Analysis
3 Conclusion
```

The current heading represents the section in which the cursor currently resides.

### 6.1 Scrolling behavior

If the user moves through the document and enters another section, the active heading in the Outline View MUST update automatically.

For example:

```text
User moves cursor from:

1.2 Current situation

to:

2.1 Methodology
```

The Outline View MUST update accordingly:

```text
1 Background
    1.1 History
    1.2 Current situation
2 Analysis
    2.1 Methodology        ← highlighted
    2.2 Results
3 Conclusion
```

The Outline View SHOULD automatically scroll when necessary so that the active heading remains visible.

---

## 7. Navigation

Clicking a heading in the Outline View MUST navigate the document to that heading.

Example:

```text
2 Analysis
    2.1 Methodology
```

When the user clicks **2.1 Methodology**, the document view MUST navigate to the corresponding heading.

The document's main editing area MUST receive focus after navigation, allowing the user to immediately continue editing.

The cursor SHOULD be positioned at the heading or at an appropriate location associated with the heading.

---

## 8. Navigation Does Not Modify the Document

Navigating through the Outline View MUST NOT modify the document.

Clicking an outline item:

* MUST NOT change heading text.
* MUST NOT change formatting.
* MUST NOT create an undoable document edit.
* MUST NOT alter the document's structure.

It is purely a navigation operation.

---

## 9. Expand and Collapse

Hierarchical sections SHOULD be expandable and collapsible.

Example:

```text
▼ 2 Analysis
    ▼ 2.1 Methodology
        2.1.1 Data collection
        2.1.2 Processing
    2.2 Results
```

Clicking the disclosure control next to `2.1 Methodology` collapses its children:

```text
▼ 2 Analysis
    ▶ 2.1 Methodology
    2.2 Results
```

Collapsing an item in the Outline View MUST NOT collapse or hide the corresponding content in the document itself.

The operation controls the **outline representation only**.

---

## 10. Outline State vs. Document State

The expanded/collapsed state of the Outline View is a navigation/UI state.

It MUST be independent from:

* document content,
* heading formatting,
* document pagination,
* document visibility,
* document structure.

For example, collapsing:

```text
2 Analysis
```

in the Outline View MUST NOT hide the `Analysis` section from the document.

---

## 11. Synchronization with Document Changes

The Outline View MUST update when the document structure changes.

Examples include:

* Adding a heading.
* Removing a heading.
* Changing a paragraph into a heading.
* Changing a heading level.
* Changing heading text.
* Reordering sections.
* Moving a heading to another location.

Example:

If:

```text
1 Introduction
2 Analysis
3 Conclusion
```

is changed so that `Analysis` becomes Heading 1.1 under Introduction, the Outline View MUST reflect the new hierarchy.

The update SHOULD occur automatically without requiring the user to refresh the Outline View.

---

## 12. Heading Level Changes

If the user changes a heading's level, the corresponding outline item MUST move to the appropriate hierarchy.

For example:

Before:

```text
1 Introduction
2 Analysis
    2.1 Method
```

After changing `Analysis` to a child heading:

```text
1 Introduction
    1.1 Analysis
        1.1.1 Method
```

The Outline View MUST reflect the new hierarchy.

---

## 13. Selection and Current Location

The Outline View SHOULD distinguish between:

1. **Current location** — where the document cursor currently is.
2. **Mouse hover** — the item currently under the pointer.
3. **Selection/focus** — the item selected through keyboard or mouse interaction.

These states SHOULD use visually distinct but subtle UI treatments.

The current location MUST remain identifiable even when the Outline View does not have keyboard focus.

---

## 14. Keyboard Navigation

The Outline View SHOULD support keyboard navigation.

Recommended behavior:

* `↑` / `↓` — move between outline items.
* `Enter` — navigate to the selected heading.
* `←` — collapse the selected heading.
* `→` — expand the selected heading.
* `Home` — move to the first visible heading.
* `End` — move to the last visible heading.

The exact shortcuts MAY depend on the application's overall keyboard navigation model.

Keyboard navigation within the Outline View MUST NOT unintentionally modify document content.

---

## 15. Mouse Interaction

Recommended interaction:

### Single click

Single-clicking a heading navigates to the corresponding location in the document.

### Disclosure control

Clicking the expand/collapse control changes the visibility of child headings in the Outline View.

### Hover

Hovering over a heading MAY provide a visual hover state.

Hovering MUST NOT navigate the document unless explicitly defined by the application.

---

## 16. Long Documents

The Outline View is particularly important for long documents.

For documents containing many headings:

* The Outline View MUST be independently scrollable.
* The active heading SHOULD remain visible when practical.
* The hierarchy MUST remain visually understandable.
* The panel SHOULD avoid excessive horizontal scrolling.
* Long heading text SHOULD be truncated or wrapped according to the UI design.

If text is truncated, the full heading SHOULD be available through an appropriate tooltip or equivalent accessible mechanism.

---

## 17. Empty Outline

If the document contains no recognized headings, the Outline View SHOULD remain available but display an appropriate empty state.

Example:

```text
OUTLINE

No headings in this document.
```

The empty state SHOULD explain what the user needs to do to populate the outline, where appropriate.

For example:

> Add headings to your document to create an outline.

---

## 18. Dynamic Updates

The Outline View MUST respond to document changes without requiring a manual refresh.

The following operations MUST trigger an update:

| Document operation   | Outline update |
| -------------------- | -------------- |
| Add heading          | MUST           |
| Delete heading       | MUST           |
| Change heading text  | MUST           |
| Change heading level | MUST           |
| Move heading         | MUST           |
| Undo heading change  | MUST           |
| Redo heading change  | MUST           |
| Paste heading        | MUST           |
| Cut heading          | MUST           |

The update SHOULD be sufficiently fast that the Outline View appears continuously synchronized with the document.

---

## 19. Scrolling Synchronization

The Outline View and document viewport have related but independent scrolling behavior.

Scrolling the document MUST update the active heading.

Scrolling the Outline View MUST NOT change the document's scroll position.

Clicking an outline heading MUST navigate the document to the associated location.

The two views therefore have a **logical synchronization**, rather than being physically coupled scroll views.

---

## 20. Navigation Position

When navigating to a heading, the word processor SHOULD position the heading appropriately within the document viewport.

The heading SHOULD normally be positioned near the top portion of the visible document area rather than directly against the top edge.

If the heading is already visible, the application MAY avoid unnecessary scrolling.

The navigation operation SHOULD preserve a comfortable visual context around the heading.

---

## 21. Active Heading Determination

The active heading is determined from the cursor's current document position.

Conceptually:

```text
Document

Heading A
    Text
    Text
    Cursor ← current position
    Text

Heading B
    Text
```

The Outline View identifies `Heading A` as the active heading.

If the cursor is positioned before the first heading, the application SHOULD either:

* highlight no heading, or
* highlight the first applicable heading according to the document model.

The chosen behavior MUST be consistent.

---

## 22. Accessibility

The Outline View MUST be accessible to keyboard and assistive-technology users.

Each outline item SHOULD expose:

* Heading text.
* Heading level.
* Expanded/collapsed state where applicable.
* Current/active state.
* Navigational relationship to the document.

The hierarchy SHOULD be exposed semantically rather than relying exclusively on visual indentation.

The expand/collapse controls MUST be keyboard accessible.

Focus MUST be visibly indicated.

---

## 23. Performance

The Outline View SHOULD remain responsive for large documents.

The implementation SHOULD avoid rebuilding the entire outline unnecessarily when a small document change occurs.

For example, editing normal body text that does not affect headings SHOULD NOT require a complete reconstruction of the outline.

The outline model SHOULD preferably be derived from the document's structural model or an efficiently maintained heading index.

---

## 24. Visual Design

The Outline View SHOULD visually communicate that it is a navigation aid rather than part of the document itself.

Recommended characteristics:

* Subtle separation from the document canvas.
* Compact typography.
* Clear indentation.
* Clear active-heading highlight.
* Clearly identifiable expand/collapse controls.
* Minimal visual noise.
* Consistent alignment of heading text.

The active heading highlight SHOULD be noticeable but not visually dominant.

---

## 25. Example

Document:

```text
1 Introduction

This document describes...

2 Background

2.1 Existing system

The existing system...

2.2 Requirements

The system must...

3 Architecture

3.1 Components

...

3.2 Integration

...

4 Conclusion
```

Outline:

```text
OUTLINE

1 Introduction

2 Background
    2.1 Existing system
    2.2 Requirements

3 Architecture
    3.1 Components
    3.2 Integration

4 Conclusion
```

If the cursor is currently inside **3.2 Integration**, the Outline View becomes:

```text
OUTLINE

1 Introduction

2 Background
    2.1 Existing system
    2.2 Requirements

3 Architecture
    3.1 Components
    3.2 Integration       ← ACTIVE

4 Conclusion
```

Clicking **2.2 Requirements** navigates the document directly to that heading and makes `2.2 Requirements` the active heading.

---

## 26. Core Requirements

The implementation MUST satisfy the following:

1. The Outline View MUST be integrated into the left side of the active document window.
2. It MUST display a hierarchical representation of the document's recognized headings.
3. Heading hierarchy MUST be represented visually.
4. The currently active document section MUST be highlighted.
5. The active heading MUST update as the user moves through the document.
6. Clicking an outline heading MUST navigate to the corresponding heading in the document.
7. Navigation MUST NOT modify document content.
8. The outline MUST update automatically when headings are added, removed, moved, renamed, or have their levels changed.
9. The Outline View MUST have independent scrolling from the document.
10. The Outline View SHOULD support expanding and collapsing heading branches.
11. Collapsing an outline branch MUST NOT hide or alter the corresponding document content.
12. The Outline View MUST remain usable with keyboard navigation.
13. The hierarchy MUST be exposed appropriately to assistive technologies.
14. The implementation SHOULD remain responsive for large documents.

---

## 27. Conceptual Model

The Outline View can be considered a **navigation projection of the document structure**:

```text
                  DOCUMENT MODEL
                        │
                        │
             ┌──────────┴──────────┐
             │                     │
             ▼                     ▼
       Document View          Outline View
             │                     │
       Full document          Heading tree
             │                     │
             │              Navigation
             │                     │
             └──────────┬──────────┘
                        │
                  Current location
                     synchronized
```

The Outline View does not constitute a second copy of the document. It is a structural view of the same underlying document model and should remain synchronized with it at all times.
