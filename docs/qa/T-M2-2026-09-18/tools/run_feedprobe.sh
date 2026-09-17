#!/bin/bash
set -u
BUILD=$1; OUT=$2; LOG=$OUT/probe.log
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$OUT"
pkill -x opencraft 2>/dev/null; sleep 0.5
rm -rf "$BUILD/saves"; cd "$BUILD" || exit 1
./opencraft > "$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 80); do grep -q "spawn scan" "$LOG" 2>/dev/null && break; sleep 0.5; done
WIN=$(/tmp/tm2input win "$PID" | awk '$4==1280 && $5==748 {print $1; exit}')
echo "pid=$PID win=$WIN"
python3 "$SCRIPT_DIR/feed_probe.py" "$PID" "$WIN" "$LOG" "$OUT"
sleep 1; kill "$PID" 2>/dev/null; sleep 1.5
pgrep -x opencraft > /dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
echo "probe done; instances left: $(pgrep -x opencraft | wc -l | tr -d ' ')"
