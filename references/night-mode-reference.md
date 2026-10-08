Below is a reference in the same style as the zoom-behavior guide: implementation-oriented, with **MUST** for required behavior and **SHOULD** for recommended behavior.

# Night Mode behavior guide for word processors

This guide defines expected behavior for **Night Mode / Dark Mode** in a word processor. The purpose is to reduce visual strain in dark environments while preserving the meaning and appearance of the document itself.

**MUST** marks a required behavior. **SHOULD** marks a recommended behavior.

---

## 1. Core principle

Night Mode is a **UI display mode**, not a document formatting operation.

### 1.1 Document content must remain unchanged

**MUST**:

* Not modify the document's underlying text.
* Not modify font sizes.
* Not modify font families.
* Not modify paragraph formatting.
* Not modify document styles.
* Not modify margins or page dimensions.
* Not modify document colors stored as formatting.
* Not create undo/redo entries when Night Mode is toggled.
* Not mark the document as modified merely because Night Mode was changed.

Switching:

> Light Mode → Night Mode → Light Mode

must leave the document exactly as it was.

### 1.2 Night Mode is a presentation layer

Night Mode should be treated as a visual layer between the document model and the display.

Conceptually:

```text
Document
   ↓
Document formatting
   ↓
Night Mode display transformation
   ↓
Screen rendering
```

The Night Mode transformation must not be written back into the document.

---

# 2. Application UI

When Night Mode is enabled, the **application interface** should use a dark color scheme.

This includes, where applicable:

* Application background
* Toolbar
* Ribbon
* Menus
* Context menus
* Sidebars
* Navigation panes
* Dialogs
* Status bar
* Tooltips
* Buttons
* Input fields
* Scrollbars
* Selection controls
* Find/replace interface
* Comments interface
* Track Changes interface

### 2.1 Consistency

**MUST** ensure that UI elements do not remain unintentionally bright.

For example, this is undesirable:

```text
Dark toolbar
Dark sidebar
Dark menus
WHITE document area
```

unless the white document page is intentionally preserved according to the document-page rules below.

---

# 3. Document page / canvas

The document page requires special treatment because it represents the document itself rather than the application UI.

There are two common Night Mode models.

### Model A — Dark application, light document

The page remains white or otherwise retains its document appearance:

```text
┌──────────────────────────────┐
│ Dark application UI          │
│                              │
│       ┌──────────────┐       │
│       │ White page   │       │
│       │              │       │
│       │ Document     │       │
│       │ text         │       │
│       └──────────────┘       │
│                              │
└──────────────────────────────┘
```

### Model B — Fully dark document view

The page itself is rendered using a dark representation:

```text
┌──────────────────────────────┐
│ Dark application UI          │
│                              │
│       ┌──────────────┐       │
│       │ Dark page    │       │
│       │              │       │
│       │ Light text   │       │
│       └──────────────┘       │
│                              │
└──────────────────────────────┘
```

**MUST** define explicitly which model the application uses.

A high-quality word processor **SHOULD support both**, for example:

* **Dark UI**
* **Dark UI + dark page**

This is particularly useful because some users want a dark interface but still want the document to look like a normal printed page.

---

# 4. Dark document rendering

If the application supports a dark document/page view, the document must be **visually transformed without modifying its actual formatting**.

For example:

```text
Normal document:

Page:        #FFFFFF
Text:        #000000

Night Mode:

Displayed page:  dark
Displayed text:  light
```

The actual document values remain:

```text
Page: #FFFFFF
Text: #000000
```

### 4.1 Black and white inversion

The implementation **SHOULD NOT simply invert every RGB value**.

Naive inversion:

```text
black → white
white → black
red   → cyan
blue  → yellow
```

can produce highly undesirable results.

Instead, Night Mode should use a controlled rendering transformation.

---

# 5. Text colors

### 5.1 Default text

Black or very dark text should be rendered as a light neutral color in dark document mode.

For example:

```text
Document:
#000000

Night Mode display:
#E6E6E6
```

The exact display color may depend on the application's theme.

### 5.2 Colored text

Explicitly colored text should normally remain distinguishable as the same semantic color.

For example:

```text
Red text
Blue text
Green text
```

should not simply be inverted.

However, colors that have poor contrast against a dark background **SHOULD be adjusted for display**.

### 5.3 Document color preservation

**MUST NOT** change the stored text color.

If the user explicitly selected:

> Red text

Night Mode must not change the document's actual formatting to:

> Light red text.

Only the rendering changes.

