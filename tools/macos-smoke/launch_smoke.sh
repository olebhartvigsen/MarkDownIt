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
    MARKDOWNIT_QA_STATES="doc,wheel,find,edit,outline,source,lower,end,tail,tail2,narrow,wide,no-outline,grow,no-headings,svg,svg-fresh,svg-swap,zoom-hud,zoom-hud-clear,zoom-reopen,mark,welcome,welcome-scrolled,dark,zoom-hud-dark,zoom-hud-clear,settings,keys-source,keys-doc" "$BIN" "$DOC" &
    PID=$!
    seen=" "
    waited=0
    while [ "$waited" -lt 240 ]; do
        for name in doc wheel find edit outline source lower end tail tail2 narrow wide no-outline grow no-headings svg svg-fresh svg-swap zoom-hud zoom-hud-clear zoom-reopen mark welcome welcome-scrolled dark zoom-hud-dark zoom-hud-clear settings keys-source keys-doc; do
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
    # The zoom overlay: a level that names itself while it changes, and
    # nothing left of it once it has gone.
    ZOOM_PERCENT=$(grep -o "percent=[0-9]*" "$LOG" 2>/dev/null \
        | head -1 | cut -d= -f2)
    ZOOM_SHOWING=$(grep -o "showing=[0-9]" "$LOG" 2>/dev/null \
        | head -1 | cut -d= -f2)
    if [ -z "$ZOOM_PERCENT" ] || [ -z "$ZOOM_SHOWING" ]; then
        echo "launch smoke FAILED: no zoom overlay in the trace"
        exit 1
    fi
    if [ "$ZOOM_PERCENT" -le 100 ] || [ "$ZOOM_SHOWING" != "1" ]; then
        echo "launch smoke FAILED: the zoom overlay did not name the new" \
             "level (percent=$ZOOM_PERCENT showing=$ZOOM_SHOWING)"
        exit 1
    fi
    echo "launch smoke: the zoom overlay names the level ($ZOOM_PERCENT%)"
    if ! grep -q "qa zoom-hud: cleared showing=0" "$LOG" 2>/dev/null; then
        echo "launch smoke FAILED: the zoom overlay was still up when it" \
             "had been taken down"
        grep "qa zoom-hud:" "$LOG" | head -3
        exit 1
    fi
    echo "launch smoke: the zoom overlay leaves nothing behind"

    # A document opens at 100%: the level belongs to the page the reader
    # is on, not to the app or to the last file.
    ZOOM_REOPEN=$(grep "qa zoom-reopen:" "$LOG" 2>/dev/null | head -1)
    if [ -z "$ZOOM_REOPEN" ]; then
        echo "launch smoke FAILED: no zoom-reopen in the trace"
        exit 1
    fi
    REOPEN_BEFORE=$(printf '%s' "$ZOOM_REOPEN" | grep -o "before=[0-9]*" | cut -d= -f2)
    REOPEN_AFTER=$(printf '%s' "$ZOOM_REOPEN" | grep -o "after=[0-9]*" | cut -d= -f2)
    REOPEN_SHOWING=$(printf '%s' "$ZOOM_REOPEN" | grep -o "showing=[0-9]" | cut -d= -f2)
    if [ "$REOPEN_BEFORE" -le 100 ] || [ "$REOPEN_AFTER" != "100" ]; then
        echo "launch smoke FAILED: a document did not open at 100%" \
             "(before=$REOPEN_BEFORE after=$REOPEN_AFTER)"
        exit 1
    fi
    if [ "$REOPEN_SHOWING" != "0" ]; then
        echo "launch smoke FAILED: the 100% reset flashed the zoom overlay"
        exit 1
    fi
    echo "launch smoke: a document opens at 100%, quietly" \
         "($REOPEN_BEFORE% -> $REOPEN_AFTER%)"

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
    # Keep the app's trace with the captures, so the artifact carries
    # everything the run said.
    if [ -f "$LOG" ]; then
        cp "$LOG" "$QA_DIR/crash_trace.log"

    # The second picture opened after another one must look like the
    # second picture. The parsed drawing used to be keyed by the block's
    # offset alone, and a standalone SVG sits at the same offset in every
    # file, so a swap quietly kept the first drawing.
    FRESH_H=$(sed -n 's/.*qa svg: fresh .* height=\([0-9]*\).*/\1/p' \
        "$LOG" 2>/dev/null | tail -1)
    SWAP_H=$(sed -n 's/.*qa svg: swap .* height=\([0-9]*\).*/\1/p' \
        "$LOG" 2>/dev/null | tail -1)
    DIAGRAM_H=$(sed -n 's/.*qa svg: picture .* height=\([0-9]*\).*/\1/p' \
        "$LOG" 2>/dev/null | tail -1)
    if [ -z "$FRESH_H" ] || [ -z "$SWAP_H" ]; then
        echo "launch smoke FAILED: the SVG states reported no page height"
        grep "qa svg:" "$LOG" | tail -4
        exit 1
    fi
    if [ "$FRESH_H" != "$SWAP_H" ]; then
        echo "launch smoke FAILED: the swapped SVG kept the first drawing"
        grep "qa svg:" "$LOG" | tail -4
        exit 1
    fi
    if [ -n "$DIAGRAM_H" ] && [ "$DIAGRAM_H" = "$SWAP_H" ]; then
        echo "launch smoke FAILED: the two SVG fixtures draw the same page"
        grep "qa svg:" "$LOG" | tail -4
        exit 1
    fi
    echo "launch smoke: a second SVG shows its own drawing"

    # The three everyday actions, in both views. The rendered view may
    # walk over hidden syntax, so its steps are checked as "at least as
    # far as the press count"; the source view draws every byte, so its
    # steps are exact.
    KEYS_SRC=$(grep "qa keys source:" "$LOG" | tail -1)
    KEYS_DOC=$(grep "qa keys doc:" "$LOG" | tail -1)
    if [ -z "$KEYS_SRC" ] || [ -z "$KEYS_DOC" ]; then
        echo "launch smoke FAILED: the key runs reported nothing"
        grep "qa keys" "$LOG" | head -6
        exit 1
    fi
    SRC_NEXT=$(sed -n 's/.*qa keys rect: ok=1 .* next_line=\([0-9]*\).*/\1/p' \
        "$LOG" | sed -n '1p')
    SRC_RIGHT=$(echo "$KEYS_SRC" | sed 's/.*right=+\([0-9]*\).*/\1/')
    SRC_CARET=$(echo "$KEYS_SRC" | sed 's/.*caret=\([0-9]*\).*/\1/')
    SRC_DOWN=$(echo "$KEYS_SRC" | sed 's/.*down=\([0-9]*\).*/\1/')
    SRC_LEN=$(echo "$KEYS_SRC" | sed 's/.*typed_len=\([0-9]*\)->\([0-9]*\).*/\1 \2/')
    SRC_AFTER=$(echo "$KEYS_SRC" | sed 's/.*caret_after_type=\([0-9]*\).*/\1/')
    SRC_SCROLL=$(echo "$KEYS_SRC" | sed 's/.*scroll=\([0-9]*\).*/\1/')
    SRC_HEIGHT=$(echo "$KEYS_SRC" | sed 's/.*height=\([0-9]*\).*/\1/')
    SRC_VIEW=$(echo "$KEYS_SRC" | sed 's/.*view=\([0-9]*\).*/\1/')
    DOC_RIGHT=$(echo "$KEYS_DOC" | sed 's/.*right=+\([0-9]*\).*/\1/')
    DOC_CARET=$(echo "$KEYS_DOC" | sed 's/.*caret=\([0-9]*\).*/\1/')
    DOC_RIGHT_TO=$(echo "$KEYS_DOC" | sed 's/.*right_to=\([0-9]*\).*/\1/')
    DOC_DOWN=$(echo "$KEYS_DOC" | sed 's/.*down=\([0-9]*\).*/\1/')
    if [ "$SRC_RIGHT" != "5" ]; then
        echo "launch smoke FAILED: five rights in the source view moved $SRC_RIGHT"
        echo "$KEYS_SRC"
        exit 1
    fi
    # Down lands on the line below, in the column the arrows left the
    # caret in, so the offset sits inside that line and not before it.
    if [ -z "$SRC_NEXT" ] || [ "$SRC_DOWN" -lt "$SRC_NEXT" ] || \
            [ "$SRC_DOWN" -gt "$(( SRC_NEXT + 120 ))" ]; then
        echo "launch smoke FAILED: down in the source view skipped a line"
        grep "qa keys rect" "$LOG" | tail -2
        echo "$KEYS_SRC"
        exit 1
    fi
    set -- $SRC_LEN
    if [ "$2" != "$(( $1 + 1 ))" ] || [ "$SRC_AFTER" != "$(( SRC_DOWN + 1 ))" ]; then
        echo "launch smoke FAILED: typing in the source view went nowhere"
        echo "$KEYS_SRC"
        exit 1
    fi
    # The caret reveal keeps a margin under the last line, so the place
    # lands near the foot of the page rather than exactly on it.
    if [ "$SRC_SCROLL" -lt "$(( (SRC_HEIGHT - SRC_VIEW) * 9 / 10 ))" ]; then
        echo "launch smoke FAILED: the source view did not scroll to the caret"
        echo "$KEYS_SRC"
        exit 1
    fi
    # In the rendered view the step may land on a position the heading
    # shares with the blank line under it, so only movement is required.
    if [ "$DOC_RIGHT" -lt 5 ] || [ "$DOC_DOWN" -le "$DOC_CARET" ]; then
        echo "launch smoke FAILED: the rendered view's keys did not move"
        echo "$KEYS_DOC"
        exit 1
    fi
    echo "launch smoke: the arrows, the scroll and typing behave in both views"

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
