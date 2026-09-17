#!/bin/bash
# Builds the surface probe against the worktree's own build tree: the same
# worldgen the client uses, printed for the columns the death scene needs.
#   usage: build_surface_probe.sh <worktree> <build_dir> <seed>
set -eu
WT=$1
BUILD=$2
SEED=${3:-5715144129572389190}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INC=$(sed -n 7p "$BUILD/game/client/CMakeFiles/opencraft.dir/flags.make" | sed 's/^CXX_INCLUDES = //')
clang++ -std=c++20 $INC -O1 -o /tmp/td45_surface "$HERE/surface_probe.cpp" \
    "$BUILD/game/common/libopencraft_game.a" "$BUILD/game/server/libopencraft_sim.a" \
    "$BUILD/game/server/libopencraft_storage.a" "$BUILD/game/server/libopencraft_worldgen.a" \
    "$BUILD/engine/render/libopencraft_render.a" "$BUILD/engine/voxel/libopencraft_voxel.a" \
    "$BUILD/engine/physics/libopencraft_physics.a" "$BUILD/engine/noise/libopencraft_noise.a" \
    "$BUILD/engine/core/libopencraft_core.a" "$BUILD/_deps/zstd-build/lib/libzstd.a" \
    "$BUILD/_deps/spdlog-build/libspdlog.a"
/tmp/td45_surface
