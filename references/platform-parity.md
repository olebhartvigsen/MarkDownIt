# Platform parity ledger

Which features and sources differ between the Windows and macOS ports,
and why. Three checkers read this file:

- `tools/check_parity_ledger.py` holds the table below against the
  build: a difference without a row fails, and a row that names a file
  or surface that no longer exists fails.
- `tools/check_command_coverage.py` holds the command tables against
  `src/ribbon.xml` and the mac menu and toolbar.
- `tools/check_sources_listed.py` reads the unbuilt list at the end.

## Standing differences

| Source or feature | Windows | macOS | Reason |
|---|---|---|---|
| Mermaid measurement (`src/mermaid/measure_dwrite.cpp`) | exe | DirectWrite is not there; the layout seam measures with Core Text | A DirectWrite measure file cannot build elsewhere. Batch 2 moves both sides onto one seam. |
| Text layout engine (`src/textlayout_dwrite.cpp` against `src/textlayout_coretext.cpp`) | exe and tests | core and tests | One engine per platform behind `src/textlayout.h`. |
| SVG document (`src/svgdoc.cpp`) | exe and tests | not built, the mac renderer paints through the shared reader `src/macos/svg_mac.cpp` | The DirectWrite SVG path is Windows side. The reader `svg_mac.cpp` is shared and lives under `src/macos/` for historical reasons. |
| Window chrome | ribbon, `src/ribbon.xml` | native menu and toolbar, `menu_mac.mm` and `toolbar_mac.mm` | The platform UI guidelines: `ribbon-menu-guidelines.md` on Windows, `macos-native-chrome.md` on macOS. |
| Settings store | registry, `src/settings_win.cpp` | JSON under Application Support, `src/macos/settings_mac.cpp` | Platform convention. The `AppSettings` struct and the recent-files logic are shared in `src/settings.cpp`. |
| Clipboard | Win32 handles, `src/clipboard_win.cpp` | NSPasteboard, `src/macos/clipboard_mac.mm` | Platform convention. The interface, the escaping, the HTML converter and the line ending normalizers are shared in `src/clipboard.cpp`. |
| Usage telemetry transport | WinHTTP, `src/windows/telemetry_win.cpp` | `NSURLSession`, `src/macos/telemetry_mac.mm` | An HTTP stack is genuinely per platform: WinHTTP is an OS component on Windows and NSURLSession is part of Foundation on macOS, and neither builds on the other. Everything else is shared: the payload and the send (`src/analytics.*`), the one seam (`platform::TelemetrySink`, `src/platform/seams.h`), the opt-out setting and the decision to send. The OS version differs with the platform too (RtlGetVersion, because GetVersionEx lies without a manifest, against `NSProcessInfo`); the app version comes from `src/app.rc` on both. |
| File watch | a poller thread and `WM_USER_RELOAD`, `src/filewatch.cpp` | an `NSTimer` on the main run loop, `src/macos/filewatch_mac.cpp` | The poll is platform work, and the two differ in thread model. The policy that reads the stamp is shared in `src/filewatch_model.h` (`file-watch.md`), so both windows reload at the same moment and ask the same question. |
| Find bar | `src/findbar.cpp`, plus `findbar_window_test.cpp` | `src/macos/findbar_mac.mm` | A real Win32 window belongs on Windows. The navigation itself is the shared `FindBarModel` and both suites test it. |
| Legacy Mermaid entry points (`src/mermaid.cpp`, `mermaidlayout.cpp`, `diagramcache.cpp`) | Windows tests only | not built | Kept while `mermaid_legacy_test.cpp` moves to the shared engine. |
| Content width cap (`LayoutMetrics::maxContentWidth`) | applied | not applied | By design: `theme.h` says the mac renderer follows the pane width. The width commands below expose the modes on Windows only. |
| Mermaid painters | `renderer_pie.cpp`, `renderer_sequence.cpp` | `mermaid_painter_mac.cpp` | Each side paints the shared layout with its own API. |
| Printing | the modern dialog first: the Windows.Graphics.Printing contract in `src/print_winrt.cpp` (dialog preview fed page by page, final job as D2D command lists), with the classic `PrintDlg` in `src/print_win.cpp` as the fallback behind `MARKDOWNIT_NO_MODERN_PRINT=1` or a contract that cannot open | `src/macos/print_mac.mm`: an `NSPrintOperation` over a page view, whose preview and Save as PDF live inside AppKit's panel | The panels, the preview sinks and the printer plumbing are platform chrome. The page model, the cut, the slice, the description-to-page rule (`PrintPageFromDescription`), the band painter (`PaintBandPage`) and the print-path PDF writer are shared (`src/layout/print_pages.*`, `src/layout/display_player.*`, `src/print_pdf.*`, `printing.md`); every layout walk stays on the window's thread, reached through the one shared build. Both ports feed a preview now: the dialogs differ in whose pane it is, AppKit's own against the system print dialog's. The interim standalone preview window `src/print_preview_win.*` was removed 2026-11-19. Both ports write the QA PDF (`MARKDOWNIT_QA_PDF`): the mac through `CGPDFContext`, Windows through the shared pdfio writer. |

