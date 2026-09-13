# macOS port of MarkDownIt: feasibility and architecture

Date: 2026-09-12
Status: analysis only, no code changed
Scope: can a pipeline be added so MarkDownIt also builds for macOS, with no
commitment to implementation

## Verdict: go

The port is feasible, and it leaves the Windows app alone. The plan is a
second native UI layer, Cocoa + Core Graphics + Core Text, built on the part
of the code that is already platform-free. 38% of the source (about 7,400 of
19,500 lines) transfers unchanged, another 10% (about 2,000 lines) needs only
small per-file branches, and the remaining 52% is the Windows UI shell plus
the Direct2D/DirectWrite bindings that get rewritten for macOS.

Two facts make this affordable. The mermaid engine was built with a measure
seam (`MeasureFn`, `SeqMeasureFn`): the DirectWrite implementation lives in
one file (`measure_dwrite.cpp`) and layout math never calls it directly, so
macOS gets a Core Text implementation behind the same seam. And the test
suite is already portable: 221 tests, all pure C++, zero Windows symbols,
with 32 committed mermaid golden JSON files as the geometry oracle.

Everything else is a dead end. Direct2D, DirectWrite, WIC, and the Ribbon
framework exist only in the Windows SDK and system DLLs, with no open
reimplementation, and the ribbon import library is synthesized from Windows
runtime DLLs at build time. Cross-compiling the current tree to macOS is not
possible; only a native macOS build on macOS runners is.

## Verified source split

All numbers below are from the working tree on branch
feat/mermaid-flowchart (f5896d5), counted with `wc -l` over all
`src/*.cpp` and `src/*.h` (19,500 lines total). The classification walks
direct includes and then the include chain, so a file counts as bound when
one of its headers pulls in a Windows type.

| Bucket | Files | Lines | Share | Action |
|--------|-------|-------|-------|--------|
| Portable core, transfers as-is | dom, textbuffer, caret, editcontroller, undostack, formatting, autoformat, inputfilter, svgtext, svgtypes, resource, all of mermaid/ except measure_dwrite | 7,413 | 38% | copy into the mac target unchanged |
| Shared code, small mac branch | parser, clipboard, settings, filewatch, fileassoc, theme, crash_trace | 2,002 | 10% | guard the Windows piece, keep the rest |
| UI-bound, rewritten for macOS | main, app, renderer, renderer_pie, renderer_sequence, colortext, imagehelper, layoutcache, navigation, welcomescreen, svgdoc, ribbon, measure_dwrite | 10,085 | 52% | new src/macos/ UI layer |

The bucket labels hide three judgment calls, stated so nobody is surprised
later:

1. **parser.cpp** is 95% portable parsing code, but `ParseMarkdown` wraps
   the parse in Win32 structured exceptions (`__try`/`__except
   (EXCEPTION_EXECUTE_HANDLER)`) and writes a crash log with `CreateFileA`
   when one trips. Both constructs are MSVC/Windows-only, so the guard goes
   behind `#ifdef _WIN32` and the portable body stays. This is the only
   shared file with MSVC-only syntax.
2. **theme.h** includes `<d2d1.h>` and types every color as `D2D1_COLOR_F`,
   plus two `const wchar_t*` font names. The fix is small (a 4-float RGBA
   struct, `std::string` font names) but it is not a copy-as-is file.
3. **navigation.cpp** and **layoutcache.cpp** hold portable text-movement
   and hit-testing logic, but their types are DirectWrite-bound:
   `navigation.h` includes `layoutcache.h`, which includes `<dwrite.h>`.
   They cannot compile without that header, which is why they sit in the
   rewrite bucket; a stub header (below) may let them build almost as-is.

## Windows-only surface inventory (deliverable 1)

| Windows piece (where it lives) | macOS replacement |
|-------------------------------|--------------------|
| Message loop and `wWinMain` (main.cpp) | NSApplication event loop (main_mac.cpp) |
| `RegisterClassW` / `CreateWindowExW` / HWND (app.cpp) | NSView subclass |
| Direct2D `ID2D1HwndRenderTarget` (app.cpp, renderer.cpp) | Core Graphics: CGContext in NSView drawRect |
| DirectWrite `IDWriteTextLayout` (renderer, layoutcache, colortext, welcomescreen) | Core Text: `CTLine` + hand-rolled word wrap |
| `DWRITE_TEXT_RANGE` styling, `IDWriteTextRenderer` effects | attributed runs; per-run CG drawing state |
| Hit testing (`u16ToSrc` mapping in layoutcache) | `CTLine` char hit testing + existing mapping |
| WIC image decode + WinHTTP fetch (imagehelper.cpp) | NSImage / NSBitmapImageRep + NSURLSession |
| Win32 clipboard (clipboard.cpp `#ifdef` half) | NSClipboard + custom pasteboard type |
| Registry settings (settings.cpp, Reg* at HKEY_CURRENT_USER) | JSON in ~/Library/Application Support/MarkDownIt/ |
| Ribbon, UICC.xml, app.rc, manifest (ribbon.cpp) | NSMenu (app menu + document menu) |
| MessageBoxW, GetOpenFileNameW, GetSaveFileNameW | NSAlert, NSOpenPanel, NSSavePanel |
| ShellExecuteW for links | NSWorkspace.nsopenURL |
| File association registry keys (fileassoc.cpp) | .app bundle Info.plist document types |
| File watcher thread (filewatch.cpp) | keep the 500 ms poll, swap thread primitives |
| DPI (SetProcessDPIAware, GetDpiForWindow, app.cpp) | keep DIP layout, CG applies display scale |
| app.rc icon resources | .icns in the bundle |
| Segoe UI, Cascadia Mono | system stack: SF Pro / Helvetica, Menlo / Monaco for code |
| `std::wstring` (2-byte wchar) | none in mac code; wchar_t is 4 bytes on macOS |

