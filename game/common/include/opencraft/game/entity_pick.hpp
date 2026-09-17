#pragma once

// T-M2: entity pick geometry - one copy for both ends, the same arrangement
// game::kAttackReach / game::pickup_box_contains use (T-A1's rule: the client
// picks with it, the authority re-checks with its own entity boxes, and neither
// pre-empts the other's rules).
//
// The client needs it to decide "am I looking at a mob, or at a block?" before it
// sends an Attack/Feed request, and the authority needs the same answer to
// validate the range. Voxel rays are game::raycast_voxel's job; this is the
// other half - the ray against an axis-aligned BOX, which is what an entity is.

#include <algorithm>
#include <cmath>
#include <optional>

#include <glm/glm.hpp>

namespace opencraft::game {

// Where a ray enters an AABB, or nullopt when it misses or the box is behind the
// origin. The classic slab test: for each axis, the ray's entry/exit parameters
// against that axis' pair of planes, intersected across the three axes.
//
// `dir` must be non-zero (normalising is the caller's business: the returned
// value is a distance along `dir`, so a unit direction makes it blocks). A
// direction component of exactly 0 is handled by the parallel-ray branch rather
// than by dividing by zero.
[[nodiscard]] inline std::optional<double> ray_box_entry(const glm::dvec3 &origin, const glm::dvec3 &dir,
                                                         const glm::dvec3 &box_min, const glm::dvec3 &box_max) {
    double t_near = -1e308;
    double t_far = 1e308;
    for (int axis = 0; axis < 3; ++axis) {
        const double o = origin[axis];
        const double d = dir[axis];
        const double lo = box_min[axis];
        const double hi = box_max[axis];
        if (std::abs(d) < 1e-12) {
            if (o < lo || o > hi) {
                return std::nullopt; // parallel to this slab and outside it
            }
            continue;
        }
        double t0 = (lo - o) / d;
        double t1 = (hi - o) / d;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        t_near = std::max(t_near, t0);
        t_far = std::min(t_far, t1);
        if (t_near > t_far) {
            return std::nullopt;
        }
    }
    if (t_far < 0.0) {
        return std::nullopt; // the whole box is behind the origin
    }
    // Inside the box (t_near < 0): the entry point is the origin, distance 0 -
    // which is the right answer for "the player is standing in it".
    return std::max(t_near, 0.0);
}

} // namespace opencraft::game