---

# 6. Page background

If dark document mode is enabled, the displayed page background should use a dark neutral color.

It should generally **not be pure black**.

For example:

```text
Preferred:
#181818
#1E1E1E
#202020
```

rather than:

```text
#000000
```

The exact value is theme-dependent.

### 6.1 Contrast

The page background and text must provide sufficient contrast.

**MUST** ensure that:

* Normal text remains clearly readable.
* Headings remain distinguishable.
* Links remain identifiable.
* Disabled text remains distinguishable from the background.
* Comments and annotations remain readable.

---

# 7. Images

Images are document content and should generally **not be inverted**.

For example:

```text
Document image:
[normal photograph]

Night Mode:
[normal photograph]
```

The image should remain visually faithful to the original document.

### 7.1 Image exceptions

The application **MAY** offer an optional setting such as:

> Reduce image brightness in Night Mode

but this must be a display-only operation.

It must never modify the image stored in the document.

### 7.2 Transparent images

Images with transparency must continue to composite correctly against the Night Mode page background.

---

# 8. Shapes, drawings and diagrams

Shapes and drawings should be rendered according to their document formatting while ensuring that they remain visible.

For example:

```text
White shape on white page
```

may need a display-only adjustment when the page becomes dark.

**MUST NOT** permanently alter the shape's fill or outline color.

---

# 9. Tables

Tables must remain structurally and visually understandable.

Night Mode must account for:

* Table borders
* Cell backgrounds
* Header rows
* Alternating row colors
* Text
* Selected cells
* Merged cells

A light table border that becomes invisible against a dark page **SHOULD** receive a display-only rendering adjustment.

---

# 10. Links

Hyperlinks must remain recognizable in Night Mode.

For example:

```text
Normal:
blue + underline

Night Mode:
readable blue/light-blue + underline
```

The hyperlink's actual document color must not be changed.

---

# 11. Highlights / markers

Text highlighting requires particular care.

If the word processor has a **marker/highlight layer**, Night Mode must render that layer above the Night Mode document transformation.

Conceptually:

```text
Document
   ↓
Night Mode transformation
   ↓
Text marker / annotation layer
   ↓
Selection / cursor
```

This ensures that highlighted text remains recognizable.

### 11.1 Highlight colors

A yellow marker should remain recognizably yellow rather than becoming an unrelated inverted color.

The marker's stored color must remain unchanged.

---

# 12. Selection

Text selection must remain highly visible in Night Mode.

When the user selects text:

```text
Normal:
blue selection background
```

Night Mode should use a selection color that provides strong contrast against both:

* the document background
* the selected text

**MUST** ensure that selected text remains readable.

---

# 13. Cursor / caret

The text cursor must remain clearly visible.

If the cursor is normally black, it should use a light display color against a dark page.

The cursor's display color should be determined by the current theme.

### 13.1 Cursor blinking

Night Mode must not change cursor blinking behavior.

The caret should continue blinking according to the normal editor timing.

---

# 14. Comments and annotations

Comments, suggestions and annotations are part of the editing UI and must remain readable.

This includes:

* Comment markers
* Comment bubbles
* Comment panels
* Track Changes
* Suggested edits
* Annotation indicators

**MUST** ensure sufficient contrast in Night Mode.

Annotation colors should remain semantically distinguishable.

---

# 15. Track Changes

Track Changes must continue to communicate the same information.

For example:

* Insertions
* Deletions
* Formatting changes
* Reviewer identities

must remain distinguishable.

Night Mode must not cause different change types to become visually indistinguishable.

---

# 16. Find and Replace

Find/Replace must remain fully usable.

This includes:

* Search field
* Search result highlighting
* Current match
* Replace controls
* Match counters

Search highlighting must remain visible against a dark document.

For example:

```text
Document
    ↓
Night Mode
    ↓
Search highlight
```

The search highlight must be rendered on top of the dark-mode document representation.

---

# 17. Spell checking

Spell-check indicators must remain visible.

For example, a red underline must remain identifiable against a dark background.

The underlying spelling state must not change.

---

# 18. Focus and keyboard navigation

Night Mode must not alter keyboard behavior.

All existing shortcuts must continue to work.

Examples:

* `Ctrl+F`
* `Ctrl+H`
* `Ctrl+C`
* `Ctrl+V`
* `Ctrl+Z`
* `Ctrl+Y`
* `Ctrl+A`

The visual focus indicator must remain visible.

---

# 19. Scrolling

Night Mode must apply consistently while scrolling.

