#!/bin/bash
# T-A3 朝向取证：同一机位，把 grass_block_side 换成上下不对称探针各跑一次。
#   02_orient_white  = 上半白 / 下半黑
#   03_orient_black  = 上半黑 / 下半白
# 之后用 orient_judge.py 判定"哪半在屏幕上更靠上"。
set -u
BUILD=$1
OUT=$2
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
WIN_TOOL=/tmp/ta3win
FIXTURE=/tmp/ta3_saves_fixture
BUILD=$(cd "$BUILD" && pwd)
OUT=$(cd "$OUT" && pwd)
mkdir -p "$OUT"

pkill -x opencraft 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then
    echo "ABORT: another opencraft instance is alive"
    exit 1
fi

rm -rf /tmp/ta3_blocks_full
mv "$REPO/assets/blocks" /tmp/ta3_blocks_full
mkdir -p "$REPO/assets/blocks"

for MODE in white black; do
    rm -rf "$BUILD/saves"
    cp -R "$FIXTURE" "$BUILD/saves"
    python3 "$OUT/tools/orient_probe.py" "$REPO/assets/blocks" "${MODE}_top" || exit 2

    cd "$BUILD" || exit 1
    LOG="$OUT/0${MODE}_orient.log"
    [ "$MODE" = "white" ] && LOG="$OUT/02_orient_white.log" || LOG="$OUT/03_orient_black.log"
    SHOT=$([ "$MODE" = "white" ] && echo 02_orient_white || echo 03_orient_black)
    ./opencraft > "$LOG" 2>&1 &
    PID=$!
    for _ in $(seq 1 120); do
        grep -q "atlas:" "$LOG" 2>/dev/null && break
        sleep 0.5
    done
    sleep 12
    echo "--- $MODE loader lines ---"
    grep -E "atlas:|respawn point" "$LOG" | head -3
    WIN=$("$WIN_TOOL" win "$PID" 2>/dev/null | awk '$4 == 1280 && $5 > 700 {print $1; exit}')
    if [ -z "$WIN" ]; then
        echo "ABORT: no window for pid $PID"
        kill "$PID" 2>/dev/null
        exit 3
    fi
    "$WIN_TOOL" activate "$PID"
    sleep 0.6
    screencapture -x -o -l"$WIN" "$OUT/${SHOT}.png"
    sleep 0.4
    screencapture -x -o -l"$WIN" "$OUT/${SHOT}.png"
    echo "shot $OUT/${SHOT}.png (pid=$PID win=$WIN)"
    kill "$PID" 2>/dev/null
    sleep 2
    pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
    sleep 0.5
done

rm -rf "$REPO/assets/blocks"
mv /tmp/ta3_blocks_full "$REPO/assets/blocks"
echo "restored assets/blocks: $(ls -1 "$REPO/assets/blocks" | wc -l) files"
echo "--- done ---"
