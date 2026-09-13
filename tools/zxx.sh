#!/bin/bash
# Local compile gate: build a test binary with zig c++ (no MSVC in container).
set -e
cd /workspace/MarkDownIt
Z=~/.local/zig-linux-aarch64-0.13.0/zig
$Z c++ -std=c++17 -I. -Isrc -Ithird_party/md4c "$@"