Non-GUI Windows dependencies: the parser and editor core have none. md4c
(vendored, MIT, release-0.5.3) is portable C with no Windows references, and
the mermaid golden tests already inject a pure-C++ stub measurer.

## Porting effort per area (deliverable 2)

| Area | Effort | Why |
|------|--------|-----|
| Parser, editor engine, undo, formatting, autoformat, input filter | none, transfers | pure C++, no Windows deps |
| Mermaid layout pipeline | none for layout, medium for the mac measure impl | MeasureFn seam; CTLine measurement and wrapping |
| SVG text extraction | none, transfers | svgtext.* is portable and covered by tests |
| Window shell, event loop | small | Cocoa boilerplate |
| Renderer: block types, inline styles, tables, code, quotes, links | large | DWrite bindings swapped for CT; table measurement redone against CT |
| Images: local, remote, 32 MB cap | medium | NSImage + NSURLSession, same queue logic |
| Hit testing and caret | medium | CTLine hit testing plus the existing mapping |
| Settings, recent files, welcome screen | medium | JSON store; welcome screen is CG/CT drawing |
| Menus, dialogs, drag-drop, file association | small | thin Cocoa wrappers |
| Packaging (.app, icon) | small | bundle layout, icns |

Total: weeks of work, sized in the 8-task plan referenced below. The single
biggest risk is line breaking: Core Text wraps nothing
(`CTTypesetterCreateLine` produces single lines), so a word-based wrapping
loop becomes part of the mac text engine. That is straightforward code, but
it is where long words, mixed scripts, and CJK live, so it gets visual QA
rather than free coverage.

### Option (a) Cocoa + Core Text / Metal, or equivalent

| Option | Verdict |
|--------|---------|
| Cocoa + Core Graphics + Core Text | Recommended. Native, zero shipped deps, mirrors Win32 + D2D + DWrite one-to-one. CTLineDraw renders directly into a CG context. |
| Skia as canvas | Viable, wrong fit: needs a second renderer or a Windows retrofit, plus a large vendored dependency. Only if cross-platform canvas becomes a product goal. |
| Qt / wxWidgets | Out. Qt brings licensing and runtime baggage that break the no-dependency promise. |
| Electron / Tauri | Out on purpose. The product exists to avoid them. |
| Metal | Considered and rejected. Metal is the GPU/3D API; Direct2D never used GPU features, and a 2D document renderer draws with CG, not Metal. |

### Option (b) cross-platform layer

Rejected as the primary path for the same reason the Windows build is
native: the project promise is one native exe with no runtime and no shipped
deps. Skia is the only layer with a real 2D story, and it pays for itself
only if a canvas abstraction becomes a product goal. Recommended structure
either way: a `src/macos/` tree behind three narrow seams (TextEngine,
RenderTarget, AppShell) with the shared core untouched.

## CI options (deliverable 3)

Native GitHub Actions runners only. Cross-compilation is ruled out by the
SDK-only GUI stack, by `#pragma comment(linker, ...)` in main.cpp, and by
the ribbon lib that CI synthesizes from Windows DLLs with dumpbin and lib.

Runner choice, current as of September 2026: **macos-15-arm64**. The
macos-14 images are deprecated (announcement July 6, 2026) and unsupported
after November 2, 2026, so do not start on them. macos-26 (arm64) is the
current macos-latest and a fine forward option; pin one label either way so
toolchain churn does not move the parity baseline. The images ship Xcode 26
with clang, CMake 4.x, and ninja, so the "Mac toolchain" step is nothing
more than choosing the default compiler.

The configure step succeeds on macOS today: every Windows-only CMake block
(UICC, rc.exe, SDK lib discovery) sits inside `if(MSVC)` and is skipped.
Building the default target is the problem, so the test job builds
`MarkDownIt.tests` explicitly until the APPLE branch exists.

Minimal macOS test job, in the build.yml style (checkout, configure, build,
test, artifact):

```yaml
  test-macos:
    name: Tests (macOS, Apple silicon)
    runs-on: macos-15-arm64
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Configure
        run: cmake -B build-native -DBUILD_TESTS=ON

      - name: Build tests
        run: cmake --build build-native --target MarkDownIt.tests

      - name: Run tests
        run: ./build-native/MarkDownIt.tests
```

