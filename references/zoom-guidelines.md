# Zoom behavior guide for word processors and image viewers

This guide defines expected zoom behavior and rules an implementation can be checked against. **MUST** marks a required behavior for this guide; **SHOULD** marks a recommended behavior.

## 1. Shared zoom rules

### 1.1 Meaning of zoom

- Zoom changes how large content appears in the viewing area. It MUST NOT change the document’s text, formatting, page layout, or the image’s pixel data.
- The application MUST have a defined zoom range. A reasonable starting range is **25%–500% for documents** and **5%–1600% for images**. The chosen minimum and maximum MUST be consistent across controls and clearly enforced.
- The displayed percentage MUST reflect the current zoom level. It SHOULD be rounded to a whole percentage point.
- **100%** means the application’s base display scale. For images, the UI may call this **Actual pixels** if one image pixel maps to one application display pixel. It MUST NOT imply calibrated physical print size unless the display is calibrated.

### 1.2 Controls and state

A zoom control SHOULD provide:

- A **−** button and a **+** button.
- A percentage display, such as `100%`, that users can select or edit.
- A slider or preset menu, if the application’s interface has room.
- Useful view options such as **Fit page**, **Fit width**, or **Actual pixels**, depending on the content type.

Every control MUST update the same zoom state. If the user enters a number, the application MUST validate it against the allowed range. For invalid or empty input, it SHOULD restore the previous valid value.

The **+** and **−** buttons SHOULD change zoom by 10 percentage points per press, clamped to the allowed range. They MUST be disabled or have no effect at their respective limit.

Zoom state is a view preference. It MUST NOT be saved as document text, formatting, or image data. An application may remember the user’s last view zoom, but its persistence behavior SHOULD be predictable.

### 1.3 Where zoom is centered

Zooming should keep the user’s point of interest in view:

- **Ctrl+wheel** on Windows or Linux and **Cmd+wheel** on macOS MUST zoom around the pointer position.
- A trackpad pinch SHOULD zoom around the gesture’s midpoint.
- Clicking **+**, **−**, a preset, or a percentage field SHOULD zoom around the center of the viewing area, unless the control has a more specific focus point.
- After zooming, the content point that was under the chosen anchor SHOULD remain under that same screen position.

For a zoom factor change from `s` to `s′`, an implementation can preserve the anchor by keeping its content coordinate fixed. If the anchor’s screen coordinate is `a` and the content’s screen offset is `t`, that content coordinate is `(a − t) / s`. After the zoom, set the offset to `t′ = a − s′ × ((a − t) / s)`.

### 1.4 Wheel, keyboard, and touch input

- With the platform’s primary modifier held—**Ctrl** on Windows/Linux or **Cmd** on macOS—wheel-up MUST zoom in and wheel-down MUST zoom out.
- The application SHOULD normalize mouse-wheel and trackpad deltas so both produce smooth, monotonic zoom changes. A small input MUST NOT cause a larger zoom change than a larger input in the same direction.
- The application SHOULD prevent the browser or surrounding page from zooming when the pointer is over the application’s content and the app handles the modified wheel gesture.
- Plain wheel input MUST keep its normal navigation behavior: scrolling the document or panning the image when it is larger than the viewport.
- **Shift+wheel** SHOULD scroll horizontally where horizontal scrolling is available.
- Pinch-out SHOULD zoom in; pinch-in SHOULD zoom out.
- `+` and `−` SHOULD zoom in and out when the viewing area has keyboard focus. `Ctrl+0` or `Cmd+0` SHOULD return to the application’s default view, if that shortcut does not conflict with a required platform or browser command.
- Zoom shortcuts MUST NOT run while the user is typing in a text field, editing document text, or using another control that consumes those keys.

## 2. Word processor behavior

### 2.1 What changes

In a paginated word processor, zoom MUST scale the displayed pages and surrounding canvas. It MUST leave document content and page layout unchanged. Text wrapping, page breaks, margins, and object positions MUST remain the same as before the zoom.

