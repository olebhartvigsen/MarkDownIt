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

## Mandatory rules

- Humanizer skill is OBLIGATORY on all generated or reviewed text for use
  outside the chat (docs, README, commits, PR text). Load with
  skill_view(name='humanizer') and follow its 34 patterns.
- No em dashes in user-facing text. Use commas, periods, or parens instead.
- Do NOT commit or push without explicit permission from the user.
- Do NOT read or print secrets. Leave .env and credential files alone.
- Verify with `git status` / `git branch` before trusting workspace snapshots.

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

## Sandbox gotcha

The output layer strips angle brackets from file content display and from
write_file. Write C++ files via execute_code with base64 encode/decode to
get raw bytes on disk. Verify with raw byte inspection, not the display.

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
| .github/workflows/ | build.yml CI pipeline |
| .hermes/plans/ | implementation plan (14 tasks) |
