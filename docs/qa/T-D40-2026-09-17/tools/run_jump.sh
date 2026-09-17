#!/bin/bash
set -u
BUILD=$1; LABEL=$2
OUT=/tmp/td40_ev; LOG=$OUT/${LABEL}_jump.log
pkill -x opencraft 2>/dev/null; sleep 0.5
rm -rf "$BUILD/saves"
cd "$BUILD" || exit 1
./opencraft > "$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 60); do grep -q "spawn scan" "$LOG" 2>/dev/null && break; sleep 0.5; done
sleep 1
python3 /tmp/td40_ab/jump_probe.py "$PID" "$LOG" "$LABEL"
sleep 1; kill "$PID" 2>/dev/null; sleep 1.5
pgrep -x opencraft >/dev/null && kill -9 $(pgrep -x opencraft)
echo "instances left: $(pgrep -x opencraft | wc -l | tr -d ' ')"
