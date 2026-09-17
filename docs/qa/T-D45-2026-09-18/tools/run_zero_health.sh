#!/bin/bash
# T-D45 evidence run: the 0-health save (card §2.4 / acceptance 8).
#
# No patching: the save this reads is the one the DEATH SCENE session left
# behind (its 200-tick autosave wrote health = 0 and the death position), so this
# is the production path - "walked the death path and saved" - not a hand edit.
#
#   usage: run_zero_health.sh <build_dir> <out_dir>
set -u
BUILD=$1
OUT=$2
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LEVEL="$BUILD/saves/world/level.ocd"

pkill -x opencraft 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then echo "ABORT: another instance is alive"; exit 1; fi
[ -f "$LEVEL" ] || { echo "ABORT: no $LEVEL (run run_death_scene.sh first)"; exit 2; }

# The artifact copied out mid-death, before the respawn healed the player: the
# real "walked the death path and saved" file, not a hand edit.
ART="$OUT/level_after_death_autosave.ocd"
[ -f "$ART" ] || { echo "ABORT: no $ART (run run_death_scene.sh first)"; exit 2; }
cp "$ART" "$LEVEL"
echo "--- the save copied out while the player lay dead ---"
python3 "$TOOLS/patch_level.py" show "$LEVEL" | grep -E "spawn_|player_[xyz]|health|on_ground"

cd "$BUILD" || exit 1
LOG="$OUT/session3_zero_health_save.log"
./opencraft > "$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 80); do grep -q "respawn point" "$LOG" 2>/dev/null && break; sleep 0.5; done
sleep 2.0
WIN=$(/tmp/td45input win "$PID" 2>/dev/null | awk '$4==1280 && $5==748 {print $1; exit}')
echo "pid=$PID win=$WIN"
if [ -n "$WIN" ]; then
    /tmp/td45input front "$PID" > /dev/null
    /tmp/td45input activate "$PID" > /dev/null
    sleep 0.5
    screencapture -x -o -l "$WIN" "$OUT/04_zero_health_save_respawned.png"
    sleep 0.3
    screencapture -x -o -l "$WIN" "$OUT/04_zero_health_save_respawned.png"
fi
grep -E "save: loaded|save: stored health|respawn point|pos \(" "$LOG" | head -6
kill "$PID" 2>/dev/null
sleep 1.5
pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
echo "instances left: $(pgrep -x opencraft | wc -l | tr -d ' ')"
