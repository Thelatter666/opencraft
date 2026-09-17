#!/bin/bash
# T-D40 in-game A/B runner: restore the pristine save, launch the binary from
# its own build dir, drive the scripted scene, then stop it.
# usage: run_scene.sh <build_dir> <label>
set -u
BUILD=$1
LABEL=$2
OUT=/tmp/td40_ev
LOG=$OUT/${LABEL}_session.log

pkill -x opencraft 2>/dev/null
sleep 0.5
if [ "$(pgrep -x opencraft | wc -l | tr -d ' ')" != "0" ]; then
    echo "another instance is still running - aborting"; exit 1
fi

# The world seed is srv::WorldSim::kSeed (a compile-time constant), so an
# empty saves/ makes both runs generate the identical world from scratch.
rm -rf "$BUILD/saves"

cd "$BUILD" || exit 1
./opencraft > "$LOG" 2>&1 &
PID=$!
echo "launched $LABEL pid=$PID (cwd=$BUILD)"

# wait for the game to be up (startup gen + spawn scan)
for _ in $(seq 1 60); do
    if grep -q "spawn scan" "$LOG" 2>/dev/null; then break; fi
    sleep 0.5
done
grep -m1 "spawn scan" "$LOG" || echo "WARN: no spawn scan yet"

python3 /tmp/td40_ab/scenario.py "$PID" "$LOG" "$OUT" "$LABEL"
RC=$?

sleep 1
kill "$PID" 2>/dev/null
sleep 1.5
if pgrep -x opencraft > /dev/null; then kill -9 $(pgrep -x opencraft) 2>/dev/null; fi
echo "scene done (rc=$RC); instances left: $(pgrep -x opencraft | wc -l | tr -d ' ')"
