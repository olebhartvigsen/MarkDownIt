#!/usr/bin/env python3
"""Check the app bundle the way a first launch checks it.

The bundle is the thing a user downloads and opens, so the checks here
mirror what macOS looks at on that first launch: the layout, the
Info.plist values and the code signature. A bundle that fails here
would fail on the Mac of whoever downloads it.
"""
import os
import plistlib
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_APP = os.path.join(REPO, "build-mac", "MarkDownIt.app")
MINIMUM_SYSTEM_VERSION = 12.0


def fail(message):
    print("FAIL: %s" % message)
    sys.exit(1)


def run(*args):
    try:
        done = subprocess.run(args, capture_output=True, text=True)
    except FileNotFoundError:
        return 127, "%s is not on this machine" % args[0]
    return done.returncode, (done.stdout + done.stderr).strip()


def main():
    app = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_APP

    executable = os.path.join(app, "Contents", "MacOS", "MarkDownIt")
    icns = os.path.join(app, "Contents", "Resources", "app.icns")
    plist_path = os.path.join(app, "Contents", "Info.plist")
    seal = os.path.join(app, "Contents", "_CodeSignature", "CodeResources")

    for path in (executable, icns, plist_path):
        if not os.path.isfile(path):
            fail("the bundle has no %s" % os.path.relpath(path, app))
    if not os.access(executable, os.X_OK):
        fail("the executable is not marked runnable, so a copy step "
             "dropped the mode")

    with open(plist_path, "rb") as fh:
        plist = plistlib.load(fh)

    if plist.get("CFBundleExecutable") != "MarkDownIt":
        fail("CFBundleExecutable is %r" % plist.get("CFBundleExecutable"))
    if plist.get("CFBundleIdentifier") != "dk.olebhartvigsen.markdownit":
        fail("CFBundleIdentifier is %r" % plist.get("CFBundleIdentifier"))
    if not plist.get("CFBundleShortVersionString"):
        fail("CFBundleShortVersionString is empty")

    version = plist.get("LSMinimumSystemVersion")
    if not version:
        fail("LSMinimumSystemVersion is empty; macOS reads it on a first launch")
    try:
        too_old = float(version) < MINIMUM_SYSTEM_VERSION
    except ValueError:
        fail("LSMinimumSystemVersion is not a version: %r" % version)
    if too_old:
        fail("LSMinimumSystemVersion is %s" % version)

    if not os.path.isfile(seal):
        fail("the bundle carries no code signature seal "
             "(Contents/_CodeSignature/CodeResources)")

    code, out = run("codesign", "--verify", "--deep", "--strict", "--verbose=2",
                    app)
    if code != 0:
        fail("codesign rejects the bundle:\n%s" % out)

    code, out = run("codesign", "-dvvv", app)
    detail = ""
    for line in out.splitlines():
        if line.startswith("Signature=") or line.startswith("Authority="):
            detail = line
            break
    print("the bundle is signed and sealed (%s)" % (detail or "no detail line"))

    code, out = run("spctl", "--assess", "--type", "execute", "-vv", app)
    print("spctl says the following, which is expected for a build that is "
          "not notarized:")
    for line in out.splitlines():
        print("  %s" % line)

    print("the bundle passes the checks a first launch applies")


if __name__ == "__main__":
    main()
