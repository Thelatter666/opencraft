#!/bin/bash
set -euo pipefail
SRC=${1:-/Users/happy/Desktop/opencraft/game/client/src}
OUT=/tmp/ta2_baseline/dump_atlas
/usr/bin/c++ -O2 -std=c++20 -arch arm64 \
  -I"$SRC" \
  -I/Users/happy/Desktop/opencraft/engine/voxel/include \
  -I/Users/happy/Desktop/opencraft/engine/core/include \
  -I/Users/happy/Desktop/opencraft/engine/render/include \
  -I/Users/happy/Desktop/opencraft/build/_deps/spdlog-src/include \
  -DSPDLOG_COMPILED_LIB \
  /tmp/ta2_baseline/dump_atlas.cpp "$SRC/atlas.cpp" \
  /Users/happy/Desktop/opencraft/build/engine/voxel/libopencraft_voxel.a \
  /Users/happy/Desktop/opencraft/build/engine/core/libopencraft_core.a \
  /Users/happy/Desktop/opencraft/build/engine/render/libopencraft_render.a \
  /Users/happy/Desktop/opencraft/build/_deps/spdlog-build/libspdlog.a \
  -o "$OUT"
