# Modern Windows print integration, with a shared core

> **For Hermes:** Implement task by task. Each task ends with its own
> verification step. The compile gate is CI (windows-2022); the manual proof
> is the user's machine, because no CI step can open a print dialog.

**Goal:** Ctrl+P on Windows opens the modern print dialog with a live page
preview fed by MarkDownIt, through the Windows.Graphics.Printing contract,
with every piece that is not genuinely Windows-native living in shared code.

**Architecture:** The shared core already owns the page model
(`src/layout/print_pages.*`). This plan adds one shared painter
(`PaintBandPage`, one placement rule, one player) and one shared
page-from-description helper, then builds the Windows contract plumbing on
top: a classic COM source class implementing `IPrintDocumentSource`,
`IPrintDocumentPageSource` and `IPrintPreviewPageCollection`, a DXGI preview
path, and a command-list print path. macOS keeps its `NSPrintOperation`
unchanged; both ports then feed a preview from the same band and placement
rules.

**Tech stack:** C++17, C++/WinRT headers (shipped in the Windows 10 SDK,
linked against `windowsapp.lib`, OS component, nothing new to ship), Direct2D
1.1 print control, D3D11 for preview surfaces, the existing seams
(`RenderTarget` via `D2DTarget`, `TextEngine` via `DWriteEngine`), md4c parse
on the contract thread.

---

## Research findings (the contract, verified)

The message "denne app understøtter ikke vis udskrift" in the dialog comes
from the dialog itself: a Win32 app that calls `PrintDlg` gets the unified
modern dialog on Windows 11 22H2+, and the preview pane only fills when the
app prints through the WinRT print contract. Sources, all verified this
evening:

- `IPrintManagerInterop` (SDK `printmanagerinterop.h`): the desktop entry.
  `GetForWindow(hwnd, IID_IPrintManager, ...)` gives the `PrintManager` for a
  plain Win32 window; `ShowPrintUIForWindowAsync(hwnd, IID, &asyncOp)` opens
  the dialog. Desktop apps only, Windows 8+. No package identity needed.
  <https://learn.microsoft.com/en-us/windows/win32/api/printmanagerinterop/nf-printmanagerinterop-iprintmanagerinterop-getforwindow>
