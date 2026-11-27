#!/usr/bin/env python3
"""Keep the Aptabase launch event out of every build and test run.

The app sends one event per launch, on by default, so any step that starts the
built product during a pipeline run would report from the runner. The veto is
`MARKDOWNIT_NO_ANALYTICS`, read inside `analytics::TrackAppStart`
(`src/analytics.cpp`, proven by `tests/analytics_test.cpp`). That test cannot
see the pipeline, so this check holds the configuration side:

1. every workflow has a top-level `env:` with `MARKDOWNIT_NO_ANALYTICS: 1`,
   which is what all of its jobs and steps inherit,
2. every harness that starts the product sets the variable itself, so a
   developer running it locally is silent too,
3. the Aptabase endpoint is written in one place only. A second literal is a
   second sender, and a sender that does not pass through the veto.

Usage: python3 tools/check_no_telemetry.py [repo-root]
"""

import os
import re
import sys

WORKFLOWS = (".github/workflows/build.yml",
             ".github/workflows/release.yml")

# The harnesses that start the app: the mac launch smoke and the Windows
# capture walk. Both inherit the workflow env in CI and set it themselves for
# a local run.
HARNESSES = ("tools/macos-smoke/launch_smoke.sh",
             "tools/windows-smoke/capture.ps1")

VETO = "MARKDOWNIT_NO_ANALYTICS"

# Where the endpoint may be spelled out: the one definition, and the test that
# pins the definition. Everything else must go through
# `analytics::TrackAppStart`.
ENDPOINT = "aptabase.com"
ENDPOINT_ALLOWED = ("src/analytics.h", "tests/analytics_test.cpp")

SCAN_EXT = (".cpp", ".h", ".mm", ".m", ".c", ".cc", ".cs", ".py", ".sh",
            ".ps1", ".js", ".mjs", ".yml", ".yaml")
SKIP_DIRS = {".git", "build", "build-win", "build-mac", "build-native",
             "build-tests", "build-tools", "third_party", "node_modules",
             ".hermes", "docs"}
SELF = "tools/check_no_telemetry.py"
SELF_TEST = "tools/test_check_no_telemetry.py"


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


def workflow_env_has_veto(text):
    """True when the top-level `env:` block sets the veto to a true value."""
    in_env = False
    for _, line in code_lines(text):
        if not line.strip():
            continue
        at_column_zero = not line[0].isspace()
        if at_column_zero:
            # A new top-level key ends the block we were reading.
            in_env = line.rstrip() == "env:"
            continue
        if in_env:
            m = re.match(r"\s+%s\s*:\s*(\S+)" % VETO, line)
            if m:
                value = m.group(1).strip("'\"")
                # "0", "false" and "" are the documented ways of saying off.
                return value not in ("0", "false", "False", "")
    return False


def scan_endpoint(root):
    """Every file that spells out the endpoint, with line numbers."""
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
            with open(os.path.join(base, name), "r", encoding="utf-8",
                      errors="replace") as fh:
                for n, line in enumerate(fh, 1):
                    if ENDPOINT in line:
                        hits.setdefault(rel, []).append(n)
    return hits


def run(root):
    errors = []

    for rel in WORKFLOWS:
        if not os.path.exists(os.path.join(root, rel)):
            errors.append("%s is missing" % rel)
            continue
        if not workflow_env_has_veto(read(root, rel)):
            errors.append(
                "%s: the top-level env: block does not set %s: 1, so any "
                "step that starts the app would send an event from the "
                "runner" % (rel, VETO))

    for rel in HARNESSES:
        if not os.path.exists(os.path.join(root, rel)):
            errors.append("%s is missing" % rel)
            continue
        if VETO not in read(root, rel):
            errors.append("%s starts the app without setting %s, so a local "
                          "run reports" % (rel, VETO))

    hits = scan_endpoint(root)
    for rel, lines in sorted(hits.items()):
        if rel in ENDPOINT_ALLOWED:
            continue
        errors.append("%s:%s spells out the Aptabase endpoint outside the one "
                      "sender; a second sender does not pass the veto"
                      % (rel, ",".join(str(n) for n in lines)))

    return errors, hits


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))
    errors, hits = run(root)
    print("telemetry: %d file(s) name the Aptabase endpoint" % len(hits))
    if errors:
        for e in errors:
            print("FAIL: " + e)
        print("A build or a test run must send nothing. Set %s in the "
              "workflow env and in the harness, and keep the one sender in "
              "src/analytics.cpp behind analytics::TrackAppStart." % VETO)
        return 1
    print("telemetry: ok, no pipeline or harness can report")
    return 0


if __name__ == "__main__":
    sys.exit(main())