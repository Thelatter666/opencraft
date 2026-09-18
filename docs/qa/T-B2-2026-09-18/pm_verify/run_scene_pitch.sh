#!/bin/bash
# T-B1 PM verification harness (rev 2): deterministic paused-frame capture.
#
# usage: run_scene.sh <tree_dir> <out_dir> <mode> [tool]
#   tree_dir: source tree containing build/opencraft (assets resolved as <tree>/assets)
#   out_dir : where shots + session.log land
#   mode    : "control" (pause only) | "mossback" | "wretch" | "blastbud" (pause + summon)
#   tool    : HID injection binary (default: <tree_dir parent>/ti2input)
#
# Four gates (each bought with a mistake; rev 2 adds gate 0):
#   0. STREAMING PLATEAU - pausing FREEZES the streaming progress (streaming runs on
#      ticks, and a paused frame runs none). Rev 1 paused right after "spawn scan",
#      so ESC landed mid-stream run to run: two byte-identical-but-different worlds
#      (observed: 566,858 / 957,440 px between two "no model" trees that must match).
#      Now: wait for THREE consecutive "fps ... chunks K | stream-meshed M" lines
#      with identical K and M before pausing.
#   1. PAUSE CONFIRMED - the harness log's LAST "EVIDENCE paused=" must say 1
#      (a missed ESC looks exactly like a render regression).
#   2. FRAME STABLE - two captures 0.4 s apart must be byte-identical (nothing moves).
#   3. SAME STATE - harness binaries pin spawn yaw (patch 02) and disable natural
#      spawning (patch 03), so two runs of the SAME binary produce identical pixels.
set -u
TREE=$1
OUT=$2
MODE=${3:-control}
TOOL=${4:-$(dirname "$TREE")/ti2input}
[ -x "$TOOL" ] || TOOL=/Users/happy/Desktop/opencraft_scratch/tb1/ti2input
BUILD="$TREE/build"
mkdir -p "$OUT"
pkill -x opencraft 2>/dev/null
sleep 0.5
rm -rf "$BUILD/saves"
cd "$BUILD" || exit 1
./opencraft > "$OUT/session.log" 2>&1 &
PID=$!
for _ in $(seq 1 80); do
    grep -q "spawn scan" "$OUT/session.log" 2>/dev/null && break
    sleep 0.5
done
WIN=$($TOOL win "$PID" | awk '$4==1280 && $5==748 {print $1; exit}')
if [ -z "$WIN" ]; then
    echo "no window found for pid=$PID"
    kill $PID 2>/dev/null
    exit 1
fi
# gate 0: streaming plateau (3 consecutive identical chunk/mesh readings)
plateau() {
    grep "fps " "$OUT/session.log" | tail -n 3 |
        awk -F'[ |]+' '{for(i=1;i<=NF;i++){if($i=="chunks"){k=$(i+1)};if($i=="stream-meshed"){m=$(i+1)}}; print k,m}' |
        sort -u | wc -l | tr -d ' '
}
stream_lines() { grep -c "fps " "$OUT/session.log"; }
ok=0
for _ in $(seq 1 60); do
    if [ "$(stream_lines)" -ge 3 ] && [ "$(plateau)" = "1" ]; then ok=1; break; fi
    sleep 1
done
if [ "$ok" != 1 ]; then
    echo "streaming never plateaued: $(grep 'fps ' "$OUT/session.log" | tail -3)"
    kill $PID 2>/dev/null
    exit 4
fi
last_pause() { grep "EVIDENCE paused=" "$OUT/session.log" 2>/dev/null | tail -1; }
paused=0
for _ in 1 2 3 4; do
    case "$(last_pause)" in
    *"paused=1") paused=1; break ;;
    esac
    $TOOL activate "$PID" >/dev/null
    sleep 0.4
    $TOOL keytap 53 130 >/dev/null
    sleep 1.2
done
if [ "$paused" != 1 ]; then
    echo "ESC/pause never confirmed (last='$(last_pause)')"
    kill $PID 2>/dev/null
    exit 2
fi
case "$MODE" in
mossback) KEY=97 ;;
wretch) KEY=98 ;;
blastbud) KEY=100 ;;
*) KEY="" ;;
esac
if [ -n "$KEY" ]; then
    $TOOL activate "$PID" >/dev/null
    sleep 0.4
    $TOOL keytap "$KEY" 130 >/dev/null
    sleep 1.2
fi
if [ -n "${PITCH:-}" ]; then
    $TOOL activate "$PID" >/dev/null
    sleep 0.3
    $TOOL move 0 "$PITCH" >/dev/null
    sleep 0.6
fi
stable=0
for _ in 1 2 3 4 5 6 7 8; do
    $TOOL activate "$PID" >/dev/null
    sleep 0.3
    screencapture -x -o -l "$WIN" "$OUT/shot_a.png"
    sleep 0.4
    screencapture -x -o -l "$WIN" "$OUT/shot_b.png"
    if [ "$(md5 -q "$OUT/shot_a.png")" = "$(md5 -q "$OUT/shot_b.png")" ]; then stable=1; break; fi
    sleep 1.5
done
kill $PID 2>/dev/null
sleep 1.5
pgrep -x opencraft >/dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
STREAM=$(grep "fps " "$OUT/session.log" | tail -1 | sed 's/.*| //;s/^/ /')
echo "mode=$MODE win=$WIN summons=$(grep -c 'EVIDENCE summon' "$OUT/session.log") natural=$(grep -c 'spawned at' "$OUT/session.log") stable=$stable$(grep -E 'chunks' "$OUT/session.log" | tail -1 | sed -n 's/.*chunks \([0-9]*\) | stream-meshed \([0-9]*\).*/ chunks=\1 meshed=\2/p') left=$(pgrep -x opencraft | wc -l | tr -d ' ')"
[ "$stable" = 1 ] || exit 3