The following must remain visually consistent:

* Document background
* Page background
* Text
* Headers
* Footers
* Selection
* Annotations

No flash of the light theme should occur during scrolling.

### 19.1 Scrollbars

Scrollbars should use the Night Mode UI theme.

They should remain visible but visually subordinate to the document.

---

# 20. Zoom

Night Mode must work independently of zoom.

Changing zoom:

```text
80% → 100% → 150% → 200%
```

must not change Night Mode state.

Likewise:

```text
Light Mode → Night Mode
```

must not change the current zoom level.

Night Mode and zoom are independent display transformations.

---

# 21. Full-screen mode

If the application supports full-screen or distraction-free mode, Night Mode must continue to apply.

The transition should not cause a temporary return to Light Mode.

---

# 22. System theme integration

The application **SHOULD** support the operating system's theme.

Possible modes:

```text
Appearance:
    Light
    Dark
    System
```

### 22.1 System mode

When set to **System**, the application follows the operating system's light/dark preference.

Changing the OS theme should update the application without modifying the document.

### 22.2 Explicit user choice

If the user explicitly selects:

> Light

or:

> Dark

the application should not override that choice merely because the OS theme changes.

---

# 23. Automatic switching

If automatic switching is supported, it may follow:

* Operating-system preference
* Sunrise/sunset
* Scheduled time

Automatic switching must only affect presentation.

It must never:

* modify the document
* trigger Save
* create an undo operation
* change document formatting

---

# 24. Printing

Night Mode must **not normally affect printing**.

If the document is printed while Night Mode is active:

```text
Night Mode screen
       ↓
      Print
       ↓
Normal document appearance
```

The printed document should use the document's actual formatting.

For example, black text stored in the document should print as black text—not as the light text used by Night Mode.

### 24.1 Print preview

Print Preview **SHOULD** represent the actual printed output rather than the Night Mode screen representation.

---

# 25. PDF export

PDF export must similarly use the document's actual formatting.

Night Mode must not cause:

```text
Dark page
Light text
```

to accidentally be exported into the PDF.

Unless the user explicitly requests a **"Export current appearance"** feature, the PDF should represent the document itself.

---

# 26. Copy and paste

Night Mode must not affect copied document content.

Copying text from the editor must copy the underlying content and formatting, not the screen representation.

For example:

```text
Screen:
light text on dark background

Copy
 ↓

Clipboard:
original document text/formatting
```

The dark background must not be copied merely because Night Mode is active.

---

# 27. Saving

Changing Night Mode:

```text
Light → Dark
```

must not make the document appear modified.

**MUST NOT:**

* Enable the Save button solely because Night Mode changed.
* Add an undo entry.
* Change the document's modification timestamp solely because of the theme change.
* Write theme colors into document formatting.

---

# 28. Undo / Redo

Night Mode changes must not participate in the document's undo/redo history.

Example:

```text
Type text
↓
Toggle Night Mode
↓
Ctrl+Z
```

`Ctrl+Z` should undo the text operation—not turn Night Mode off.

---

# 29. Accessibility

Night Mode must meet the same accessibility requirements as Light Mode.

**MUST** ensure adequate contrast for:

* Body text
* Headings
* Links
* Buttons
* Form controls
* Selection
* Focus indicators
* Error indicators
* Spell checking
* Comments
* Annotations

Night Mode should not be treated as inherently more accessible simply because it is darker.

---

# 30. Reduced brightness

The application **SHOULD** avoid extremely bright UI elements.

For example:

```text
Bad:
#000000 background
#FFFFFF everywhere
```

A more comfortable dark theme generally uses:

```text
Dark neutral background
Off-white text
Moderately bright controls
Limited saturated colors
```

This reduces large areas of high luminance.

---

# 31. Color semantics

Colors that carry meaning must retain their semantic relationship.

For example:

| Meaning     | Light Mode      | Night Mode              |
| ----------- | --------------- | ----------------------- |
| Error       | Red             | Readable red            |
| Warning     | Orange          | Readable orange         |
| Success     | Green           | Readable green          |
| Information | Blue            | Readable blue           |
| Selection   | Selection color | High-contrast selection |
| Highlight   | Yellow          | Recognizably yellow     |

The exact RGB value may change for readability, but the **semantic color must remain recognizable**.

---

# 32. Theme transition

If the application supports animated theme transitions, the transition should be short and subtle.

**SHOULD** avoid animations that:

* interfere with typing
* cause noticeable flickering
* temporarily reduce text readability
* create flashing regions

