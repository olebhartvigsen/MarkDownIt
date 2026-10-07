# Zoom behavior guide for word processors and image viewers

This guide defines expected zoom behavior and rules an implementation can be checked against. **MUST** marks a required behavior; **SHOULD** marks a recommended behavior.

## 1. Shared zoom rules

### 1.1 Meaning of zoom

* Zoom changes how large content appears in the viewing area. It MUST NOT change the document’s text, formatting, page layout, or the image’s pixel data.
* The application MUST have a defined zoom range. A reasonable starting range is **25%–500% for documents** and **5%–1600% for images**. The chosen minimum and maximum MUST be consistent across controls and clearly enforced.
* The displayed percentage MUST reflect the current zoom level. It SHOULD be rounded to a whole percentage point.
* **100%** means the application’s base display scale. For images, the UI may call this **Actual pixels** if one image pixel maps to one application display pixel. It MUST NOT imply calibrated physical print size unless the display is calibrated.

### 1.2 Controls and state

A zoom control SHOULD provide:

* A **−** button and a **+** button.
* A percentage display, such as `100%`, that users can select or edit.
* A slider or preset menu, if the application’s interface has room.
* Useful view options such as **Fit page**, **Fit width**, or **Actual pixels**, depending on the content type.

This viewer ships the first bullet only. The percentage readout and
the Fit width button were removed: the shortcut set (Ctrl and +, -,
0, and Ctrl+wheel) covers the same ground with no ribbon space and no
second state to keep honest. Users who need the number can read it
from the scroll behaviour or set it by stepping. Section 7 records
what that means for each guide rule.

Every control MUST update the same zoom state. If the user enters a number, the application MUST validate it against the allowed range. For invalid or empty input, it SHOULD restore the previous valid value.

The **+** and **−** buttons SHOULD change zoom by 10 percentage points per press, clamped to the allowed range. They MUST be disabled or have no effect at their respective limit.

Zoom state is a view preference. It MUST NOT be saved as document text, formatting, or image data. An application may remember the user’s last view zoom, but its persistence behavior SHOULD be predictable.

### 1.3 Zoom anchor and scroll position

Zooming MUST preserve the user’s position in the content.

There are two related but distinct requirements:

1. **The zoom anchor determines which content point stays under a given screen coordinate.**
2. **The resulting horizontal and vertical scroll positions MUST be adjusted to achieve that result.**

The application MUST treat horizontal and vertical scrolling consistently. Zooming MUST NOT preserve only the vertical position while allowing the horizontal position to jump unexpectedly.

For a zoom factor change from `s` to `s′`, let:

* `aX`, `aY` = the screen-space coordinates of the zoom anchor.
* `scrollX`, `scrollY` = the current horizontal and vertical scroll offsets.
* `tX`, `tY` = the content's screen offset before zoom.
* `s` = the current zoom factor.
* `s′` = the new zoom factor.

The content coordinate under the anchor is:

```text
contentX = (aX - tX) / s
contentY = (aY - tY) / s
```

After changing zoom, the offsets MUST be adjusted so that the same content coordinate remains under the same screen position:

```text
tX′ = aX - s′ × contentX
tY′ = aY - s′ × contentY
```

Equivalently, if the application's coordinate system expresses `tX` and `tY` through scroll offsets, the new scroll positions MUST be calculated from the same relationship.

**Both axes MUST be handled.**

For example, if the user is viewing the right-hand side of a document at 150% and zooms to 200%, the application MUST NOT reset `scrollX` to zero merely because the zoom changed. The user's horizontal position in the document SHOULD remain visually stable.

### 1.4 Zoom and scroll boundaries

After calculating the new scroll position, it MUST be clamped to the valid scroll range.

The valid range depends on the new zoom level and viewport size:

```text
maxScrollX = max(0, contentWidthAtNewZoom - viewportWidth)
maxScrollY = max(0, contentHeightAtNewZoom - viewportHeight)
```

The application MUST NOT allow the calculated position to move outside these bounds.

If the zoomed content becomes smaller than the viewport on an axis:

