#!/bin/bash
# T-D46 on-machine evidence run.
#
#   usage: run_combat_scene.sh <build_dir> <out_dir>
#
# Launches the evidence build (the delivered sources + the temporary hook in
# tools/td46_evidence_hook.patch) in a FRESH world, then drives the three scenes
# with HID-injected keys. Single-instance check before and after, and it kills
# its own instance on the way out (docs/05 §3.1 rules 2/3).
set -u
BUILD=$1
OUT=$2
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$OUT"

pkill -x opencraft 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then echo "ABORT: another instance is alive"; exit 1; fi

# A fresh world: the scene is "a wretch walks up to a player standing on the
# spawn surface", and a world with history could put terrain in the way.
rm -rf "$BUILD/saves"
cd "$BUILD" || exit 1

LOG="$OUT/combat_session.log"
./opencraft > "$LOG" 2>&1 &
PID=$!
echo "pid=$PID"
sleep 3
# The window id is looked up from the PID and filtered by size: the same PID also
# owns the menu-bar window, and a name match hits dead windows (docs/05 §3.1 3).
TOOL=/tmp/td46input
WIN=$("$TOOL" win "$PID" 2>/dev/null | awk '$4==1280 && $5==748 {print $1; exit}')
echo "win=$WIN"
[ -n "$WIN" ] || { echo "ABORT: no game window found"; kill "$PID"; exit 3; }

python3 "$TOOLS/combat_scene.py" "$PID" "$WIN" "$LOG" "$OUT"
sleep 1
kill "$PID" 2>/dev/null
sleep 1.5
pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
echo "instances left: $(pgrep -x opencraft | wc -l | tr -d ' ')"
echo "--- combat log ---"
grep -E "EVIDENCE|mob event|mob melee|explosion for|player died|respawn" "$LOG" | head -60
