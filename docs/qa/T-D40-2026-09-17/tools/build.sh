#!/bin/bash
# T-D40 A/B harness builder: same source, two trees, one output binary each.
set -eu
BASE=${1:?root}
OUT=${2:?out}
FLAGS="$BASE/build/tests/CMakeFiles/opencraft_tests.dir/flags.make"
LINK="$BASE/build/tests/CMakeFiles/opencraft_tests.dir/link.txt"

INCLUDES=$(grep '^CXX_INCLUDES' "$FLAGS" | sed 's/^CXX_INCLUDES = //; s#/tmp/td40_base_src#'"$BASE"'#g')
LIBS=$(tr ' ' '\n' < "$LINK" | grep -E '\.a$' | sed "s#^\.\./#$BASE/build/#")

/usr/bin/c++ -O3 -DNDEBUG -std=c++20 -arch arm64 $INCLUDES /tmp/td40_ab/ab_scenario.cpp $LIBS -o "$OUT"
echo "built $OUT"
