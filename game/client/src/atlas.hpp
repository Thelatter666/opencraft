#pragma once

#include <cstdint>
#include <vector>

#include "opencraft/voxel/block_registry.hpp"

namespace opencraft::client {

// Programmatically generated placeholder texture atlas (task T005). Tile
// layout mirrors the mesher convention (render/mesher.hpp):
//   tile(block_id, slot) = block_id * 3 + slot, slot 0 = top, 1 = side, 2 = bottom
// Each tile is 16x16 RGBA; the atlas is the smallest square tile grid that
// fits registry.size() * 3 tiles.
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

[[nodiscard]] AtlasImage generate_atlas(const voxel::BlockRegistry &registry);

} // namespace opencraft::client
