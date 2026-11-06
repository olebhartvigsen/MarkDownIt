# macOS startup verification and Windows parity

*2026-10-31. The plan that makes "it starts and works on macOS" a
proved fact instead of a hope.*

## Why this exists

The app could not be started on the Mac, and its icon was wrong. Both
were ship blockers that no gate caught:

- **The icon.** `tools/macos-icons/make_icns.py` wrote each block's
  length as the length of its PNG alone. The icns format counts the
  eight byte block header in that number, so every reader, macOS
  included, loses the thread after the first block and falls back to a
  generic icon. The bundle check looked for the file, not inside it.
- **The launch.** Nothing in CI ever started the app. A bundle that
  builds, signs and zips can still die in `main()`, and the first
  person to find out was the user.

## Goal

Every path the user takes (download the zip, open the app, open a
document, use the editor) is either covered by an automated gate in CI
or by a written, one minute manual check. Full compliance between the
Windows and the mac build stays measured against `references/`.

## Layers

The same shape as the test story the port already has, with the two
gaps closed (L1's depth, L2 as a whole).

### L0, the artifact is what we think it is

CI builds the bundle from the commit, and the shipped zip is unzipped
again and re-checked (`ditto -x -k` round trip, already in place). The
hash of the zip is compared whenever it is handed over.

### L1, bundle structure, deep

`tools/check_mac_bundle.py` grows from "the files are there" to "the
files are right":

- **the icns is parsed strictly**: blocks walk with inclusive lengths,
  the last block ends exactly at the declared file size, every block is
  a PNG, and the block set covers 16 to 1024 plus the `@2x` set, with
  the pixel size matching the block type;
- **the binary is read**: 64 bit Mach-O, arm64, minimum macOS from
  `LC_BUILD_VERSION` at most 12.0, and only system libraries in
  `LC_LOAD_DYLIB` (a bundled or stray dylib is a startup crash dyld
  writes);
- **the Info.plist carries the keys macOS reads on a first launch**:
  `CFBundleExecutable`, `CFBundleIdentifier`, `CFBundleIconFile`,
  `CFBundlePackageType`, `CFBundleShortVersionString`,
  `LSMinimumSystemVersion`, `NSPrincipalClass`,
  `NSHighResolutionCapable`;
- the fonts and the strip icons stay checked, `codesign --verify
  --deep --strict` and `spctl` stay reported.

### L2, the launch smoke (new)

The app traces every step of its startup into
`~/Library/Logs/MarkDownIt/crash_trace.log`, and a fatal signal writes
a backtrace into the same log before the process dies. That is the
oracle: it exists on the user's Mac and on the CI runner alike.

`tools/macos-smoke/launch_smoke.sh`:

1. clears the trace log;
2. launches the built binary, the way a double click ends up doing;
3. waits up to 40 seconds for `the window is on screen`;
4. asserts the process is still alive and no `*** crash` line was
   written;
5. quits the app, waits for the exit, and asserts no crash line
   appeared on the way out;
6. prints the log tail on any failure, so a red run explains itself.

CI runs it in the macOS build job right after the bundle checks and
before the artifact upload, so a bundle that cannot start is never
published.

### L3, behaviour suites

Unchanged and already green in CI: the portable suite (now ~7.6k
checks), the mac test suite, the renderer harness with the fake text
engine, and the app layer under the mac compile gate. This plan only
moves the launch step into the same gate.

### L4, parity gates

The checkers that keep the mac build honest next to the Windows one
stay in the red-run set: `check_mac_ribbon.py` (the strip mirrors
`ribbon.xml`), `check_mac_menu.py` (every menu command reaches a
handler), `check_mac_sources.py` (every source is in the build),
`check_shared_guards.py` (no Windows API in shared code), the office
and Mermaid oracles, and the reference audits under `references/`.

### L5, the human minute

What no runner can see (the real Finder icon, the real double click
experience, window placement across monitors) stays a written checklist
in `references/macos-port-status.md`, with the crash trace log as the
first thing to read when something looks wrong:

    ~/Library/Logs/MarkDownIt/crash_trace.log

## The reference compliance ledger

`references/` is the specification set for both platforms. The second
re-audit (2026-10-23) reconciled every row: 239 compliant, 9 blocking
and 69 minor. The blocking rows and the minors are closed batch by
batch in
`.hermes/plans/2026-10-23_173000-macos-spec-gap-closure-2.md`
(batches A through J landed: H, accessibility and ribbon, in `8f10ce6`;
the first audit round's twelve mac gaps as Batch I; the second round's
leftovers as Batch J), and the rows that have no mac side at all are
listed there as open on both platforms.

## Acceptance

- The macOS build job is green with the launch smoke and the deep
  bundle check enabled.
- The published zip, unzipped on a Mac, opens with the documented first
  run steps and reaches the welcome screen.
- The gap plan closes its last batch, and every remaining audit row is
  either implemented or explicitly marked as open on both platforms.

## What happened

- The icns writer was fixed (`51423d5`): the block lengths now include
  their own eight byte header, `installer/macos/app.icns` was rebuilt
  from the fixed writer, and `tools/check_mac_bundle.py` parses the
  icon block by block, reads the executable the way dyld reads it and
  requires the plist keys a first launch wants.
- The launch smoke was added in the same commit and runs in the mac
  build job before the zip step: the app is started the way a double
  click starts it, the window trace is awaited in
  `~/Library/Logs/MarkDownIt/crash_trace.log`, crash breadcrumbs fail
  the job, and the app is quit again.
- Batch H (accessibility, IME composition, find announcements) landed
  in `8f10ce6`; its first runner compile surfaced three defects that
  are fixed in `9d36293` (the accessibility map used the renderer's
  private region list, the nested type was not named, and the model's
  UTF-32 cell text went into a UTF-8 label).
- The green run is `41247337901` (`9d36293`): every job passed,
  including the launch smoke; the mac suite reports 7918 checks, 0
  failures, 694 passed, 1 known gap.
- The delivery at `/workspace/MarkDownIt-macos-arm64/` was rebuilt
  from that run (zip md5 `563da6efca12cdc5b52fe4976e41545e`, 1,961,081
  bytes) and the repaired icon is verified inside the shipped bundle.
- The local mac compile gate was restored (the 11.3 SDK is the arm64
  capable one; the 10.15 SDK predates Apple Silicon) and eight mac
  sources compile clean against it; the recipe lives in the
  `markdownit-linux-portable-tests` skill.
- The fresh audit of all reference specs was started on October 31,
  2026; its gap list closes the last acceptance bullet.
- The audit finished: the first round (three read-only audits of every
  reference doc) found twelve mac gaps, closed as Batch I; the second
  round (three more, covering the rest of the docs) returned 222
  compliant rows and three mac-side leftovers, closed as Batch J.
  Everything else it listed is open on both platforms and stays
  tracked in the gap-closure plan.
- The Return crash (the user's report) was root-caused with an ASan
  repro of the edit flow and fixed (`fb41a9a`, `75d247b`); the fix and
  Batch J passed CI, and the delivery was rebuilt from the newest
  green run (`41250669654`, zip md5
  `d999568904a74c35df53418e974845a6`, 1,966,571 bytes).