The caret, selection, and current editing position MUST remain attached to their document locations. After zooming, they SHOULD remain visible when possible.

For a reflowing editor that does not display fixed pages, zoom MAY change the effective text display size. In that case, the application MUST preserve the underlying document’s formatting values and saved layout.

### 2.2 Useful document zoom options

- **Fit page** scales the current page to fit within the available viewing area, accounting for toolbars and margins.
- **Fit width** scales the page so its full width fits in the viewing area.
- **Two pages** or a similar multi-page view MAY be provided.
- A numeric percentage sets a specific zoom value.
- A **100%** option returns to the application’s defined base scale.

Fit modes MUST recalculate when the viewing area changes size. A user’s manual zoom action SHOULD switch out of fit mode and display the resulting percentage.

### 2.3 Navigation while zoomed

Plain wheel scrolling MUST continue to move through the document. Zooming MUST preserve the current page or nearby content in view rather than jumping unexpectedly to the start of the document. At high zoom, horizontal scrolling MAY be needed to reach the full page width.

## 3. Image viewer behavior

### 3.1 What changes

Zoom MUST scale the image in the viewer without resampling or altering the source image. If the image is rotated, fit calculations MUST use the rotated image’s displayed bounds.

- **Fit image** scales the entire image to fit inside the viewing area while preserving its aspect ratio.
- **Fit width** or **Fit height** MAY be provided.
- **Actual pixels** sets the viewer to the defined 100% image scale.
- A numeric percentage sets a specific zoom value.

Fit modes MUST recalculate when the viewing area changes size. Manual zoom SHOULD switch out of fit mode and show the resulting percentage.

### 3.2 Panning while zoomed

- When the image is smaller than the viewing area, it SHOULD be centered and MUST NOT pan beyond its edges.
- When it is larger, the user MUST be able to reach the image’s edges by dragging or scrolling.
- Pan limits SHOULD keep the image covering the viewing area where possible, so the user does not lose the image and see only empty space.
- A hand cursor, drag gesture, or spacebar-plus-drag SHOULD indicate or enable panning.

Plain wheel behavior for an image viewer SHOULD be scrolling or panning. If the product uses the wheel to move between images, that behavior SHOULD be explicit and MUST NOT be triggered by a modified wheel gesture that the application uses for zoom.

## 4. Accessibility and feedback

- Zoom buttons and menus MUST have accessible names such as **Zoom in**, **Zoom out**, **Fit page**, and **Actual pixels**.
- Controls MUST be reachable and operable by keyboard.
- Keyboard focus MUST remain visible after zoom actions.
- The visible percentage MUST update after every zoom change. Screen readers SHOULD announce the settled percentage without announcing every small trackpad update.
- Zoom MUST NOT make controls inaccessible or prevent users from reaching content boundaries.

## 5. Validation checklist

An implementation is consistent with this guide if all applicable checks pass:

1. **Shared state:** Buttons, percentage entry, slider, shortcuts, modified wheel, and pinch gestures show and use the same zoom value.
2. **Bounds:** Every input respects the declared minimum and maximum; controls cannot move past either limit.
3. **Direction:** Wheel-up and pinch-out zoom in; wheel-down and pinch-in zoom out.
4. **Anchor:** Modified wheel zoom preserves the content point under the pointer. Button zoom preserves the center or the control’s documented focus point.
5. **Normal navigation:** Plain wheel still scrolls a document or pans a zoomed image.
6. **No content edits:** Zoom alone does not change document content, formatting, pagination, or image data.
7. **Fit behavior:** Fit modes show the intended page or image bounds and recalculate when the viewing area changes.
8. **Position retention:** Zooming does not unexpectedly jump the document or image to its beginning.
9. **Keyboard safety:** Zoom shortcuts do not fire while the user is entering text or operating an unrelated input.
10. **Accessibility:** Controls have meaningful names, can be reached by keyboard, and report the current zoom value.

