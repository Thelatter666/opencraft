#!/bin/bash
set -euo pipefail
WT=/Users/happy/Desktop/opencraft_worktree/opencraft-ta2
/usr/bin/c++ -O2 -std=c++20 -arch arm64 \
  -I"$WT/game/client/src" \
  -I"$WT/engine/voxel/include" \
  -I"$WT/engine/core/include" \
  -I"$WT/engine/render/include" \
  -I"$WT/build/_deps/spdlog-src/include" \
  -I"$WT/build/_deps/stb-src" \
  -DSPDLOG_COMPILED_LIB \
  /tmp/ta2_baseline/dump_atlas.cpp "$WT/game/client/src/atlas.cpp" "$WT/game/client/src/asset_atlas.cpp" \
  "$WT/build/engine/voxel/libopencraft_voxel.a" \
  "$WT/build/engine/core/libopencraft_core.a" \
  "$WT/build/engine/render/libopencraft_render.a" \
  "$WT/build/_deps/spdlog-build/libspdlog.a" \
  -o /tmp/ta2_baseline/dump_atlas_new
