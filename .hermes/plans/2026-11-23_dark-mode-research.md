# Supplementary dark mode: research and implementation plan

Date: 2026-11-23
Scope: one dark appearance that works on Windows and macOS, selected by the user and by the system, without touching the document.

## The shape of the feature

`references/night-mode-reference.md` already defines the behavior contract. It asks for three modes and two models:

- Light: light UI, normal paper.
- Dark UI: dark UI, normal paper.
- Dark: dark UI, dark document surface.
- System: the OS choice decides between Light and Dark.

This plan recommends shipping **Dark** (dark UI plus dark document) as the offline reading experience, with **System** as the default on both ports. Dark UI with a white page is allowed by the reference guide, but a bright page inside a dark window is exactly the half-finished look the guide warns about in section 2.1. If someone wants paper-like dark UI later, that is a fourth setting, not a default.

### Requirement: adopt the OS scheme

The app adopts the colour scheme the operating system is currently in. This is the default behaviour, not an opt-in extra, and it applies on Windows and macOS alike.

What "adopt" means, precisely, so nobody has to guess later:

- **The decision comes from the OS.** On launch, the app asks the OS whether it is in light or dark mode and starts in that appearance. There is no separate first-run question and no window that flashes the wrong theme before it settles.
- **It keeps following while running.** A change in the OS setting, or the OS switching on a schedule, moves the app to the new appearance without a restart, without a reload of the document, and without losing the caret or the scroll position.
- **The document follows too.** Dark OS means dark paper. Today the mac port stops halfway: chrome turns dark, and the default Apple style keeps a white page. That gap closes as part of this work.
- **System chrome comes from the system.** Window frame, title bar, menus, scrollbars, the sidebar material, the selection colour and the accent colour are taken from the OS, because those are the parts the user expects to match other applications.
- **Document inks come from the shared tokens.** Body text, headings, links, rules, code cards, table grids, callouts and JSON syntax colours come from `DarkPalette` and `LightPalette`, not from sampled OS colours, so both ports render the same page for the same style and the capture comparison stays meaningful. The native selection colour is the one deliberate exception.
- **An explicit choice wins over the OS, and is remembered.** If the user picks Light or Dark, that holds across launches even when the OS is in the other mode. Choosing System hands control back to the OS. That is the `/references/night-mode-reference.md` rule in section 22.2, and it is the only case where the app ignores the OS.
- **High contrast outranks both.** If the OS reports a high contrast theme, the app uses system colours and does not impose a palette, whether the mode is System or an explicit Dark.
- **Nothing about the document changes.** No undo entry, no dirty flag, no Save prompt, no reparse, and printing and PDF export stay light. Adopting the OS scheme is a rendering decision only.

The resolution lives in one shared function, `ResolveAppearance(ThemeMode, bool systemIsDark)`, so the rule is testable without a window on either platform.

## What the codebase already has

This is not a greenfield feature, and that changes the estimate.

- `src/theme.h` holds every colour as a named token in `Palette` and `ChromePalette`. Both renderers and the display player read from it, so a second palette has one place to live.
- `ChromePalette` already has a dark counterpart, `DarkChromePalette()`, and macOS already selects it through `ChromePaletteForAppearance()`.
- macOS already resolves AppKit semantic colours in `SystemPalette()` (`src/macos/system_theme_mac.mm`), so the mac document follows Light and Dark today in the GitHub style. `content_view_mac.mm` re-resolves on `viewDidChangeEffectiveAppearance`, which is the hook the adoption requirement needs.
- macOS already has QA capture states named `dark` and `zoom-hud-dark` in `main_mac.mm`.
- The print path already refuses the dark palette. `Renderer::PrintLayout` builds with `BasePalette()` and says why: paper is white. `print_pdf.cpp` and `print_mac.mm` agree. The print requirement from the reference guide is therefore already structurally satisfied.

What is missing:

| Gap | Where |
|---|---|
| No page background token; the surface is hardcoded white | `app.cpp` lines 3543, 3578, `renderer.cpp` 3563 |
| Caret ink is hardcoded black at 0.72 alpha | `app.cpp` 3487 |
| No dark document palette for Windows, and no shared dark palette at all | `src/theme.h` |
| No persisted theme setting | `AppSettings` in `src/settings.h` |
| No appearance mode resolution in shared code | none |
| SVG `light-dark()` always resolves to the light branch | `src/svgtext.cpp` 441-460 |
| `mermaid theme=dark` never reaches the engine; the parser strips everything after the first space in the fence info string | `src/parser.cpp` 578-588 |
| Windows chrome is light only, and the ribbon is not palette-driven | `src/ribbon.cpp`, `src/app.cpp` |

