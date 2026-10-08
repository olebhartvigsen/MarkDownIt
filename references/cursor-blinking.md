In a word processor such as Microsoft Word, the **text cursor (caret)** is the visual indicator showing where the next typed character will be inserted.

### Cursor appearance

* The cursor is normally a **thin vertical line** (`│`).
* It is positioned **between two characters**, or immediately before/after a character.
* Its height generally matches the current **font's line height**, rather than the height of an individual character.
* The cursor uses a high-contrast color, normally the application's text/accent color.
* When the document has focus, the cursor is clearly visible.
* When the document loses focus, the cursor may become hidden or visually de-emphasized.

For example:

```text
Hello wor│ld
```

Typing `X` at this position produces:

```text
Hello worXld
```

### Blinking behavior

The cursor normally **blinks periodically** to make its position noticeable without permanently drawing attention to it.

Conceptually:

```text
Hello wor│ld    ← cursor visible

Hello world    ← cursor hidden

Hello wor│ld    ← cursor visible

Hello world    ← cursor hidden
```

The blink is normally implemented as a **visual animation**, not by changing the document contents.

A typical implementation should:

1. Display the cursor when the editor has focus.
2. Toggle its visibility at a regular interval.
3. Reset the blink cycle whenever the user interacts with the document.
4. Keep the cursor visible briefly after keyboard or mouse input.
5. Stop blinking or hide it when the editor loses focus, depending on the platform.
6. Never modify the underlying document text when the cursor blinks.

### Cursor position

The cursor represents an **insertion position**, not a character.

For example:

```text
The quick│ brown fox
```

The insertion position is between `k` and the space.

If the user presses:

* **Left Arrow** → cursor moves one logical character position left.
* **Right Arrow** → cursor moves one logical character position right.
* **Up/Down** → cursor moves toward the corresponding horizontal position on another line.
* **Home** → moves to the beginning of the relevant line.
* **End** → moves to the end of the relevant line.

### Interaction with text selection

When text is selected, the cursor is generally represented by the **active end of the selection** rather than being shown as a separate blinking line.

```text
The quick brown fox
    └──────────┘
       selected
```

If the user starts typing, the selected text is replaced at the cursor position.

### Cursor after clicking

When the user clicks inside text, the word processor calculates the nearest valid **text insertion position** based on the mouse coordinates.

For example:

```text
The quick brown fox
          ↑
       mouse click
```

The cursor might become:

```text
The quick│brown fox
```

The exact position depends on which side of the nearest character the click falls.

### Cursor at the end of a line

The cursor can exist after the final character:

```text
This is the end of the line│
```

It can also exist on an empty line:

```text
This is the previous line
│
```

### Cursor at line wrapping

A cursor position is associated with the **logical text position**, not simply an X/Y coordinate. Therefore, when text wraps:

```text
This is a long sentence that
wraps onto the next line│
```

the cursor remains associated with the position in the underlying text even though its visual position changes when the document is reflowed.

### Important implementation distinction

For a word processor, it is useful to treat these as separate concepts:

```text
Document model
      │
      ├── Text
      │
      ├── Selection
      │
      └── Caret position
              │
              ▼
       Cursor rendering
              │
              └── Blink animation
```

The **caret position** is part of the editor state. The **blinking cursor is only its visual representation**.

This distinction is particularly important if you're writing an implementation specification: **blinking must never cause a change to the document, undo history, dirty state, autosave state, or text model.**
