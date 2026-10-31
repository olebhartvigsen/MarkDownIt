#!/usr/bin/env bash
# Launch the app the way a double click ends up doing, and prove it came
# up. The app traces its startup into
# ~/Library/Logs/MarkDownIt/crash_trace.log, and a fatal signal writes a
# backtrace there before the process dies, so the log is the oracle
# whether a person or CI watches the launch.
#
# Usage: launch_smoke.sh [path/to/MarkDownIt.app]
set -uo pipefail

APP="${1:-build-mac/MarkDownIt.app}"
BIN="$APP/Contents/MacOS/MarkDownIt"
LOG="$HOME/Library/Logs/MarkDownIt/crash_trace.log"
WAIT_SECONDS=40

if [ ! -x "$BIN" ]; then
    echo "launch smoke: $BIN is not there or not executable"
    exit 1
fi

rm -f "$LOG"

# The app is started directly, which is where a double click ends up;
# `open` would add LaunchServices and nothing else to what is verified.
"$BIN" &
PID=$!

waited=0
window_up=""
while [ "$waited" -lt $((WAIT_SECONDS * 2)) ]; do
    if [ -f "$LOG" ] && grep -q "the welcome screen is up" "$LOG"; then
        window_up=1
        break
    fi
    if ! kill -0 "$PID" 2>/dev/null; then
        break
    fi
    sleep 0.5
    waited=$((waited + 1))
done

alive=0
kill -0 "$PID" 2>/dev/null && alive=1

fail_smoke() {
    echo "launch smoke FAILED: $1"
    echo "--- $LOG ---"
    if [ -f "$LOG" ]; then tail -50 "$LOG"; else echo "(no log was written)"; fi
    if [ "$alive" = 1 ]; then kill "$PID" 2>/dev/null; fi
    exit 1
}

[ -n "$window_up" ] || fail_smoke "the welcome screen never came up within ${WAIT_SECONDS}s"
[ "$alive" = 1 ] || fail_smoke "the process died during startup"
grep -q '\*\*\* crash' "$LOG" && fail_smoke "a crash was traced during startup"

echo "launch smoke: the window is on screen and the process is alive"

# The quit path, so a crash on the way out is caught too.
kill "$PID" 2>/dev/null
for _ in $(seq 1 40); do
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.5
done
if kill -0 "$PID" 2>/dev/null; then
    kill -9 "$PID" 2>/dev/null
    echo "launch smoke: the app did not quit on a terminate signal, it was killed"
fi

if grep -q '\*\*\* crash' "$LOG"; then
    echo "launch smoke FAILED: a crash was traced while quitting"
    tail -60 "$LOG"
    exit 1
fi

echo "launch smoke: launched, drew its window, quit, no crash traced"