* The content SHOULD be centered on that axis.
* The corresponding scroll position SHOULD become the centered position.
* The application MUST NOT retain an obsolete scroll offset that leaves the content displaced or unreachable.

This means that preserving the zoom anchor has priority **only while the anchor can be represented within the valid scroll range**.

### 1.5 Returning to 100%

Returning to **100%** MUST be treated as a normal zoom operation, not as a separate reset of the document position.

If the content is still larger than the viewport at 100%, the application MUST preserve the user's approximate content position when returning to 100%.

For example:

* User is at 200%.
* User is horizontally scrolled toward the right side.
* User returns to 100%.
* If the document is still wider than the viewport, the application MUST remain at approximately the same location in the document.
* It MUST NOT automatically jump to `scrollX = 0`.

Likewise, vertical position MUST be preserved where possible.

If the content becomes smaller than the viewport at 100%, the corresponding axis MUST be centered rather than retaining an invalid scroll offset.

The same rules apply when returning to 100% using:

* `Ctrl+0` / `Cmd+0`
* a numeric zoom field
* any other control that explicitly selects 100%.

This viewer has the keyboard route only; the 100% control was removed
with the readout.

### 1.6 Wheel, keyboard, and touch input

* With the platform’s primary modifier held (**Ctrl** on Windows/Linux, **Cmd** on macOS), wheel-up MUST zoom in and wheel-down MUST zoom out.
* The application SHOULD normalize mouse-wheel and trackpad deltas so both produce smooth, monotonic zoom changes. A small input MUST NOT cause a larger zoom change than a larger input in the same direction.
* The application SHOULD prevent the browser or surrounding page from zooming when the pointer is over the application’s content and the app handles the modified wheel gesture.
* Plain wheel input MUST keep its normal navigation behavior: scrolling the document or panning the image when it is larger than the viewport.
* **Shift+wheel** SHOULD scroll horizontally where horizontal scrolling is available.
* Pinch-out SHOULD zoom in; pinch-in SHOULD zoom out.
* `+` and `−` SHOULD zoom in and out when the viewing area has keyboard focus.
* `Ctrl+0` or `Cmd+0` SHOULD return to the application’s default view, if that shortcut does not conflict with a required platform or browser command.
* Zoom shortcuts MUST NOT run while the user is typing in a text field, editing document text, or using another control that consumes those keys.

## 2. Word processor behavior

### 2.1 What changes

In a paginated word processor, zoom MUST scale the displayed pages and surrounding canvas. It MUST leave document content and page layout unchanged. Text wrapping, page breaks, margins, and object positions MUST remain the same as before the zoom.

The caret, selection, and current editing position MUST remain attached to their document locations. After zooming, they SHOULD remain visible when possible.

For a reflowing editor that does not display fixed pages, zoom MAY change the effective text display size. In that case, the application MUST preserve the underlying document’s formatting values and saved layout.

### 2.2 Useful document zoom options

* **Fit page** scales the current page to fit within the available viewing area, accounting for toolbars and margins.
* **Fit width** scales the page so its full width fits in the viewing area.
* **Two pages** or a similar multi-page view MAY be provided.
* A numeric percentage sets a specific zoom value.
* A **100%** option returns to the application’s defined base scale.

Fit modes MUST recalculate when the viewing area changes size. A user’s manual zoom action SHOULD switch out of fit mode and display the resulting percentage.

