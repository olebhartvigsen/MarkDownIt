#!/usr/bin/env python3
"""Compare the Windows and macOS transcript digests, case by case.

Both test jobs run the same editing transcripts (tests/transcripts/,
fed through the shared document controller by the runner in
tests/gtest_lite-based targets) and write a digest per case when
MDI_TRANSCRIPT_OUT is set: the case name, whether it passed, the undo
depth the case reached and the hash of the final text. This check diffs
the two digests the way tools/check_svg_parity.py diffs the drawing
dumps: the shared core must answer both platforms identically, so a
difference in any field of any case is a platform branch that changed
editing behavior (plan finding 2).

It fails when:

- a case exists on one platform only, so one runner quietly skipped
  part of the corpus;
- a case's name, outcome, undo depth or final text hash differs
  between the platforms;
- a digest is malformed: a case without one of the four fields, or
  two cases under one name, is a broken runner rather than parity.

Until the transcript corpus lands (batch 5 family 3), both digests
hold an empty case list; the check passes then and prints the reason,
so the gate can go required before the corpus exists.

Usage:
  python3 tools/check_transcript_parity.py --windows F --macos F
"""

import json
import os
import sys

# The four fields every case must carry. The comparison is exact:
# equality is the point of the gate.
CASE_FIELDS = ("name", "passed", "undo_depth", "text_hash")


def read_digest(path, side):
    """The cases by name, or None when the digest is unusable.

    Malformed digests are reported with a FAIL line here; main turns
    the None into its usage-exit code, the way the inventory checker
    does, so the unit tests can drive the paths as returns.
    """
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    except OSError as err:
        print("FAIL: the %s digest %s could not be read (%s)." %
              (side, path, err))
        return None
    except ValueError as err:
        print("FAIL: the %s digest %s is not valid JSON (%s)." %
              (side, path, err))
        return None
    if not isinstance(data, dict) or not isinstance(
            data.get("cases"), list):
        print("FAIL: the %s digest %s must hold an object with a "
              "cases list, for example {\"cases\": []}." % (side, path))
        return None
    cases = {}
    for entry in data["cases"]:
        if not isinstance(entry, dict):
            print("FAIL: the %s digest has a case that is not an "
                  "object." % side)
            return None
        missing = [f for f in CASE_FIELDS if f not in entry]
        if missing:
            print("FAIL: the %s digest has a case without %s. Cases "
                  "carry %s." % (side, " and ".join(missing),
                                 ", ".join(CASE_FIELDS)))
            return None
        name = entry["name"]
        if not isinstance(name, str) or not name:
            print("FAIL: the %s digest has a case whose name is not a "
                  "non-empty string." % side)
            return None
        if not isinstance(entry["passed"], bool):
            print("FAIL: the case %s in the %s digest has no boolean "
                  "passed field." % (name, side))
            return None
        if not isinstance(entry["undo_depth"], int) or \
                entry["undo_depth"] < 0:
            print("FAIL: the case %s in the %s digest has no "
                  "non-negative integer undo_depth." % (name, side))
            return None
        if not isinstance(entry["text_hash"], str) or \
                not entry["text_hash"]:
            print("FAIL: the case %s in the %s digest has no "
                  "non-empty text_hash." % (name, side))
            return None
        if name in cases:
            print("FAIL: the %s digest names the case %s twice." %
                  (side, name))
            return None
        cases[name] = entry
    return cases


def main():
    args = sys.argv[1:]
    flags = {}
    it = iter(args)
    for a in it:
        if a in ("--windows", "--macos"):
            flags[a] = next(it)
        else:
            print("FAIL: unknown argument %s" % a)
            return 2
    missing = [k for k in ("--windows", "--macos") if k not in flags]
    if missing:
        print("FAIL: %s is required" % " and ".join(missing))
        return 2

    win = read_digest(flags["--windows"], "windows")
    if win is None:
        return 2
    mac = read_digest(flags["--macos"], "macos")
    if mac is None:
        return 2

    errors = []
    for name in sorted(set(win) | set(mac)):
        if name not in win:
            errors.append("%s exists in the macOS digest only" % name)
            continue
        if name not in mac:
            errors.append("%s exists in the Windows digest only" % name)
            continue
        for field in ("passed", "undo_depth", "text_hash"):
            if win[name][field] != mac[name][field]:
                errors.append("%s: %s differs (windows %r, macos %r)"
                              % (name, field, win[name][field],
                                 mac[name][field]))

    print("transcript digests: %d windows, %d macos, %d shared, "
          "%d differing" % (len(win), len(mac),
                            len(set(win) & set(mac)), len(errors)))
    if errors:
        for error in errors:
            print("FAIL: " + error)
        print("The fix belongs in the platform branch that changed "
              "behavior, never in this checker. Re-run the transcript "
              "corpus on both runners (MDI_TRANSCRIPT_OUT) after "
              "correcting it.")
        return 1
    if not win and not mac:
        print("both digests hold no cases yet: the transcript corpus "
              "(batch 5) writes them through MDI_TRANSCRIPT_OUT")
    else:
        print("the two transcript digests agree on every case")
    return 0


if __name__ == "__main__":
    sys.exit(main())
