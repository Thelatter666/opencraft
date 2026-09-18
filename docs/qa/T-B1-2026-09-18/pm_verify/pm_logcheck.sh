#!/bin/bash
# T-B1 PM verification: product-code fallback-table check, one fixture per run.
#
# usage: pm_logcheck2.sh <tree_dir> <fixture_path_or_-> <out_log> [palette_png|_]
#   Places <fixture> as <tree>/assets/mobs/mossback.vox ("-" = absent), optionally
#   <palette_png> as <tree>/assets/palettes/mossback.png, cold-starts the game,
#   waits for startup, kills it, prints the mob-model lines + WARN count, cleans up.
#   No patches, no injection: pure product path.
set -u
TREE=$1
FIX=$2
OUT=$3
PAL=${4:-_}
mkdir -p "$(dirname "$OUT")"
pkill -x opencraft 2>/dev/null
sleep 0.5
rm -rf "$TREE/build/saves" "$TREE/assets/mobs" "$TREE/assets/palettes"
if [ "$FIX" != "-" ]; then
    mkdir -p "$TREE/assets/mobs"
    cp "$FIX" "$TREE/assets/mobs/mossback.vox"
    if [ "$PAL" != "_" ]; then
        mkdir -p "$TREE/assets/palettes"
        cp "$PAL" "$TREE/assets/palettes/mossback.png"
    fi
fi
cd "$TREE/build" || exit 1
./opencraft > "$OUT" 2>&1 &
PID=$!
for _ in $(seq 1 60); do
    grep -q "spawn scan" "$OUT" 2>/dev/null && break
    sleep 0.5
done
sleep 1.0
kill $PID 2>/dev/null
sleep 1.5
pgrep -x opencraft >/dev/null && kill -9 $(pgrep -x opencraft) 2>/dev/null
rm -rf "$TREE/assets/mobs" "$TREE/assets/palettes"
NAME=$(basename "$FIX")
[ "$FIX" = "-" ] && NAME=absent
[ "$PAL" != "_" ] && NAME="$NAME +palette=$(basename "$PAL")"
echo "── $NAME ──"
grep -E "mob model|mobs:" "$OUT" | sed 's/^\[[^]]*\] \[[a-z]*\] //'
echo "all_warning_lines:"
grep "\[warning\]" "$OUT" | sed 's/^\[[^]]*\] \[warning\] /  /' || true
echo "warn_count=$(grep -c "\[warning\]" "$OUT")"
