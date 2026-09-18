#!/bin/bash
# T-B1 PM verification harness: deterministic paused-frame capture.
#
# usage: run_scene.sh <tree_dir> <out_dir> <mode>
#   tree_dir: source tree containing build/opencraft (assets resolved as <tree>/assets)
#   out_dir : where shots + session.log land
#   mode    : "control" (pause only) | "mossback" | "wretch" | "blastbud" (pause + summon)
#
# Three gates, each one bought with a mistake made on 2026-09-18:
#   1. PAUSE CONFIRMED - HID injection is flaky here (T-D21). An ESC that silently
#      did not register leaves a bright, moving frame that looks exactly like a
#      render regression (observed: 920k of 957k pixels "differing"). The harness
#      log's LAST "EVIDENCE paused=" line must say 1 before anything is captured.
#   2. FRAME STABLE - the paused frame is not automatically static: chunk streaming
#      keeps loading/meshing after the pause (chunk count climbs for a second or
#      two). Two captures 0.4 s apart must be byte-identical (the "nothing is
#      moving" criterion of docs/05 §3.1 item 6), retried until they are.
#   3. SAME STATE - the harness binaries pin the two things that made two runs of
#      the SAME binary differ: the per-entity spawn yaw (patch 02) and the natural
#      spawner (patch 03). Without them the "device noise floor" is hundreds of
#      pixels of somebody else's cube.
set -u
TREE=$1
OUT=$2
MODE=${3:-control}
TOOL=/tmp/tm2input
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
last_pause() { grep "EVIDENCE paused=" "$OUT/session.log" 2>/dev/null | tail -1; }
paused=0
for _ in 1 2 3 4; do
    case "$(last_pause)" in
    *"paused=1")
        paused=1
        break
        ;;
    esac
    $TOOL front "$PID" >/dev/null
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
    $TOOL front "$PID" >/dev/null
    $TOOL activate "$PID" >/dev/null
    sleep 0.4
    $TOOL keytap "$KEY" 130 >/dev/null
    sleep 1.2
fi
stable=0
for _ in 1 2 3 4 5 6 7 8; do
    $TOOL front "$PID" >/dev/null
    $TOOL activate "$PID" >/dev/null
    sleep 0.3
    screencapture -x -o -l "$WIN" "$OUT/shot_a.png"
    sleep 0.4
    screencapture -x -o -l "$WIN" "$OUT/shot_b.png"
    if [ "$(md5 -q "$OUT/shot_a.png")" = "$(md5 -q "$OUT/shot_b.png")" ]; then
        stable=1
        break
    fi
    sleep 1.5
done
kill $PID 2>/dev/null
sleep 1.5
pgrep -x opencraft >/dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
echo "mode=$MODE win=$WIN summons=$(grep -c 'EVIDENCE summon' "$OUT/session.log") pause=$(last_pause) natural=$(grep -c 'spawned at' "$OUT/session.log") stable=$stable left=$(pgrep -x opencraft | wc -l | tr -d ' ')"
[ "$stable" = 1 ] || exit 3