| Dark appearance (`src/app.cpp`, `src/ribbon.cpp` against `src/macos/main_mac.mm`) | uxtheme ordinals, DWM frame attributes, the ribbon's undocumented property 2004, `DarkMode_Explorer` scrollbars | `NSApp.appearance`, `effectiveAppearance`, semantic colours | The mechanism is native chrome on both sides; the appearance *decision* is shared (`src/thememode.h`, `src/theme_model.h`) and the document palette is shared (`theme.h`). A window frame belongs to the OS, so this one forks by necessity, like the ribbon row above. |
| Appearance setting key | `ThemeMode` DWORD in the registry | `themeMode` in `settings.json` | The existing settings-store split; the `AppSettings` field and its meaning are shared. |

## Commands: ribbon to mac menu

Every command the ribbon offers and the selector that carries it out
on the mac side. `tools/check_command_coverage.py` walks this table
against `src/ribbon.xml`, `menu_mac.mm` and `toolbar_mac.mm`. A new
ribbon command needs a row here or a row in the next section.

| Ribbon command | mac selector |
|---|---|
| cmdAbout | orderFrontStandardAboutPanel |
| cmdAddColumn | addColumnRight |
| cmdAddColumnLeft | addColumnLeft |
| cmdAddRow | addRowBelow |
| cmdAddRowAbove | addRowAbove |
| cmdAlignCenter | alignCenter |
| cmdAlignLeft | alignLeft |
| cmdAlignRight | alignRight |
| cmdAssocMd | setAsDefaultReader |
| cmdBold | toggleBold |
| cmdBullets | toggleBullets |
| cmdClearFormat | clearFormat |
| cmdCode | toggleCode |
| cmdEditToggle | editMode |
| cmdExportDocx | exportWord |
| cmdExportPdf | exportPdf |
| cmdFind | findDocument |
| cmdHeading1 | setHeading1 |
| cmdHeading2 | setHeading2 |
| cmdHeading3 | setHeading3 |
| cmdImportDocx | importWord |
| cmdOpenInJsonEditor | openInJsonEditor |
| cmdIndent | indentBlock |
| cmdInsertTable | insertTable |
| cmdItalic | toggleItalic |
| cmdLink | toggleLink |
| cmdMark | toggleMark |
| cmdNew | newDocument |
| cmdNumbering | toggleNumbering |
| cmdOpen | openDocument |
| cmdOutdent | outdentBlock |
| cmdOutline | toggleOutline |
| cmdQuote | toggleQuote |
| cmdRecent | showRecents |
| cmdRedo | redo |
| cmdPrint | printDocument |
| cmdReload | reloadDocument |
| cmdRemoveColumn | removeColumn |
| cmdRemoveRow | removeRow |
| cmdRemoveTable | removeTable |
| cmdReplace | replaceInDocument |
| cmdSave | saveDocument |
| cmdSaveAs | saveDocumentAs |
| cmdSplitCell | splitCell |
| cmdStrike | toggleStrike |
| cmdTheme | toggleTheme |
| cmdUndo | undo |
| cmdUsageStatistics | toggleUsageStatistics |
| cmdViewSource | toggleSourceView |
| cmdWrap | toggleWrap |
| cmdZoomIn | zoomIn |
| cmdZoomOut | zoomOut |

`cmdUsageStatistics` is the one preference command the mac carries twice:
Windows has no settings dialog, so it is a ribbon toggle in the View
group there, while macOS has both the View-menu item and the checkbox in
the settings window. Both mac surfaces edit the same stored field, so
they cannot disagree.

## Commands without a mac counterpart

| Ribbon command | Reason |
|---|---|
| cmdWidthStandard | The mac renderer follows the pane width on purpose, see the width cap row above. The width modes live in `renderer_mac.cpp` but no mac command runs them. |
| cmdWidth960 | Same as cmdWidthStandard. |
| cmdWidth1600 | Same as cmdWidthStandard. |
| cmdWidthFull | Same as cmdWidthStandard. |

## Mac commands without a ribbon counterpart

The mac menu is allowed to carry platform items. Every one of them is
listed here with its reason, so the set stays deliberate.

