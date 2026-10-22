# Selawik, vendored

The mac build ships these files as a metric compatible stand in for
Segoe UI, so a markdown document wraps at the same words on macOS as it
does on Windows. Selawik is Microsoft's open replacement for Segoe UI
and its advance widths match; it was made for exactly this job.

Source: the `microsoft/Selawik` release 1.01 (`Selawik_Release.zip`).
Files: the TrueType build of Regular, Bold and Semibold. The Semibold
face is used for headings, the way Segoe UI Semibold is on Windows.

Selawik has no italic faces. The mac text engine slants italic runs
itself; see `src/macos/text_engine_mac.cpp`.

`LICENSE-OFL.txt` is the SIL Open Font License 1.1 from the same
repository. Keep it next to the fonts.
