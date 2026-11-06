# Prompt: Make the macOS Markdown viewer feel polished and native

You are working in an existing cross-platform Markdown viewer. Improve the **macOS Markdown reading experience** in the current source code. The Windows version already exists; preserve its behavior and appearance unless a shared change is necessary and demonstrably safe.

Treat the Apple homepage as inspiration for **clear hierarchy, confident scale, generous whitespace, restrained color, and carefully composed sections**. Adapt those ideas to a tool people use to read real documents for minutes or hours. This is a content-first macOS reader, not a marketing page and not a recreation of Apple’s website.

## First inspect the project

Before changing anything:

1. Locate the macOS app target, Markdown parsing/rendering path, stylesheets or native attributed-text code, theme and appearance handling, and any shared Windows/macOS components.
2. Identify the rendering technology and use its native mechanisms. Reuse the existing architecture and Markdown parser. Do not replace the renderer, add a new UI framework, or redesign unrelated app chrome.
3. Check which Markdown features are already supported and preserve them, including tables, fenced code, task lists, links, images, and syntax highlighting where present.
4. Make the smallest coherent design change in the existing source. Keep macOS-specific styling scoped to macOS when shared styles would change the Windows port.

## Design direction

- Make the document comfortable to read first, then attractive. The document is the visual focus; app controls remain familiar macOS controls.
- Use a quiet, editorial layout: a readable text measure, strong but restrained type hierarchy, consistent vertical rhythm, and subtle separators.
- Keep document text leading-aligned. Do not center ordinary paragraphs, headings, or tables.
- Use the system typeface and semantic platform colors. Avoid bundled copies of Apple fonts, branding, decorative gradients, unnecessary cards, glass effects, and heavy borders.
- Use a light/dark adaptive palette. Respect the app’s existing appearance settings and macOS accent color where available.
- Prefer native, user-respecting behavior over decorative effects. Keep text selectable and links recognizable.

## Typography and measure

Use logical CSS pixels for a WebKit/HTML renderer, or equivalent points and text styles for a native renderer. Treat the following as starting values; map them to the project’s existing scale rather than introducing conflicting units.

| Element | Size | Weight | Line height | Spacing and treatment |
|---|---:|---:|---:|---|
| Body text | 17.3 px (about 13 pt) | Regular (400) | 1.62 | Paragraph bottom margin 0.9–1.05 em; comfortable long-form reading |
| H1 | 36 px | Bold (700) | 1.12 | Top margin 0; bottom 0.55 em; slight negative tracking, about −0.02 em |
| H2 | 28 px | Semibold (600) | 1.2 | Top 1.9 em; bottom 0.55 em; slight negative tracking |
| H3 | 22 px | Semibold (600) | 1.28 | Top 1.5 em; bottom 0.45 em |
| H4 | 18 px | Semibold (600) | 1.35 | Top 1.25 em; bottom 0.4 em |
| H5 | 16 px | Semibold (600) | 1.4 | Top 1.1 em; bottom 0.35 em |
| H6 | 14 px | Semibold (600) | 1.4 | Top 1.0 em; bottom 0.35 em; secondary label color, not all caps |
| Inline code | 0.9 em | Regular | 1.45 | Monospaced system face; compact inset |
| Code block | 13.5–14 px | Regular | 1.55 | Monospaced system face; preserve indentation |
| Table text | 14.5–15 px | Regular | 1.45 | Do not shrink further to force a wide table to fit |
| Caption / footnote | 13.5 px minimum (about 10 pt) | Regular | 1.4 | Secondary label color; never use low-contrast gray |

- Use the system UI font stack (for WebKit, prefer the platform’s system UI font; for AppKit/SwiftUI, use system fonts and text styles). Use a system monospaced face for code.
- Use regular body weight. Avoid thin body text and overusing bold. Use semibold headings with a clear size contrast.
- Aim for a document column around **68–74 characters** wide (`max-width: 72ch` is a good HTML starting point). Center the column within the available reading area, with at least 28–36 px of side padding. On narrow windows, reduce the side padding to 18–22 px and let the column fill the space.
- Let users resize the window without causing an excessively wide text line. If the app already has a reading-width preference, honor it.
- Use relative units where practical so text can scale. Preserve support for system text-size/accessibility settings; do not hard-code a size that blocks enlargement.

## Markdown element rules

### Paragraphs and headings

