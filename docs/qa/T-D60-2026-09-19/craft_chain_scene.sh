#!/bin/zsh
set -u
QA=/Users/happy/Desktop/opencraft_worktree/opencraft-td60/docs/qa/T-D60-2026-09-19
TI=/Users/happy/Desktop/opencraft_scratch/ti2input
LOG=$QA/injection_timeline.txt
: > "$LOG"
step() { printf '%s  %s\n' "$(date +%H:%M:%S)" "$1" >> "$LOG"; }
PID=""
for i in {1..40}; do PID=$(pgrep -f "^\./opencraft$" | head -1); [[ -n "$PID" ]] && break; sleep 0.5; done
[[ -z "$PID" ]] && { echo "no game pid"; exit 1; }
WID=$($TI win "$PID" | head -1 | awk '{print $1}')
step "game pid=$PID window=$WID"
shot() { $TI activate "$PID" >/dev/null; sleep 0.35; screencapture -x -o -l"$WID" "$QA/$1"; step "shot $1"; }
rclick() { $TI activate "$PID" >/dev/null; sleep 0.2; $TI clicktap 1 90 "$1" "$2"; step "R-click $1,$2"; sleep 0.5; }
lclick() { $TI activate "$PID" >/dev/null; sleep 0.2; $TI clicktap 0 90 "$1" "$2"; step "L-click $1,$2"; sleep 0.5; }
key() { $TI activate "$PID" >/dev/null; sleep 0.2; $TI keytap "$1" 90; step "key $1"; sleep 0.6; }
sleep 3
key 14                   # E: open the pocket screen
lclick 908 589           # hotbar cell 2: take the 8 planks
rclick 856 389           # grid (0,0)
rclick 882 389           # grid (1,0)
rclick 856 415           # grid (0,1)
rclick 882 415           # grid (1,1)
lclick 856 389           # merge the leftover planks into cell (0,0): empty cursor
shot 04_grid_bench_ready.png
lclick 960 415           # the result cell: one craft onto the cursor
shot 05_bench_on_cursor.png
key 14                   # E: close (the bench goes to the inventory)
key 14                   # E: open again
lclick 908 667           # main cell 18: take the bench
lclick 908 589           # hotbar cell 2: swap it in
key 14                   # E: close
key 20                   # '3': select hotbar cell 2
shot 06_bench_in_hand.png
rclick 1000 640          # place the bench
rclick 1000 640          # right-click the placed bench: the 3x3
shot 07_bench_screen.png
key 14                   # E: close
# ── the gate differential: the same right-click, with and without the panel ──
key 14                   # E: open again
rclick 1000 640          # right-click the BENCH with the panel up (must not reopen/leak)
shot 08_gate_open_click.png
key 14                   # E: close
rclick 1000 640          # the same click with the panel down: reopens the 3x3
shot 09_gate_closed_click.png
key 14
echo done
