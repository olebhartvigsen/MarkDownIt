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

# A picture of what the app drew, while it is still up. Best effort: a
# session without a screen, or without the capture permission, must not
# turn into a smoke failure; the artifact just comes up empty.
sleep 1
SHOT="${SMOKE_CAPTURE:-build-mac/window.png}"
mkdir -p "$(dirname "$SHOT")"
if screencapture -x "$SHOT" 2>/dev/null && [ -s "$SHOT" ]; then
    echo "launch smoke: captured the screen to $SHOT"
else
    echo "launch smoke: no screen capture (refused, or no display)"
    rm -f "$SHOT"
fi

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

# The close path: the window is closed the way a person closes it, and
# the app quits through the last-window route. This is where the frame
# is saved while the window is still alive; reading it after the close
# was the reported CTD.
rm -f "$LOG"
MARKDOWNIT_CLOSE_SMOKE=1 "$BIN" >/dev/null 2>&1 &
PID=$!
for _ in $(seq 1 80); do
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.25
done
if kill -0 "$PID" 2>/dev/null; then
    kill -9 "$PID" 2>/dev/null
    echo "launch smoke FAILED: the app did not quit after its window closed"
    exit 1
fi
if grep -q '\*\*\* crash' "$LOG" 2>/dev/null; then
    echo "launch smoke FAILED: a crash was traced while closing the window"
    tail -60 "$LOG"
    exit 1
fi
echo "launch smoke: closed the window, quit, no crash traced"