A simple immediate transition is acceptable.

---

# 33. Performance

Night Mode must not introduce noticeable editor latency.

It should not cause:

* delayed typing
* delayed cursor rendering
* scrolling stutter
* selection lag
* unnecessary document re-rendering

The implementation **SHOULD** apply theme changes primarily through the rendering/UI layer rather than rewriting document content.

---

# 34. Multi-page documents

Night Mode behavior must remain consistent across all pages.

For example:

```text
Page 1 → dark
Page 2 → dark
Page 3 → dark
```

There must not be accidental white flashes when new pages enter the viewport.

Headers, footers and page separators must also remain consistent.

---

# 35. Page boundaries

If dark document mode is used, page boundaries should remain visible.

For example:

```text
       dark page
┌─────────────────────┐
│                     │
│       content       │
│                     │
└─────────────────────┘
       dark page
```

The boundary may be represented using:

* subtle shadow
* contrast
* thin border
* slightly different background

It should not rely solely on a bright white border.

---

# 36. User preference persistence

The application **SHOULD** remember the user's Night Mode preference.

For example:

```text
User selects:
Dark Mode

Close application
↓
Open application
↓
Dark Mode remains enabled
```

The preference should normally be an **application/user setting**, not a document property.

---

# 37. Document portability

Opening the same document on another computer must not unexpectedly transfer the user's Night Mode preference.

For example:

```text
Computer A
Dark Mode enabled
        ↓
Save document
        ↓
Computer B
Light Mode enabled
```

The document should open according to Computer B's display preference.

---

# 38. Recommended rendering order

For implementation, the visual rendering pipeline should conceptually be:

```text
                 DOCUMENT MODEL
                       │
                       ▼
              Document formatting
                       │
                       ▼
              Night Mode rendering
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
       Selection    Search       Comments
          │            │            │
          └────────────┼────────────┘
                       ▼
                  Cursor/Caret
                       │
                       ▼
                  Final display
```

This is important because Night Mode should be a **display transformation**, while transient UI layers such as selection, search matches and the cursor must remain visible above it.

---

# 39. Mode definitions

A word processor implementing Night Mode should ideally expose three modes:

### Light

```text
UI:       Light
Document: Normal/light
```

### Dark UI

```text
UI:       Dark
Document: Normal/light
```

### Dark

```text
UI:       Dark
Document: Dark display representation
```

Optionally:

### System

```text
OS theme
    ↓
Light or Dark
```

---

# 40. Acceptance criteria

The implementation can be tested against the following requirements.

### MUST

* [ ] Night Mode does not modify document content.
* [ ] Night Mode does not modify document formatting.
* [ ] Night Mode does not create an undo/redo operation.
* [ ] Night Mode does not mark the document as modified.
* [ ] UI changes consistently to the dark theme.
* [ ] Text remains readable.
* [ ] Selection remains readable.
* [ ] Cursor remains visible.
* [ ] Links remain distinguishable.
* [ ] Spell-check indicators remain visible.
* [ ] Comments remain readable.
* [ ] Track Changes remains understandable.
* [ ] Images are not automatically inverted.
* [ ] Find/Replace remains usable.
* [ ] Zoom continues to work independently.
* [ ] Scrolling does not produce light-theme flashes.
* [ ] Printing is unaffected by Night Mode.
* [ ] PDF export is unaffected by Night Mode.
* [ ] Copy/paste is unaffected by Night Mode.
* [ ] Night Mode does not alter the underlying document colors.

### SHOULD

* [ ] Support Light / Dark UI / Dark Document modes.
* [ ] Support following the OS theme.
* [ ] Persist the user's theme preference.
* [ ] Preserve semantic colors.
* [ ] Provide sufficient contrast for accessibility.
* [ ] Keep images visually unchanged.
* [ ] Keep page boundaries visible.
* [ ] Avoid pure black/pure white surfaces where unnecessary.
* [ ] Apply Night Mode through the rendering layer.
* [ ] Avoid visible flicker during theme changes.
* [ ] Maintain good performance while switching themes.

---

## 41. Fundamental rule

The most important implementation rule is:

> **Night Mode changes how the document is displayed, not what the document is.**

Therefore:

```text
                SAME DOCUMENT
                     │
          ┌──────────┴──────────┐
          ▼                     ▼
      Light Mode             Night Mode
          │                     │
          ▼                     ▼
    Light rendering       Dark rendering
```

Both modes must ultimately represent **the same underlying document**.
