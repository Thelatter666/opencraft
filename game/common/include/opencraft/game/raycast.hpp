#pragma once

#include <cstdint>
#include <functional>

#include <glm/glm.hpp>

#include "opencraft/render/mesher.hpp"

namespace opencraft::game {

// Result of a voxel DDA raycast (task T008 contract):
//   hit         - whether a targetable block was reached within max_dist
//   block_pos   - integer cell of the hit block (equal to floor(origin) when
//                 the ray starts inside a targetable block)
//   face_normal - unit axis vector pointing from the hit block back toward
//                 the ray (the face the ray entered through); {0,0,0} for a
//                 start-inside hit
//   t           - ray parameter at the entry boundary of the hit block;
//                 origin + dir * t lies on the hit face. 0 for a
//                 start-inside hit. Distance, since dir must be normalized.
struct VoxelRayHit {
    bool hit = false;
    glm::ivec3 block_pos{0, 0, 0};
    glm::ivec3 face_normal{0, 0, 0};
    double t = 0.0;
};

// Target filter: true when the block id should stop the ray. The default
// targets every non-air block. Callers typically exclude liquids so aiming
// through water works.
using RaycastFilter = std::function<bool(std::uint16_t block_id)>;

// Default filter: any block that is not air (id 0).
[[nodiscard]] bool target_non_air(std::uint16_t block_id);

// Amanatides & Woo voxel DDA (docs/research/03 §6.2). Pure function: depends
// only on (origin, dir, max_dist, blocks, filter). dir must be non-zero and
// is normalized internally; blocks.block_at() must answer for any coordinate
// (the render IBlockSource contract: unloaded chunks / out-of-world y read
// as air, so rays pass through the void and simply expire at max_dist).
//
// A ray starting inside a targetable cell hits that cell immediately
// (face_normal = {0,0,0}, t = 0). Shared by block targeting and any future
// projectile picking.
[[nodiscard]] VoxelRayHit raycast_voxel(glm::dvec3 origin, glm::dvec3 dir, double max_dist,
                                        const opencraft::render::IBlockSource &blocks,
                                        const RaycastFilter &filter = target_non_air);

// Placement cell for a ray hit: the empty cell adjacent to the hit face.
// Only meaningful for a hit with a non-zero face normal.
[[nodiscard]] glm::ivec3 placement_cell(const VoxelRayHit &hit);

} // namespace opencraft::game
