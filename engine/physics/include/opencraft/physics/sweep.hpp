#pragma once

// T-D40: the collision primitives, made public so the player and the entities
// move through ONE implementation instead of a copy each. Until this header
// existed they were file-private inside player_physics.cpp, and the drop
// simulation (game/server/sim/item_sim.hpp) had no way to reach them, so it
// carried its own copy of all three — the "double source of truth" this
// project spends effort avoiding. Anything that needs to move an AABB through
// the voxel world now comes here: the mob card (T-M2) included.
//
// The dependency direction is entity → physics: this header depends on
// `IBlockSource` only, never on `PlayerState` or on the entity layer.

#include <cmath>

#include <glm/glm.hpp>

#include "opencraft/physics/block_source.hpp"

namespace opencraft::physics {

// Working AABB in double precision (opencraft::core::AABB is float-based and
// the sim requires bit-stable doubles for the golden replay).
struct Box {
    double min_x;
    double min_y;
    double min_z;
    double max_x;
    double max_y;
    double max_z;
};

// The AABB of a body whose feet-centre is `position`, `half_width` to either
// side and `height` tall. The same shape drives the player (0.3 x 1.8, or 1.5
// while sneaking) and every entity (per-type values from EntityDef), so the
// caller passes its own dimensions rather than this header knowing them.
[[nodiscard]] inline Box box_of(const glm::dvec3 &position, const double half_width, const double height) {
    return Box{position.x - half_width, position.y,          position.z - half_width,
               position.x + half_width, position.y + height, position.z + half_width};
}

// Strict voxel overlap: a box face exactly touching a block face is NOT a
// collision, which keeps the "clamped against wall" state quiescent.
// Height-aware (T-D8): a block occupies [by, by + shape_top_at], so a 0.5-high
// block does not block a box whose feet are at or above its top face.
[[nodiscard]] inline bool box_collides(const IBlockSource &world, const Box &b) {
    const int x0 = static_cast<int>(std::floor(b.min_x));
    const int x1 = static_cast<int>(std::floor(b.max_x));
    const int y0 = static_cast<int>(std::floor(b.min_y));
    const int y1 = static_cast<int>(std::floor(b.max_y));
    const int z0 = static_cast<int>(std::floor(b.min_z));
    const int z1 = static_cast<int>(std::floor(b.max_z));
    for (int bx = x0; bx <= x1; ++bx) {
        for (int by = y0; by <= y1; ++by) {
            for (int bz = z0; bz <= z1; ++bz) {
                const double top = world.shape_top_at(bx, by, bz);
                if (top > 0.0 && b.max_x > bx && b.min_x < bx + 1 && b.max_y > by && b.min_y < by + top &&
                    b.max_z > bz && b.min_z < bz + 1) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Highest collision surface within [bottom_y, from_y] across the box
// footprint — the face a descent is clamped to. Height-aware, so a partial
// block is landed ON instead of on the full block above it. Returns < 0 when
// the range contains nothing to stand on.
[[nodiscard]] inline double highest_surface_below(const IBlockSource &world, const Box &b, const double from_y,
                                                  const double bottom_y) {
    const int x0 = static_cast<int>(std::floor(b.min_x));
    const int x1 = static_cast<int>(std::floor(b.max_x));
    const int y0 = static_cast<int>(std::floor(bottom_y));
    const int y1 = static_cast<int>(std::floor(from_y));
    const int z0 = static_cast<int>(std::floor(b.min_z));
    const int z1 = static_cast<int>(std::floor(b.max_z));
    double best = -1.0;
    for (int bx = x0; bx <= x1; ++bx) {
        for (int by = y0; by <= y1; ++by) {
            for (int bz = z0; bz <= z1; ++bz) {
                const double top = world.shape_top_at(bx, by, bz);
                if (top <= 0.0 || !(b.max_x > bx && b.min_x < bx + 1 && b.max_z > bz && b.min_z < bz + 1)) {
                    continue;
                }
                const double surface = static_cast<double>(by) + top;
                if (surface <= from_y + 1e-12 && surface >= bottom_y - 1e-12 && surface > best) {
                    best = surface;
                }
            }
        }
    }
    return best;
}

// What one axis move did. `delta` is the displacement actually applied, which
// is the input delta unless the move clamped. The three flags are per-call —
// they describe THIS move, so a caller that accumulates hits across substeps
// ors them itself. `ground` / `ceiling` are meaningful on the Y move only.
struct AxisSweep {
    double delta = 0.0;
    bool hit = false;
    bool ground = false;
    bool ceiling = false;
};

// Per-axis clamped moves (docs/research/03 §6.1: Y → X → Z). A substep
// penetrates less than one block, so clamping against the single leading-edge
// block layer (Y) or column (X, Z) is sufficient: the clamped position is put
// flush against that block's face, a hair outside it.
//
// ★ The collision response is deliberately NOT here. The player zeroes the
// velocity component it just lost, the drop multiplies it by its restitution
// knob — that difference is content, not geometry, and lives with each caller
// so this header cannot silently change either feel.
[[nodiscard]] inline AxisSweep sweep_axis_y(glm::dvec3 &position, const double half_width, const double height,
                                            const IBlockSource &world, const double dy) {
    AxisSweep out;
    if (dy == 0.0) {
        return out;
    }
    const double before = position.y;
    position.y += dy;
    if (!box_collides(world, box_of(position, half_width, height))) {
        out.delta = position.y - before;
        return out;
    }
    out.hit = true;
    if (dy < 0.0) {
        // Height-aware (T-D8): clamp onto the actual surface face, which for a
        // partial block is below the full-block top. The box's own bottom keeps
        // the search bounded to the layer it actually descended into.
        // (`before` is that box bottom: the descent started there.)
        const double surface = highest_surface_below(world, box_of(position, half_width, height), before, position.y);
        position.y = surface >= 0.0 ? surface : std::floor(position.y) + 1.0;
        out.ground = true;
    } else {
        const int by = static_cast<int>(std::floor(position.y + height));
        position.y = static_cast<double>(by) - height;
        out.ceiling = true;
    }
    out.delta = position.y - before;
    return out;
}

[[nodiscard]] inline AxisSweep sweep_axis_x(glm::dvec3 &position, const double half_width, const double height,
                                            const IBlockSource &world, const double dx) {
    AxisSweep out;
    if (dx == 0.0) {
        return out;
    }
    const double before = position.x;
    position.x += dx;
    if (!box_collides(world, box_of(position, half_width, height))) {
        out.delta = position.x - before;
        return out;
    }
    out.hit = true;
    if (dx > 0.0) {
        const int bx = static_cast<int>(std::floor(position.x + half_width));
        position.x = static_cast<double>(bx) - half_width;
    } else {
        const int bx = static_cast<int>(std::floor(position.x - half_width));
        position.x = static_cast<double>(bx) + 1.0 + half_width;
    }
    out.delta = position.x - before;
    return out;
}

[[nodiscard]] inline AxisSweep sweep_axis_z(glm::dvec3 &position, const double half_width, const double height,
                                            const IBlockSource &world, const double dz) {
    AxisSweep out;
    if (dz == 0.0) {
        return out;
    }
    const double before = position.z;
    position.z += dz;
    if (!box_collides(world, box_of(position, half_width, height))) {
        out.delta = position.z - before;
        return out;
    }
    out.hit = true;
    if (dz > 0.0) {
        const int bz = static_cast<int>(std::floor(position.z + half_width));
        position.z = static_cast<double>(bz) - half_width;
    } else {
        const int bz = static_cast<int>(std::floor(position.z - half_width));
        position.z = static_cast<double>(bz) + 1.0 + half_width;
    }
    out.delta = position.z - before;
    return out;
}

} // namespace opencraft::physics
