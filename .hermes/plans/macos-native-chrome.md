# Plan: the mac window wears native chrome

One-time task plan. The task: the mac build's ribbon looked like a
Windows control sitting in a mac window. Research the official Apple
guidance, then implement a mac-app chrome that follows it. The user
allowed anything, a ribbon included, as long as it complies.

## Research, short form

Sources read: the macOS Human Interface Guidelines pages for Windows,
Toolbars, Sidebars, The Menu Bar, and Designing for macOS, plus the
two standing docs already in the repo (osx-markdown-visual-style-guide
and ribbon-menu-guidelines).

Findings that decide the design:

- Windows: "Avoid creating custom window UI. ... don't try to replicate
  the system-provided appearance. Doing so without perfectly matching
  the system's look and behavior can make your app feel broken."
- Toolbars: choose items deliberately to avoid overcrowding; prefer
  system-provided symbols without borders; reduce toolbar backgrounds
  and tinted controls; consider letting people customize.
- Sidebars: disclosure controls for hierarchy; show or hide through a
  button and View menu commands; icons follow the user's accent colour.
- The menu bar is where the app name and the long tail of commands
  live on macOS.

Verdict: the ribbon strip goes, a standard NSToolbar comes in. The
Windows window keeps its ribbon; ribbon-menu-guidelines.md keeps
governing that window. The mac chrome gets its own standing doc,
references/macos-native-chrome.md, with the citations.

## The work, in batches

- [x] HA, the toolbar: src/macos/toolbar_mac.h/.mm (six items, system
      symbols, the Edit/Done switch, customization on), ribbon_mac.*
      deleted, main_mac rewired (title bar standard, toolbarSync,
      pointer-anchored table picker, document-name title with folder
      subtitle), CMake sources swapped and the raster icon set dropped
      from the bundle, tools/check_mac_chrome.py added,
      check_mac_bundle.py loses the strip-icon check, the comparison
      tool checks the Windows palette only.
- [x] HB, the sidebar: the outline pane wears the sidebar material
      (vibrancy backdrop, system selection colour with rounded corner,
      label colours, disclosure chevrons). Landed in 2e522b7.
- [x] HC, the find bar: the system's search field, a segmented
      control with the system's chevrons for the match steps,
      standard push buttons; behaviour unchanged.
- [x] HD, the finish: screenshots reviewed on CI, docs updated. The
      review found real drift the checker could not: the chevron's
      tint punched a square hole in the selection band, the rounded
      rectangle fill pinched every rounded shape, the find bar's rows
      floated mid-bar, and a Windows capture caught its blinking
      caret. All four are fixed and recorded in references/macos-qa.md
      (f686ed9, 2c622b6, cbe7547, a3b7e3e).

## Verification

- Local gate: every touched mac source compiles against the 11.3 SDK
  with the zig toolchain; check_mac_chrome, check_mac_sources,
  check_mac_menu run green.
- CI: the macOS build, tests, and capture-parity jobs. The parity tool
  still compares the two apps' document panes structurally per state;
  its colour checks now cover the Windows palette alone, because the
  mac chrome follows the system (appearance, accent colour, wallpaper)
  by design.
- Screenshots: the QA walk's mac captures show the toolbar; they are
  read back with vision and fixed where they look off.

## Notes and risks

- The Windows ribbon framework cannot switch tabs from code, so the
  two walks both stay on their open state; the GZ commit already made
  the walk stop switching.
- NSToolbar perf: items are created once; validation runs through the
  window delegate's toolbarSync, not through AppKit's autovalidation,
  so the states stay deterministic in screenshots.
- The strip icons no longer ship in the mac bundle; the Windows exe
  still embeds its own set.