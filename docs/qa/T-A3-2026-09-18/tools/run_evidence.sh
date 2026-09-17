#!/bin/bash
# T-A3 实机取证：同一份存档夹具、同一机位，跑一次客户端截一张窗口图。
#
#   usage: run_evidence.sh <build_dir> <out_dir> <shot_name> <assets_mode>
#     assets_mode: clean  -> assets/blocks 清空（无外部贴图，程序化基线）
#                  batch  -> assets/blocks 放本卡首批 42 张
#
# 单实例纪律：开头 kill 并复检，结尾 kill 并复检（残留实例会继续渲染/覆写存档）。
# 机位一致性：每段开头把 /tmp/ta3_saves_fixture 整份拷回 build/saves，
#   保证两段用的是**同一份世界、同一个玩家位置与朝向**（客户端退出时才落盘）。
# 窗口号按 PID 反查 + 按 1280×748 尺寸过滤（同名死窗口/菜单栏窗口都会拿错号）。
set -u
BUILD=$1
OUT=$2
SHOT=$3
MODE=$4
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

# ── 机位一致性：复位存档夹具 ────────────────────────────────────────────────
rm -rf "$BUILD/saves"
cp -R "$FIXTURE" "$BUILD/saves"
echo "save fixture restored: $(du -sh "$BUILD/saves" | cut -f1)"

# ── 贴图布置 ────────────────────────────────────────────────────────────────
if [ "$MODE" = "clean" ]; then
    rm -rf /tmp/ta3_blocks_stash
    mv "$REPO/assets/blocks" /tmp/ta3_blocks_stash
    mkdir -p "$REPO/assets/blocks"
    echo "assets/blocks content: $(ls -1 "$REPO/assets/blocks" | wc -l) files"
else
    python3 "$REPO/docs/qa/T-A3-2026-09-18/tools/art_source.py" "$REPO/assets/blocks" || exit 2
    echo "assets/blocks content: $(ls -1 "$REPO/assets/blocks" | wc -l) files"
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
grep -E "atlas:|assets|respawn point" "$LOG" | head -5

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
if [ -d /tmp/ta3_blocks_stash ]; then
    rm -rf "$REPO/assets/blocks"
    mv /tmp/ta3_blocks_stash "$REPO/assets/blocks"
    echo "restored assets/blocks: $(ls -1 "$REPO/assets/blocks" | wc -l) files"
fi
echo "--- done ---"