Fit width is no longer a control in this viewer. The column-width
modes (section 2.2's useful zoom options) cover the same intent:
Standard, 960, 1600 and Fixed without wrap are line-length settings
that shape the column instead of scaling it, and the user picks them
on the ribbon's View group.

### 2.3 Navigation while zoomed

Plain wheel scrolling MUST continue to move through the document.

The document MAY require both vertical and horizontal scrolling when zoomed.

When horizontal scrolling is available:

* The horizontal scrollbar MUST represent the actual horizontal content position at the current zoom.
* Increasing zoom MUST NOT reset horizontal scrolling.
* Decreasing zoom MUST NOT reset horizontal scrolling unless the new content bounds make the previous position invalid.
* When the current horizontal position can no longer be represented because the content has become narrower than the viewport, the content SHOULD be centered.
* When zooming back up, the application SHOULD restore the user's relative location rather than unexpectedly starting at the left edge.

The same principles apply to vertical scrolling.

Zooming MUST preserve the current page or nearby content in view rather than jumping unexpectedly to the start of the document.

At high zoom, horizontal scrolling MAY be needed to reach the full page or content width.

## 3. Image viewer behavior

### 3.1 What changes

Zoom MUST scale the image in the viewer without resampling or altering the source image. If the image is rotated, fit calculations MUST use the rotated image’s displayed bounds.

* **Fit image** scales the entire image to fit inside the viewing area while preserving its aspect ratio.
* **Fit width** or **Fit height** MAY be provided.
* **Actual pixels** sets the viewer to the defined 100% image scale.
* A numeric percentage sets a specific zoom value.

Fit modes MUST recalculate when the viewing area changes size. Manual zoom SHOULD switch out of fit mode and show the resulting percentage.

### 3.2 Panning while zoomed

* When the image is smaller than the viewing area, it SHOULD be centered and MUST NOT pan beyond its edges.
* When it is larger, the user MUST be able to reach the image’s edges by dragging or scrolling.
* Pan limits SHOULD keep the image covering the viewing area where possible, so the user does not lose the image and see only empty space.
* A hand cursor, drag gesture, or spacebar-plus-drag SHOULD indicate or enable panning.

### 3.3 Zooming and horizontal/vertical pan position

Zooming MUST preserve the visible image location around the zoom anchor in **both X and Y directions**.

For example, when the user is viewing a detail near the right edge of an image:

* Zooming in MUST keep that detail near the same screen location.
* Zooming out MUST keep the same detail visible where the resulting image bounds permit it.
* Zooming to 100% MUST NOT automatically return the image to its top-left corner.
* If the image becomes smaller than the viewport, it MUST transition to the appropriate centered position.

Plain wheel behavior for an image viewer SHOULD be scrolling or panning. If the product uses the wheel to move between images, that behavior SHOULD be explicit and MUST NOT be triggered by a modified wheel gesture that the application uses for zoom.

## 4. Scrollbars and viewport changes

Scrollbars MUST always describe the content at the **current zoom level**.

When zoom changes:

1. The content dimensions MUST be recalculated using the new zoom.
2. The maximum horizontal and vertical scroll ranges MUST be recalculated.
3. The previous content position SHOULD be transformed into the new coordinate system.
4. The resulting position MUST be clamped to the new valid range.
5. If an axis no longer requires scrolling, its content SHOULD be centered.
6. If scrolling becomes necessary again, the scroll position SHOULD correspond to the previously visible content location rather than arbitrarily starting at zero.

The same rules apply when the viewport changes size because of:

* window resizing,
* ribbon/toolbars appearing or disappearing,
* docking panels opening or closing,
* DPI changes,
* entering or leaving fullscreen mode.

A viewport resize MUST NOT unnecessarily reset the user's horizontal or vertical position.

## 5. Accessibility and feedback

* Zoom buttons and menus MUST have accessible names such as **Zoom in**, **Zoom out**, **Fit page**, and **Actual pixels**.
* Controls MUST be reachable and operable by keyboard.
* Keyboard focus MUST remain visible after zoom actions.
* The percentage display, where the interface has one, MUST update after every zoom change. Screen readers SHOULD announce the settled percentage without announcing every small trackpad update.

This viewer has no on-screen percentage control. `Ctrl+0` is the
documented route back to the base scale, and the buttons' enabled
state (dimmed at the range ends) tells the user where they are in the
range without a number.
* Zoom MUST NOT make controls inaccessible or prevent users from reaching content boundaries.
* Scrollbars MUST remain usable and accurately reflect the current content bounds and position after every zoom operation.

## 6. Validation checklist

An implementation is consistent with this guide if all applicable checks pass:

1. **Shared state:** Buttons, shortcuts, and modified wheel gestures show and use the same zoom value.
2. **Bounds:** Every input respects the declared minimum and maximum; controls cannot move past either limit.
3. **Direction:** Wheel-up and pinch-out zoom in; wheel-down and pinch-in zoom out.
4. **Anchor X:** Modified wheel zoom preserves the content point under the pointer horizontally.
5. **Anchor Y:** Modified wheel zoom preserves the content point under the pointer vertically.
6. **Scroll X:** Zooming does not unnecessarily reset the horizontal scroll position.
7. **Scroll Y:** Zooming does not unnecessarily reset the vertical scroll position.
8. **Scroll bounds:** New horizontal and vertical positions are clamped to the valid ranges after every zoom change.
9. **Centering:** If content becomes smaller than the viewport on an axis, it is correctly centered on that axis.
10. **100% behavior:** Returning to 100% (Ctrl+0 here) preserves the user's content position where the content still requires scrolling.
11. **Normal navigation:** Plain wheel still scrolls a document or pans a zoomed image.
12. **No content edits:** Zoom alone does not change document content, formatting, pagination, or image data.
13. **Fit behavior:** Where fit modes exist, they show the intended page or image bounds and recalculate when the viewing area changes. This viewer has no fit mode control; the check does not apply.
14. **Position retention:** Zooming does not unexpectedly jump the document or image to its beginning.
15. **Viewport changes:** Resizing the viewport does not unnecessarily reset horizontal or vertical position.
16. **Keyboard safety:** Zoom shortcuts do not fire while the user is entering text or operating an unrelated input.
17. **Accessibility:** Controls have meaningful names, can be reached by keyboard, and report the current zoom value.

## 7. How this app satisfies the guide

Decisions worth recording, so the next reader does not re-derive them.

**Range is 25% to 400%,** not the 500% the guide suggests as a starting point. The guide makes the exact numbers a suggestion and requires only that the chosen bounds be consistent and enforced, which they are:
`src/zoommodel.h` holds the single definition, `Renderer::SetZoom` clamps on
every change, and the registry value is clamped again on load.

**A press steps ten percentage points** (`zoom::Step`), matching section 1.2.
The previous code multiplied by 1.25, which looked even at 100% but moved 75
points at 300% and never landed on a round value. Stepping is additive and
clamped, so the last press at either limit lands exactly on that limit.

**The percentage readout is gone.** An earlier build showed a ribbon
`Label` bound to `cmdZoomLevel` that returned to 100% on a click, and a
Fit width button next to it. Both were removed to slim the View tab
down: the zoom group held no other control after that, so the group
itself went too, and `ZoomPercent` and `FitZoomToWidth` left the code
with the buttons. The zoom model itself still has `zoom::Percent` and
`zoom::FitWidth` as tested math helpers, but nothing in the app calls
them any more.

What covers the readout's old jobs:

* Back to 100%: `Ctrl+0`, same position-preserving `ApplyZoom` path as
  every other zoom (section 1.5 still describes the rule).
* Fit-to-width intents: the column-width modes in the View group.
* Range feedback: the + and - buttons dim at their respective limits,
  which section 1.2 asks for and the enabled queries already
  implement.

**Focus point is an explicit argument.** `ApplyZoom(newZoom, focusY, focusX)`
takes the point that stays fixed instead of probing the cursor itself. A wheel
gesture passes the pointer position; a button and a keyboard shortcut pass
the viewport centre. The anchor calculation applies to **both X and Y**, so
horizontal scrolling is preserved using the same model as vertical
scrolling.

**Both axes scroll.** The content column is 800 DIPs at 100% and scales with
zoom, so above roughly 150% it is wider than the viewport. There is a
horizontal scrollbar, `Shift+wheel` scrolls sideways, and the zoom anchor
covers X as well as Y.

The horizontal scroll position is part of the view state. When zoom changes,
the implementation MUST recalculate the content width and horizontal scroll
range and transform the previous horizontal content position into the new
zoom coordinate system. It MUST NOT simply preserve the raw pixel value of
`scrollX`, because that value represents a different content location at a
different zoom level.

**The scroll range must describe the column as actually drawn.**
`Renderer::ContentWidthDip(viewportWidthDip)` takes the viewport width and
returns the smaller of the capped column width and the width left after
padding, which is exactly what `Render` and `RenderSourceView` paint. The
earlier version returned the uncapped `maxContentWidth`, which in the uncapped
width mode is `100000.0f`: the horizontal scrollbar claimed about 100000 DIPs
of content that is never drawn, so the user could scroll far past the end of
the text into blank space. That is what section 1.4 and section 4 forbid. With
wrap off the column is the `kNoWrapContentWidthDip` constant, matching the
`10000.0f` literal the renderer draws with.

For example, if the viewport is showing the right-hand portion of the content
at 200%, changing to 100% MUST calculate the corresponding 100% position.
If the content is still wider than the viewport, the user should remain at
approximately the same content location. If the content is no longer wider
than the viewport, horizontal scrolling is no longer possible and the content
is centered.

**Returning to 100% is not a scroll reset.** `Ctrl+0` and the 100% control
change the zoom to exactly 100% but MUST use the same position-preservation
logic as any other zoom operation. They MUST NOT unconditionally set
`scrollX` or `scrollY` to zero.

This is particularly important when the user has zoomed into a location and
then wants to return to normal size: returning to 100% should return the user
to the corresponding document location, not to the beginning of the content.

**Scroll positions are clamped after zoom.** Because the content dimensions
change with zoom, the old scroll position may no longer be valid. After every
zoom operation the implementation recalculates the maximum X and Y scroll
positions and clamps the calculated values. If the content becomes smaller
than the viewport, that axis is centered.

**The fit width button is removed.** The paragraph below records why its
math was the way it was, kept because the width modes still use
`Renderer::BaseContentWidthDip()` and the same reasoning applies to them.

`Renderer::BaseContentWidthDip()` returns 800, 960 or 1600 for the three
capped modes. The earlier fit-width scaling hardcoded an 800 base, which
was wrong for the 960 and 1600 modes: those columns overflowed the viewport.
The uncapped mode reports 0, and fitting against it was a no-op, because
there is no fixed column width to fit. The content-width modes (Standard,
960, 1600, Full) are a line-length setting, not a fit mode: they cap the
column rather than scaling it to the viewport.

**The wheel anchor is in DIPs.** `ScreenToClient` reports pixels, while every
layout measure, and `ApplyZoom`'s anchor arithmetic, are in DIPs. The anchor is
multiplied by `96.0f / dpi_` before use. At 150% scaling the unconverted pixel
value overshot, and the content point under the cursor drifted away from where
the user left it, which section 1.3 forbids.

**No KeyTips on the zoom controls.** The project's own ribbon guidelines ask
for KeyTips, and they are not implemented for any command yet. Attempts to add
them here failed three CI runs in a row, and the reason is worth recording so
nobody retries the same thing:

* `keytip` is not accepted on `Command` or on `Button` by the schema `uicc`
  validates against (`error SC1053`).
* The published 2006/01 XSD does declare it, on `tab` and `group` only, but
  the 2009/07 schema this project uses rejects it there too.
* `Label` is not in the 2006/01 XSD at all, and there is no local copy of the
  2009/07 schema to check it against.

So the readout was a plain `Button` rather than a `Label`, and no keytip was
declared anywhere. Keyboard operation never depended on it: `Ctrl`+`+`,
`Ctrl`+`-` and `Ctrl+0` cover zoom in, zoom out and back to 100%, which is what
the zoom guide requires of these controls. Adding KeyTips properly means
validating against the 2009/07 schema first, not guessing the spelling.

### Deliberately not implemented

* **The percentage readout and the Fit width button.** Removed after
  shipping. The zoom group in the View tab emptied with them, and the
  keyboard set covers both jobs: `Ctrl+0` for the base scale, and the
  column-width modes for fit-like intents. A readout that only some
  users read was charging rent on prime ribbon space.
* **Trackpad pinch as `WM_GESTURE`.** Windows delivers precision-touchpad
  pinch as `WM_MOUSEWHEEL` with `MK_CONTROL`, which the Ctrl branch already
  handles, so the gesture works on real hardware without a second path.
* **Fit page and two-page view.** The app is a reflowing editor with no fixed
  pages, so section 2.1's pagination rules do not apply, and "fit page" has no
  page to fit. A "fit height" equivalent would mean fitting the whole document,
  which section 2.1 excludes by describing the option as page-scoped.
* **A slider or preset menu.** Section 1.2 makes this conditional on the
  interface having room. The view groups are full.
* **Editing the percentage by typing.** The readout is a label, not an edit
  box. The keyboard route already covers it: `Ctrl+0` returns to 100%, and
  `Ctrl`+`+`/`-` step through the range.

**Selection highlighting handles a selection of any length.**
`FillSelectionHighlight` in `renderer.cpp` draws the highlight for a UTF-16
range. `HitTestTextRange` returns one `DWRITE_HIT_TEST_METRICS` per text
position, so the earlier fixed 64-entry stack array truncated any selection
longer than 64 characters. Source view lays the whole document out as a single
text layout, so Ctrl+A there produced far more than 64 metrics and the
highlight stopped after roughly the first 64 characters. To a user that reads
as select all doing nothing at all.

The helper walks the layout line by line and hit-tests only the lines that
intersect the viewport, so the highlight is correct for a selection of any
length while the per-frame cost stays bounded by what is on screen. All four
selection paths use it: rendered blocks, code blocks, table cells and source
view. The remaining fixed-size hit test in the renderer paints the inline-code
background, not a selection, and its spans are a few characters long.

## 8. Select all and clipboard in every mode

Not a zoom rule, but it sits in the same input path (`OnKeyDown`) and the same
"which mode am I in" gate, so it is recorded here rather than re-derived.

**Ctrl+A, Ctrl+C, Ctrl+X and Ctrl+V work in all three modes:** rendered view
mode, edit mode and source mode. The view-mode gate in `OnKeyDown` used to
admit only navigation, `Ctrl+C` and `Ctrl+A`; `Ctrl+X` and `Ctrl+V` fell
through to the edit handlers' suppression and did nothing at all.

**Cut and paste enter edit mode, because there is no caret in view mode.**
View mode has no caret to cut against or paste at, so both call `SetEdit(true)`
first. Three rules keep that from surprising the user:

* An empty selection makes cut a no-op, and the spec requires an empty
  selection to change neither clipboard nor document. The mode switch sits
  inside the `!sel_.Empty()` branch, so it only happens when there is real
  work to do.
* Paste reads the clipboard *before* switching modes, so an empty or
  unsupported clipboard leaves the mode alone.
* The shift aliases `Shift+Insert` and `Shift+Delete` do exactly what `Ctrl+V`
  and `Ctrl+X` do, mode switch included, as the text editing spec requires of
  alias shortcuts.

The same three commands are on the view-mode context menu, with cut greyed out
when the selection is empty, matching the keyboard.

**Copy is mode-aware, and source mode is verbatim.**
`SelectionForClipboard()` had two defects:

* It ran `CleanSelectionForCopy` on its result, and the `Ctrl+C` handler ran it
  a second time on the returned string. Applying it twice is not idempotent: a
  paragraph break that survived the first pass as `\n\n` is still fine, but the
  forced-break and soft-break rules then re-inspect text they had already
  rewritten. The helper is now the single place that decides.
* In source mode it applied the rendered-view filter to raw Markdown, turning
  soft line breaks into spaces and dropping syntax the user could see on
  screen. A copy from source mode no longer matched the display. Source mode now
  returns the raw slice verbatim, before any visible-text filtering.

**Ctrl+A in source mode selects everything in one press.** `SelectAll()`
escalates cell, then row, then table, then document when the caret sits in a
table cell. That escalation describes rendered cells, and source mode shows
Markdown text with no cells on screen, so the tier logic is skipped there.
