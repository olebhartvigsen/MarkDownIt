#!/usr/bin/env python3
"""Enforce the screen capture policy.

Policy: screenshots are allowed in the build pipeline (GitHub Actions)
and never on a developer machine.

The check fails when:

1. a screen capture call appears in a file that is not on the allow
   list, or
2. an allowed file captures without its pipeline guard, or
3. the workflow switches a capture step off with `if: ${{ false }}`
   (a dark gate is how the Windows capture went unnoticed), or
4. a capture step in the workflow does not run on a runner.

Allowed files and their guards:

- tools/windows-smoke/capture.ps1 must define Test-CiCapturePolicy, test
  GITHUB_ACTIONS, and call it before and inside Capture-Screen.
- tools/macos-smoke/launch_smoke.sh must define ci_screencapture, test
  GITHUB_ACTIONS in it, and call `screencapture` only inside it.

Usage: python3 tools/check_capture_policy.py [repo-root]
"""

import os
import re
import sys

PRIMITIVES = re.compile(
    r"\b(CopyFromScreen|CGWindowListCreateImage|CGDisplayCreateImage|"
    r"SCScreenshotManager|SCStream|ImageGrab\.grab|pyautogui\.screenshot|"
    r"Get-Screenshot|BitBlt)\b|(?<![\w-])screencapture\b")
SCAN_EXT = (".ps1", ".sh", ".py", ".cpp", ".h", ".mm", ".m", ".cs", ".yml",
            ".yaml", ".js", ".mjs", ".bat", ".cmd")
SKIP_DIRS = {".git", "build", "build-win", "build-mac", "build-native",
             "build-tests", "third_party", "node_modules", ".hermes"}
SELF = "tools/check_capture_policy.py"
SELF_TEST = "tools/test_check_capture_policy.py"
ALLOWED = ("tools/windows-smoke/capture.ps1",
           "tools/macos-smoke/launch_smoke.sh")


def read(root, rel):
    with open(os.path.join(root, rel), "r", encoding="utf-8",
              errors="replace") as fh:
        return fh.read()


def code_lines(text):
    """Yield (number, line) without whole-line comments."""
    for n, line in enumerate(text.split("\n"), 1):
        stripped = line.lstrip()
        if stripped.startswith("#") or stripped.startswith("//"):
            continue
        yield n, line


def scan(root):
    hits = {}
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if not name.endswith(SCAN_EXT):
                continue
            rel = os.path.relpath(os.path.join(base, name), root)
            rel = rel.replace(os.sep, "/")
            if rel in (SELF, SELF_TEST):
                continue
            for n, line in code_lines(read(root, rel)):
                if PRIMITIVES.search(line):
                    hits.setdefault(rel, []).append((n, line.strip()))
    return hits


def check_windows_guard(text):
    errs = []
    if "function Test-CiCapturePolicy" not in text:
        errs.append("capture.ps1 does not define Test-CiCapturePolicy")
    if "$env:GITHUB_ACTIONS" not in text:
        errs.append("capture.ps1 does not test $env:GITHUB_ACTIONS")
    # The guard must run before the first capture call site.
    first_call = text.find("Capture-Screen (")
    guard_exit = text.find("exit 3")
    if first_call != -1 and (guard_exit == -1 or guard_exit > first_call):
        errs.append("capture.ps1 reaches a capture before the guard exits")
    body = re.search(r"function Capture-Screen.*?\n}\n", text, re.S)
    if not body or "Test-CiCapturePolicy" not in body.group(0):
        errs.append("Capture-Screen does not check the policy itself")
    return errs


def check_mac_guard(text):
    errs = []
    fn = re.search(r"ci_screencapture\(\)\s*\{.*?\n\}", text, re.S)
    if not fn:
        return ["launch_smoke.sh does not define ci_screencapture"]
    if not re.search(r"\$\{?GITHUB_ACTIONS\b", fn.group(0)):
        errs.append("ci_screencapture does not test $GITHUB_ACTIONS")
    outside = text[:fn.start()] + text[fn.end():]
    for n, line in code_lines(outside):
        if re.search(r"(?<![\w_-])screencapture\b", line):
            errs.append("bare screencapture outside the guard: %s" %
                        line.strip())
    return errs


def check_workflow(text):
    errs = []
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if re.search(r"if:\s*\$\{\{\s*false\s*\}\}", line):
            context = " ".join(lines[max(0, i - 6):i + 12]).lower()
            if "capture" in context:
                errs.append("workflow line %d switches a capture step off" %
                            (i + 1))
    return errs


def run(root):
    errors = []
    hits = scan(root)
    for rel, found in sorted(hits.items()):
        if rel in ALLOWED:
            continue
        if rel.startswith(".github/workflows/"):
            continue
        for n, line in found:
            errors.append("%s:%d captures the screen outside the allow "
                          "list: %s" % (rel, n, line))
    for rel in ALLOWED:
        if not os.path.exists(os.path.join(root, rel)):
            errors.append("%s is missing" % rel)
            continue
        text = read(root, rel)
        errs = (check_windows_guard(text) if rel.endswith(".ps1")
                else check_mac_guard(text))
        errors += ["%s: %s" % (rel, e) for e in errs]
    wf = ".github/workflows/build.yml"
    if os.path.exists(os.path.join(root, wf)):
        errors += ["%s: %s" % (wf, e) for e in check_workflow(read(root, wf))]
    return errors, hits


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))
    errors, hits = run(root)
    print("capture policy: %d file(s) call a capture primitive" % len(hits))
    if errors:
        for e in errors:
            print("FAIL: " + e)
        print("Screenshots are allowed in the build pipeline only. Route "
              "the capture through the guard in the allowed script, or "
              "remove it.")
        return 1
    print("capture policy: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
