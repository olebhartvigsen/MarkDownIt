# macOS-Style Markdown Display Guide

**Apple Human Interface Guidelines–aligned recommendations**

## Purpose and scope

Apple doesn’t prescribe a specific visual style or point-size scale for rendered Markdown. This guide defines a practical Markdown presentation style that follows Apple’s broader guidance for macOS: use the system’s typography and colors, keep content comfortable to read, and let people adapt the app to their workspace and preferences.

## 1. Design principles

- **Put the document first.** Keep controls and decoration from competing with the text.
- **Use familiar macOS typography and color.** Prefer system-provided styles and semantic colors over fixed font files and hard-coded color values.
- **Balance density with readability.** Mac displays are spacious, but text should still be easy to scan without excessive empty space.
- **Support personalization.** Let the window resize naturally and, where appropriate, let people adjust the reading font or size.

Apple’s macOS guidance emphasizes comfortable information density, resizable windows, and user choice of fonts and colors. ([Designing for macOS](https://developer.apple.com/design/human-interface-guidelines/designing-for-macos/))

## 2. Typography

Use the **system font through the platform’s font APIs**. This lets macOS select the appropriate system typeface and respond to platform and accessibility changes. For web-based renderers, use the system UI font family. Use a monospaced system font for code. Apple recommends system fonts and predefined text styles wherever possible; exact text-style sizes can vary by platform. ([Text display](https://developer.apple.com/documentation/technologyoverviews/text-display), [Fonts](https://developer.apple.com/documentation/technologyoverviews/fonts))

| Markdown role | Suggested macOS text style | Treatment |
|---|---|---|
| H1 (document title) | Large Title or Title | Largest heading; semibold or system-defined title weight |
| H2 (main section) | Title 2 | Clearly below H1; semibold |
| H3 (subsection) | Title 3 | Semibold, distinct from body text |
| H4 (smaller section) | Headline | Semibold |
| H5 (minor heading) | Headline or Subheadline | Smaller than H4; retain clear contrast with body |
| H6 (smallest heading) | Subheadline | Compact, but still distinguishable from body |
| Body text | Body | Regular weight and comfortable line spacing |
| Metadata and captions | Footnote or Caption | Secondary in hierarchy, but still legible |
| Inline and block code | Monospaced system font | Regular weight; code blocks may use a slightly smaller size than body text |

Treat these as **semantic roles**, not fixed point sizes. If a renderer needs explicit sizes, use a modest descending scale and test it in the actual macOS window size. Don’t force one point scale across all appearances, languages, or accessibility settings.

## 3. Reading width and spacing

Use the available window space thoughtfully. Keep prose from stretching so wide that it becomes hard to follow, but don’t force a narrow, fixed column on every Mac display. Let the window resize, and preserve enough side padding that text doesn’t sit against the edges.

Use spacing to reveal structure:

- Give paragraphs a consistent, modest separation.
- Put more space before a new major section than between paragraphs within one section.
- Keep list items visually grouped; increase indentation for nested levels.
- Keep related headings close to their content.
- Use section dividers only when they make a real change in topic easier to see.

These are renderer recommendations, not spacing values specified by Apple for Markdown.

## 4. Markdown element treatments

| Markdown element | Display treatment |
|---|---|
| **H1–H6 headings** | Apply the descending semantic text styles above. Use size, weight, and spacing to communicate hierarchy; hide `#` markers in reading view. |
| **Paragraphs** | Use the Body style, regular weight, and comfortable line spacing. Separate paragraphs with space rather than first-line indentation. |
| **Bold** | Apply semibold emphasis within a sentence. Avoid bolding whole paragraphs by default. |
| *Italic* | Use the system italic style for emphasis, titles, or terms. |
| ~~Strikethrough~~ | Use a thin strike through the text. Keep the text readable and use this treatment sparingly. |
| **Bulleted lists** | Use a clear bullet and a hanging indent, so wrapped lines align with the list text. Keep items close enough to read as one group. |
| **Numbered lists** | Align number markers consistently. Use the same hanging-indent and spacing logic as bulleted lists. |
| **Nested lists** | Indent each level consistently and make nesting clear without adding decorative boxes. |
| **Task lists** | Show a checkbox and checked state. Don’t rely on color alone to communicate completion. |
| **Links** | Use the system link color. Make keyboard focus visible; use an underline on hover or focus if needed to clarify interactivity. |
| **Inline code** | Use a monospaced system font. A subtle background tint can distinguish code from prose without overpowering it. |
| **Fenced code blocks** | Use a monospaced system font in a clearly separated area. Keep syntax coloring restrained and text selectable. |
| **Block quotes** | Indent the quoted text and use a subtle leading rule or other restrained cue. Keep contrast high enough for comfortable reading. |
| **Tables** | Use descriptive column headings and concise cell text. Follow the table guidance below; don’t make a static Markdown table look like an interactive control. |
| **Images** | Preserve the original aspect ratio and keep images within the document’s content area. Place captions close to their images and style them as secondary text. |
| **Horizontal rules** | Use a subtle separator with enough surrounding space to mark a section break. |
| **Footnotes** | Keep footnote text readable and visually subordinate to the main text. Make the return link or marker easy to locate. |

## 5. Tables

A Markdown table is usually **document content**, while a macOS table view is an **interface control**. Style a document table for reading; use native table behavior only when the table is meant to support interaction.

| Table part | Apple-aligned treatment |
|---|---|
| Column headings | Use short, descriptive labels. Apple recommends noun phrases and title-style capitalization for multicolumn table headings; omit ending punctuation. |
| Cell content | Keep text concise and easy to scan. Avoid truncating text when it would make the value unclear. |
| Alignment | Align text columns consistently. Right-align numeric values when that makes comparisons easier. |
| Row separation | Use restrained separators or spacing that helps readers track across columns. |
| Alternating row backgrounds | Consider them for wide, multicolumn tables when they help readers follow values across a row. Keep the effect subtle. |
| Wide tables | Preserve legibility as the window changes size. If the table is interactive, consider resizable columns; for document tables, avoid shrinking text until it becomes difficult to read. |

Apple’s macOS table guidance recommends concise content, descriptive headings, and considering alternating row colors for multicolumn tables. ([Lists and tables](https://developer.apple.com/design/human-interface-guidelines/lists-and-tables/))

## 6. Color and appearance

Use semantic system colors for primary text, secondary text, links, backgrounds, and separators. Don’t hard-code colors based on how they look in Light Mode: system colors adapt to appearance and accessibility settings. Keep link color consistent so it continues to signal interactivity. Don’t use color as the only way to convey meaning.

Check the display in both Light and Dark Mode, and with increased contrast enabled. ([Color](https://developer.apple.com/design/human-interface-guidelines/color), [Dark Mode](https://developer.apple.com/design/human-interface-guidelines/dark-mode))

## 7. Reading and editing views

In **reading view**, hide Markdown syntax markers and show the formatted document.

In **editing view**, show the source Markdown. Syntax markers can be visually quieter than the document text, while remaining readable and selectable. This is a product recommendation for Markdown apps, not a specific rule in Apple’s HIG.

## 8. GitHub document profile

The app draws documents with GitHub's markdown structure on top of the Apple
typography and colour rules above. The measurements, the platform mapping and
the deliberate deviations live in
[github-markdown-style.md](github-markdown-style.md).

The short version:

- Body 16px at 1.5, code 13.6px at 1.45, headings 2 / 1.5 / 1.25 / 1 / .875 /
  .85 em at 1.25.
- A paragraph keeps 1rem; a heading 1.5rem above and 1rem below; a rule 1.5rem
  on both sides. The gaps collapse to the larger margin.
- H1 and H2 carry a 1px rule. H6 is muted.
- Code blocks are borderless 6px cards with 1rem padding. Inline code is a 6px
  card with .2em and .4em padding.
- Tables are full 1px grids with 6px 13px cells, a white header and a stripe on
  every second body row.
- Links keep their underline at rest, and the mac keeps AppKit's semantic
  colours, so the text still follows Dark Mode.

## 9. Final standard

A macOS-style Markdown display should feel native because it uses system typography, semantic colors, and comfortable spacing, not because it adds visual effects. Keep the document readable as the window changes size, and make hierarchy clear through type and layout.

### Apple references

- [Designing for macOS](https://developer.apple.com/design/human-interface-guidelines/designing-for-macos/)
- [Text display](https://developer.apple.com/documentation/technologyoverviews/text-display)
- [Fonts](https://developer.apple.com/documentation/technologyoverviews/fonts)
- [Color](https://developer.apple.com/design/human-interface-guidelines/color)
- [Dark Mode](https://developer.apple.com/design/human-interface-guidelines/dark-mode)
- [Lists and tables](https://developer.apple.com/design/human-interface-guidelines/lists-and-tables/)