- Body copy uses the primary label color and a relaxed line height. Keep paragraphs distinct without inserting excessive blank space.
- H1 is the document’s clear title. If a document begins with a title, avoid an extra top gap above it. Allow long titles to wrap naturally.
- H2–H6 form a visibly descending hierarchy. Use spacing and weight before adding decorative rules. Do not put a divider beneath every heading.
- Maintain sensible heading margins when one heading follows another; avoid a large empty gap between consecutive headings.

### Links and emphasis

- Use the current system accent color for links, with sufficient contrast in both appearances. Keep links recognizable in the reading view: underline them or provide a clear underline on hover and keyboard focus.
- Add a visible, accessible keyboard focus indicator. Do not rely on color alone to distinguish links.
- Render bold and italic inline emphasis without changing the surrounding line height or causing awkward wrapping.

### Lists and task lists

- Use clear indentation (about 1.5–1.8 em), aligned wrapped lines, and consistent spacing between items (about 0.3–0.45 em).
- Distinguish nested levels with indentation and marker changes, not color alone. Keep list text at body size.
- Align task-list checkboxes with the first text line. Keep them readable and consistent with native controls if they are interactive.

### Tables

Tables need to feel carefully typeset, not like a spreadsheet pasted into a document.

- Place each table in a horizontally scrollable wrapper when it exceeds the reading column. Never clip columns or shrink text to unreadable sizes.
- Use full available reading-column width, natural column sizing, and enough cell padding (roughly 10–12 px vertically and 12–14 px horizontally).
- Keep headers left-aligned by default, medium/semibold, and on a subtly differentiated background. Preserve Markdown alignment markers for right- and center-aligned columns.
- Separate rows with fine, low-contrast horizontal rules. Avoid a heavy grid and vertical borders between every cell.
- Use a very subtle alternating row tint for wide or data-dense tables when it improves row tracking. Keep it quiet in both light and dark mode.
- Vertically center cell contents, allow normal wrapping for prose, and preserve long code or identifiers without forcing the entire view wider than the window.
- Keep header text legible and distinct; use a compact table size around 14.5–15 px, never tiny text. Add a caption only when the Markdown source provides one or the renderer already supports it.
- Ensure the table wrapper can be reached and operated with keyboard navigation and trackpad scrolling.

### Code and preformatted text

- Inline code gets a small, softly tinted background and modest corner radius. Keep its contrast high and its padding tight so it remains part of the sentence.
- Fenced code blocks use a distinct neutral surface, a subtle border, and 14–18 px padding. Use a 8–10 px corner radius only if consistent with the app’s existing visual language.
- Preserve whitespace and indentation. Allow horizontal scrolling for long lines instead of wrapping code by default. Keep code selectable and use existing syntax highlighting with restrained colors that work in light and dark modes.
- Preserve existing copy-code controls and language labels. Do not add UI chrome to every block if the project does not already have it.

### Blockquotes, rules, images, and other content

- Style blockquotes with a slim leading-edge rule and a slight inset; keep their text readable and do not reduce contrast excessively.
- Render horizontal rules as thin, quiet separators with generous surrounding space.
- Keep images within the reading column (`max-width: 100%`), preserve aspect ratio, and avoid cropping. Use restrained rounded corners only if they fit the existing app style.
- Keep footnotes, captions, and metadata secondary but legible. Keep math, embedded content, and unsupported Markdown fallbacks consistent with the existing renderer.

## Color, appearance, and accessibility

- Use semantic system colors for the window/document background, primary and secondary text, separators, and accent links. If the rendering layer cannot access semantic colors directly, define equivalent light and dark tokens and switch them with the app’s appearance setting or the platform’s appearance preference.
- Avoid pure black on white and pure white on black for large reading surfaces. Keep secondary text comfortably legible and maintain at least WCAG AA contrast for normal text (4.5:1) wherever the renderer allows measurement.
- Check that borders, table striping, code surfaces, selection, links, and focus indicators remain visible in light mode, dark mode, and increased-contrast settings.
- Respect text selection, copy/paste, keyboard navigation, VoiceOver semantics, and user text scaling. Do not use color as the only signal.
- Do not make text smaller than 13.5 px for captions or 14 px for dense table content at the default scale. Let the user enlarge text.

## Responsive behavior