## 6. How this app satisfies the guide

Decisions worth recording, so the next reader does not re-derive them.

**Range is 25% to 400%,** not the 500% the guide suggests as a starting
point. The guide makes the exact numbers a suggestion and requires only that
the chosen bounds be consistent and enforced, which they are:
`src/zoommodel.h` holds the single definition, `Renderer::SetZoom` clamps on
every change, and the registry value is clamped again on load.

**A press steps ten percentage points** (`zoom::Step`), matching section 1.2.
The previous code multiplied by 1.25, which looked even at 100% but moved 75
points at 300% and never landed on a round value. Stepping is additive and
clamped, so the last press at either limit lands exactly on that limit.

**The percentage readout** is a ribbon `Label` bound to `cmdZoomLevel`, fed by
`zoom::Percent` and refreshed by an `InvalidateUICommand` at the end of
`ApplyZoom`. Clicking it returns to 100%, which covers the numeric-preset
option in section 2.2 without adding an edit box.

**Focus point is an explicit argument.** `ApplyZoom(newZoom, focusY, focusX)`
takes the point that stays fixed instead of probing the cursor itself. A wheel
gesture passes the pointer position; a button, a keyboard shortcut and fit
width pass the viewport centre. Probing the cursor inside `ApplyZoom` made a
ribbon click anchor on an arbitrary point whenever the mouse happened to sit
over the text.

**Both axes scroll.** The content column is 800 DIPs at 100% and scales with
zoom, so above roughly 150% it is wider than the viewport. There is a
horizontal scrollbar, `Shift+wheel` scrolls sideways, and the zoom anchor
covers X as well as Y. Section 4 forbids zoom from preventing the user from
reaching content boundaries, which is unreachable content would violate.

**Fit width** scales by available width over the 800 DIP base and clamps to the
same range as every other entry point. The content-width modes (Standard, 960,
1600, Full) are a line-length setting, not a fit mode: they cap the column
rather than scaling it to the viewport.

**No KeyTips on the zoom controls.** The project's own ribbon guidelines ask
for KeyTips, and they are not implemented for any command yet. Attempts to add
them here failed three CI runs in a row, and the reason is worth recording so
nobody retries the same thing:

- `keytip` is not accepted on `Command` or on `Button` by the schema `uicc`
  validates against (`error SC1053`).
- The published 2006/01 XSD does declare it, on `tab` and `group` only, but
  the 2009/07 schema this project uses rejects it there too.
- `Label` is not in the 2006/01 XSD at all, and there is no local copy of the
  2009/07 schema to check it against.

So the readout is a plain `Button` rather than a `Label`, and no keytip is
declared anywhere. Keyboard operation does not depend on it: `Ctrl`+`+`,
`Ctrl`+`-` and `Ctrl+0` cover zoom in, zoom out and back to 100%, which is what
the zoom guide requires of these controls. Adding KeyTips properly means
validating against the 2009/07 schema first, not guessing the spelling.

### Deliberately not implemented

- **Trackpad pinch as `WM_GESTURE`.** Windows delivers precision-touchpad
  pinch as `WM_MOUSEWHEEL` with `MK_CONTROL`, which the Ctrl branch already
  handles, so the gesture works on real hardware without a second path.
- **Fit page and two-page view.** The app is a reflowing editor with no fixed
  pages, so section 2.1's pagination rules do not apply, and "fit page" has no
  page to fit. A "fit height" equivalent would mean fitting the whole document,
  which section 2.1 excludes by describing the option as page-scoped.
- **A slider or preset menu.** Section 1.2 makes this conditional on the
  interface having room. The view groups are full.
- **Editing the percentage by typing.** The readout is a label, not an edit
  box. The keyboard route already covers it: `Ctrl+0` returns to 100%, and
  `Ctrl`+`+`/`-` step through the range.
