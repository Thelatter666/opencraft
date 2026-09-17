#!/bin/bash
# T-D45 evidence run 1: create a real save (autosave window), then build the
# death scene in it by POSITIONING the player (a 40-block drop) and aiming the
# persisted view pitch at the ground.
#
#   usage: run_death_scene.sh <build_dir> <out_dir>
set -u
BUILD=$1
OUT=$2
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$OUT"

pkill -x opencraft 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then echo "ABORT: another instance is alive"; exit 1; fi

# ── session 1: a fresh world, long enough for one autosave (200 ticks = 10 s) ─
rm -rf "$BUILD/saves"
cd "$BUILD" || exit 1
LOG1="$OUT/session1_save_fresh.log"
./opencraft > "$LOG1" 2>&1 &
PID1=$!
for _ in $(seq 1 120); do grep -q "autosave:" "$LOG1" 2>/dev/null && break; sleep 0.5; done
sleep 2.5   # let the save's IO thread finish the batch
kill "$PID1" 2>/dev/null
sleep 1.5
pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
sleep 0.5
echo "--- session 1 (fresh save) ---"
grep -E "spawn scan|respawn point|autosave|pos \(" "$LOG1" | tail -6
LEVEL="$BUILD/saves/world/level.ocd"
[ -f "$LEVEL" ] || { echo "ABORT: no $LEVEL"; exit 2; }

# ── build the death scene in the save ────────────────────────────────────────
# The death point is a DIFFERENT column from the respawn point (so "back at the
# respawn point" is visibly a teleport), and it is a column whose surface the
# evidence run looked up with the real worldgen (/tmp/td45_surface, built from
# the same seed): surface(x=24, z=0) = 144, so 184 is 40 blocks of air.
# pitch = +1.35 looks DOWN (the client's convention: view_dir uses -sin(pitch)),
# which is what puts the drop pile under the corpse in frame.
echo "--- level before ---"
python3 "$TOOLS/patch_level.py" show "$LEVEL" | grep -E "spawn_|player_|health|pitch"
python3 "$TOOLS/patch_level.py" set "$LEVEL" player_x=24.5 player_y=184.0 player_z=0.5 on_ground=0 pitch=1.35 \
    > "$OUT/level_patched.txt"
echo "--- level after ---"
cat "$OUT/level_patched.txt"

# ── session 2: fall, die, probe the dead inputs, respawn ────────────────────
LOG2="$OUT/session2_death_scene.log"
./opencraft > "$LOG2" 2>&1 &
PID2=$!
for _ in $(seq 1 120); do grep -q "player died at" "$LOG2" 2>/dev/null && break; sleep 0.5; done
WIN=$(/tmp/td45input win "$PID2" 2>/dev/null | awk '$4==1280 && $5==748 {print $1; exit}')
echo "pid=$PID2 win=$WIN"
[ -n "$WIN" ] || { echo "ABORT: no game window found"; kill "$PID2"; exit 3; }
python3 "$TOOLS/death_scene.py" "$PID2" "$WIN" "$LOG2" "$OUT" "$BUILD"
sleep 1
kill "$PID2" 2>/dev/null
sleep 1.5
pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
echo "instances left: $(pgrep -x opencraft | wc -l | tr -d ' ')"