- Working unpackaged reference implementation without XAML/WPF/WinForms
  (C#, from 2024): smourier/XpsPrintSamples, project
  `CustomPrintDocumentAot`. Its `MainWindow.cs` shows the whole flow on a
  raw `HWND`; its `BasePrintDocument.cs` declares the two contract
  interfaces with GUIDs; its `PdfPrintDocument.cs` implements preview
  surfaces and the Direct2D print path.
  <https://github.com/smourier/XpsPrintSamples>
- The Windows 8 Direct2D printing walkthroughs (still accurate for the call
  shape): "Quickstart: Add printing to your app (DirectX)",
  "How to format content for Direct2D printing".
  <https://learn.microsoft.com/en-us/previous-versions/windows/apps/jj863493(v=win.10)>
- Preview: `IPrintPreviewDxgiPackageTarget` with
  `SetJobPageCount(PageCountType, count)`, `DrawPage(jobPage, IDXGISurface*,
  dpiX, dpiY)` and `InvalidatePreview()`. Win32 classic COM, GUID in the
  windows crate as `Win32::Graphics::Printing`.
  <https://learn.microsoft.com/en-us/previous-versions/jj553554(v=vs.85)>
- Final pages: `IPrintDocumentPackageTarget` (SDK, winspool side) fed via
  `ID2D1PrintControl::Create/1AddPage(commandList, pageSize, ticket)` from
  `d2d1_1.h`. AddPage converts a closed D2D command list into the print
  subsystem's fixed page.
  <https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/nf-d2d1_1-id2d1printcontrol-addpage>

### The exact ABI we implement

A classic COM class (no WinRT base needed):

```
IPrintDocumentSource        {dedc0c30-f1eb-47df-aae6-ed5427511f01}  IInspectable marker, no methods
IPrintDocumentPageSource    {a96bb1db-172e-4667-82b5-ad97a252318f}  IUnknown:
                              GetPreviewPageCollection(IPrintDocumentPackageTarget*, IPrintPreviewPageCollection**)
                              MakeDocument(IInspectable* printTaskOptions, IPrintDocumentPackageTarget*)
IPrintPreviewPageCollection {0b31cc62-d7ec-4747-9d6e-f2537d870f2b}  IUnknown:
                              Paginate(uint32 currentJobPage, IInspectable* printTaskOptions)
                              MakePage(uint32 desiredJobPage, float width, float height)
```

Flow, confirmed by the sample:

1. App: activation factory of `Windows.Graphics.Printing.PrintManager` as
   `IPrintManagerInterop` -> `GetForWindow(hwnd)` -> keep `PrintManager`,
   subscribe `PrintTaskRequested`.
2. Ctrl+P: `ShowPrintUIForWindowAsync(hwnd)`.
3. `PrintTaskRequested`: `args.Request().CreatePrintTask(title, handler)`;
   in the handler `args.SetSource(ourSource)`; also hook `PrintTask.Completed`.
4. The dialog calls `GetPreviewPageCollection`: we
   `QueryInterface` the package target for
   `IPrintPreviewDxgiPackageTarget`, we report page count, and we return
   ourselves as the preview collection.
5. The dialog calls `Paginate(options)` whenever the user changes paper,
   orientation and so on: reread the page description, rebuild the bands,
   `SetJobPageCount`.
6. The dialog calls `MakePage(n, width, height)`: render band n into a
   DXGI surface, `DrawPage(n, surface, 96, 96)`.
   `desiredJobPage == 0xFFFFFFFF` (`JOB_PAGE_APPLICATION_DEFINED`) starts a
   new preview pass; treat it as page 1.
7. The user presses Print: `MakeDocument(options, packageTarget)`: build a
   `ID2D1PrintControl` on the package target, then per band: command list,
   `BeginDraw`, paint, `EndDraw`, `Close`, `AddPage`, finally
   `printControl->Close()`. Swallow `ERROR_PRINT_CANCELLED`
   (`0x80070000 | 122`). Optional progress: advise an
   `IPrintDocumentPackageStatusEvent` sink on the package target
   connection point.
8. `PrintTask.Completed` fires (Submitted, Canceled, Abandoned, Failed);
   release everything.

### Threading (from the DirectX quickstart)

The contract callbacks arrive off the UI thread, and preview and printing
can run concurrently with the window. Rules for our implementation:

- One D2D1 factory with `D2D1_FACTORY_TYPE_MULTI_THREADED` created per
  print job, used only by the contract. The main window keeps its own
  single-threaded factory; nothing is shared across threads.
- The contract thread never touches the live window renderer, the
  document controller or any UI state. The source owns an immutable
  snapshot: the markdown text (source string), the style, the settings that
  affect layout. It reparses with the shared parser and lays out with a
  fresh `DWriteEngine` on its own thread. Parse, layout, paginate and paint
  are all shared code and are all per-call stateless, so they are thread
  safe by construction.
- D3D11 for preview: `D3D11CreateDevice(HARDWARE, BGRA)` first, `WARP`
  fallback (RDP, locked-down machines). Surface: B8G8R8A8, bind
  SHADER_RESOURCE | RENDER_TARGET, one surface reused while size is
  unchanged.

---

## The shared pieces (built first, both ports exercise them)

These tasks are the "fælles kodebase" requirement. Nothing in the contract
task may duplicate logic that can live here.

### Task 1: `PlayWithOrigin` in the shared player

**Files:** Modify `src/layout/display_player.h`, `src/layout/display_player.cpp`.
Test: `tests/display_player_test.cpp` (both runners already).

Add `display::PlayWithOrigin(const DisplayList&, RenderTarget*, PointF dl)`
that shifts every item's geometry by `dl` and plays the result. One loop
inside the player, one implementation. This is what makes the placement a
player concern instead of each target's private transform dance.

**Step 1:** failing test: build the standard 3-item fake list, play it with
origin (37.5, -12), assert the recording target saw shifted coordinates.
**Step 2:** implement (translate every item in `Play`'s walk; the player
already computes `ItemBounds`, so the shift is one addition per item).
**Step 3:** both test targets green locally (portable g++ chain, see
scratch printpdf-build precedent).

### Task 2: `layout::PaintBandPage`, one shared page painter

**Files:** Modify `src/layout/print_pages.h`, `src/layout/print_pages.cpp`.
Test: new `tests/print_paint_test.cpp`, registered in both test targets in
`CMakeLists.txt`.

```cpp
namespace layout {
// One band as one page, played through any render target. The placement
// is PlacementForBand, the one rule; callers hand in the sheet origin in
// seam units (the page's own margin), and the player shifts the items.
void PaintBandPage(display::RenderTarget& target, const DocumentLayout& laid,
                   const PrintPage& page,
                   const std::vector<PrintBand>& bands, size_t index);
}
```

Body: `PlayWithOrigin(PageItems(laid, bands[index]), &target,
PlacementForBand(page, bands[index]))` (adapt to whatever
`PlacementForBand` returns; if it is a scalar y-offset, add the x from the
page; keep the signature honest to the existing rule).

**Test:** with the fake engine and a recording target, a laid document of
known geometry, assert the item set and coordinates of page 1 and page 2.
Reuse `tests/print_pages_test.cpp` fixtures.

### Task 3: both existing painters move onto it

**Files:** Modify `src/print_win.cpp` (the DC fallback path), `src/print_pdf.cpp`.

- `print_win.cpp`: per page, drop the manual placement from the transform
  (keep the printer-dpi scale from the 2026-11-19 fix), call
  `PaintBandPage` with the DC-backed `D2DTarget`.
- `print_pdf.cpp`: replace its private placement with `PaintBandPage`
  (its pdfio target keeps its y-flip and points conversion, both of which
  stay inside that target).

**Verification (must be zero-change):** run the portable suite; the
`print-pages` QA numbers, `print_pdf_test` digests and all goldens must be
byte-identical. If any number moves, the placement rule was implemented
twice; fix it before Task 4.

### Task 4: `PrintPage` from a page description

**Files:** Modify `src/layout/print_pages.h`, `src/layout/print_pages.cpp`.
Test: extend `tests/print_paint_test.cpp` or `tests/print_pages_test.cpp`.

```cpp
// From a dialog's description (paper and printable area, seam units for
// DIP-based callers): the PrintPage the shared pipeline paginates on.
PrintPage PrintPageFromDescription(float pageWidth, float pageHeight,
                                   float imageableX, float imageableY,
                                   float imageableW, float imageableH);
```

The margin is the distance from the sheet edge into the imageable area
(see `PrintPage`'s existing margin rules; small sheets shrink the margin
rather than eat the page). Windows converts a `PrintPageDescription` into
this struct; nothing else in shared code learns that the description came
from WinRT. Test: A4, a cut-down tray, a landscape sheet.

**Step:** tests first, both runners green, then `git status` clean of
anything else.

---

## The Windows contract (new code, thin, all painting via Task 2)

### Task 5: the ABI header

**Files:** Create `src/print_contract_abi.h` (Windows exe only).

Hand-declared classic COM, in vtable order above, plus:

- `IPrintPreviewDxgiPackageTarget` (`DrawPage`, `SetJobPageCount`,
  `InvalidatePreview`) and `PageCountType` (Final, Intermediate, AppDefined).
  If the installed SDK has it under another header, prefer the SDK and
  delete the hand copy; verify against
  <https://microsoft.github.io/windows-docs-rs/doc/windows/Win32/Graphics/Printing/struct.IPrintPreviewDxgiPackageTarget.html>
- `IPrintDocumentPackageTarget` (from the SDK's
  `DocumentPackagePrint.h`/XPS printing headers if present, else declared:
  `GetPackageTarget(GUID type, IID iid, void**)`, `Cancel()`, plus its
  `IConnectionPointContainer` use for the status event).
- Cross-check every GUID against the sample source and the Rust windows
  crate before first push; put the source of each GUID in a comment.

Compile-only task: it must build in CI (Task 8's push) and drag in no new
dependency beyond the Windows SDK.

### Task 6: `WindowsPrintSource`, the contract class

**Files:** Create `src/print_winrt.h`, `src/print_winrt.cpp`. Register the
.cpp in the Windows exe list in `CMakeLists.txt` (beside `print_win.cpp`)
and in `cmake/sources.cmake` on the Windows-only surface.

A single classic COM class implementing the three interfaces of Task 5.
State, all owned by the contract thread:

- Inputs (set once at creation, immutable): the markdown source text, the
  style, title, base directory (for future images).
- Lazy, rebuilt by `Paginate`: `PrintPage`, `DocumentLayout` (from a fresh
  engine+parser on this thread), `std::vector<PrintBand>`, the D3D11/D2D
  device chain.
- Runtime: `IPrintPreviewDxgiPackageTarget*`, current surface and size.

Behaviour:

- `GetPreviewPageCollection`: QI the package target for the DXGI preview
  target; build the device chain; return self.
- `Paginate(currentJobPage, options)`: read the page description through
  C++/WinRT (`PrintTaskOptions::GetPageDescription`), convert with Task 4's
  helper, parse and lay out (shared), paginate, `SetJobPageCount(FinalPageCount,
  bands.size())`. Since the page count is known before the first page is
  drawn, it is always final.
- `MakePage(n, w, h)`: JOB_PAGE_APPLICATION_DEFINED maps to 1. Ensure the
  surface for (w,h) (w and h are DIPs; create the texture at
  `w x h` pixels since DrawPage is told 96 dpi). Draw band n:
  `CreateBitmapFromDxgiSurface`, `SetTarget`, `BeginDraw`, `PaintBandPage`
  (Task 2) with white page fill first, `EndDraw`, then
  `DrawPage(n, surface, 96, 96)`.
- `MakeDocument(options, packageTarget)`: description -> helper -> layout
  -> bands, then `ID2D1PrintControl` via the device's factory
  (`D2D1_PRINT_CONTROL_PROPERTIES{rasterDPI = printer dpi from the
  description}`), per band a command list with `PaintBandPage` inside,
  `AddPage(commandList, pageSizeDip, nullptr)`, `Close()`. Swallow cancel.

### Task 7: entry, fallback, app wiring

**Files:** Create `src/print_winrt_entry.h` (used by app.cpp), or fold into
`print_winrt.h`. Modify `src/app.cpp` (`Print()`), `src/app.h`.

- One registration per window (on first Print, or at `EnsureRenderer`
  time): activation factory -> `GetForWindow` -> keep `PrintManager` alive
  as a member; subscribe `PrintTaskRequested` (create task titled like
  today's caption, hook `Completed`).
- `AppWindow::Print()` builds the source from a text snapshot
  (`DocumentController` source string), then
  `ShowPrintUIForWindowAsync(hwnd)`.
- Fallback: any failure in the modern path (activation, factory,
  `ShowPrintUIForWindowAsync` returning false, MarkDownIt launched with
  `MARKDOWNIT_NO_MODERN_PRINT=1`) falls back to today's
  `print_win::PrintDocument` (PrintDlg + DC). The fallback is also the
  Windows 8/10-era shape, though our floor is Windows 10.
- The QA walk (`MARKDOWNIT_QA_STATES=print-pages`) builds layout straight
  from the document and is untouched; the smoke assertions stay green.

The source never reads window state after creation: a document edited
while the dialog is open prints the snapshot taken at Ctrl+P (Word behaves
the same way; note it in printing.md).

### Task 8: status, errors, UI polish

- `IPrintDocumentPackageStatusEvent` sink for progress and completion
  (advised in `MakeDocument`, unadvised after); failures raise the same
  "MarkDownIt - Udskrift" MessageBox as today, through the app thread.
- `PrintTask.Completed` maps Submitted/Canceled/Abandoned/Failed to a
  short status line; cancel is silence, not an error (book rule: a
  cancelled panel is nothing at all).

---

## Ports, docs and CI

### Task 9: docs and ledger

- `references/printing.md`: rewrite the Windows sections. The contract and
  both render paths (DXGI preview, command list print) replace the "no
  preview" paragraph written this evening; the fallback stays documented;
  the paper difference changes meaning: with the contract the preview
  follows the dialog's chosen paper (Paginate rebuilds), so the old
  A4-versus-printer-paper difference applies only to the fallback.
- `references/platform-parity.md`: the Printing row: Windows feeds a
  preview into the system dialog through the contract; mac previews inside
  AppKit's panel; the page model, the paginating, `PaintBandPage` and the
  print-path PDF writer are shared. Note the Windows-only suites
  (`print_winrt` has none if Task 4 and Task 2 tests carry the logic;
  if a Windows-only test appears, it needs a row here).
- This file gets an execution note per task (house rule for plans).

### Task 10: CI and the user's machine

- `tools/check_alignment.sh` green locally before every push.
- One push per milestone (shared painter tasks separate from the contract
  tasks, so a red Windows run never traps shared work): run on
  windows-2022 + macOS runner, 12/12 green.
- Build-swap-verify the exe per the wsl-windows-native-dev skill, then the
  user opens Ctrl+P and sees the preview; the acceptance is the user's
  screenshot of a fed preview pane, plus a paper print and a Microsoft
  Print to PDF pass.

---

## Acceptance criteria

1. Ctrl+P on Windows 11 shows the modern dialog with our pages rendered in
   its preview pane, updating when paper or orientation changes.
2. Print and Microsoft Print to PDF produce the same pages the preview
   showed, with the 96-dpi print scale rule from 2026-11-19 holding (no
   multiply-too-large fonts).
3. Cancel is silence; a failed job says so instead of dying quietly.
4. The shared suite (fake engine) covers placement and pagination helper
   on both runners; all existing print tests and QA numbers unchanged.
5. macOS build, tests and behaviour unchanged; the parity ledger says why
   the Windows dialog path differs.
6. `MARKDOWNIT_NO_MODERN_PRINT=1` forces the classic flow; a broken
   activation falls back on its own.

## Risks and open items

- GUID accuracy for the three contract interfaces: the sample is a working
  2024 proof, the cross-check against the Rust crate and the SDK is a
  checklist item in Task 5; a wrong GUID fails fast (the dialog simply
  never calls us), which is visible in one manual test.
- C++/WinRT headers with `/MT`: header-only consumption from the Windows 10
  SDK plus `windowsapp.lib`, both OS components; risk is build flags on
  the CI image only, caught on the first push.
- GDI-only printers: the contract renders to XPS-based targets and the
  print pipeline converts; nothing for us to do, but the fallback exists if
  a corner case appears.
- Preview cost: a band is laid out once per Paginate and painted per
  MakePage; large documents paginate identically to the QA walk, so
  performance is the same class as the existing print path.
- Windows Apps wie Win10: dialog look differs; preview still works. Pessimistisk estimate overall about double the realistic one (CI turnarounds).

## Current state before this plan

This evening already removed the interim `print_preview_win.*` window
(worktree uncommitted: 8 files). That removal stands on its own; the modern
contract replaces it with the native preview instead of a home-made window.
If the user prefers, the removal commits first, then this plan lands on
top.

## Execution log

- **Committed first:** d063d1a removed the interim preview window (the 8
  files above); CI 44624390356 green 12/12.
- **Task 1, `PlayWithOrigin`:** in `display_player.h/.cpp`. One Translate
  overload per item alternative (no generic fallback, the same
  compile-guard as ItemBounds), a shifted copy played by the untouched
  walk; the unshifted path is bit-identical by construction and the
  player's goldens stayed green.
- **Task 2, `PaintBandPage`:** in `print_pages.h/.cpp`, the placement and
  the player in one call for any seam target.
- **Task 3, deviation on the pdfio half:** the DC driver now paints through
  PaintBandPage (scale-only transform, identical float association to the
  old Scale*Translation, so the print path is unchanged), but the pdfio
  writer KEEPS its placement inside MapX/MapY. Reason: pdfio writes
  coordinates at `%.6f` and the writer's stream bytes are golden-backed;
  the player's shift is a float round-trip of the placement and moves the
  sixth decimal, so the zero-change proof rejects the swap. The header
  comment on PaintBandPage says the same. The modern contract's preview,
  command list and final paths all use PaintBandPage.
- **Task 4, `PrintPageFromDescription`:** in `print_pages.h/.cpp`; the
  margin is the widest edge around the imageable area, floored by the DC
  rule's inset (2 cm, shrinking to 15% on small sheets). PageFromDC now
  routes through it with the sheet passed twice, byte-identical margins.
- **Tests:** new `tests/print_paint_test.cpp`, registered in both test
  targets; local portable build green (215 checks, 24 suites: player
  goldens, page cut, the new suite). `tools/check_alignment.sh` green.
- **Task 5, the ABI header:** `src/print_contract_abi.h`. The three
  contract interfaces declared IUnknown-style with the reference's GUIDs
  (`dedc0c30...`, `a96bb1db...`, `0b31cc62...`), the DXGI preview target
  with the Win8.1 IID and the Win8.0 fallback pair from Chromium, and
  `IPrintDocumentPackageTarget` left to the SDK's `documenttarget.h`.
  `JOB_PAGE_APPLICATION_DEFINED` and `PageCountType` follow the docs.
- **Task 6, the source class:** `src/print_winrt.{h,cpp}`,
  `PrintSourceWin` (classic COM identity over the three interfaces plus
  `IInspectable` and `IAgileObject`) with the DXGI preview path (D3D11
  hardware with WARP fallback, one texture per page, `DrawPage` at 96)
  and the final job (`ID2D1PrintControl`, one command list and one
  `AddPage` per band, `ERROR_PRINT_CANCELLED` swallowed). DEV note:
  threading settled differently than planned, and better: no layout or
  engine is created on the pipeline thread at all. Every build request
  is messaged to the window (`HandleWindowMessage` answers inside the
  app's message loop through `SendMessageTimeoutW`), so the one
  existing print build (renderer's `PrintLayout` plus
  `PaginateDocument`) serves both paths, documents with the dialog open
  print their latest text, and no second layout code path exists.
- **Task 7, the wiring:** app.cpp registers the window in `OnCreate`,
  the WndProc answers the build message, and `Print()` opens the modern
  dialog with a builder that walks `PrintPageFromDescription` ->
  `renderer_.PrintLayout` -> `PaginateDocument`; anything the contract
  path cannot do falls through to the classic `PrintDocument`, as does
  `MARKDOWNIT_NO_MODERN_PRINT=1`.
- **Task 8, deviation:** the `IPrintDocumentPackageStatusEvent` sink was
  not wired. The task's own goal (errors spoken, cancel silent, the
  user's window greyed while a job runs) is already carried by the
  `PrintTask.Completed` event, which reports Failed with a message box
  and treats the other ends as silence; a connection-point sink would
  add plumbing with no observable gain. Revisit only if a printer
  reports errors the task event does not carry.
- **Task 9, the docs:** printing.md rewritten for the two Windows paths
  (contract first, classic fallback behind `MARKDOWNIT_NO_MODERN_PRINT`
  or a contract that cannot open), the pieces table carries the painter
  and the description, and the parity ledger's printing row states the
  new shape: both ports preview now, in their own dialog; the
  fallback prints without one.
- **Task 10, CI and install:** the contract milestone went in as
  **494b6ba**, took three mechanical Windows build errors (missing
  `windows/render_target_win.h` include, enum member spellings this SDK
  only names through helpers, a winrt/WRL com_ptr assignment mismatch),
  fixed in **9bc049b**: run **44633739360**,  12/12 green, the first
  Windows build of the whole contract. Exe installed by build-swap-verify
  (MD5 6ad644cff7e5d1b9ca65190f9228c6ba, PE32+ GUI x86-64, backup
  `MarkDownIt.exe.bak-2026-11-19-9bc049b-pre`). What remains of the
  acceptance is on the user's machine: Ctrl+P with a fed preview pane,
  a paper print, and a Microsoft Print to PDF pass.
- **Preview scale fix (7801e24):** the first user test showed text about
  2-3x too large, clipped at the right edge of the preview page. The
  screenshot measurements matched the paper margin drawn at 1:1 into a
  smaller box: MakePage hands the PANE's page box in DIPs, not the
  paper's size, and DrawPage needs the dpi the surface was generated at
  (Chromium's metro print driver, `print_document_source.cc`, is the
  working reference: surface = box x displayDpi/96 px, context at that
  dpi, a uniform 1/scale transform from the paper into the box). The
  preview now takes the window's display dpi, scales the bands into the
  box, and DrawPage reports that dpi; Paginate calls InvalidatePreview
  on paper changes, and the job pass resets the context to 96 and
  identity in case a preview pass left state behind. CI run on
  7801e24: green, exe staged as `MarkDownIt.exe.new-7801e24`.
