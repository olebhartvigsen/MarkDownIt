#!/usr/bin/env python3
"""Check that every mac source file is in the CMake build.

A file that sits in src/macos/ but is in no source list never gets
compiled, so it can rot without anyone noticing. The headers do not go
into a source list, so they are left out.
"""
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAC = os.path.join(REPO, "src/macos")

with open(os.path.join(REPO, "CMakeLists.txt"), "r", encoding="utf-8") as fh:
    cmake = fh.read()

compiled = sorted(f for f in os.listdir(MAC) if f.endswith((".cpp", ".mm")))
headers = sorted(f for f in os.listdir(MAC) if f.endswith(".h"))

missing = [name for name in compiled if ("src/macos/%s" % name) not in cmake]

print("the mac layer has %d compiled sources and %d headers" % (
    len(compiled), len(headers)))
if missing:
    print("FAIL: these are in no source list: %s" % ", ".join(missing))
    sys.exit(1)
print("every mac source file is in the build")
