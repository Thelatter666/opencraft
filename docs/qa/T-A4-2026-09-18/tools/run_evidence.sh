#!/bin/bash
# T-A4 实机取证：同一份存档夹具 + 同一块展示台 + 同一机位，跑一次客户端截一张窗口图。
#
#   usage: run_evidence.sh <build_dir> <out_dir> <shot_name> <mode>
#     mode: batch1 -> assets/blocks 只留首批 42 张（本卡前，日志应为 42/63）
#           batch2 -> assets/blocks 含本卡 18 张（本卡后，日志应为 60/63）
#
# 单实例纪律：开头 kill 并复检，结尾 kill 并复检。
# 机位一致性：每段先复位夹具，再跑 patch_showcase.py（把玩家放到展示台前、摆好 6 种方块）。
#   两段唯一的差别是 assets/blocks 里有没有本卡那 18 张 PNG。
set -u
BUILD=$1
OUT=$2
SHOT=$3
MODE=$4
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
TOOLS="$REPO/docs/qa/T-A4-2026-09-18/tools"
WIN_TOOL=/tmp/ta4win
FIXTURE=/tmp/ta4_saves_fixture

BUILD=$(cd "$BUILD" && pwd)
OUT=$(cd "$OUT" && pwd)
mkdir -p "$OUT"

pkill -x opencraft 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then
    echo "ABORT: another opencraft instance is alive"
    exit 1
fi

# ── 机位一致性：复位存档夹具 ────────────────────────────────────────────────
rm -rf "$BUILD/saves"
cp -R "$FIXTURE" "$BUILD/saves"
echo "save fixture restored: $(du -sh "$BUILD/saves" | cut -f1)"

# ── 展示台：摆 6 种方块 + 把玩家放到台前 ────────────────────────────────────
python3 "$TOOLS/patch_showcase.py" "$BUILD/saves/world" || exit 2

# ── 贴图布置 ────────────────────────────────────────────────────────────────
NEW_TILES="cobblestone_top cobblestone_side cobblestone_bottom gravel_top gravel_side gravel_bottom
sandstone_top sandstone_side sandstone_bottom bedrock_top bedrock_side bedrock_bottom
snow_block_top snow_block_side snow_block_bottom obsidian_top obsidian_side obsidian_bottom"
STASH=/tmp/ta4_new_tiles_stash
if [ "$MODE" = "batch1" ]; then
    rm -rf "$STASH"; mkdir -p "$STASH"
    for t in $NEW_TILES; do
        [ -f "$REPO/assets/blocks/$t.png" ] && mv "$REPO/assets/blocks/$t.png" "$STASH/"
    done
    echo "batch1: moved $(ls -1 "$STASH" | wc -l | tr -d ' ') new tiles out of assets/blocks"
else
    for t in $NEW_TILES; do
        [ -f "$STASH/$t.png" ] && mv "$STASH/$t.png" "$REPO/assets/blocks/"
    done
    echo "batch2: assets/blocks content: $(ls -1 "$REPO/assets/blocks" | wc -l | tr -d ' ') files"
fi

# ── 启动 ────────────────────────────────────────────────────────────────────
cd "$BUILD" || exit 1
LOG="$OUT/${SHOT}.log"
./opencraft > "$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 120); do
    grep -q "atlas:" "$LOG" 2>/dev/null && break
    sleep 0.5
done
sleep 12   # chunk streaming / meshing settle

echo "--- loader lines ---"
grep -E "atlas:|respawn point" "$LOG" | head -4

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
echo "shot $OUT/${SHOT}.png"

kill "$PID" 2>/dev/null
sleep 2
pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then echo "WARN: instance still alive"; fi

# ── 还原 assets/blocks ──────────────────────────────────────────────────────
for t in $NEW_TILES; do
    [ -f "$STASH/$t.png" ] && mv "$STASH/$t.png" "$REPO/assets/blocks/"
done
echo "assets/blocks restored: $(ls -1 "$REPO/assets/blocks" | wc -l | tr -d ' ') files"
echo "--- done ---"
