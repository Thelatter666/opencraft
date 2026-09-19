#!/bin/bash
# T-D59 on-machine evidence run.
#
#   usage: run_attack_scene.sh <build_dir> <out_dir>
#
# Launches the evidence build (the delivered sources + the temporary hook in
# tools/td59_evidence_hook.patch) in a FRESH world and drives the four scenes
# with HID-injected input. Single-instance check before and after, and it kills
# its own instance on the way out (docs/05 §3.1 rules 2/3).
set -u
BUILD=$1
OUT=$2
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$OUT"

pkill -x opencraft 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then echo "ABORT: another instance is alive"; exit 1; fi

# A fresh world: the scene is "a wretch in front of a player standing on the
# spawn surface", and a world with history could have the player somewhere else.
rm -rf "$BUILD/saves"
cd "$BUILD" || exit 1

LOG="$OUT/attack_session.log"
./opencraft > "$LOG" 2>&1 &
PID=$!
echo "pid=$PID"
sleep 3
TOOL=/tmp/td59input
WIN=$("$TOOL" win "$PID" 2>/dev/null | awk '$4==1280 && $5==748 {print $1; exit}')
echo "win=$WIN"
[ -n "$WIN" ] || { echo "ABORT: no game window found"; kill "$PID"; exit 3; }

python3 "$TOOLS/attack_scene.py" "$PID" "$WIN" "$LOG" "$OUT"
sleep 1
kill "$PID" 2>/dev/null
sleep 1.5
pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
echo "instances left: $(pgrep -x opencraft | wc -l | tr -d ' ')"
echo "--- attack log ---"
grep -E "attacked mob|sprint start|EVIDENCE summon|EVIDENCE timber_edge|died at" "$LOG" | head -80
