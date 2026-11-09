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
| Mermaid state diagrams (`src/mermaid/state_parse.cpp`, `state_layout.cpp`) | tests only, the exe does not build them | app and tests | The Windows exe never listed the files, so the mac app got state diagrams first. The plan promotes the exe in Batch 3, or this row records the decision to stay without them. |
| Mermaid measurement (`src/mermaid/measure_dwrite.cpp`) | exe | DirectWrite is not there; the layout seam measures with Core Text | A DirectWrite measure file cannot build elsewhere. Batch 2 moves both sides onto one seam. |
| Text layout engine (`src/textlayout_dwrite.cpp` against `src/textlayout_coretext.cpp`) | exe and tests | core and tests | One engine per platform behind `src/textlayout.h`. |
| SVG document (`src/svgdoc.cpp`) | exe and tests | not built, the mac renderer paints through the shared reader `src/macos/svg_mac.cpp` | The DirectWrite SVG path is Windows side. The reader `svg_mac.cpp` is shared and lives under `src/macos/` for historical reasons. |
| Window chrome | ribbon, `src/ribbon.xml` | native menu and toolbar, `menu_mac.mm` and `toolbar_mac.mm` | The platform UI guidelines: `ribbon-menu-guidelines.md` on Windows, `macos-native-chrome.md` on macOS. |
| Settings store | registry, `src/settings_win.cpp` | JSON under Application Support, `src/macos/settings_mac.cpp` | Platform convention. The `AppSettings` struct and the recent-files logic are shared in `src/settings.cpp`. |
| Clipboard | Win32 handles, `src/clipboard_win.cpp` | NSPasteboard, `src/macos/clipboard_mac.mm` | Platform convention. The interface, the escaping, the HTML converter and the line ending normalizers are shared in `src/clipboard.cpp`. |
| Find bar | `src/findbar.cpp`, plus `findbar_window_test.cpp` | `src/macos/findbar_mac.mm` | A real Win32 window belongs on Windows. The navigation itself is the shared `FindBarModel` and both suites test it. |
| Legacy Mermaid entry points (`src/mermaid.cpp`, `mermaidlayout.cpp`, `diagramcache.cpp`) | Windows tests only | not built | Kept while `mermaid_legacy_test.cpp` moves to the shared engine. |
| Content width cap (`LayoutMetrics::maxContentWidth`) | applied | not applied | By design: `theme.h` says the mac renderer follows the pane width. The width commands below expose the modes on Windows only. |
| Mermaid painters | `renderer_pie.cpp`, `renderer_sequence.cpp` | `mermaid_painter_mac.cpp` | Each side paints the shared layout with its own API. |

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
| cmdRedo | redo |
| cmdReload | reloadDocument |
| cmdRemoveColumn | removeColumn |
| cmdRemoveRow | removeRow |
| cmdRemoveTable | removeTable |
| cmdReplace | replaceInDocument |
| cmdSave | saveDocument |
| cmdSaveAs | saveDocumentAs |
| cmdSplitCell | splitCell |
| cmdStrike | toggleStrike |
| cmdUndo | undo |
| cmdViewSource | toggleSourceView |
| cmdWrap | toggleWrap |
| cmdZoomIn | zoomIn |
| cmdZoomOut | zoomOut |

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
| showRecents | The recents list is the Start Menu jump list on Windows. |
| shareDocument | Share is a platform service with no Windows twin in this app. |
| toggleTabBar | The mac window has a tab bar setting with no Windows twin; the Windows frame uses the ribbon. |
| zoomReset | The Windows zoom resets through the width or zoom commands. |

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
| ClearInlineFormatting.* | macos | The clear formatting tests run on macOS only today, although the Windows side carries the command. Move the file into the shared suite in plan Batch 4. |
| InlineMarkerRuns.* | macos | Same as ClearInlineFormatting. |
| TextEngineMac.* | macos | The Core Text engine behind the seam. |
| SvgNativeMac.* | macos | The CoreGraphics SVG probes behind the same seam. |
| RendererMac.* | macos | The mac renderer layout arithmetic. |
| WelcomeMac.* | macos | The mac welcome pane. |

## Sources built by no target

- `src/textlayout_generic.cpp`: the third, platform-neutral engine
  behind the same `textlayout.h` seam, for builds that have neither
  DirectWrite nor Core Text. No target compiles it today, because both
  test suites run on Windows or macOS. It joins the portable build in
  plan Batch 2.2, or the fake measurer of Batch 2.3 replaces it and the
  file is deleted.
