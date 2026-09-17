#!/bin/bash
# T-M2 in-game evidence runner: launch the evidence binary from its own build dir,
# wait for the spawn scan, drive the scripted scene, then stop it and report.
# usage: run_scene.sh <build_dir> <outdir>
set -u
BUILD=$(cd "$1" && pwd)
OUT=$2
LOG=$OUT/session.log
# Resolved BEFORE the cd into the build dir, or the relative path below breaks.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$OUT"

pkill -x opencraft 2>/dev/null
sleep 0.5
if [ "$(pgrep -x opencraft | wc -l | tr -d ' ')" != "0" ]; then
    echo "another instance is still running - aborting"; exit 1
fi

rm -rf "$BUILD/saves"
cd "$BUILD" || exit 1
./opencraft > "$LOG" 2>&1 &
PID=$!
echo "launched pid=$PID (cwd=$BUILD)"

for _ in $(seq 1 80); do
    if grep -q "spawn scan" "$LOG" 2>/dev/null; then break; fi
    sleep 0.5
done
grep -m1 "spawn scan" "$LOG" || echo "WARN: no spawn scan line yet"

# The window id is looked up by PID and filtered by SIZE (docs/05 §3.1: name
# matching hits dead windows, and the same PID owns a menu-bar window).
WIN=$(/tmp/tm2input win "$PID" | awk '$4==1280 && $5==748 {print $1; exit}')
if [ -z "$WIN" ]; then WIN=$(/tmp/tm2input win "$PID" | head -1 | awk '{print $1}'); fi
echo "window=$WIN pid=$PID"

python3 "$SCRIPT_DIR/scenario.py" "$PID" "$WIN" "$LOG" "$OUT"
RC=$?

sleep 1
kill "$PID" 2>/dev/null
sleep 1.5
if pgrep -x opencraft > /dev/null; then kill -9 $(pgrep -x opencraft) 2>/dev/null; fi
echo "scene done (rc=$RC); instances left: $(pgrep -x opencraft | wc -l | tr -d ' ')"
