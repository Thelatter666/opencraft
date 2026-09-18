#!/bin/bash
# T-B1 产品代码日志核对（★ 无 evidence patch，纯产品码）。
#
#   usage: check_product_log.sh <repo_dir> <build_dir> <out_dir>
#
# 三次冷启动，只读日志，不截图、不注入：
#   A. assets/mobs/mossback.vox = B0 夹具（tests/fixtures/mobs/mob_column.vox）
#      ⇒ "mobs: 1/3 ..." + 每个模型的体素/面数/关节数 INFO 行
#   B. assets/mobs/ 里没有模型文件
#      ⇒ "mobs: 0/3 ..."，且没有任何 WARN
#   C. assets/mobs/mossback.vox = 故意损坏的文件（截断）
#      ⇒ "mobs: 0/3 ..." + 恰好 1 条 WARN（含原因），不崩
#
# 结束时删掉 assets/mobs（本卡不产出生物模型，assets/** 必须零改动）。
set -u
REPO=$1
BUILD=$2
OUT=$3
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO=$(cd "$REPO" && pwd)
BUILD=$(cd "$BUILD" && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

run_once() {
    local name=$1
    pkill -x opencraft 2>/dev/null
    sleep 0.5
    if pgrep -x opencraft > /dev/null; then echo "ABORT: another opencraft instance is alive"; exit 1; fi
    rm -rf "$BUILD/saves"
    ( cd "$BUILD" && ./opencraft > "$OUT/$name.log" 2>&1 & echo $! > "$OUT/$name.pid" )
    local pid
    pid=$(cat "$OUT/$name.pid")
    for _ in $(seq 1 60); do
        grep -qE "mobs: " "$OUT/$name.log" 2>/dev/null && break
        sleep 0.5
    done
    sleep 2
    kill "$pid" 2>/dev/null
    sleep 1.5
    pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
    sleep 0.5
    echo "--- $name ---"
    grep -E "mobs:|mob model" "$OUT/$name.log" || echo "(no mobs: line)"
    local warns
    warns=$(grep -c "\[warning\]" "$OUT/$name.log")
    echo "warnings in log: $warns"
    grep "\[warning\]" "$OUT/$name.log" | head -3
}

# Clean slate: an earlier run may have left a model or a palette behind, and
# the three sections below are about what THIS run put there.
rm -rf "$REPO/assets/mobs" "$REPO/assets/palettes"

echo "════ A. B0 fixture installed as assets/mobs/mossback.vox ════"
mkdir -p "$REPO/assets/mobs"
cp "$REPO/tests/fixtures/mobs/mob_column.vox" "$REPO/assets/mobs/mossback.vox"
md5 "$REPO/assets/mobs/mossback.vox"
run_once A_fixture_present

echo
echo "════ B. no model file ════"
rm -f "$REPO/assets/mobs/mossback.vox"
run_once B_no_model_file

echo
echo "════ C. a deliberately damaged model file ════"
python3 - "$REPO/tests/fixtures/mobs/mob_column.vox" "$REPO/assets/mobs/mossback.vox" <<'PY'
import sys
from pathlib import Path
data = Path(sys.argv[1]).read_bytes()
Path(sys.argv[2]).write_bytes(data[:len(data) - 100])  # truncated inside the RGBA content
print(f"wrote a {len(data) - 100}-byte truncation of {Path(sys.argv[1]).name}")
PY
run_once C_damaged_file

echo
echo "════ cleanup: assets/mobs removed (this card ships no mob model) ════"
rm -rf "$REPO/assets/mobs" "$REPO/assets/palettes"
echo "assets/ diff: $(git -C "$REPO" status --short -- assets | wc -l | tr -d ' ') line(s)"
echo "--- done ---"
