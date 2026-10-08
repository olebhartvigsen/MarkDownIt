# AGENTS.md - Instructions for AI agents in the MarkDownIt workspace

## Language (FIRM RULE)

**Only English and Danish are allowed in this workspace and chat. No other
languages whatsoever.** This applies to every reply, every file, every
commit message, every PR description, every code comment, every doc.

No exceptions. Do not use German, Swedish, Norwegian, French, Spanish,
Chinese, or any other language for any purpose here. If a user writes to
you in a third language, reply in English or Danish.

Both English and Danish are fine; pick based on context (Danish for
casual or project talk, English for code comments and technical docs).

## Project overview

MarkDownIt is a native Windows markdown viewer. No Electron, no .NET
runtime, no external dependencies shipped. Single exe, opens .md files,
renders them with Direct2D/DirectWrite.

## Tech stack

- Language: C++17 (MSVC, static runtime /MT for zero-dep binary)
- UI: Win32 API + Direct2D 1.1 + DirectWrite (ships in Windows 10/11)
- Markdown parser: md4c release-0.5.3, vendored at third_party/md4c/
- Build: CMake 3.20+, Visual Studio 2022 Build Tools, Windows 10 SDK
- CI: GitHub Actions windows-2022, .github/workflows/build.yml

## Markdown style guide

Markdown authored or reviewed in this project should follow the Google
Markdown Style Guide:
https://google.github.io/styleguide/docguide/style.html

Key takeaways: use proper list syntax (1. for ordered, - for unordered),
not bold paragraphs with manual numbering. Use semantic markdown, not
visual hacks.

## Mandatory rules

- Humanizer skill is OBLIGATORY on all generated or reviewed text for use
  outside the chat (docs, README, commits, PR text). Load with
  skill_view(name='humanizer') and follow its 34 patterns.
- No em dashes in user-facing text. Use commas, periods, or parens instead.
- Do NOT commit or push without explicit permission from the user.
- Do NOT read or print secrets. Leave .env and credential files alone.
- Verify with `git status` / `git branch` before trusting workspace snapshots.
- All ribbon and menu work must follow references/ribbon-menu-guidelines.md.
  Read it before changing ribbon.xml or adding, moving, or renaming tabs,
  groups, or commands, and use it as the checklist when reviewing the menu.
- All text editing work must follow
  references/windows-standardtekstredigering-specifikation.md. Read it before
  changing the text model, caret, selection, mouse or keyboard input,
  clipboard, undo/redo, scroll, inline formatting (bold, italic, link),
  find/replace, Tab/Enter handling, or link activation. Treat its acceptance
  criteria as the test list for those areas, and keep behavior inside its
  scope rules (plain typing must never auto-format Markdown).
- All caret and paragraph model work must follow
  references/word-processor-guidelines.md. Read it before changing caret
  positioning, mouse hit-testing, Return/Shift+Return handling, paragraph
  splitting or merging, Backspace/Delete paragraph behavior, or the canonical
  position model. Its architectural rules are binding: one canonical caret
  representation shared by mouse, keyboard, and clipboard paths, and mouse
  hit-testing must never decide document semantics on its own. Treat its
  acceptance criteria (A-H) as the regression test list.
- All table editing work must follow
  references/word-processor-table-guidelines.md. Read it before changing the
  table document model, cell caret or selection, Tab/Shift+Tab cell
  navigation, Enter behavior in cells, or row and column operations. Tables
  are structural blocks: cell navigation follows the same canonical position
  model as the rest of the editor.
- All Office import/export work must follow
  references/office-import-export-spec.md. Read it before changing docx or
  PDF import, docx or PDF export, or the office bridge. Changes are measured
  against the CI oracle goldens (tools/office-oracle); goldens never change
  to match new output.

## Build and deploy

MSVC cannot run on WSL. The compile gate is CI (windows-2022). Use the
build-swap-verify loop from the wsl-windows-native-dev skill:

1. git push to main (triggers CI)
2. Poll gh run view until green
3. gh run download the MarkDownIt-x64 artifact
4. Copy to /mnt/c/Users/au19277/projekter/MarkDownIt/build/Release/
5. Verify MD5 matches CI artifact, file type is PE32+ GUI x86-64

Local g++ syntax checks are fine for parser.cpp (portable C++), but the
real compile gate is always CI.

## Release pipeline

Releases are cut by tag, the same way as win-dir-fan (FanFolder): bump the
version in src/app.rc, push to main, create a vX.Y.Z release there, and
.github/workflows/release.yml builds MarkDownItSetup-x64.msi and
MarkDownIt-x64.msix and attaches them to the release. The full procedure,
the Partner Center identity notes, and the post-build MSIX compliance checks
live in references/release-pipeline.md. Follow it whenever a release or a
Store submission is involved; its pitfalls (windows-2022 pinning,
Identity-scoped sed, app.rc bump before tag) are easy to rediscover the
hard way.

## Sandbox gotcha

The output layer once stripped angle brackets from file content display and
from write_file, which mangled markup files. A byte probe on 2026-10-08
showed write_file preserves angle brackets in the current container, so
markup files go through write_file directly now. If XML or RC output ever
looks mangled again, verify with a raw byte count of '<' before mass
writing, and fall back to execute_code with base64 encode/decode to get
raw bytes on disk. Verify with raw byte inspection, not the display.

## Tests

- tests/parser_smoke.cpp: parser smoke tests, run via MarkDownIt.tests.exe
- tests/gtest_lite.h: minimal test framework, no Google Test dependency
- CI test job builds with -DBUILD_TESTS=ON and runs the test exe

## Directory structure

| Path | Purpose |
|------|---------|
| src/ | C++ source (main.cpp, parser.cpp, dom.h, app, renderer) |
| third_party/md4c/ | vendored md4c parser (md4c.c, md4c.h, LICENSE) |
| tests/ | gtest_lite.h + parser_smoke.cpp |
| .github/workflows/ | build.yml CI pipeline, release.yml release pipeline (MSI + MSIX) |
| installer/ | WiX MSI project (MarkDownIt.wxs), License.rtf, MSIX manifest with committed PNG assets |
| tools/msix-icons/ | Renders the MSIX PNG asset set from the app icon SVG (resvg + PIL) |
| .hermes/plans/ | one-time task plans (viewer, typography, wysiwyg, svg/mermaid, mermaid flowchart closed, macos port, office implementation) |
| references/ | standing design docs: ribbon menu, text editing spec, caret model, table model, office import/export spec, dagre port, macos pipeline, release pipeline |
