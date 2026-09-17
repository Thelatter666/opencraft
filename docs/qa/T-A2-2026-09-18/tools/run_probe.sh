#!/bin/bash
# T-A2 evidence run: launch the real client with one set of probe textures in
# assets/blocks/ and capture the window.
#
#   usage: run_probe.sh <build_dir> <assets_dir> <out_dir> <shot_name> [probe ...]
#     probe = <block_id>_<slot>=<mode>   mode: magenta | orient | size32
#
# Waits for the client's own "atlas: N/63 block tiles loaded from ..." line, so
# the captured frame is one where the loader has already reported its outcome.
# Single-instance discipline is enforced at both ends: a leftover client would
# keep rendering (and keep autosaving) into this evidence.
set -u
BUILD=$1
ASSETS=$2
OUT=$3
SHOT=$4
shift 4
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIN_TOOL=/tmp/ta2win
mkdir -p "$OUT" "$ASSETS/blocks"
# The client has to run with cwd = build dir (that is where saves/ goes), so
# every path this script passes along must be absolute first.
BUILD=$(cd "$BUILD" && pwd)
ASSETS=$(cd "$ASSETS" && pwd)
OUT=$(cd "$OUT" && pwd)

pkill -x opencraft 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then
    echo "ABORT: another opencraft instance is alive"
    exit 1
fi

# ── probe textures ──────────────────────────────────────────────────────────
find "$ASSETS/blocks" -maxdepth 1 -name '*.png' -delete
for probe in "$@"; do
    target=${probe%%=*}
    mode=${probe##*=}
    python3 "$TOOLS/make_probe_png.py" "$ASSETS/blocks/${target}.png" "$mode" || exit 2
done
echo "--- assets/blocks ---"
ls -1 "$ASSETS/blocks"

# ── run ─────────────────────────────────────────────────────────────────────
cd "$BUILD" || exit 1
LOG="$OUT/${SHOT}.log"
./opencraft > "$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 120); do
    grep -q "atlas:" "$LOG" 2>/dev/null && break
    sleep 0.5
done
sleep 7   # let chunk streaming and meshing settle before the frame is claimed

echo "--- loader lines ---"
grep -E "atlas:|assets/" "$LOG" | head -5

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
screencapture -x -o -l"$WIN" "$OUT/${SHOT}.png"   # second capture: the first can be a stale surface
echo "shot $OUT/${SHOT}.png"

kill "$PID" 2>/dev/null
sleep 1.5
pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
sleep 0.5
if pgrep -x opencraft > /dev/null; then echo "WARN: instance still alive"; fi
echo "--- done ---"