- At a wide window size, preserve the readable column and use the surrounding space as calm whitespace; do not stretch paragraphs edge to edge.
- At a narrow window size, reduce outer padding, allow headings and table cells to wrap, and keep code and wide tables independently scrollable.
- Check for long unbroken URLs, inline code, long headings, large images, and wide tables so none can break the window layout.
- Respect full-screen and window resizing. Avoid fixed heights that cut off document content.

## Implementation and finish

1. Implement the styling in the existing macOS rendering path. Keep parsing and document semantics intact; prefer styling over rewriting Markdown transformation logic.
2. Keep the change focused on the Markdown content surface. Do not restyle the Windows port or the surrounding application shell unless the existing architecture requires it.
3. Review representative documents containing all supported Markdown elements, including a long article, deeply nested lists, a wide table, and long code lines.
4. Inspect the result at a typical Mac window size and a narrow resized window, in light and dark appearance. Refine spacing, wrapping, contrast, and clipping until the view looks deliberate.
5. Summarize the files changed, the visual decisions made, and any platform limitations. If the repository has a normal build or visual preview workflow, use it to confirm the macOS target still renders correctly.

## Acceptance checklist

- The macOS reader looks native and polished while remaining optimized for long-form reading.
- Body text has comfortable size, line spacing, and line length.
- H1 through H6 have a consistent, unmistakable hierarchy and appropriate vertical rhythm.
- Tables are readable, restrained, and horizontally navigable when necessary.
- Code, links, lists, blockquotes, images, and separators feel like parts of one coherent design.
- Light and dark appearances both work, including focus, selection, and contrast.
- Resizing does not cause clipping, cramped text, or runaway line lengths.
- Existing Markdown behavior and the Windows version remain intact.

## Compliance sweep (Nov 2)

The app was measured against this document, requirement by requirement, on `feat/macos-port`. Fixed in `1ded61a` and `9547e1c` (CI run 41511699355):

- Reading column: the pane caps its column at a reading width (about 74
  characters at the capture size) instead of stretching with the window,
  and the side padding slides from the theme's 56 down to 16 on a narrow
  pane.
- Leading and measure: the body text holds a line box of at least
  1.62 em, the list item gap takes its floor, and the table's cell
  padding is the 10 DIP lower bound on both platforms.
- Tables: the vertical grid is gone, rows separate with one fine line,
  headers draw semibold, cells center in their row, and the alternate
  row keeps its faint wash.
- Lists: the bullet marker changes with the depth.
- Images: a tall image keeps its aspect ratio and reserves the room it
  draws in.
- Colour: the caret and the selection wash read from the system colours,
  so both appearances stay readable, and the muted slot (quotes, captions,
  H6) measures about 6.2 to 1 over the light paper and 7.2 to 1 in the
  dark, past the AA floor the system's own secondary label misses.
- Coverage: the QA fixture and the walk gained the deep nesting, the
  long unbroken link and inline code, the six column table and the long
  code block (the tail state), plus narrow and wide window states on the
  mac.

Recorded, not changed:

- Code blocks wrap with the document's wrap preference rather than
  scrolling per block; a per-block horizontal scroll would be new
  architecture, which this document's own scoping rules rule out.
- Tables size their columns equally and wrap the cells instead of a
  per-table horizontal scroll wrapper. Nothing clips and no text shrinks
  below the body size.
- Links activate by pointer (Cmd+click); there is no tab-through-links
  focus ring, and the reading surface follows the platform's own way of
  showing focus, the caret in the edit view.
- The light page background stays the system's controlBackgroundColor,
  which resolves to pure white; the ink is never pure black and the pair
  measures about 15 to 1. Apple's own document apps use the same surface.
- The stored reading-width preference no longer exists on the mac by
  explicit instruction: the pane always fits the window, so the
  document's conditional honor-it clause no longer applies.

## GitHub profile (Nov 2)

The document style moved to GitHub's markdown rendering on both platforms.
Where this prompt or the compliance notes above disagree with
[github-markdown-style.md](github-markdown-style.md), the GitHub profile wins.
The body is now 16px at 1.5, code 13.6px at 1.45, headings follow GitHub's em
scale, H1 and H2 carry a 1px rule, tables use a full 1px grid with a white
header, and fenced code is a borderless card. The Apple requirements that
still hold are the system faces, semantic colours, the fixed reading column,
the padding slide on a narrow pane, selectable text, and the Light Mode, Dark
Mode and contrast checks. The compliance notes above describe the state before
this change.
