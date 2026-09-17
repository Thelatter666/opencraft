#pragma once

// T-E1: the item pick-up geometry - ONE copy for both ends, exactly like
// game::kReachDistance and game::check_placement (T-A1's rule: the client aims
// with it, the authority re-checks it against its own data, and neither side
// pre-empts the other's rules).
//
// The client needs it so it does not spray one request per drop in memory: it
// filters its candidates locally and only asks for the ones inside the box.
// The authority needs the same predicate to refuse a request that claims a
// drop it cannot reach.

#include <glm/glm.hpp>

#include "opencraft/game/protocol.hpp"
#include "opencraft/physics/player_state.hpp"

namespace opencraft::game {

// ⚖ research/11 §4.2 (wiki "Item (entity)" §Behavior, 2026-09-17): the actor's
// collision box grown by 1 block horizontally and 0.5 blocks vertically, where
// the horizontal faces are INCLUSIVE (≤ 1 block) and the vertical ones
// EXCLUSIVE (< 0.5). The asymmetry is the source's, not a reading of ours -
// hence two constants and two comparisons rather than one overlap helper.
inline constexpr double kPickupReachHorizontal = 1.0;
inline constexpr double kPickupReachVertical = 0.5;

// True when the entity box [box_min, box_max] lies inside the actor's pick-up
// box. `actor.height` is the CURRENT pose height, so a sneaking player picks up
// within a smaller box - the same coupling the wiki describes (拾取盒随玩家碰撞
// 盒变化), and the reason height travels in the pose instead of being a
// constant here.
//
// Touching the horizontal boundary COUNTS (an entity whose box edge is exactly
// 1 block outside the actor's box edge is still picked up); touching the
// vertical boundary does NOT.
[[nodiscard]] inline bool pickup_box_contains(const ActorPose &actor, const glm::dvec3 &box_min,
                                              const glm::dvec3 &box_max) {
    const double half = physics::PlayerState::kHalfWidth;
    const double min_x = actor.feet.x - half - kPickupReachHorizontal;
    const double max_x = actor.feet.x + half + kPickupReachHorizontal;
    const double min_z = actor.feet.z - half - kPickupReachHorizontal;
    const double max_z = actor.feet.z + half + kPickupReachHorizontal;
    if (box_max.x < min_x || box_min.x > max_x || box_max.z < min_z || box_min.z > max_z) {
        return false;
    }
    const double min_y = actor.feet.y - kPickupReachVertical;
    const double max_y = actor.feet.y + actor.height + kPickupReachVertical;
    return box_max.y > min_y && box_min.y < max_y;
}

} // namespace opencraft::game
