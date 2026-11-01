# Document styling review (macOS)

One-time task: turn the same eye the chrome review used on the mac
window onto the markdown text itself, against
`references/osx-markdown-visual-style-guide.md` and Apple's HIG, and
fix what does not hold up. Windows keeps its own document look where
the two platforms legitimately differ; the shared rhythm stays shared.

## Method

- Read the binding style guide and map every part of it to the code:
  faces, sizes, weights, spacing, colours, and the element table
  (headings, lists, tasks, links, code, quotes, tables, rules).
- Judge the real captures, not the source: the launch smoke's named
  states, plus pixel measurements for anything the eye fumbles (the
  dark appearance was read wrong once and the numbers corrected it).
- Where an element had no capture, extend the walk instead of
  guessing: `lower` and `end` now photograph the page below the fold
  on both platforms.

## Findings and outcomes

- The H1 and the H2 wore a hairline rule, a web convention neither
  the guide's treatment table nor its final standard asks for
  (hierarchy through size, weight and spacing, no added effects).
  Removed on both platforms; the air the rule sat in stays reserved,
  so the rhythm and the comparison's row pairing are unchanged. Done
  (fbff059).
- The system faces, the modest descending heading scale, semantic
  colours, the shape-carried task checkbox, the restrained quote,
  table and code block, and the paragraph-versus-section spacing all
  hold up; no change.
- theme.h told an old story about the mac drawing with the bundled
  Selawik copies; the engine draws with the system faces. Comment
  fixed in the same commit.
- Coverage: the captures only showed the top of the page. `lower` and
  `end` states added to both walks, both smoke scripts and the
  comparison (5ef6e95).

## Status

Lands with fbff059 (styling) and 5ef6e95 (coverage); the captures of
the new states and the parity run that follows are the oracle.
