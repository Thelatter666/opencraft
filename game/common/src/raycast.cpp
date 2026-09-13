#include "opencraft/game/raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace opencraft::game {

bool target_non_air(std::uint16_t block_id) {
    return block_id != 0;
}

VoxelRayHit raycast_voxel(glm::dvec3 origin, glm::dvec3 dir, double max_dist,
                          const opencraft::render::IBlockSource &blocks, const RaycastFilter &filter) {
    const RaycastFilter &targetable = filter != nullptr ? filter : target_non_air;

    const double length = glm::length(dir);
    if (length == 0.0) {
        return {};
    }
    dir /= length;

    VoxelRayHit result;
    glm::ivec3 cell{static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)),
                    static_cast<int>(std::floor(origin.z))};

    // Starting inside a targetable block hits immediately (MC targeting
    // behaves the same way).
    if (targetable(blocks.block_at(cell.x, cell.y, cell.z))) {
        result.hit = true;
        result.block_pos = cell;
        result.t = 0.0;
        return result;
    }

    // Per-axis boundary crossing times for the normalized ray. t measures
    // distance because |dir| == 1. Zero components never cross a boundary.
    glm::dvec3 step(0.0);
    glm::dvec3 t_max(0.0);
    glm::dvec3 t_delta(0.0);
    for (int axis = 0; axis < 3; ++axis) {
        const double d = dir[axis];
        const double o = origin[axis];
        if (d > 0.0) {
            step[axis] = 1.0;
            t_max[axis] = (static_cast<double>(cell[axis] + 1) - o) / d;
            t_delta[axis] = 1.0 / d;
        } else if (d < 0.0) {
            step[axis] = -1.0;
            t_max[axis] = (static_cast<double>(cell[axis]) - o) / d;
            t_delta[axis] = -1.0 / d;
        } else {
            t_max[axis] = std::numeric_limits<double>::infinity();
            t_delta[axis] = std::numeric_limits<double>::infinity();
        }
    }

    while (true) {
        // Advance into the next cell along the axis whose boundary is closest.
        int axis = 0;
        if (t_max.y < t_max.x) {
            axis = t_max.z < t_max.y ? 2 : 1;
        } else if (t_max.z < t_max.x) {
            axis = 2;
        }
        const double t_entry = t_max[axis];
        if (t_entry > max_dist) {
            return {}; // ray expires before reaching any further cell
        }
        cell[axis] += static_cast<int>(step[axis]);
        t_max[axis] += t_delta[axis];

        if (targetable(blocks.block_at(cell.x, cell.y, cell.z))) {
            result.hit = true;
            result.block_pos = cell;
            result.face_normal = glm::ivec3{0, 0, 0};
            result.face_normal[axis] = static_cast<int>(-step[axis]);
            result.t = t_entry;
            return result;
        }
    }
}

glm::ivec3 placement_cell(const VoxelRayHit &hit) {
    return hit.block_pos + hit.face_normal;
}

} // namespace opencraft::game