# The QA walk: the app opens the showcase document and steps through
# its states by itself, announcing each one in the trace. The watcher
# here photographs the window on every announcement, so the CI run
# carries pictures of the states without any UI driving.
QA_DIR="build-mac/qa"
DOC="$(pwd)/tests/fixtures/layout-showcase.md"
if [ -f "$DOC" ] && [ "${SMOKE_QA:-1}" = "1" ]; then
    rm -rf "$QA_DIR"
    mkdir -p "$QA_DIR"
    rm -f "$LOG"
    MARKDOWNIT_QA_HIT_TRACE=1 \
    MARKDOWNIT_QA_STATES="doc,wheel,find,edit,outline,source,lower,end,tail,tail2,narrow,wide,no-outline,grow,no-headings,svg,svg-fresh,svg-swap,mark,welcome,welcome-scrolled,dark,settings,keys-source,keys-doc" "$BIN" "$DOC" &
    PID=$!
    seen=" "
    waited=0
    while [ "$waited" -lt 240 ]; do
        for name in doc wheel find edit outline source lower end tail tail2 narrow wide no-outline grow no-headings svg svg-fresh svg-swap mark welcome welcome-scrolled dark settings keys-source keys-doc; do
            case "$seen" in *" $name "*) continue ;; esac
            if grep -q "qa state ready: $name" "$LOG" 2>/dev/null; then
                sleep 0.6
                if screencapture -x "$QA_DIR/$name.png" 2>/dev/null; then
                    echo "launch smoke: captured $QA_DIR/$name.png"
                fi
                # The state's document rectangle, so the cross-platform
                # comparison crops both sides to the same part.
                rect=$(grep -m1 "qa state ready: $name " "$LOG" \
                    | sed -n 's/.* rect=//p')
                if [ -n "$rect" ]; then
                    echo "$name $rect" >> "$QA_DIR/rects.txt"
                fi
                seen="$seen$name "
            fi
        done
        if grep -q "qa states done" "$LOG" 2>/dev/null; then
            break
        fi
        kill -0 "$PID" 2>/dev/null || break
        sleep 0.5
        waited=$((waited + 1))
    done
    # The app quits itself at the end of its walk; make sure it is
    # gone either way.
    for _ in $(seq 1 20); do
        kill -0 "$PID" 2>/dev/null || break
        sleep 0.5
    done
    if kill -0 "$PID" 2>/dev/null; then
        kill -9 "$PID" 2>/dev/null
    fi
    # The reader's report: with the outline pane closed, a window that
    # grows must give the text the new width instead of keeping the
    # frame it had. The grow state shrinks to 786 and grows to 1024.
    GROW_BEFORE=$(grep "qa grow: before" "$LOG" 2>/dev/null \
        | sed -n 's/.*document=\([0-9]*\).*/\1/p' | head -1)
    GROW_AFTER=$(grep "qa grow: settled" "$LOG" 2>/dev/null \
        | sed -n 's/.*document=\([0-9]*\).*/\1/p' | head -1)
    if [ -n "$GROW_BEFORE" ] && [ -n "$GROW_AFTER" ]; then
        if [ $((GROW_AFTER - GROW_BEFORE)) -lt 150 ]; then
            echo "launch smoke FAILED: the text kept its width when the" \
                 "window grew with the pane closed" \
                 "($GROW_BEFORE -> $GROW_AFTER)"
            exit 1
        fi
        echo "launch smoke: the text follows a window that grows with" \
             "the pane closed ($GROW_BEFORE -> $GROW_AFTER)"
    fi
    # The reader's report: a mark made with the space beside the word
    # in the selection must store the words alone. The mark state marks
    # "text with spaces " at bytes 0 to 17 and " leading" at 23 to 31.
    if grep -q "qa mark: markers=" "$LOG" 2>/dev/null; then
        if ! grep -q 'qa mark: exact="text with spaces" start=0 end=16' \
                "$LOG" 2>/dev/null; then
            echo "launch smoke FAILED: a mark kept the trailing space"
            grep "qa mark:" "$LOG" | head -5
            exit 1
        fi
        if ! grep -q 'qa mark: exact="leading" start=24 end=31' \
                "$LOG" 2>/dev/null; then
            echo "launch smoke FAILED: a mark kept the leading space"
            grep "qa mark:" "$LOG" | head -5
            exit 1
        fi
        if ! grep -q 'qa mark: exact="code" start=50 end=54' \
                "$LOG" 2>/dev/null; then
            echo "launch smoke FAILED: a mark on inline code kept its syntax"
            grep "qa mark:" "$LOG" | head -5
            exit 1
        fi
        echo "launch smoke: a mark sits on the words, not on the space" \
             "beside them"
    fi
    # The second picture opened after another one must look like the
    # second picture. The parsed drawing used to be keyed by the block's
    # offset alone, and a standalone SVG sits at the same offset in every
    # file, so a swap quietly kept the first drawing.
    if [ -f "$QA_DIR/svg-fresh.png" ] && [ -f "$QA_DIR/svg-swap.png" ]; then
        if cmp -s "$QA_DIR/svg-fresh.png" "$QA_DIR/svg-swap.png"; then
            echo "launch smoke: a second SVG shows its own drawing"
        else
            echo "launch smoke FAILED: the swapped SVG kept the first drawing"
            grep "qa svg:" "$LOG" | tail -4
            exit 1
        fi
    fi

    # The recents page must fit the window and start at its own origin,
    # even when the document behind it was scrolled sideways. The page
    # used to be drawn at the document's scroll position, shifted left
    # and clipped, with a scroller under it.
    BX=$(sed -n 's/.*qa welcome-scrolled: before offset=\([0-9-]*\).*/\1/p' \
        "$LOG" 2>/dev/null | tail -1)
    OX=$(sed -n 's/.*qa welcome-scrolled: after offset=\([0-9-]*\).*/\1/p' \
        "$LOG" 2>/dev/null | tail -1)
    FW=$(sed -n 's/.*after offset=[^ ]* frame=\([0-9-]*\).*/\1/p' \
        "$LOG" 2>/dev/null | tail -1)
    CW=$(sed -n 's/.*after offset=[^ ]* frame=[^ ]* clip=\([0-9-]*\).*/\1/p' \
        "$LOG" 2>/dev/null | tail -1)
    if [ -z "$BX" ] || [ "$BX" -le 0 ]; then
        echo "launch smoke FAILED: the wide page did not scroll sideways"
        grep "qa welcome-scrolled:" "$LOG" | head -4
        exit 1
    fi
    if [ -z "$OX" ] || [ -z "$FW" ] || [ -z "$CW" ]; then
        echo "launch smoke FAILED: the recents page reported no geometry"
        grep "qa welcome-scrolled:" "$LOG" | head -4
        exit 1
    fi
    if [ "$OX" != "0" ] || [ "$FW" -gt "$CW" ]; then
        echo "launch smoke FAILED: the recents page kept the document's scroll"
        grep "qa welcome-scrolled:" "$LOG"
        exit 1
    fi
    echo "launch smoke: the recents page fits the window, at its own origin"

    # Keep the app's trace with the captures, so the artifact carries
    # everything the run said.
    if [ -f "$LOG" ]; then
        cp "$LOG" "$QA_DIR/crash_trace.log"
    fi
    if grep -q '\*\*\* crash' "$LOG" 2>/dev/null; then
        echo "launch smoke FAILED: a crash was traced during the QA walk"
        tail -60 "$LOG"
        exit 1
    fi
    shots=$(ls "$QA_DIR"/*.png 2>/dev/null | wc -l | tr -d ' ')
    echo "launch smoke: the QA walk produced $shots state captures"
else
    echo "launch smoke: no showcase fixture (or SMOKE_QA=0), the QA walk was skipped"
fi

# The Finder's route: a cold launch where the file arrives with the
# double click. The Apple event lands before the window exists, so the
# document is open before the screen is built, and the window has to
# show it rather than the recent documents.
COLD_DOC="$(pwd)/tests/fixtures/diagram.svg"
if [ -f "$COLD_DOC" ]; then
    mkdir -p build-mac/qa
    APP_ABS="$(cd "$(dirname "$APP")" && pwd)/$(basename "$APP")"
    rm -f "$LOG"
    open -n -a "$APP_ABS" "$COLD_DOC"
    waited=0
    while [ "$waited" -lt 120 ]; do
        if [ -f "$LOG" ] && grep -q "launch: file=" "$LOG"; then break; fi
        sleep 0.5
        waited=$((waited + 1))
    done
    if ! grep -q "launch: file=.*welcome=0" "$LOG" 2>/dev/null; then
        echo "launch smoke FAILED: a document opened at launch was buried under the recent documents"
        tail -40 "$LOG" 2>/dev/null
        pkill -x MarkDownIt 2>/dev/null
        exit 1
    fi
    sleep 1
    screencapture -x "build-mac/qa/cold-open.png" 2>/dev/null || true
    cp "$LOG" "build-mac/qa/cold-open-trace.log" 2>/dev/null || true
    pkill -x MarkDownIt 2>/dev/null
    echo "launch smoke: the document opened at launch is on screen"
fi
