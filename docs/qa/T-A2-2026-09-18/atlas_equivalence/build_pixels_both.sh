#!/bin/bash
set -euo pipefail
WT=/Users/happy/Desktop/opencraft_worktree/opencraft-ta2
MAIN=/Users/happy/Desktop/opencraft
# $1 = out name, $2 = mode (old|new)
if [ "$2" = "old" ]; then
  SRC=("$MAIN/game/client/src/atlas.cpp")
  LIBBASE=$MAIN
else
  SRC=("$WT/game/client/src/atlas.cpp" "$WT/game/client/src/asset_atlas.cpp")
  LIBBASE=$WT
fi
/usr/bin/c++ -O2 -std=c++20 -arch arm64 \
  -I"$LIBBASE/game/client/src" \
  -I"$LIBBASE/engine/voxel/include" \
  -I"$LIBBASE/engine/core/include" \
  -I"$LIBBASE/engine/render/include" \
  -I"$WT/build/_deps/spdlog-src/include" \
  -I"$WT/build/_deps/stb-src" \
  -DSPDLOG_COMPILED_LIB \
  /tmp/ta2_baseline/dump_pixels.cpp "${SRC[@]}" \
  "$LIBBASE/build/engine/voxel/libopencraft_voxel.a" \
  "$LIBBASE/build/engine/core/libopencraft_core.a" \
  "$LIBBASE/build/engine/render/libopencraft_render.a" \
  "$WT/build/_deps/spdlog-build/libspdlog.a" \
  -o "/tmp/ta2_baseline/$1"
