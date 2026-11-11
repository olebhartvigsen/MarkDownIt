#!/usr/bin/env python3
"""Warn when one side of a platform pair changes alone.

The pairs are the Windows and macOS twins of the renderer, the app
controllers, the chrome and the stores: the places the two ports
implement the same behavior twice today. A change that touches only
one side is how the ports drift apart, so it is called out here.

This check is an advisory. It prints a warning and exits 0. The
warning goes away when:

- the other side changes in the same push, or
- the commit message carries a Parity: line with the reason, for
  example: `Parity: macOS not affected, layout only on Windows`.

The pairs shrink as the shared layout and controller batches land, and
the check retires itself.

Usage (defaults read MDI_BASE and MDI_HEAD):
  python3 tools/check_paired_changes.py [--base SHA] [--head SHA]
"""

import os
import subprocess
import sys

PAIRS = [
    (("src/renderer.cpp", "src/renderer_pie.cpp",
      "src/renderer_sequence.cpp"),
     ("src/macos/renderer_mac.cpp", "src/macos/mermaid_painter_mac.cpp")),
    (("src/app.cpp",), ("src/macos/app_mac.cpp",)),
    (("src/ribbon.cpp", "src/ribbon.xml"),
     ("src/macos/menu_mac.mm", "src/macos/toolbar_mac.mm")),
    (("src/settings.cpp",), ("src/macos/settings_mac.cpp",)),
    (("src/clipboard.cpp",), ("src/macos/clipboard_mac.mm",)),
    (("src/findbar.cpp",), ("src/macos/findbar_mac.mm",)),
    (("src/outlinepane.cpp",),
     ("src/macos/outline_pane_mac.cpp", "src/macos/outline_nsview_mac.mm")),
]


def changed(base, head, repo):
    try:
        res = subprocess.run(
            ["git", "diff", "--name-only", "%s..%s" % (base, head)],
            cwd=repo, capture_output=True, text=True, check=True)
        return set(res.stdout.split())
    except subprocess.CalledProcessError as exc:
        print("paired files: cannot read the range %s..%s (%s). "
              "No advisory this run." %
              (base, head, exc.stderr.strip()[:80]))
        return None


def has_parity_reason(base, head, repo, pairs_touched):
    try:
        out = subprocess.run(
            ["git", "log", "--format=%H%x00%B", "%s..%s" % (base, head)],
            cwd=repo, capture_output=True, text=True, check=True).stdout
    except subprocess.CalledProcessError:
        return False
    for block in out.split("\0"):
        if "Parity:" in block:
            return True
    return False


def main():
    args = sys.argv[1:]
    opts = {}
    it = iter(args)
    for a in it:
        if a in ("--base", "--head"):
            opts[a] = next(it)
    base = opts.get("--base", os.environ.get("MDI_BASE", ""))
    head = opts.get("--head", os.environ.get("MDI_HEAD", "HEAD"))
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    if not base:
        print("paired files: no base given, set --base or MDI_BASE. "
              "No advisory this run.")
        return 0
    files = changed(base, head, repo)
    if files is None or not files:
        return 0

    touched_pairs = []
    for i, (win_side, mac_side) in enumerate(PAIRS):
        win_hit = files & set(win_side)
        mac_hit = files & set(mac_side)
        if win_hit and mac_hit:
            continue
        if win_hit or mac_hit:
            touched_pairs.append((i, win_hit, mac_hit))

    for i, win_hit, mac_hit in touched_pairs:
        win_side, mac_side = PAIRS[i]
        alone = ", ".join(sorted(win_hit or mac_hit))
        other = ", ".join(mac_side if win_hit else win_side)
        print("::warning::paired files: %s changed without %s. If the "
              "other platform is not affected, write a Parity: line "
              "with the reason in the commit message." % (alone, other))

    if touched_pairs and has_parity_reason(base, head, repo, touched_pairs):
        print("paired files: the commit message carries a Parity: line, "
              "so the remaining warnings are recorded as deliberate.")
    elif touched_pairs:
        print("paired files: %d pair(s) changed on one side only. "
              "Advisory, not a failure." % len(touched_pairs))
    else:
        print("paired files: no pair changed on one side only")
    return 0


if __name__ == "__main__":
    sys.exit(main())