| mac selector | Reason |
|---|---|
| openSettings | The Windows app keeps the settings dialog under the File menu (`app.cpp`); the mac carries it in the application menu per platform convention. |
| closeDocument | The Windows frame closes through the title bar; the mac menu needs the item per the platform menu convention. |
| copy | Keyboard and context menu carry it on Windows; the mac menu convention lists it. |
| cut | Same as copy. |
| paste | Same as copy. |
| selectAll | Same as copy. |
| findNext | Part of the shared find bar on Windows; a menu item on macOS per platform convention. |
| findPrevious | Same as findNext. |
| importPdf | The Office bridge offers PDF import on macOS; the ribbon does not expose it yet. Promote with a row change when it does. |
| showHelp | No help system is shipped on Windows yet. |
| shareDocument | Share is a platform service with no Windows twin in this app. |
| toggleTabBar | The mac window has a tab bar setting with no Windows twin; the Windows frame uses the ribbon. |
| setBodyText | The Windows ribbon sets a heading but never takes one off, so the command has no ribbon twin. The mac format bar's style popup names Body Text directly, and the Blocks menu carries it. Give Windows a row when the ribbon grows one. |
| setHeading4 | The Windows ribbon offers levels 1 to 3; the mac menu and the format bar offer the six levels markdown has. Levels 4 to 6 stay reachable on Windows through their accelerators. |
| setHeading5 | Same as setHeading4. |
| setHeading6 | Same as setHeading4. |
| zoomReset | The Windows zoom resets through the width or zoom commands. |

## The mac format bar

The mac window carries a format bar over the editable page
(`src/macos/format_bar_mac.mm`), described in macos-native-chrome.md.
It is chrome, not a command surface of its own: every control sends a
row of `src/commands.h` through the responder chain, and the Windows
ribbon in `ribbon.xml` keeps its own grouping of the same rows. The
shared side is the command table and `GetFormatState()`; nothing in the
bar decides what a command means.

## Tests built on one platform

The suites the Windows and macOS test exes do not share, and why.
tools/check_test_inventory.py compares the two `--list` outputs and
fails on a suite that exists on one side only without a row here. A
row whose pattern matches nothing on its platform is stale and fails.

| Test | Platform | Reason |
|---|---|---|
| FindBarWindow.* | windows | A real Win32 window test. The navigation itself is covered by the shared FindBarModel suite on both runners. |
| SvgDocReference.* | windows | The DirectWrite SVG scene is Windows only, see the svgdoc row above. |
| SvgDocTransform.* | windows | Same as SvgDocReference. |
| MermaidLegacy.* | windows | Kept while the legacy entry points migrate, see the legacy row above. |
| ClipboardPaste.* | windows | The paste priority rules of the Win32 clipboard, driven through a real window with forged custom formats. The shared markdown escaping and line endings have their own portable tests on both runners. |
| EmptyListMapping.DirectWritePlaceholderHasOneClickableCaret | windows | The DirectWrite caret landing rule of the Windows engine. The Core Text engine has its own TextEngineMac suite. |
| RendererWin.* | windows | The Windows renderer's selection wash twin driven through the walk engine seam with fake D2D and DWrite shims, so the real SDK surface stays in the test. The shared rect rule itself is portable and cross-checked from the mac side by RendererMac reading the same walk records, plus the shared suites below. |
| ClearInlineFormatting.* | macos | The clear formatting tests run on macOS only today, although the Windows side carries the command. Move the file into the shared suite in plan Batch 4. |
| InlineMarkerRuns.* | macos | Same as ClearInlineFormatting. |
| TextEngineMac.* | macos | The Core Text engine behind the seam. |
| SeamConformance.DWriteEngineFollowsTheSeam | windows | The DirectWrite enrollment in the shared conformance grid, the twin of the Core Text row below. |
| SeamConformance.DWriteEngineDrawsALaidOutBlock | windows | The laid out block grid over the DirectWrite engine and the Direct2D target, the twin of the Core Text row below. |
| SeamConformance.CoreTextEngineFollowsTheSeam | macos | The Core Text enrollment in the shared conformance grid. The grid itself runs on both runners through the fake engine; the DirectWrite engine has its own row above. |
| SeamConformance.CoreTextEngineDrawsALaidOutBlock | macos | The laid out block grid over the Core Text engine and the Core Graphics target. The same block assertions run on both runners through the fake engine and the portable path. |
| SvgNativeMac.* | macos | The CoreGraphics SVG probes behind the same seam. |
| RendererMac.* | macos | The mac renderer layout arithmetic. |

## Sources built by no target

- `src/textlayout_generic.cpp`: the third, platform-neutral engine
  behind the same `textlayout.h` seam, for builds that have neither
  DirectWrite nor Core Text. No target compiles it today, because both
  test suites run on Windows or macOS. It joins the portable build in
  plan Batch 2.2, or the fake measurer of Batch 2.3 replaces it and the
  file is deleted.
