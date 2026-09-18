#!/bin/bash
# T-B1 实机取证：同一份"无存档"冷启动 + 同一机位 + 同一个被钉住的生物，跑一棵树截一张窗口图。
#
#   usage: run_evidence.sh <repo_dir> <build_dir> <out_dir> <shot_name> <models|nomodels>
#
# 两段唯一的差别是仓库 assets/mobs/ 里有没有 B0 夹具（models 模式把它放进去，
# nomodels 模式确保它不在）。产品代码在提交版里不动；两棵树都打了同一份
# evidence_patch.diff（见同目录说明），取证后 git checkout 还原。
#
# 单实例纪律：开头 kill 并复检，结尾 kill 并复检。
# 机位一致性：删掉 build/saves 冷启动 ⇒ 同种子同出生点，view_yaw/pitch = 0。
set -u
REPO=$1
BUILD=$2
OUT=$3
SHOT=$4
MODE=$5
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIN_TOOL=${TB1_WIN_TOOL:-/tmp/tb1win}

REPO=$(cd "$REPO" && pwd)
BUILD=$(cd "$BUILD" && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

pkill -x opencraft 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then
    echo "ABORT: another opencraft instance is alive"
    exit 1
fi

# ── 模型布置：唯一的两段之间的差别 ──────────────────────────────────────────
if [ "$MODE" = "models" ] || [ "$MODE" = "palette" ]; then
    mkdir -p "$REPO/assets/mobs"
    cp "$TOOLS/evidence_mossback.vox" "$REPO/assets/mobs/mossback.vox"
    if [ "$MODE" = "palette" ]; then
        # ...plus the §3.3 palette-PNG override, whose cells are deliberately
        # different colors from the .vox's own palette: body magenta, head cyan,
        # arms orange/blue, legs yellow/violet.
        mkdir -p "$REPO/assets/palettes"
        cp "$REPO/tests/fixtures/mobs/mob_column_palette.png" "$REPO/assets/palettes/mossback.png"
        echo "palette: assets/mobs = $(ls -1 "$REPO/assets/mobs"), assets/palettes = $(ls -1 "$REPO/assets/palettes")"
    else
        rm -f "$REPO/assets/palettes/mossback.png"
        echo "models: assets/mobs = $(ls -1 "$REPO/assets/mobs")"
    fi
else
    rm -f "$REPO/assets/mobs/mossback.vox" "$REPO/assets/palettes/mossback.png"
    echo "nomodels: assets/mobs contains $(ls -1 "$REPO/assets/mobs" 2>/dev/null | wc -l | tr -d ' ') model file(s)"
fi

# ── 冷启动：同种子同出生点 ──────────────────────────────────────────────────
rm -rf "$BUILD/saves"

cd "$BUILD" || exit 1
LOG="$OUT/${SHOT}.log"
./opencraft > "$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 120); do
    grep -q "mobs:" "$LOG" 2>/dev/null && break
    sleep 0.5
done
sleep 25   # chunk streaming / meshing / camera spring settle

echo "--- loader lines ---"
grep -E "mobs:|mob model|atlas:|respawn point" "$LOG" | head -8

CANDIDATES=$("$WIN_TOOL" win "$PID" 2>/dev/null)
echo "--- windows of pid $PID ---"
echo "$CANDIDATES"
WIN=$(echo "$CANDIDATES" | awk '$4 == 1280 && $5 > 700 {print $1; exit}')
if [ -z "$WIN" ]; then
    echo "ABORT: no 1280x748 client window found"
    kill "$PID" 2>/dev/null
    exit 3
fi
echo "pid=$PID win=$WIN"

"$WIN_TOOL" activate "$PID"
sleep 0.6
screencapture -x -o -l"$WIN" "$OUT/${SHOT}.png"
sleep 0.4
screencapture -x -o -l"$WIN" "$OUT/${SHOT}.png"   # 第二张：第一张可能是旧表面
sleep 3.0
screencapture -x -o -l"$WIN" "$OUT/${SHOT}_b.png" # 3 秒后的第二张：同一次运行里的 A/A
echo "shot $OUT/${SHOT}.png and ${SHOT}_b.png"

kill "$PID" 2>/dev/null
sleep 2
pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then echo "WARN: instance still alive"; fi

echo "--- done ---"
