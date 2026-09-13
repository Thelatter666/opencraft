#pragma once

#include <cstdint>
#include <vector>

#include "opencraft/voxel/block_registry.hpp"

namespace opencraft::client {

// Programmatically generated placeholder texture atlas (task T005; crack
// stages appended in T008). Tile layout mirrors the mesher convention
// (render/mesher.hpp):
//   tile(block_id, slot) = block_id * 3 + slot, slot 0 = top, 1 = side, 2 = bottom
// After all block tiles follow 10 crack overlay tiles (mining progress
// stages, docs/01 §4):
//   crack overlay tile = crack_tile_base(registry.size()) + stage (0..9).
// Each tile is 16x16 RGBA; the atlas is the smallest square tile grid that
// fits everything.
//
// Compliance note: all patterns are abstract procedural motifs (dithers,
// stripes, cell grids) with low-saturation palettes per block type - nothing
// is modeled after any specific game's texture art.
struct AtlasImage {
    int width = 0;  // atlas width in pixels
    int height = 0; // atlas height in pixels
    int tiles_per_row = 0;
    std::vector<std::uint32_t> pixels; // RGBA8 (a<<24 | b<<16 | g<<8 | r), row 0 first
};

// First crack overlay tile index (same registry the atlas was built from).
[[nodiscard]] constexpr std::uint16_t crack_tile_base(std::uint16_t registry_size) {
    return static_cast<std::uint16_t>(registry_size * 3);
}

[[nodiscard]] AtlasImage generate_atlas(const voxel::BlockRegistry &registry);

} // namespace opencraft::client
