#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "opencraft/render/mesher.hpp"
#include "opencraft/voxel/block_registry.hpp"

namespace opencraft::game {

// Outcome of a placement attempt (docs/01 §4 ⚖: AABB no-overlap + face
// adjacency; right-click interaction takes priority - no interactive blocks
// exist yet, the hook is documented at the call site).
enum class PlacementStatus {
    Ok,              // cell is free and clear of the player: place it
    CellOccupied,    // target cell already holds a non-replaceable block
    IntersectsPlayer // cell overlaps the player AABB (would place into the body)
};

// True when a block can be replaced by a placement (air and fluids).
[[nodiscard]] bool is_replaceable(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id);

// Validates one placement attempt. Pure function:
//   cell          - candidate cell (raycast hit block + face normal)
//   player_feet   - player position (feet center)
//   player_height - current pose height (1.8 standing / 1.5 sneaking)
//   half_width    - player half width (0.3)
// Overlap between the cell cube and the player AABB uses strict inequalities,
// so a block placed flush against the player's feet/head is allowed (MC
// behavior) while placing into the body is rejected.
[[nodiscard]] PlacementStatus check_placement(const opencraft::voxel::BlockRegistry &registry,
                                              const opencraft::render::IBlockSource &blocks, glm::ivec3 cell,
                                              glm::dvec3 player_feet, double player_height, double half_width);

} // namespace opencraft::game
