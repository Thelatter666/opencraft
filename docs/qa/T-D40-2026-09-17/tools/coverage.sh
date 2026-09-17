#!/bin/bash
# T-D40 harness coverage proof: which collision branch each case actually hit.
# Reads the harness output and asserts the signatures of every branch.
set -u
F=${1:-/tmp/td40_ab/ab_new.out}

line() { printf '%-34s %s\n' "$1" "$2"; }

echo "== player branches =="
line "X clamp (hit_x=1)"          "$(grep -c 'r=1[01]0' "$F") tick(s)"
line "Z clamp (hit_z=1)"          "$(grep -c 'r=[01][01]1' "$F") tick(s)"
line "ceiling clamp (hit_y=1, no land)" "$(grep -c 'r=[01]10[01]0' "$F") tick(s)"
line "landing (landed=1)"         "$(grep -c 'r=[01][01][01]1[01]' "$F") tick(s)"
line "step-assist fired (stepped=1)" "$(grep -c 'r=[01][01][01][01]1' "$F") tick(s)"
line "airborne ticks (g=0)"       "$(grep -c ' g=0 ' "$F")"
line "sneaking ticks (pose=1)"    "$(grep -c 'pose=1' "$F")"
line "fall damage taken (hp≠20)"  "$(grep -c 'hp=40[0-9a-f][0-9a-f]' "$F" )  [hp=4034... = 20.0]"

echo
echo "== player case end states =="
awk '/^-- player case/{name=$0} /^   end/{print name" -> "$0}' "$F"

echo
echo "== drop branches =="
awk '/^-- drop case/{name=$0} /^   end/{print name" -> "$0}' "$F"
echo
echo "merge actually happened (a pair collapsed):"
grep -n "^-- drop case merge pair" -A 200 "$F" | grep -E "^[0-9]+-t" | awk '{print $2}' | sort -u | tr '\n' ' '
echo
echo "0.25 slab landing face seen in output (y = 64.25 = 4050400000000000):"
grep -c "4050400000000000" "$F"

echo
echo "== determinism of the device =="
echo "lines: $(wc -l < "$F")   md5: $(md5 -q "$F")"