## How each platform actually works

### Windows

Win32 has no automatic dark mode. Every opt-in is either documented-but-partial or undocumented-but-stable.

Documented pieces:

- The title bar is opt-in per window: `DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value))`. The attribute is 20 since Windows 11 / 10 20H1, and 19 on 1809 to 19H2. Microsoft documents it as Windows 11 only, and it works on Windows 10 in practice.
- Windows 11 adds `DWMWA_CAPTION_COLOR`, `DWMWA_TEXT_COLOR` and `DWMWA_BORDER_COLOR` if the frame should match a warm dark chrome instead of the neutral system frame.
- `UISettings::ColorValuesChanged` and `WM_SETTINGCHANGE` with `"ImmersiveColorSet"` are the documented ways to notice a theme change.

Undocumented pieces, in order of importance:

1. `uxtheme.dll` ordinal 135. On Windows 10 1809 it is `AllowDarkModeForApp(bool)`. On 1903 and later the same ordinal is `SetPreferredAppMode(PreferredAppMode)`, where the enum is Default, AllowDark, ForceDark, ForceLight. Ordinal 133 is `AllowDarkModeForWindow(HWND, bool)`. Ordinal 132 is `ShouldAppsUseDarkMode()`, and 138 is `ShouldSystemUseDarkMode()`. Chromium wraps exactly these in `base/win/dark_mode_support.cc`.
2. A known restore bug: switching back to light through `SetPreferredAppMode(ForceLight)` alone can leave some controls dark. Notepad++ and others force dark once, then force light, then call `RefreshImmersiveColorPolicyState()` (ordinal 104). This sequencing matters for a live toggle.
3. Menus and non-client areas need `FlushMenuThemes()` (ordinal 136) plus `SetWindowCompositionAttribute` with `WCA_USEDARKMODECOLORS`, which is what TortoiseGit does on every switch.
4. Native scrollbars and common controls are themed per window with `SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr)`, with `L"Explorer"` to go back.

The ribbon is the one genuine risk. The Windows Ribbon framework does not read the palette and has no documented dark property. Two shipping applications show the working path anyway:

- BowPad (`CMainWindow::SetTheme`) and TortoiseGit (`CMainFrame::SetTheme`) both declare `DEFINE_UIPROPERTYKEY(UI_PKEY_DarkModeRibbon, VT_BOOL, 2004)`, call `AllowDarkModeForApp(TRUE)`, `AllowDarkModeForWindow(hwnd, TRUE)`, then `IUIFramework::QueryInterface(IPropertyStore)`, `SetValue(UI_PKEY_DarkModeRibbon, TRUE)`, `Commit()`.
- BowPad also carries a version gate. Reading the `uiribbon.dll` file version, `major == 10 && micro > 17762` (that is 1809 and later) means the old HSB colour route no longer changes the ribbon background, so the property is used instead. The property itself was found by reverse engineering and is not in any header.
- TortoiseGit additionally sets `GCLP_HBRBACKGROUND` to `BLACK_BRUSH` and flushes the menu themes, because the ribbon's parent window still erases with the light brush.

That is three undocumented dependencies on the Windows side, all proven by shipping code but all without a contract.

Reference implementations worth reading before writing any of this:

- `ysc3839/win32-darkmode`: the canonical sample, 529 stars, updated November 2026, with a delay-load branch.
- `notepad-plus-plus`: `NppDarkMode.cpp` and `DarkMode/DarkMode.cpp`, the most complete Win32 dark implementation, including `WM_CTLCOLOR` handling, subclassed buttons, dark message boxes and high-contrast bailout.
- `sumatrapdfreader/sumatrapdf` plus Stefan Küng's `darkmodelib`: a library-shaped approach where callers ask for "theme this window", not for specific API calls.
- `chromium/base/win/dark_mode_support.cc`: the cleanest short read on the ordinals and version gating.

High contrast must short-circuit all of it. Notepad++ checks `SPI_GETHIGHCONTRAST` and falls back to system colours. Our palette is fixed, so the same rule applies: when high contrast is on, do not impose dark.

### macOS

macOS is the easy half, and mostly already done here.