Bundle job, added once the APPLE branch lands (Task 2 of the plan), shaped
after the existing Windows job including the artifact upload:

```yaml
  macos:
    name: Build (macOS arm64)
    runs-on: macos-15-arm64
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Configure
        run: cmake -B build-macos -DCMAKE_BUILD_TYPE=Release

      - name: Build .app bundle
        run: cmake --build build-macos --config Release

      - name: Verify binary type
        shell: bash
        run: file build-macos/MarkDownIt.app/Contents/MacOS/MarkDownIt

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: MarkDownIt-macOS
          path: build-macos/MarkDownIt.app
```

## Blockers (deliverable 4)

Missing macOS APIs: none for the chosen stack. Core Graphics, Core Text,
NSURLSession, NSClipboard, and NSMenu all ship with macOS. The missing
surfaces are the Windows ones being replaced, not mac ones being searched
for.

wchar_t and MSVC syntax: macOS wchar_t is 4 bytes (Windows: 2), so no mac
code may use `std::wstring`. The shared core already uses `std::string` and
`std::u32string`; `wstring` appears only in theme.h font names and in the
Windows-only fileassoc.cpp. The one MSVC-only construct in a shared file is
the parser.cpp SEH guard above, and it is a two-line change.

Licensing and runtime: none. Cocoa/CG/CT are part of macOS, md4c is MIT,
and nothing extra gets shipped. Signing is the only cost: an unsigned build
shows the "developer cannot be verified" warning and needs right-click open.
That is fine for personal use; public distribution later needs an Apple
Developer certificate (about USD 99 per year) or a Homebrew cask built from
a signed binary.

Test strategy: 221 tests in 22 files, all pure C++ (zero Windows symbols in
tests/), all native-run on macOS once the two guards below land. This
includes the mermaid golden comparisons, so layout drift is caught without
Windows. The UI itself has no headless test path; SVG and mermaid use the 32
committed golden JSON files as the geometry oracle, and final visual QA
compares against the Windows app by eye.

CI cost: macOS runners bill at 10x the Linux rate, the same cost class as
the existing windows-2022 job. Fine for push and PR builds.

The test gate needs exactly two changes before the suite runs on mac:

1. tests/stubs/dwrite.h: define the interface types layoutcache.h
   references (IDWriteTextLayout and friends). layoutcache.cpp itself makes
   zero DirectWrite calls (verified), so names alone suffice. Add the stub
   dir to the test include path only when NOT WIN32.
2. parser.cpp: put the `__try`/`__except` crash-log block behind
   `#ifdef _WIN32`.

Verified non-issues: clipboard.cpp compiles on mac (Win32 half guarded,
clipboard.h declares `typedef void* HWND`), clipboard_test only calls the
portable escape/convert helpers, mermaid_measure_test uses the portable
StubMeasure, and the test compile definitions (UNICODE, NOMINMAX, ...) are
harmless off Windows.

## Recommendation (deliverable 5)

Go, staged. Preferred architecture: Cocoa + Core Graphics + Core Text, zero
shipped dependencies, a mirror of the Windows design. The task breakdown is
the 8-task plan in `.hermes/plans/2026-09-12_203000-macos-port.md`, with
acceptance criteria per task. Rough shape:

1. Native test gate: stub header, parser guard, macos test job. Small, and
   it makes every later shared-core change verified on both platforms.
2. Skeleton window that opens a .md and renders text, proving the three seams.
3. Renderer parity: tables, code blocks, quotes, lists, inline styles,
   images, SVG, mermaid painters. The long pole; golden JSON is the oracle.
4. Editor parity: caret, selection, edits, undo, formatting, clipboard.
   Mostly wiring, because the editor engine is portable.
5. Platform polish: menus, settings JSON, recent files, welcome screen,
   file association, zoom.
6. Packaging: .app bundle and CI artifact.
7. Parity pass and cleanup.
8. Decide whether to fold the seams back into the Windows renderer later.

Start with task 1 (the test gate): it is cheap, unblocks a native CI signal
for the whole duration of the port, and would have caught every issue the
rest of the plan in this document is built on.

## Verification ceiling, stated plainly

Everything above was verified by reading the tree on this branch: file
inventory, Windows-symbol grep, include-chain walk, CMake test target and
link list, test count, golden fixture count, and the runner facts from the
GitHub Actions image announcements. This session ran on Linux with no C++
toolchain and no Cocoa headers, so nothing here proves that a
renderer_mac.cpp compiles. The mac UI layer compiles only on macOS: on a
Mac with Xcode command line tools, or on a macos-15-arm64 runner. The
shared core is verifiable anywhere, and specifically on the macOS test job
once the two guards above land.

## Decisions to confirm before starting

1. Native Cocoa + CG + CT as the stack (recommended above).
2. Minimum macOS version. Suggest 12 (Monterey, 2021) so the API surface is
   stable and Apple silicon is covered.
3. Distribution: personal unsigned build first, signed or cask later.
4. Who runs the mac iteration loop: you on the Mac, CI, or both
   (recommended: both, CI as the compile gate).
