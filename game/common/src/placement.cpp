#include "opencraft/game/placement.hpp"

#include <cmath>

namespace opencraft::game {

bool is_replaceable(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id) {
    if (block_id == 0) {
        return true; // air
    }
    return registry.has_numeric(block_id) && registry.def_of(block_id).liquid;
}

PlacementStatus check_placement(const opencraft::voxel::BlockRegistry &registry,
                                const opencraft::render::IBlockSource &blocks, glm::ivec3 cell,
                                glm::dvec3 player_feet, double player_height, double half_width) {
    const std::uint16_t occupant = blocks.block_at(cell.x, cell.y, cell.z);
    if (!is_replaceable(registry, occupant)) {
        return PlacementStatus::CellOccupied;
    }

    // Player AABB [feet - half_width, feet + half_width] x [feet.y, feet.y +
    // height] x [feet.z - half_width, feet.z + half_width] vs the cell cube.
    // Strict inequalities: touching faces do not count as intersection.
    const double pmin_x = player_feet.x - half_width;
    const double pmax_x = player_feet.x + half_width;
    const double pmin_y = player_feet.y;
    const double pmax_y = player_feet.y + player_height;
    const double pmin_z = player_feet.z - half_width;
    const double pmax_z = player_feet.z + half_width;

    const bool overlaps = pmax_x > static_cast<double>(cell.x) && pmin_x < static_cast<double>(cell.x + 1) &&
                          pmax_y > static_cast<double>(cell.y) && pmin_y < static_cast<double>(cell.y + 1) &&
                          pmax_z > static_cast<double>(cell.z) && pmin_z < static_cast<double>(cell.z + 1);
    if (overlaps) {
        return PlacementStatus::IntersectsPlayer;
    }
    return PlacementStatus::Ok;
}

} // namespace opencraft::game