- `NSAppearanceCustomization` is adopted by `NSApplication`, `NSWindow` and `NSView`. `effectiveAppearance` is `appearance ?? parent.effectiveAppearance`.
- `nil` means inherit. `NSApp.appearance = nil` follows the system again. That is the only reliable reset point: a concrete value written to `NSWindow.appearance` is not cleared by a later `nil` on the window (Zed hit exactly this, and fixed it by moving the override to `NSApplication.appearance`).
- Dynamic `NSColor`s only resolve correctly inside a drawing context. Outside `drawRect:` or `updateLayer`, wrap resolution in `performAsCurrentDrawingAppearance:` (macOS 11+). The older `NSAppearance.current` assignment is deprecated since macOS 13. `system_theme_mac.mm` already does the right thing with `performAsCurrentDrawingAppearance`.
- Invalidate on `viewDidChangeEffectiveAppearance`, which `content_view_mac.mm` already implements.
- Apple's HIG advises against app-specific appearance settings. Our feature needs one anyway, because Windows has no equivalent and the reference guide asks for Light, Dark and System. The compromise: `System` is the default on macOS and is the option that makes the app stop arguing with the OS.

### Colour and typography guidance

- Do not use pure black or pure white surfaces. Material's dark theme uses #121212 and expresses elevation by lightening surfaces with an overlay. Our layout has one flat page, so the equivalent is a dark neutral paper around #1C1C1E to #242426, with code cards and table headers one step lighter or darker than the page, not the same value.
- Contrast: 4.5:1 minimum for body text, 7:1 is the safer target, 3:1 for borders and other non-text UI. White on #121212 is about 15.8:1, which is why a near-white ink such as #E6E6E6 still clears the bar comfortably.
- Never invert hues. A red danger label must stay red, a link must stay blue, a JSON string must stay green-ish. Desaturate and lighten instead.
- Reversed type reads heavier than the same weight on white (irradiation illusion). Segoe UI and Cascadia Mono are static faces, so the `GRAD` axis workaround from variable fonts is not available. The practical compensation is a slightly grey ink (#E6E6E6 rather than #FFFFFF) and no change to weights. DirectWrite also blends glyph coverage differently per background, so text can look a touch thicker on dark; do not "fix" that by lowering the weight, because the light appearance would then look wrong.
- Layering stays as the reference guide requires: page transform first, then marker layer, then find highlights, then selection, then caret. Dark mode must not flatten those into one layer.

## Recommended architecture

Shared code owns the decision, each shell owns only its own chrome. That follows the `AGENTS.md` rule and keeps screenshot parity possible.

### Where each piece lives

The split below is the architecture, applied to this feature. Anything that is not in the native column has no business in `src/app.cpp` or `src/macos/`.

| Concern | Lives in | Why |
|---|---|---|
| Mode value, parsing, appearance resolution | `src/thememode.h`, shared | one rule, testable without a window, no platform header |
| Document palette, light and dark, plus `pageBg` and `caretInk` | `src/theme.h`, shared | both ports must render the same bytes for the same style |
| What a switch means (repaint, reparse, nothing) | `src/theme_model.h`, shared | the platform reports a change, shared code decides what follows, same pattern as `filewatch_model.h` |
| "Is the OS dark, and tell me when it changed" | `src/platformservices.h` seam, two thin adapters | only the OS knows, and the answer is one bool plus one notification |
| Chrome palette, sidebar shapes | `src/theme.h`, shared; the mac adapter supplies its material colours | shapes and tokens shared, native material native |
| Ribbon dark property, title bar, scrollbars, menus | `src/app.cpp`, `src/ribbon.cpp` | native chrome, ledger rows |
| `NSApp.appearance`, view invalidation | `src/macos/main_mac.mm`, `content_view_mac.mm` | native chrome, ledger rows |
| Storing the choice | `AppSettings` plus `settings_win.cpp` / `settings_mac.cpp` | the existing split, unchanged |

Three consequences worth stating plainly, because the current code breaks two of them:

1. `SystemPalette()` loses its document role. Today `src/macos/system_theme_mac.mm` assembles an article palette out of AppKit colours, which means a document palette exists on one platform only. After this work it only reports system-derived colours (selection, accent, sidebar material), and the document palette is assembled in `src/theme.h` for both ports. That file shrinks, it does not grow.
2. A new `#ifdef _WIN32` in a shared file is not allowed. The only platform branch stays the concrete type aliases that `theme.h` already carries for `ThemeColor` and `ThemeFontName`. Everything else goes through the seam.
3. "Windows first, mac later" is not a valid staging. `PaletteFor` is consumed by both renderers in the same change, with the mac side keeping System as its default so behaviour there does not regress.

1. New shared header `src/thememode.h`:
   - `enum class ThemeMode { System, Light, Dark }` (persisted value).
   - `enum class Appearance { Light, Dark }` (resolved value).
   - `ResolveAppearance(ThemeMode mode, bool systemIsDark)`.
   - `ParseThemeMode` / `ThemeModeName` for the settings value and for an environment override.
2. `src/theme.h` gains:
   - `Palette::pageBg` and `Palette::caretInk`. Today neither exists, and both are hardcoded at the paint site.
   - `DarkPalette(MarkdownStyle)` for GitHub, Wikipedia and Apple, hand-checked per token, built from the light palette by rule rather than by arithmetic inversion.
   - `PaletteFor(MarkdownStyle, Appearance)`, so `BasePalette()` keeps its current meaning (light) and print stays untouched.
   - Optionally `DarkChromePalette()` gains a variant that takes the style, since the warm brown pane is Apple-flavoured and reads oddly with the GitHub style.
3. One seam addition in `src/platformservices.h`: `bool SystemPrefersDark()` plus a notification when it changes, with the consequence of that notification decided in `src/theme_model.h` rather than in either shell. This seam carries the adoption requirement, so it is not optional and it has to work before the first paint. Windows reads `AppsUseLightTheme` under `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize` (or ordinal 132) and listens for `WM_SETTINGCHANGE` with `"ImmersiveColorSet"`, then re-reads, because the setting can change twice in a row (Apps versus System mode); macOS compares `NSApp.effectiveAppearance` against `NSAppearanceNameDarkAqua` and reacts to `viewDidChangeEffectiveAppearance`. The seams stay small, as the architecture demands.
4. Renderers read `pageBg` and `caretInk` from the palette. Delete the hardcoded white clears and the black caret brush. `Renderer::PrintLayout` keeps forcing the light palette and gets a test that says so.
5. `AppSettings` gains `int themeMode = 0` (System). Registry on Windows, JSON on macOS, both already platform-specific and both already loading the same struct.
6. One command, `cmdTheme`, toggling Light and Dark, with a `System` entry in settings. That means a row in `src/commands.h`, a ribbon command and a group slot in `src/ribbon.xml`, a selector in `menu_mac.mm`, a requirements row in `commands::RequirementsOf`, and a paragraph in `references/platform-parity.md`. `tools/check_command_coverage.py` will fail otherwise.
7. An environment override, `MARKDOWNIT_THEME=light|dark|system`, read once at startup exactly like `MARKDOWNIT_STYLE`. This is not a convenience. It is what makes CI captures deterministic, because the runner's system setting is not ours to control.

### Platform shell work

Windows (`src/app.cpp`, `ribbon.cpp`, `findbar.cpp`, `outlinepane.cpp`):

- At startup, before the first window: load `uxtheme.dll`, resolve ordinals 135, 136, 104 and 133 with `GetProcAddress`, gate on build 17763 or later, and call `SetPreferredAppMode` according to the mode: Default for System, ForceLight for Light, ForceDark for Dark.
- Per window: `AllowDarkModeForWindow`, `DWMWA_USE_IMMERSIVE_DARK_MODE`, window class background brush, `SetWindowTheme(..., L"DarkMode_Explorer")` for scrollbars and any native child controls.
- Ribbon: after `IUIFramework::LoadUI`, `QueryInterface(IPropertyStore)`, set `UI_PKEY_DarkModeRibbon` to a `VT_BOOL` 2004 property, commit. Probe the HRESULT, log a trace line, and fall back to a light ribbon rather than a broken one.
- Menus: `FlushMenuThemes()` and `WCA_USEDARKMODECOLORS` on every switch.
- Custom surfaces (find bar, outline pane, zoom HUD, settings dialog, message boxes) already draw through palette-like code or Win32 controls. The find bar and outline pane should take their colours from `ChromePalette`; message boxes need either `TaskDialogIndirect` with a dark-aware custom draw or a small owned window.
- On `WM_SETTINGCHANGE` with `"ImmersiveColorSet"`, re-resolve when the mode is System.
- High contrast on: ignore the dark palette entirely and use the system colours.

macOS (`src/macos/main_mac.mm`, `system_theme_mac.mm`, `content_view_mac.mm`):

- `NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]` for Dark, `...Aqua` for Light, `nil` for System.
- Keep `SidebarPaletteForAppearance` for the sidebar, since the material behind it is native. Drop the document half of `SystemPalette()` and let `PaletteFor(style, appearance)` in shared code supply the page, the inks and the surfaces on both ports.
- Let every style follow the OS page, not just GitHub. `content_view_mac.mm:1090` currently paints white for any style other than GitHub, which is what makes dark mode stop at the chrome under the default Apple style. With `pageBg` in the palette that special case goes away.
- Refresh the cached palette and invalidate the content view on `viewDidChangeEffectiveAppearance`, which already exists. That handler is the live-adoption path and should be covered by a test or a capture, not left implicit.

### Content details

- SVG: `CssColor` must take the appearance and pick the dark branch of `light-dark()` when the page is dark. That is a small change with a visible payoff, because a lot of modern SVGs use it.
- Mermaid: keep the parser from dropping the info string. Pass `theme=dark` through to the mermaid engine and select a dark diagram theme there, or keep mermaid's own palette and document that diagrams stay light on a dark page. The second option is honest and cheaper; the first is what users will expect.
- Images: no inversion, no brightness filter. Transparent PNGs must composite against `pageBg`, which already happens if the clear colour is the token.
- Code blocks, tables, callouts, JSON syntax colours: each gets a dark token with a checked contrast ratio.
- Office import/export and PDF export: untouched, since they work from the document model.

## Architecture conformance

The feature goes through the same gates as any other change, and the plan is written so it passes them on the first attempt instead of being argued about afterwards.

**What stays native, on purpose.** Three things only: the OS query (`SystemPrefersDark`), the Windows chrome calls (uxtheme ordinals, DWM attributes, the ribbon property, scrollbar and menu theming), and the macOS appearance override with view invalidation. Each is a system service or native chrome, which `AGENTS.md` puts on the platform side. Nothing else is allowed to fork.

**Source manifest.** New files (`src/thememode.h`, `src/theme_model.h`, and the new suites) go into `cmake/sources.cmake` with their declared surfaces. `tools/check_sources_listed.py` fails on anything unlisted.

**Parity ledger.** Rows to add to `references/platform-parity.md`:

- Dark chrome mechanism: uxtheme ordinals, the ribbon property and the DWM frame on Windows against `NSAppearance` on macOS. Same visible outcome, different mechanism, which is exactly what a standing-difference row is for.
- Sidebar material: macOS resolves the native material, Windows paints the pane. This is already a chrome difference and now also an appearance one.
- Settings key for `themeMode`, unless the existing settings-store row already covers it.

**Command coverage.** `cmdTheme` gets its ribbon command, its mac selector and its `RequirementsOf` entry in the same change. `tools/check_command_coverage.py` fails otherwise, and `tests/commandstate_test.cpp` fails on a command without requirements.

**Shared guards.** `tools/check_shared_guards.py` stays green, so the palette and the resolution logic cannot reach for `windows.h`, `d2d1.h` or AppKit.

**Twin pairs.** `PaletteFor` touches both renderers, both QA walks and both test targets in one change. If any piece genuinely lands on one side only, the commit carries a `Parity:` line that says why.

**Test inventory.** Both new suites are registered in both test targets in `CMakeLists.txt`. `tools/check_test_inventory.py` compares `--list` output across runners, so a one-sided suite is a build failure.

**Captures.** The dark capture follows `tools/check_capture_policy.py`: taken in CI only, caret-free, compared at the existing font tolerance. No threshold gets loosened to make dark pass.

## Test plan

- `tests/palette_contrast_test.cpp` (new, both runners, no GPU): walk every `Palette` slot in both appearances and compute the WCAG relative luminance ratio against `pageBg`, and against `codeBg` where the ink sits on that surface. Assert 4.5:1 for text slots, 3:1 for borders and rules, and assert that the dark page is neither pure black nor equal to the light page. This turns "readable" into a build failure instead of an opinion, and it costs nothing at runtime.
- `tests/thememode_test.cpp`: `ResolveAppearance` truth table, parse and format of the settings value, and the environment override precedence.
- A print test that asserts `PrintLayout` uses light tokens while the window is dark.
- An adoption test: start with the setting absent, assert `Appearance` matches the reported OS state, then flip the reported state with the mode still `System` and assert the resolved appearance follows and the document is re-rendered from the other palette. This is where the "keeps following while running" promise gets pinned down without a real desktop.
- A startup test that no light frame is painted before the resolved appearance is applied, checked through the QA trace on both ports rather than through pixels.
- Layout goldens: unchanged, because colour does not affect layout. Add one dark layout smoke run so a missing token cannot be introduced silently.
- Capture parity: add a `dark` state to the Windows QA walk, mirroring the existing mac state, and keep captures caret-free as the policy requires. Note that `windows-2022` is a Server SKU, so verify the ribbon property and the DWM frame behave there before making the dark capture part of the gate. The `MARKDOWNIT_THEME` override is what makes that verification reproducible either way.
- Editing transcripts: unaffected, and they should stay unaffected, because the toggle must not touch the buffer.

## Phases

| Phase | Work | Size | Risk |
|---|---|---|---|
| P0 | `pageBg` and `caretInk` tokens, `DarkPalette` per style, `PaletteFor`, renderers on tokens | S | none |
| P1 | `ThemeMode` in settings and in `src/thememode.h`, `System` as the persisted default, command table and ribbon row, menu selector, env override | S | checkers and ledger rows |
| P2 | Windows shell: opt-in sequence, title bar, brushes, scrollbars, menus, ribbon property, custom surfaces | L | ribbon is undocumented |
| P3 | macOS: app-level override for explicit choices, every style following the OS page, palette rebuild, ledger row | S | none |
| P4 | Content: SVG `light-dark()`, mermaid theme, JSON and callout dark tokens | M | mermaid theming depth |
| P5 | Tests and CI: contrast suite, theme suite, dark capture, print-light test | M | Server SKU capture |

## Implementation status (2026-11-23)

Built in one pass, in the order the phases below describe. What each phase
landed, and what it did not, so the document does not overstate itself.

P0, the shared palette, is done:

- `src/thememode.h`: the mode value, its parsing, `ResolveAppearance`, the
  `MARKDOWNIT_THEME` override and `StartupThemeMode`.
- `src/theme_model.h`: `PlanForSwitch` (repaint, never relayout or reparse)
  and `ChromeNeedsRebuild`.
- `src/theme.h`: `Palette::pageBg` and `Palette::caretInk`, a dark palette
  per style, `ApplyDarkUiColours` for the chrome and syntax slots, `PaletteFor`
  and `ChromePaletteFor`.
- The Windows renderer holds a palette (`SetTheme`, `ThemePalette`) instead of
  calling `BasePalette()` in seven places, so measure and paint cannot disagree.
- `tests/palette_contrast_test.cpp` and `tests/thememode_test.cpp`, registered
  in both test targets. The contrast suite is what forced two real fixes during
  the pass: the Wikipedia and Apple dark palettes were inheriting the light
  find and syntax colours, at 1.1:1 and 2.8:1.

P1, the setting and the command, is done: `themeMode` in `AppSettings`,
persisted in the registry and in the JSON file; a `cmdTheme` row in the command
table; the ribbon command, its five-button View group and a generated moon
icon; the mac `Dark Mode` item; `RequirementsOf` returns `Always`, because the
welcome screen is exactly where a glare hurts.

P2, the Windows shell, is done: the uxtheme ordinals (135 and 104), the DWM
frame attributes (20 with a 19 fallback), `DarkMode_Explorer` scrollbars, the
window class background brush, the ribbon's property 2004, `WM_SETTINGCHANGE`
with `ImmersiveColorSet`, a high contrast bail-out, an inverting caret that is
visible on either paper, and a `dark` QA state that the windows capture walk
now requests. The ribbon's dark property is the one undocumented dependency and
it fails soft, by design.

P3, the macOS shell, is done: `NSApp.appearance` for an explicit choice and
`nil` for System; `SystemPalette()` no longer assembles an article palette out
of AppKit colours, it returns `PaletteFor` plus the system selection; the page
is painted from `pageBg` for every style, which removes the white-page
special case that used to stop dark mode at the chrome under the Apple style;
`platform::SystemPrefersDark()` answers from `effectiveAppearance`.

P4 is done, after the follow-up pass:

- `light-dark()` is resolved by `svg::ResolveLightDark` (`src/svgtext.cpp`) on
  the raw SVG text, before either reader sees it. That is one shared string
  transform instead of an appearance threaded through five parse functions,
  and it fixes Windows at the same time: Direct2D's SVG parser knows no
  `light-dark()` at all, so before this the Windows build drew the literal
  unresolvable colour. Both scene caches are cleared on a switch
  (`ClearSvgCache`, and `svgDocs_.clear()` in the mac `SetTheme`), because the
  chosen branch is baked in at read time.
- Mermaid diagrams have their own dark palette, taken from mermaid's
  `theme-dark` (`#1f2020` nodes, `#ccc` lines and labels, `#302F3D` clusters,
  the "neo" actor look re-expressed in dark values). The flowchart, sequence
  and pie painters on both ports now read those colours from the palette's
  `mermaid*` tokens instead of private constants, which also removed a twin
  pair's worth of duplicated hex. Pie slices keep mermaid's own palette, as a
  pie's identity is its colours; the ring, labels and legend follow the page.

The CI verdict, run 45358359215 on f229f7c: green on every job, both builds,
both test suites, the Office and Mermaid oracles, and all six parity gates.
The dark pair compares at 5.14% ink on macOS against 4.24% on Windows, with
the same 5 px first-row offset the doc state already carries, which is a WARN
inside the font tolerance rather than a failure.

Five defects surfaced while getting there, four of them real product bugs
rather than capture scaffolding:

- The frame cache key carried document, width, zoom and caret but not the
  appearance, so a theme switch replayed the light display list on Windows.
- `PlanForSwitch` asserted that no relayout was needed. Colours are baked
  into the display list, so that was false by construction.
- `AppearanceFor(nil)` read `NSApp.effectiveAppearance`, so a window-level
  appearance gave a dark frame around a light page. This one affected users
  who pin Dark while the OS is Light, not just the QA walk.
- The mac `dark` state photographed the welcome screen, and later, once it
  reopened a file, a standalone `wide-diagram.svg` the walk had opened
  earlier. It now reopens the document the process was launched with.
- The two capture jobs uploaded artifacts whose names differed by one letter,
  so a retried job could hand the comparator the wrong platform's pixels.

P5, verification, is done as far as this machine allows: the two new suites
build and pass under g++, the parity runners build, and
`tools/check_alignment.sh` passes (sources, ledger, commands, shared guards).
The native compile is still CI's job, as `AGENTS.md` says, because MSVC and
Xcode do not run here.

## The home screen (follow-up, now done)

`welcome_screen.cpp` was the one surface still painted from fixed
constants: a teal band over a mint page, so a dark window showed a bright
home screen. It now carries a palette record of its own,
`src/welcome_palette.{h,cpp}`, with the light set (the site's own colours)
and a dark set that keeps the brand rather than inverting it: the band
keeps its hue and deepens, the cards sit one step above a mint-tinted
near-black page, and the ink flips.

The record lives in its own translation unit so
`tests/palette_contrast_test.cpp` can hold both sets to the same ratios
without linking the screen or the SVG reader. Every ink is checked
against the surface it is painted on, including the band gradient's
bottom step and the solid button's dark ink.

Both shells hand the record in: Windows in `ApplyAppearance`, macOS in
`ContentView::RefreshTheme`, so the home screen follows the same
appearance as the document. A `welcome-dark` state in both capture walks
photographs it, which is the only way the branded dark set is verified
rather than asserted.

**A note on verification.** The welcome palette went in without a CI
cycle: `g++ -fsyntax-only` on `welcome_screen.cpp` and the new unit, plus
the contrast suite, caught what a compiler would have, and the capture
state is the part CI has to confirm.

## Risks and open questions

1. **The ribbon has no contract.** `UI_PKEY_DarkModeRibbon` is reverse engineered, gated by build number, and not present in any Microsoft header. BowPad and TortoiseGit ship it, which is strong evidence it works, not a promise it keeps working. Decide now what happens if a Windows update drops it: a light ribbon inside a dark window (acceptable, documented), or replacing the ribbon with a custom strip (a much bigger change). I would take the first.
2. **The palette question is settled, so treat it as closed.** macOS currently derives document colours from AppKit semantics, which looks native but makes the two ports render different bytes for the same style. The architecture decides this one: document tokens are shared, and the platform contributes only its system-derived colours (selection, accent, sidebar material). If someone later wants the mac page to match AppKit exactly, that is a ledger row with a reason, not a quiet fork in `SystemPalette()`.
3. **Explicit dark on a light system.** `SetPreferredAppMode(ForceDark)` is what makes this possible on Windows, and it is also what makes some controls (menus, common dialogs) behave inconsistently across builds. The Notepad++ source is the best catalogue of those quirks.
4. **CI determinism.** Captures must not depend on the runner's personalization setting. The env override solves it, and it should land in the same phase as the palette, not later.
5. **Typography drift.** Light text on dark reads bolder. Choose the ink deliberately and do not compensate with weight changes, or the light mode pays for it.
6. **Adoption has to happen before the first paint.** A window that appears light and turns dark a moment later reads as a bug, and Windows makes this easy to get wrong because the ribbon loads and paints during startup. Read the OS state during settings load, resolve once, and apply before `ShowWindow`. If the ribbon property has to be set after `LoadUI`, accept one frame of a light ribbon at most, or create the window hidden and show it after the theme is applied.
7. **The OS setting can be split.** Windows lets a user keep apps light while the shell is dark (the `AppsUseLightTheme` and `SystemUsesLightTheme` values differ). The app follows the app setting, since that is the one that means "what applications look like", and the document follows the same value. Do not mix the two.
8. **Scope creep toward "dark UI, light paper".** The reference guide lists it as a model. Shipping it doubles the appearance matrix and the capture grid. Ship Dark as one thing first, then decide.

## Sources

Platform mechanics:

- Microsoft, `DWMWINDOWATTRIBUTE` and `DWMWA_USE_IMMERSIVE_DARK_MODE`: https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
- Microsoft, Support Dark and Light themes in Win32 apps: https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes
- Chromium, `base/win/dark_mode_support.cc` (ordinals and version gates): https://github.com/chromium/chromium/blob/main/base/win/dark_mode_support.cc
- `ysc3839/win32-darkmode` (sample and delay-load branch): https://github.com/ysc3839/win32-darkmode
- Notepad++, `NppDarkMode.cpp` and `DarkMode/DarkMode.cpp`: https://github.com/notepad-plus-plus/notepad-plus-plus
- SumatraPDF, `src/DarkMode.cpp` and `darkmodelib`: https://github.com/sumatrapdfreader/sumatrapdf
- BowPad, `CMainWindow::SetTheme` (ribbon property and version gate): https://github.com/stefankueng/BowPad
- TortoiseGit, `src/TortoiseMerge/MainFrm.cpp` (ribbon property plus composition attribute): https://github.com/TortoiseGit/TortoiseGit

macOS:

- Apple, Choosing a specific appearance for your app: https://developer.apple.com/documentation/appkit/nsappearancecustomization/choosing_a_specific_appearance_for_your_app
- Apple HIG, Dark Mode: https://developer.apple.com/design/human-interface-guidelines/dark-mode
- Zed, matching AppKit chrome to a selected theme (the window-versus-app override trap): https://github.com/zed-industries/zed/commit/8e4e5a39eed932d943cb7a3e7078885b75d1ea54
- NSAppearance, dynamic colours and `performAsCurrentDrawingAppearance`: https://danielcosenza.com/posts/mac-nsappearance-adaptive-colors/

Colour and typography:

- Material Design, dark theme (#121212, elevation overlays, 15.8:1): https://m2.material.io/design/color/dark-theme.html
- Google Fonts, grade and reversed type: https://fonts.google.com/knowledge/glossary/grade
- CHI 2023, adjusting font grade for light and dark polarity: https://dl.acm.org/doi/full/10.1145/3544548.3581552

## Verdict

The feature is feasible on both ports with one real unknown. The document half is a token exercise plus two missing palette slots, and macOS is already halfway there. The Windows chrome half is a known-good sequence of undocumented calls that two shipping applications use, plus a ribbon property that has no contract and needs a stated fallback. Nothing here touches the document model, undo, or printing, which is the property that matters most.

The plan is implemented, with the two P4 items above still open. The ribbon
fallback was taken as written: a light ribbon inside a dark window is the
accepted failure mode, and `SetRibbonDarkMode` ignores the HRESULT on purpose.
The palette split is not an open question any more; the architecture answered
it, and the conformance section records the answer so it does not get
relitigated per platform.

What still needs a human: a CI run on both runners, and an eye on the Windows
ribbon and the DWM frame, which cannot be checked from Linux at all.
