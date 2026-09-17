#pragma once

// T-M2: the mob simulation - the goal stack of game/mob_goal.hpp driven by the
// rules of game/mob_type.hpp over the entity store of T-E1.
//
// Everything here is a set of free functions over (store, world, registries,
// rules): the caller (server::WorldSim) owns the state and decides WHEN a step
// runs, this decides WHAT a step is. That split is what makes the AI testable
// against a hand-built block world with no chunks, no client and no window -
// the same arrangement item_sim.hpp uses for the drop rules.
//
// ── ⚠ THE PATHFINDER IS SIMPLIFIED (the card asks for this to be stated) ────
// research/11 §1.4 describes the real thing: a weighted grid search where every
// traversable cell carries a penalty (−1 impassable / 0 open / 4 / 8 danger
// adjacent / 16 danger here), several candidate paths are generated per request
// and the cheapest wins, and a stuck navigator cancels its path.
//
// What this card implements:
//   * the DESTINATION model - a goal names a point to reach and the mob walks to
//     it (§1.4.1's "导航系统保存当前路径、速度、目的地");
//   * OBSTACLE DETOUR by wall following: when the direct step is blocked the mob
//     COMMITS to one side and keeps that side until the way is clear. The
//     persistent side is the load-bearing part - a per-tick "evaluate the
//     neighbours and take the best" rule oscillates left/right in front of a
//     wall and never gets past it. This is the "直线 + 遇障绕行" the card allows;
//   * STUCK DETECTION, which abandons a destination the mob is not making
//     progress toward (§1.4.1's 卡住检测) instead of grinding against it.
//
// What it does NOT implement, and what that costs:
//   * no node path, so a mob cannot plan around an obstacle it cannot walk the
//     rim of (a wall is fine, a closed U is not);
//   * no penalty table, so it never prefers a longer safe route over a short
//     dangerous one (§1.4.2's "会为了避开高代价格而绕远路" is absent). The
//     default-table + per-mob-override structure the source recommends is not
//     built either, because nothing in this world could override it: no
//     damage-dealing block exists yet;
//   * no jump arcs, no swimming, no flying, no fall damage. A mob steps onto a
//     full block (the engine's EntityKind::Mob step height, 1.0) and is stopped
//     by anything taller;
//   * single path, not "generate several and pick the cheapest" (§1.4.4).
// The natural follow-up card is a real A* over the penalty grid; only
// navigate_step() below changes when it lands.
//
// ── WHY THE MOTION IS NOT physics::step_player ──────────────────────────────
// The engine has a per-entity-kind config (PhysicsConfig::for_entity(Mob)) and a
// complete motion routine, and reusing it was the first thing tried. It cannot
// express a mob's BODY: step_player collides using PlayerState's fixed 0.6 x 1.8
// box (kHalfWidth / kStandingHeight), while docs/03 §6 freezes "物理参数按实体
// 类型实例化" and research/11 §6.1 gives the cow 1.4 x 0.9. With engine/physics
// frozen by this card, a mob with a non-player body has two options: lie about
// its size, or drive the same SHARED COLLISION PRIMITIVES directly. This file
// does the latter - every move goes through physics::sweep_axis_x|y|z,
// physics::box_collides and physics::highest_surface_below (the header T-D40 was
// created to provide), and the knobs that must not drift from the player's
// (momentum cut-off, substep size, mob step height) are READ FROM PhysicsConfig
// rather than copied. Reported to the PM as the interface gap it is: per-entity
// body dimensions in PhysicsConfig would let the next mob card reuse step_player
// outright.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "opencraft/game/entity_type.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"
#include "opencraft/game/mob_goal.hpp"
#include "opencraft/game/mob_type.hpp"
#include "opencraft/game/protocol.hpp"
#include "opencraft/game/raycast.hpp"
#include "opencraft/physics/block_source.hpp"
#include "opencraft/physics/physics_config.hpp"
#include "opencraft/physics/player_state.hpp"
#include "opencraft/physics/sweep.hpp"
#include "opencraft/render/mesher.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/sim/item_sim.hpp"
#include "opencraft/voxel/chunk.hpp"

namespace opencraft::server {

// What the mob simulation needs from the world: the geometry queries physics
// owns (physics::IBlockSource), the block query the shared raycast needs
// (render::IBlockSource), whether a chunk is in memory (the same freeze rule the
// drops use) and the effective light level (the hostile spawn gate).
struct IMobWorld : physics::IBlockSource, render::IBlockSource {
    [[nodiscard]] virtual bool chunk_loaded(int cx, int cz) const = 0;
    // ⚖ docs/01 §6 "敌对生成光照等级 0". The EFFECTIVE level: sky and block light
    // combined, so 0 means "no skylight and no block light" - which in this world
    // (no light-emitting block exists yet) means "under a solid roof".
    [[nodiscard]] virtual int light_at(int wx, int wy, int wz) const = 0;
};

// Every number the mob simulation uses that is not per-mob content. Per-mob
// values live in game::MobDef; these are the shared knobs, each with its source
// or an explicit ⚠ when no source gives one.
struct MobRules {
    // ⚖ research/11 §1.3.3: the base game's sensors scan every 20 ticks, with a
    // random first delay in [0, scanRate) so several of them never fire on the
    // same tick. R-12 recommends the same for a goal-stack build, and following
    // it is also why a mob does not lock on the instant a player rounds a
    // corner.
    int perception_period = 20;

    // ⚠ 待校准: a mob's eye height as a fraction of its body. No source gives it;
    // the ratio is the player's own (1.62 / 1.8 = 0.9), so a mob looks from
    // where a player of its height would.
    double eye_height_ratio = 0.9;

    // ⚖ research/11 §1.3.4 recommends implementing exactly two of the targeting
    // modifiers for M2c: 潜行 ×0.8 and the 2.0-block floor. Both are here.
    // ⚠ "Is the actor sneaking" is read from ActorPose::height (1.5 sneaking /
    // 1.8 standing): the pose carries the height, not a flag, so this is a
    // threshold rather than an equality.
    double sneak_sight_multiplier = 0.8;
    double min_sight_range = 2.0;
    double sneak_height_threshold = 1.7;

    // ⚖ research/11 §1.5.1: 受击后的无敌帧 10 tick, during which damage no larger
    // than the last hit is ignored and a larger one settles only the difference.
    // The card does not ask for it, but the passive roster's drops need a damage
    // channel and without the window a held mouse button would delete a mob.
    int hurt_invulnerability = 10;

    // ⚠ 待校准: how long a mob keeps a grudge (⚖ 反击 exists in the source as a
    // goal; its duration does not). 600 ticks = 30 s.
    int revenge_ticks = 600;

    // Knobs of the simplified navigator, all marked:
    double arrive_distance = 1.0;   // ⚠ when a destination counts as reached
    int stuck_limit = 60;           // ⚠ ticks without progress before giving up
    double approach_distance = 1.5; // ⚠ how close a mob walks to what it follows
    double mating_distance = 1.5;   // ⚠ how close two in-love mobs must be
    double nav_probe = 0.25;        // ⚠ how far ahead the wall probe looks

    // The physics knobs are NOT re-declared here: they are read from
    // PhysicsConfig so a future change to the player's pipeline cannot leave the
    // mobs running an older copy of it (the T-D40 lesson - one source per rule).
    [[nodiscard]] static double max_substep() { return physics::PhysicsConfig{}.max_substep; }

    [[nodiscard]] static double momentum_threshold() { return physics::PhysicsConfig{}.momentum_threshold; }

    [[nodiscard]] static double ground_probe_depth() { return physics::PhysicsConfig{}.ground_probe_depth; }

    // ⚖ docs/01 §2 / research/06 §6.3: mobs step onto a FULL block where the
    // player's 0.6 only takes a slab. Read from the engine's per-kind config, so
    // the 1.0 stays owned by the engine.
    [[nodiscard]] static double step_height() {
        return physics::PhysicsConfig::for_entity(physics::EntityKind::Mob).step_height;
    }
};

// ── deferred work ───────────────────────────────────────────────────────────
// The mob pass walks the entity store, and EntityStore::for_each_entity forbids
// SPAWNING inside the visitor (it may reallocate the slot vector). Births and
// explosions are therefore collected here and applied by the caller once the walk
// is over - the same deferred-action shape item_sim's merge pass uses.
struct MobBirth {
    std::uint16_t type = 0;
    glm::dvec3 position{0.0, 0.0, 0.0};
};

struct MobBlast {
    glm::dvec3 position{0.0, 0.0, 0.0};
    double radius = 0.0;
};

struct MobStepResult {
    std::vector<EntityId> dead;
    std::vector<MobBirth> births;
    std::vector<MobBlast> blasts;
    std::vector<game::ActorEvent> events;
};

namespace mob_detail {

// ── determinism ─────────────────────────────────────────────────────────────
// splitmix64, one stream per mob. Deterministic on purpose: a stroll
// destination, a flee direction and a loot roll must all be reproducible from
// (world seed, entity id) or the live evidence and the unit tests cannot be
// compared - and an authority that will soon replay player input must not carry
// a hidden global RNG.
[[nodiscard]] inline std::uint64_t next_random(std::uint64_t &state) {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

[[nodiscard]] inline double random_unit(std::uint64_t &state) {
    return static_cast<double>(next_random(state) >> 11) * (1.0 / 9007199254740992.0);
}

[[nodiscard]] inline double random_range(std::uint64_t &state, const double min, const double max) {
    return min + (max - min) * random_unit(state);
}

inline constexpr double kTwoPi = 6.283185307179586476925286766559;

// ── geometry ────────────────────────────────────────────────────────────────

[[nodiscard]] inline glm::dvec3 mob_eye(const Entity &e, const game::EntityDef &def, const MobRules &rules) {
    return e.position + glm::dvec3(0.0, def.height * rules.eye_height_ratio, 0.0);
}

// ⚖ research/11 §1.3.2: sight needs a clear line from the mob's eyes to the
// target's, and most solid AND semi-transparent blocks stop it. That is exactly
// the shared DDA raycast the render/placement code uses - R-13: "直接复用，不要
// 另写", with the default non-air filter.
[[nodiscard]] inline bool line_of_sight(const IMobWorld &world, const glm::dvec3 &from, const glm::dvec3 &to) {
    const glm::dvec3 delta = to - from;
    const double distance = glm::length(delta);
    if (distance <= 1e-9) {
        return true;
    }
    return !game::raycast_voxel(from, delta, distance, world, game::target_non_air).hit;
}

// Yaw in the convention client::view_dir uses (yaw 0 looks along -Z), so the
// renderer can place a mob's head with that same function.
[[nodiscard]] inline double yaw_towards(const glm::dvec3 &from, const glm::dvec3 &to) {
    return std::atan2(-(to.x - from.x), -(to.z - from.z));
}

[[nodiscard]] inline double pitch_towards(const glm::dvec3 &from, const glm::dvec3 &to) {
    const glm::dvec3 delta = to - from;
    const double horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    return std::atan2(-delta.y, horizontal);
}

[[nodiscard]] inline double horizontal_distance(const glm::dvec3 &a, const glm::dvec3 &b) {
    const double dx = a.x - b.x;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

// The box an actor occupies, from the pose the authority was told about.
[[nodiscard]] inline physics::Box actor_box(const game::ActorPose &pose) {
    return physics::box_of(pose.feet, physics::PlayerState::kHalfWidth, pose.height);
}

// The actor's eyes, derived from the pose exactly as the authority does when it
// validates a placement.
[[nodiscard]] inline glm::dvec3 actor_eye(const game::ActorPose &pose) {
    return pose.feet + glm::dvec3(0.0, pose.eye_height, 0.0);
}

// ⚖ research/11 §1.5.1: a mob's melee box is its own collision box INFLATED by
// ≈0.828, and a hit additionally needs line of sight. Deliberately not a
// centre-distance test - the card repeats that because the two disagree for wide
// bodies.
[[nodiscard]] inline bool melee_reach(const Entity &attacker, const game::EntityDef &attacker_def,
                                      const physics::Box &target_box, const double reach_extra) {
    const physics::Box a = physics::box_of(attacker.position, attacker_def.half_width, attacker_def.height);
    return a.min_x - reach_extra < target_box.max_x && a.max_x + reach_extra > target_box.min_x &&
           a.min_y - reach_extra < target_box.max_y && a.max_y + reach_extra > target_box.min_y &&
           a.min_z - reach_extra < target_box.max_z && a.max_z + reach_extra > target_box.min_z;
}

// ── motion ──────────────────────────────────────────────────────────────────

// Would the box pass through if the mob walked the given horizontal step? Used by
// the navigator ("straight ahead, or detour?") and by the step-assist ("is there
// anything to step onto?").
[[nodiscard]] inline bool step_ahead_clear(const Entity &e, const game::EntityDef &def, const IMobWorld &world,
                                           const double dx, const double dz) {
    glm::dvec3 probe = e.position;
    probe.x += dx;
    probe.z += dz;
    return !physics::box_collides(world, physics::box_of(probe, def.half_width, def.height));
}

// Step-assist: if the box, moved by (dx, dz), overlaps something whose top face
// is within one mob step of the feet, lift the feet onto that face. This is the
// T-D8 mechanism in its mob form - a full block is walked up WITHOUT losing
// horizontal speed, which is why a mob never "hops" in this build.
inline void step_assist(Entity &e, const game::EntityDef &def, const IMobWorld &world, const double dx, const double dz,
                        const double step_height) {
    if (dx == 0.0 && dz == 0.0) {
        return;
    }
    glm::dvec3 probe = e.position;
    probe.x += dx;
    probe.z += dz;
    const physics::Box probe_box = physics::box_of(probe, def.half_width, def.height);
    if (!physics::box_collides(world, probe_box)) {
        return; // nothing in the way at this height
    }
    const double surface = physics::highest_surface_below(world, probe_box, e.position.y + step_height, e.position.y);
    if (surface < 0.0 || surface <= e.position.y + 1e-9 || surface > e.position.y + step_height + 1e-9) {
        return; // too tall to step onto, or no solid face within reach
    }
    glm::dvec3 raised = e.position;
    raised.y = surface;
    if (physics::box_collides(world, physics::box_of(raised, def.half_width, def.height))) {
        return; // no headroom on top of it
    }
    e.position.y = surface;
    e.on_ground = true;
}

// One tick of a mob's motion, following docs/03 §6's friction contract:
//
//     k      = def.horizontal_drag x S     (S = slipperiness below the feet, 1 in air)
//     v     += a * dir                     (a from the steering below)
//     move by v                            (Y -> X -> Z, in substeps)
//     v     *= k                           (damping AFTER the displacement)
//
// ⚠ THE SPEED QUESTION, stated once because the report has to be checkable:
// research/01 §10.2 gives each mob a movement-speed ATTRIBUTE (zombie 0.23, cow
// 0.2) and research/11 §0.1 says the unit is the block/tick order of magnitude,
// but NO source in this repository works the conversion out. This card therefore
// defines a mob's acceleration from the attribute so that the pipeline's own
// steady state v* = a / (1 − k) EQUALS the attribute in blocks/tick on ordinary
// ground:
//
//     a_ground = move_speed x (1 − horizontal_drag x kDefaultSlipperiness)
//              = move_speed x (1 − 0.91 x 0.6) = move_speed x 0.454
//
// Cross-checked against the one speed this repository HAS measured: the player
// walks 4.317 m/s = 0.216 blocks/tick, so a zombie at 0.23 blocks/tick = 4.6 m/s
// is ~6% faster than a walking player and clearly slower than a sprinting one
// (5.612 m/s) - which is how the pair behaves in the base game. The steady state
// is asserted numerically in test_mob_ai.cpp, and the interpretation is flagged
// in the report as an interpretation rather than a quoted number.
//
// A consequence worth naming: on ice (S = 0.98) k falls to 0.892, so the same
// acceleration reaches about 4x the speed. That is the friction contract working
// as documented (k is the block half of the product), and it matches the base
// game's own "mobs are very fast on ice" behaviour.
inline void step_mob_motion(Entity &e, const game::EntityDef &def, const IMobWorld &world, const glm::dvec2 &steer,
                            const double speed) {
    // ── A: acceleration toward the steering direction ───────────────────────
    if ((steer.x != 0.0 || steer.y != 0.0) && speed > 0.0) {
        const double keep = 1.0 - def.horizontal_drag * physics::kDefaultSlipperiness;
        e.velocity.x += steer.x * (speed * keep);
        e.velocity.z += steer.y * (speed * keep);
    }
    // Gravity always pulls, steering or not: a mob that walks off a ledge falls,
    // with the entity's own def.gravity (docs/01 §2's 0.08 for a walking body,
    // half that for a falling block - which is exactly why it is on the type).
    e.velocity.y -= def.gravity;

    // ── P: displacement, per-axis clamped, Y -> X -> Z, in substeps ─────────
    e.on_ground = false;
    const double max_component = std::max({std::abs(e.velocity.x), std::abs(e.velocity.y), std::abs(e.velocity.z)});
    int substeps = static_cast<int>(std::ceil(max_component / MobRules::max_substep()));
    if (substeps < 1) {
        substeps = 1;
    }
    const double step_height = MobRules::step_height();
    for (int i = 0; i < substeps; ++i) {
        const physics::AxisSweep y =
            physics::sweep_axis_y(e.position, def.half_width, def.height, world, e.velocity.y / substeps);
        if (y.hit) {
            // A mob's collision response is the player's: the component it just
            // lost is zeroed. The drop's `restitution` multiply is ITS content
            // rule - T-D40 deliberately kept that difference out of the sweep.
            e.velocity.y = 0.0;
        }
        if (y.ground) {
            e.on_ground = true;
        }
        const double dx = e.velocity.x / substeps;
        const double dz = e.velocity.z / substeps;
        // Step-assist BEFORE the horizontal move (T-D8's order): a 1-block
        // obstacle is walked up instead of bumped into, so no speed is lost.
        step_assist(e, def, world, dx, dz, step_height);
        const physics::AxisSweep x = physics::sweep_axis_x(e.position, def.half_width, def.height, world, dx);
        if (x.hit) {
            e.velocity.x = 0.0;
            step_assist(e, def, world, dx, 0.0, step_height);
        }
        const physics::AxisSweep z = physics::sweep_axis_z(e.position, def.half_width, def.height, world, dz);
        if (z.hit) {
            e.velocity.z = 0.0;
            step_assist(e, def, world, 0.0, dz, step_height);
        }
    }

    // ── D: damping, AFTER the displacement (docs/03 §6 contract) ────────────
    const double slipperiness =
        e.on_ground ? world.slipperiness_at(static_cast<int>(std::floor(e.position.x)),
                                            static_cast<int>(std::floor(e.position.y - MobRules::ground_probe_depth())),
                                            static_cast<int>(std::floor(e.position.z)))
                    : 1.0;
    const double horizontal = def.horizontal_drag * slipperiness;
    e.velocity.x *= horizontal;
    e.velocity.z *= horizontal;
    e.velocity.y *= def.vertical_drag;

    const double cut = MobRules::momentum_threshold();
    if (std::abs(e.velocity.x) < cut) {
        e.velocity.x = 0.0;
    }
    if (std::abs(e.velocity.y) < cut) {
        e.velocity.y = 0.0;
    }
    if (std::abs(e.velocity.z) < cut) {
        e.velocity.z = 0.0;
    }
}

// ── navigation (see the file header for how much of §1.4 this is) ───────────

// Steering toward a destination: straight at it, and when a wall is in the way,
// along the wall on a side the mob COMMITS to.
[[nodiscard]] inline glm::dvec2 navigate_step(Entity &e, const game::EntityDef &def, const IMobWorld &world,
                                              const MobRules &rules, const glm::dvec3 &destination) {
    const glm::dvec3 delta{destination.x - e.position.x, 0.0, destination.z - e.position.z};
    const double length = glm::length(delta);
    if (length <= 1e-9) {
        return {0.0, 0.0};
    }
    const glm::dvec2 forward{delta.x / length, delta.z / length};
    if (step_ahead_clear(e, def, world, forward.x * rules.nav_probe, forward.y * rules.nav_probe)) {
        e.ai.nav_side = 0; // out of the detour: forget the committed side
        return forward;
    }
    if (e.ai.nav_side == 0) {
        // Commit. The choice only has to be STABLE, not clever: compare the two
        // sides ONCE, by which one leaves the mob closer to the goal.
        const glm::dvec3 left{e.position.x - forward.y * rules.nav_probe, e.position.y,
                              e.position.z + forward.x * rules.nav_probe};
        const glm::dvec3 right{e.position.x + forward.y * rules.nav_probe, e.position.y,
                               e.position.z - forward.x * rules.nav_probe};
        e.ai.nav_side = glm::length(left - destination) < glm::length(right - destination) ? 1 : -1;
    }
    const double side = static_cast<double>(e.ai.nav_side);
    // 45 degrees off the wall rather than perpendicular: this is what makes wall
    // following make progress instead of sliding on the spot.
    const glm::dvec2 out{forward.x - forward.y * side, forward.y + forward.x * side};
    const double out_length = std::sqrt(out.x * out.x + out.y * out.y);
    if (out_length <= 1e-9) {
        return forward;
    }
    return {out.x / out_length, out.y / out_length};
}

// Has the mob stopped making progress toward its destination? Sampled every tick;
// the counter resets on any real movement (§1.4.1's 卡住检测).
inline void track_progress(Entity &e, const MobRules &rules) {
    if (horizontal_distance(e.position, e.ai.nav_last_position) < 0.01) {
        ++e.ai.nav_stuck_ticks;
    } else {
        e.ai.nav_stuck_ticks = 0;
        e.ai.nav_last_position = e.position;
    }
    if (e.ai.nav_stuck_ticks > rules.stuck_limit) {
        e.ai.has_move_target = false; // give up; the goal picks another
        e.ai.nav_side = 0;
        e.ai.nav_stuck_ticks = 0;
    }
}

inline void set_move_target(Entity &e, const glm::dvec3 &destination) {
    e.ai.move_target = destination;
    e.ai.has_move_target = true;
    e.ai.nav_side = 0;
    e.ai.nav_stuck_ticks = 0;
    e.ai.nav_last_position = e.position;
}

// A random point on the ground near the mob, for strolling and for fleeing.
// Candidate offsets are tried until one has solid ground within a short reach -
// the cheap stand-in for §1.4.4's "random point, moved up to the nearest air
// cell". `away_from`/`min_distance` implement §1.5.7's "逃跑时移动到距畏惧源
// 4–15 格的位置".
[[nodiscard]] inline bool pick_ground_point(Entity &e, const IMobWorld &world, const double radius,
                                            const glm::dvec3 *away_from, const double min_distance, glm::dvec3 &out) {
    for (int attempt = 0; attempt < 6; ++attempt) {
        const double angle = random_range(e.ai.rng, 0.0, kTwoPi);
        const double distance = random_range(e.ai.rng, std::min(2.0, radius), radius);
        const glm::dvec3 candidate =
            e.position + glm::dvec3(std::cos(angle) * distance, 0.0, std::sin(angle) * distance);
        if (away_from != nullptr && horizontal_distance(candidate, *away_from) < min_distance) {
            continue; // still too close to whatever it is fleeing from
        }
        const int wx = static_cast<int>(std::floor(candidate.x));
        const int wz = static_cast<int>(std::floor(candidate.z));
        for (int dy = 3; dy >= -4; --dy) {
            const int wy = static_cast<int>(std::floor(candidate.y)) + dy;
            if (!world.solid_at(wx, wy, wz)) {
                continue;
            }
            if (world.solid_at(wx, wy + 1, wz) || world.solid_at(wx, wy + 2, wz)) {
                continue; // no room to stand on top of it
            }
            out = glm::dvec3(candidate.x, static_cast<double>(wy) + 1.0, candidate.z);
            return true;
        }
    }
    return false;
}

} // namespace mob_detail

// ── the damage / feeding / spawning entry points ────────────────────────────
// These are what the authority calls. They live here so that "what a mob is and
// what it does" stays in one file.

// Applies damage to a mob. Returns true when the mob died from this hit.
//
// ⚖ research/11 §1.5.1's invulnerability window lives here: for 10 ticks after a
// hit, damage no larger than that hit is ignored and a larger one settles only
// the difference. The card does not ask for it, but the passive roster's drops
// need a damage channel, and without the window a held mouse button would delete
// a mob in a fraction of a second.
[[nodiscard]] inline bool damage_mob(EntityStore &store, const game::MobRegistry &mobs, const EntityId id,
                                     const double amount, const EntityId source, const MobRules &rules) {
    Entity *mob = store.find(id);
    if (mob == nullptr || amount <= 0.0) {
        return false;
    }
    const game::MobDef *def = mobs.find(mob->type);
    if (def == nullptr) {
        return false;
    }
    MobAi &ai = mob->ai;
    if (ai.hurt_cooldown > 0 && amount <= ai.last_hurt_amount) {
        return false; // ⚖ 期间伤害 ≤ 原伤害则免疫
    }
    const double applied = ai.hurt_cooldown > 0 ? amount - ai.last_hurt_amount : amount;
    ai.last_hurt_amount = amount;
    ai.hurt_cooldown = rules.hurt_invulnerability;
    mob->health -= applied;
    if (mob->health > 0.0) {
        // ⚖ 反击 (priority 1 in the source's own table): whoever hit me becomes
        // my target, whether or not I can see them.
        if (source != kNoEntityId && game::goal_priority(def->goals, game::GoalKind::Revenge) > 0) {
            ai.revenge_on = source;
            ai.revenge_ticks = rules.revenge_ticks;
        }
        // ⚖ 受伤反应: 随机方向逃跑数秒 (research/11 §6.3).
        if (def->panic_ticks > 0 && game::goal_priority(def->goals, game::GoalKind::Panic) > 0) {
            ai.panic_ticks = def->panic_ticks;
            ai.has_move_target = false; // the Panic goal picks a fresh direction
        }
        ai.in_love = false; // ⚖ being attacked ends the tempt/love behaviour
    }
    return mob->health <= 0.0;
}

// Feeds one unit of `item` to a mob. The authority has already checked the reach;
// this decides whether the mob accepts it and starts the ⚖ love timer.
[[nodiscard]] inline game::ActionReject feed_mob(EntityStore &store, const game::MobRegistry &mobs, const EntityId id,
                                                 const std::uint16_t item) {
    Entity *mob = store.find(id);
    if (mob == nullptr) {
        return game::ActionReject::UnknownEntity;
    }
    const game::MobDef *def = mobs.find(mob->type);
    if (def == nullptr) {
        return game::ActionReject::NotAMob;
    }
    if (!def->is_breedable() || item == game::ItemRegistry::kEmptyId) {
        return game::ActionReject::NotBreedable;
    }
    if (item != def->tempt_item) {
        return game::ActionReject::WrongFood;
    }
    if (mob->ai.baby) {
        return game::ActionReject::MobNotAdult;
    }
    if (mob->ai.breed_cooldown > 0) {
        return game::ActionReject::BreedingCooldown; // ⚖ 5 分钟
    }
    // ⚖ research/11 §6.2: 喂食后 30 秒未成功则退出求偶模式, and the pair may be fed
    // again immediately afterwards.
    mob->ai.in_love = true;
    mob->ai.love_ticks = 600;
    mob->ai.mating_ticks = def->love_ticks;
    return game::ActionReject::None;
}

// Creates one mob. `world_seed` (with the entity id) seeds the per-mob RNG, so
// the same world always produces the same wanderings.
[[nodiscard]] inline EntityId spawn_mob(EntityStore &store, const game::MobRegistry &mobs, const std::uint16_t type,
                                        const glm::dvec3 &position, const std::uint64_t world_seed,
                                        const MobRules &rules) {
    const game::MobDef *def = mobs.find(type);
    if (def == nullptr) {
        return EntityStore::kNoEntity;
    }
    Entity mob;
    mob.type = type;
    mob.position = position;
    mob.health = static_cast<double>(def->physics.max_health);
    const EntityId id = store.spawn(mob);
    Entity *e = store.find(id);
    e->ai.rng = world_seed ^ (static_cast<std::uint64_t>(id) * 0xD1B54A32D192ED03ULL);
    // ⚖ research/11 §1.3.3: the first sensor scan is delayed by a random amount
    // in [0, scanRate) so a spawned group does not all ray on the same tick.
    const int period = rules.perception_period > 0 ? rules.perception_period : 1;
    e->ai.perception_timer = static_cast<int>(mob_detail::next_random(e->ai.rng) % static_cast<std::uint64_t>(period));
    e->ai.yaw = mob_detail::random_range(e->ai.rng, 0.0, mob_detail::kTwoPi);
    e->ai.nav_last_position = e->position;
    return id;
}

// The loot a dead mob leaves, as ordinary item drops (research/11 §6.1's counts).
// The count comes from the mob's own stream, so a kill is reproducible.
[[nodiscard]] inline std::size_t spawn_mob_loot(EntityStore &store, const game::EntityTypeRegistry &types,
                                                const ItemRules &rules, const game::MobDef &def,
                                                const glm::dvec3 &position, std::uint64_t &rng) {
    std::size_t spawned = 0;
    for (const game::MobDrop &drop : def.drops) {
        if (drop.item == game::ItemRegistry::kEmptyId) {
            continue;
        }
        const int count =
            drop.max_count > drop.min_count
                ? drop.min_count + static_cast<int>(mob_detail::random_range(
                                       rng, 0.0, static_cast<double>(drop.max_count - drop.min_count) + 1.0))
                : drop.min_count;
        if (count <= 0) {
            continue;
        }
        // The drop's box is centred where the mob's middle was.
        if (spawn_item_stack_at(store, types, rules, drop.item, count,
                                position + glm::dvec3(0.0, def.physics.height * 0.5, 0.0)) != EntityStore::kNoEntity) {
            ++spawned;
        }
    }
    return spawned;
}

// Kills a mob: rolls its loot and removes it. The ONE place a death turns into
// world state, so the three damage paths (the player's Attack, a mob killing
// another mob, and a mob detonating itself) cannot drift apart.
[[nodiscard]] inline std::size_t kill_mob(EntityStore &store, const game::EntityTypeRegistry &types,
                                          const ItemRules &rules, const game::MobRegistry &mobs, const EntityId id) {
    Entity *mob = store.find(id);
    if (mob == nullptr) {
        return 0;
    }
    const game::MobDef *def = mobs.find(mob->type);
    if (def == nullptr) {
        return 0;
    }
    // Copy what the loot needs first: spawn_mob_loot spawns entities, and spawning
    // can move the slot vector.
    const glm::dvec3 position = mob->position;
    std::uint64_t rng = mob->ai.rng;
    const std::size_t dropped = spawn_mob_loot(store, types, rules, *def, position, rng);
    store.erase(id);
    return dropped;
}

// ── the goals ───────────────────────────────────────────────────────────────
// Each goal answers two questions (may I start / may I continue) and, while
// running, contributes to the tick: a steering destination, a yaw, or a hit. The
// scheduler in game/mob_goal.hpp decides WHICH goals run; nothing here decides
// that for itself, which is what keeps the ★ concurrency rule in one place.

namespace mob_detail {

struct MobContext {
    Entity &self;
    const game::MobDef &def;
    const game::EntityDef &physics_def;
    const IMobWorld &world;
    const MobRules &rules;
    const game::MobRegistry &mobs;
    game::Difficulty difficulty = game::Difficulty::Normal;
    const game::ActorPose *actor = nullptr; // nullptr until the authority is told where the player is
    EntityStore &store;
    const game::EntityTypeRegistry &types;
    MobStepResult &out;
};

// Is the actor holding the item that tempts this mob? The authority knows what
// the player holds because observe_actor() carries it - the inventory itself is
// still the client's (T-A1). This is one of the two rules that need the answer;
// the other is Feed.
[[nodiscard]] inline bool actor_tempts(const MobContext &ctx) {
    return ctx.actor != nullptr && ctx.def.tempt_item != game::ItemRegistry::kEmptyId &&
           ctx.actor->held_item == ctx.def.tempt_item;
}

[[nodiscard]] inline bool actor_within(const MobContext &ctx, const double range) {
    return ctx.actor != nullptr && ctx.self.ai.actor_known && ctx.self.ai.actor_distance <= range;
}

// ⚖ research/11 §1.5.5 needs the line of sight NOW, not on the 20-tick
// perception schedule: the fuse counts up and down with it, and a mob about to
// explode is rare enough that paying for the raycast per tick costs nothing.
[[nodiscard]] inline bool actor_sighted_now(const MobContext &ctx) {
    return ctx.actor != nullptr &&
           line_of_sight(ctx.world, mob_eye(ctx.self, ctx.physics_def, ctx.rules), actor_eye(*ctx.actor));
}

// The breeding partner: the nearest same-type in-love adult within breed_range. A
// bounded linear scan - the mob population is capped, and the base game's own
// partner search has the same shape.
[[nodiscard]] inline Entity *find_partner(MobContext &ctx) {
    Entity *best = nullptr;
    double best_distance = ctx.def.breed_range;
    for (const EntityId other_id : ctx.store.live_ids()) {
        Entity *other = ctx.store.find(other_id);
        if (other == nullptr || other->id == ctx.self.id || other->type != ctx.self.type) {
            continue;
        }
        if (!other->ai.in_love || other->ai.baby) {
            continue;
        }
        const double distance = horizontal_distance(other->position, ctx.self.position);
        if (distance <= best_distance) {
            best_distance = distance;
            best = other;
        }
    }
    return best;
}

// The target's hit box. The actor's comes from the pose; another mob's from its
// own entity def.
[[nodiscard]] inline std::optional<physics::Box> target_box_of(const MobContext &ctx) {
    if (ctx.self.ai.target == kActorId) {
        return ctx.actor != nullptr ? std::optional<physics::Box>(actor_box(*ctx.actor)) : std::nullopt;
    }
    const Entity *other = ctx.store.find(ctx.self.ai.target);
    if (other == nullptr) {
        return std::nullopt;
    }
    const game::EntityDef &other_def = ctx.types.def_of(other->type);
    return physics::box_of(other->position, other_def.half_width, other_def.height);
}

[[nodiscard]] inline bool target_los(const MobContext &ctx) {
    const glm::dvec3 from = mob_eye(ctx.self, ctx.physics_def, ctx.rules);
    if (ctx.self.ai.target == kActorId) {
        return ctx.actor != nullptr && line_of_sight(ctx.world, from, actor_eye(*ctx.actor));
    }
    const Entity *other = ctx.store.find(ctx.self.ai.target);
    if (other == nullptr) {
        return false;
    }
    return line_of_sight(ctx.world, from, mob_eye(*other, ctx.types.def_of(other->type), ctx.rules));
}

// ── verdicts (may I start / may I continue) ─────────────────────────────────

[[nodiscard]] inline game::GoalVerdicts build_verdicts(MobContext &ctx) {
    game::GoalVerdicts verdicts;
    const MobAi &ai = ctx.self.ai;
    const auto set = [&verdicts](const game::GoalKind kind, const bool can, const bool keep) {
        verdicts.can_use[static_cast<std::size_t>(kind)] = can;
        verdicts.continue_use[static_cast<std::size_t>(kind)] = keep;
    };
    for (const game::GoalEntry &entry : ctx.def.ordered_goals()) {
        switch (entry.kind) {
        case game::GoalKind::Panic:
            // ⚖ 受伤反应: 随机方向逃跑数秒 (research/11 §6.3). The timer is armed by
            // whoever lands the hit; the goal itself is just "am I panicking".
            set(entry.kind, ai.panic_ticks > 0, ai.panic_ticks > 0);
            break;
        case game::GoalKind::Revenge: {
            // ⚖ research/11 §1.2.2 puts 反击 at priority 1, ABOVE target
            // selection: a mob that was hit goes after the hitter even if it
            // cannot see them. It gives up on distance (follow_range) or when the
            // grudge expires.
            const bool grudge = ai.revenge_on != kNoEntityId && ai.revenge_ticks > 0;
            const bool chasing = ai.target == ai.revenge_on && grudge && actor_within(ctx, ctx.def.follow_range);
            set(entry.kind, grudge && ai.target != ai.revenge_on, chasing);
            break;
        }
        case game::GoalKind::TargetPlayer:
            // 察觉 happens in the perception snapshot (range + line of sight);
            // once LOCKED the mob keeps coming out to follow_range even with the
            // line broken, which is exactly the difference the card insists must
            // not be conflated with the sight range.
            set(entry.kind, ai.target == kNoEntityId && ai.actor_known && ai.sees_actor,
                ai.target == kActorId && actor_within(ctx, ctx.def.follow_range));
            break;
        case game::GoalKind::MeleeAttack:
            // ⚖ The source's own table runs this at the SAME priority as target
            // selection (both are 2), and both run at once - the concurrency the
            // card's first acceptance item is about.
            set(entry.kind, ai.target != kNoEntityId, ai.target != kNoEntityId);
            break;
        case game::GoalKind::Swell: {
            // ⚖ research/11 §1.5.5: 玩家进入 3 格内且有视线 → 引信.
            const bool close = actor_within(ctx, ctx.def.fuse_trigger_range);
            set(entry.kind, close && ai.fuse == 0 && actor_sighted_now(ctx), ai.fuse > 0);
            break;
        }
        case game::GoalKind::Tempt: {
            // ⚖★ All THREE tempt distances, in one place, doing three different
            // jobs - which is the whole reason they are three fields:
            //   * tempt_range (attribute 10) is what the mob can SENSE: a player
            //     holding the food further away than this is not noticed at all;
            //   * tempt_start_range (6) is where following BEGINS, and it is the
            //     binding constraint on the shipping grazing mob (10 > 6);
            //   * tempt_stop_range (10) is where following ENDS, and it is the one
            //     that lets a following mob keep going after the player has
            //     stepped back out of the start radius.
            // research/11 §1.5.6 states exactly this split, and §1.3.1's warning
            // ("混用会导致生物行为明显异常") is what the three fields prevent.
            const bool tempting = actor_tempts(ctx);
            const bool sensed = tempting && actor_within(ctx, ctx.def.tempt_range);
            set(entry.kind, sensed && actor_within(ctx, ctx.def.tempt_start_range),
                tempting && actor_within(ctx, ctx.def.tempt_stop_range));
            break;
        }
        case game::GoalKind::Breed:
            set(entry.kind, ai.in_love && find_partner(ctx) != nullptr, ai.in_love);
            break;
        case game::GoalKind::RandomStroll: {
            // ⚖ research/11 §1.5.1: 玩家在 32 格内才游荡. The "no target / not
            // following / not panicking" part is the source's OWN recipe for
            // mutual exclusion (research/11 §1.2.2: 互斥通过让低优先级目标的 canUse
            // 去读"当前是否已有攻击目标"这类状态来实现) - NOT the scheduler refusing
            // to run a low-priority goal.
            const bool free = ai.target == kNoEntityId && !ai.in_love && ai.panic_ticks == 0 && !actor_tempts(ctx);
            const bool near_enough =
                ctx.actor == nullptr || !ai.actor_known || ai.actor_distance <= ctx.def.stroll_trigger_range;
            set(entry.kind, free && near_enough, free);
            break;
        }
        case game::GoalKind::LookAtPlayer:
            set(entry.kind, ai.actor_known && ai.sees_actor, ai.actor_known && ai.sees_actor);
            break;
        case game::GoalKind::Count:
            break;
        }
    }
    return verdicts;
}

// ── the goal lifecycle's side effects ──────────────────────────────────────
// The four callbacks of research/11 §1.2.1 are canUse / start / continueUse /
// stop, and game::scan_goals only answers the two PREDICATES (which goals start
// and which stop this tick). What starting and stopping MEAN is content, so it
// lives here - one pair of functions, applied in priority order, so a goal's
// side effect cannot end up in a different place from its predicate.
//
// The selection goals are the reason `start` has to exist at all: 主动选目标's
// whole job is "take the actor as my target", and without it the scan would
// report the goal running while nothing was ever selected - the mob would look
// like it was chasing nothing.
inline void on_goal_started(MobContext &ctx, const game::GoalKind kind) {
    MobAi &ai = ctx.self.ai;
    switch (kind) {
    case game::GoalKind::TargetPlayer:
        ai.target = kActorId;
        break;
    case game::GoalKind::Revenge:
        ai.target = ai.revenge_on; // ⚖ priority 1: the hitter, seen or not
        break;
    case game::GoalKind::Panic:
        ai.has_move_target = false; // a fresh destination is picked in apply_goal
        break;
    case game::GoalKind::RandomStroll:
        ai.has_move_target = false;
        break;
    case game::GoalKind::Breed:
        ai.mating_ticks = ctx.def.love_ticks; // the ⚖ 2.5 s timer starts from full
        break;
    default:
        break; // MeleeAttack / Swell / Tempt / LookAtPlayer act through apply_goal
    }
}

inline void on_goal_stopped(MobContext &ctx, const game::GoalKind kind) {
    MobAi &ai = ctx.self.ai;
    switch (kind) {
    case game::GoalKind::RandomStroll:
    case game::GoalKind::Panic:
        ai.has_move_target = false; // release the destination: the next start picks one
        break;
    case game::GoalKind::Swell:
        // Stopping the fuse goal IS cancelling the fuse (research/11 §1.5.5's
        // 取消条件). apply_goal already zeroes it on the frame it notices; this
        // covers the frame where the PREDICATE is what gave up.
        ai.fuse = 0;
        break;
    case game::GoalKind::Revenge:
    case game::GoalKind::TargetPlayer: {
        // Both goals target the ACTOR, so giving up on one must not cancel a chase
        // the other is still running (the source's own table keeps 反击 and 主动选
        // 目标 at priorities 1 and 2, i.e. both alive at once). running_goals was
        // already updated to this tick's post-scan state, so this reads the truth.
        const game::GoalKind other =
            kind == game::GoalKind::Revenge ? game::GoalKind::TargetPlayer : game::GoalKind::Revenge;
        if (ai.target == kActorId && !game::goal_running(ai.running_goals, other)) {
            ai.target = kNoEntityId; // gave up the chase (beyond follow_range / grudge over)
        }
        break;
    }
    default:
        break;
    }
}

// ── per-tick behaviour of the running goals ────────────────────────────────

// What one running goal contributes to this tick's steering. Only ONE goal drives
// the mob at a time - the highest-priority one that wants to move - because
// movement is a single shared channel (the base game has one MoveControl per mob
// for the same reason). This is NOT the "only run the first goal" mistake the
// card warns about: every goal still RUNS (timers advance, the yaw is written,
// attacks land); only the steering channel is exclusive, claimed in priority
// order.
struct Steering {
    bool has = false;
    glm::dvec3 destination{0.0, 0.0, 0.0};
    double speed_scale = 1.0;
};

// Applies one running goal. Every running goal is applied on every tick - that
// is the card's ★ rule - and only the STEERING CHANNEL is exclusive, by
// priority: a goal that wants to move takes the channel only if a
// higher-priority goal has not already taken it. A goal that loses the channel
// still does everything else it does (writing the yaw, landing a hit, advancing
// its fuse), which is why an earlier version's "first claimer stops the loop"
// was wrong: it silently turned the goal stack back into first-match-wins and
// stopped a chasing mob from ever turning its head.
inline void apply_goal(MobContext &ctx, const game::GoalKind kind, Steering &steering) {
    Entity &self = ctx.self;
    MobAi &ai = self.ai;
    const game::MobDef &def = ctx.def;
    const MobRules &rules = ctx.rules;

    // Takes the steering channel for this goal if it is still free.
    const auto steer = [&steering](const glm::dvec3 &destination, const double scale) {
        if (!steering.has) {
            steering.has = true;
            steering.destination = destination;
            steering.speed_scale = scale;
        }
    };

    switch (kind) {
    case game::GoalKind::Panic: {
        // ⚖ research/11 §1.5.7: 逃跑时移动到距畏惧源 4–15 格的位置, and 逃跑通常成为
        // 最高优先级. ⚠ The SPEED multiplier is deliberately 1.0: the source gives
        // no value (that page is still marked wip) and T-D36 forbids inventing
        // one. The knob exists for the calibration that will fill it in.
        if (!ai.has_move_target) {
            glm::dvec3 threat{self.position.x, self.position.y, self.position.z};
            const glm::dvec3 *away_from = nullptr;
            if (ctx.actor != nullptr) {
                threat = ctx.actor->feet;
                away_from = &threat;
            }
            glm::dvec3 spot;
            if (!pick_ground_point(self, ctx.world, def.flee_max_range, away_from, def.flee_min_range, spot)) {
                ai.panic_ticks = 0; // nowhere to run: stop panicking rather than spin
                return;
            }
            set_move_target(self, spot);
        }
        if (horizontal_distance(self.position, ai.move_target) <= rules.arrive_distance) {
            ai.has_move_target = false; // arrived: the goal is done running
            return;
        }
        steer(ai.move_target, def.flee_speed_multiplier);
        return;
    }
    case game::GoalKind::Revenge:
    case game::GoalKind::TargetPlayer:
        return; // selection only: they pick the target, MeleeAttack walks it
    case game::GoalKind::MeleeAttack: {
        if (ai.target == kNoEntityId) {
            return;
        }
        const std::optional<physics::Box> box = target_box_of(ctx);
        const Entity *victim = ai.target == kActorId ? nullptr : ctx.store.find(ai.target);
        const std::optional<glm::dvec3> destination =
            ai.target == kActorId ? (ctx.actor != nullptr ? std::optional<glm::dvec3>(ctx.actor->feet) : std::nullopt)
                                  : (victim != nullptr ? std::optional<glm::dvec3>(victim->position) : std::nullopt);
        if (!box.has_value() || !destination.has_value()) {
            ai.target = kNoEntityId; // the target is gone
            return;
        }
        // Ask for the channel even when already in reach - with the destination on
        // its own feet, i.e. "stand still" - so a lower-priority goal cannot drag a
        // fighting mob away mid-swing.
        steer(melee_reach(self, ctx.physics_def, *box, def.attack_reach_extra * 0.5) ? self.position : *destination,
              1.0);
        if (ai.attack_cooldown > 0) {
            return;
        }
        if (!melee_reach(self, ctx.physics_def, *box, def.attack_reach_extra) || !target_los(ctx)) {
            return; // ⚖ 近战必要条件: 攻击盒相交 AND 有清晰视线
        }
        ai.attack_cooldown = def.attack_cooldown;
        const double damage = def.damage_for(ctx.difficulty);
        if (damage <= 0.0) {
            return; // ⚖ 和平难度: hostiles do no damage, so there is no event either
        }
        if (ai.target == kActorId) {
            // The player's hit points live on the client (T-A1 kept the player out
            // of the authority's vocabulary), so the authority reports the hit and
            // the client - which owns the health it renders - applies it. See
            // game::ActorEvent.
            game::ActorEvent event;
            event.kind = game::ActorEventKind::MeleeHit;
            event.position = ctx.actor != nullptr ? ctx.actor->feet : self.position;
            event.amount = damage;
            event.source_type = self.type;
            ctx.out.events.push_back(event);
        } else if (damage_mob(ctx.store, ctx.mobs, ai.target, damage, self.id, rules)) {
            ctx.out.dead.push_back(ai.target);
        }
        return;
    }
    case game::GoalKind::Swell: {
        // ⚖ research/11 §1.5.5, all four boxes the source gives:
        //   * 玩家进入 3 格内且有视线 → 引信开始 (fuse 1, +1 per tick)
        //   * 30 tick 后爆炸
        //   * 保持视线拉开 7 格可取消 (引信回退 - the 1.3+ behaviour, so it
        //     recedes rather than resetting)
        //   * losing the line also cancels
        const bool close = actor_within(ctx, def.fuse_trigger_range);
        const bool near = actor_within(ctx, def.fuse_cancel_range);
        const bool sight = (close || near) && actor_sighted_now(ctx);
        if (ai.fuse > 0) {
            if (close && sight) {
                ++ai.fuse;
            } else if (near && sight) {
                --ai.fuse; // ⚖ 引信回退
            } else {
                ai.fuse = 0; // ⚖ 取消
            }
        } else if (close && sight) {
            ai.fuse = 1;
        }
        // ⚖ An exploder does not walk while the fuse burns (research/11 §1.5.5:
        // 停下、嘶声、膨胀), and it never melees.
        steer(self.position, 1.0);
        if (ai.fuse >= def.fuse_ticks) {
            game::ActorEvent event;
            event.kind = game::ActorEventKind::Explosion;
            event.position = self.position;
            event.source_type = self.type;
            // ⚖ The epicentre damage is the sourced pair (普通 49 / 困难 64.5);
            // ⚠ the radius and the linear falloff are marked 待校准 in MobDef.
            if (ctx.actor != nullptr) {
                const double distance = glm::length(self.position - ctx.actor->feet);
                event.amount = def.explosion_damage_for(ctx.difficulty) * def.explosion_falloff(distance);
            }
            if (event.amount > 0.0) {
                ctx.out.events.push_back(event);
            }
            MobBlast blast;
            blast.position = self.position;
            blast.radius = def.explosion_radius;
            ctx.out.blasts.push_back(blast);
            ctx.out.dead.push_back(self.id);
            ai.fuse = 0;
        }
        return;
    }
    case game::GoalKind::Tempt: {
        if (ctx.actor == nullptr) {
            return;
        }
        // ⚖ Walk up to the player but not into them (⚠ approach_distance is a knob:
        // no source gives a stop distance for a following animal).
        steer(ai.actor_distance <= rules.approach_distance ? self.position : ctx.actor->feet, 1.0);
        return;
    }
    case game::GoalKind::Breed: {
        Entity *partner = find_partner(ctx);
        if (partner == nullptr) {
            return; // the partner left; the verdict stops the goal next tick
        }
        const bool in_contact = horizontal_distance(partner->position, self.position) <= rules.mating_distance;
        steer(in_contact ? self.position : partner->position, 1.0);
        if (in_contact) {
            // ⚖ research/11 §6.2: 交配时长约 2.5 秒, then the baby appears. Only the
            // lower id performs the birth, so a pair cannot produce two.
            if (ai.mating_ticks > 0) {
                --ai.mating_ticks;
            }
            if (ai.mating_ticks == 0 && self.id < partner->id) {
                MobBirth birth;
                birth.type = self.type;
                birth.position = (self.position + partner->position) * 0.5;
                ctx.out.births.push_back(birth);
                for (Entity *parent : {&self, partner}) {
                    parent->ai.in_love = false;
                    parent->ai.love_ticks = 0;
                    parent->ai.mating_ticks = 0;
                    parent->ai.breed_cooldown = def.breed_cooldown; // ⚖ 5 分钟
                }
            }
        } else if (ai.mating_ticks < def.love_ticks) {
            ++ai.mating_ticks; // contact progress recedes when they separate
        }
        return;
    }
    case game::GoalKind::RandomStroll: {
        if (!ai.has_move_target) {
            glm::dvec3 spot;
            if (!pick_ground_point(self, ctx.world, def.stroll_radius, nullptr, 0.0, spot)) {
                return; // nowhere to go: keep standing, a lower-priority goal may act
            }
            set_move_target(self, spot);
        }
        if (horizontal_distance(self.position, ai.move_target) <= rules.arrive_distance) {
            ai.has_move_target = false; // pick a new destination next tick
            return;
        }
        steer(ai.move_target, 1.0);
        return;
    }
    case game::GoalKind::LookAtPlayer: {
        if (ctx.actor == nullptr) {
            return;
        }
        // ⚖ 注视: the mob turns to face the player. The cheapest goal, and the
        // most visible one - and it runs ALONGSIDE the chase, which is the live
        // proof that a low-priority goal coexists with a high-priority one.
        const glm::dvec3 eye = mob_eye(self, ctx.physics_def, rules);
        ai.yaw = yaw_towards(eye, actor_eye(*ctx.actor));
        ai.pitch = pitch_towards(eye, actor_eye(*ctx.actor));
        return; // never steers, and never needs to: it only turns the head
    }
    case game::GoalKind::Count:
        break;
    }
}

// ── the per-tick bookkeeping ────────────────────────────────────────────────

// Refresh the perception snapshot: the expensive part (range + line of sight) on
// the 20-tick schedule, the cheap distance every tick (see MobAi).
inline void perceive(MobContext &ctx) {
    MobAi &ai = ctx.self.ai;
    ai.actor_known = ctx.actor != nullptr;
    ai.actor_distance = ctx.actor != nullptr ? glm::length(ctx.self.position - ctx.actor->feet) : 0.0;
    if (ctx.actor == nullptr) {
        ai.sees_actor = false;
        return;
    }
    if (ai.perception_timer > 0) {
        --ai.perception_timer;
        return;
    }
    ai.perception_timer = ctx.rules.perception_period;
    // ⚖ research/11 §1.3.1 + §1.3.4: the notice range is the mob's own
    // sight_range, reduced by the actor sneaking (×0.8 - the one modifier the
    // source recommends implementing for M2c) and floored at 2 blocks.
    const bool sneaking = ctx.actor->height < ctx.rules.sneak_height_threshold;
    const double range =
        std::max(ctx.def.sight_range * (sneaking ? ctx.rules.sneak_sight_multiplier : 1.0), ctx.rules.min_sight_range);
    ai.sees_actor = ai.actor_distance <= range &&
                    line_of_sight(ctx.world, mob_eye(ctx.self, ctx.physics_def, ctx.rules), actor_eye(*ctx.actor));
}

// The timers that tick down regardless of which goals are running.
inline void advance_timers(MobContext &ctx) {
    MobAi &ai = ctx.self.ai;
    const auto tick_down = [](int &value) {
        if (value > 0) {
            --value;
        }
    };
    tick_down(ai.attack_cooldown);
    tick_down(ai.hurt_cooldown);
    tick_down(ai.revenge_ticks);
    tick_down(ai.breed_cooldown);
    tick_down(ai.panic_ticks);
    if (ai.in_love) {
        tick_down(ai.love_ticks);
        if (ai.love_ticks == 0) {
            // ⚖ research/11 §6.2: 喂食后 30 秒未成功则退出求偶模式.
            ai.in_love = false;
            ai.mating_ticks = 0;
        }
    }
    if (ai.baby) {
        tick_down(ai.growth_ticks); // ⚖ 幼体成长 24000 tick
        if (ai.growth_ticks == 0) {
            ai.baby = false; // grown up
        }
    }
    if (ai.revenge_ticks == 0) {
        ai.revenge_on = kNoEntityId;
    }
    // A target that no longer exists cannot be chased. (The actor always
    // "exists" while a pose is known, so only real entities are checked.)
    if (ai.target != kNoEntityId && ai.target != kActorId && ctx.store.find(ai.target) == nullptr) {
        ai.target = kNoEntityId;
    }
    if (ai.target == kActorId && ctx.actor == nullptr) {
        ai.target = kNoEntityId;
    }
}

} // namespace mob_detail

// One authoritative step over every mob in the store:
//   timers -> perception -> goal scan -> goal ticks in priority order -> motion.
//
// The order matters in three places, all deliberate:
//   * the goal SCAN runs before the goal TICKS, so a goal that started this tick
//     also acts this tick and one that stopped does not act at all;
//   * the ticks run in the goal table's priority order, so a higher-priority goal
//     claims the shared steering channel first (see apply_goal);
//   * a mob that died earlier in the walk (killed by another mob) is skipped
//     before it can act.
//
// ⚖ Nothing runs for a mob whose chunk is not in memory - the same freeze rule
// the drops use (research/11 §4.4's paused-timer rule, applied to mobs). With no
// actor pose the mobs still age, fall and stroll, but nothing perceives them -
// which is what keeps the other cards' headless tests bit-identical.
inline MobStepResult step_mobs(EntityStore &store, const IMobWorld &world, const game::EntityTypeRegistry &types,
                               const game::MobRegistry &mobs, const MobRules &rules, const game::Difficulty difficulty,
                               const game::ActorPose *actor, const std::uint64_t world_seed) {
    MobStepResult result;
    const std::vector<EntityId> ids = store.live_ids(); // snapshot: the walk must not see its own edits
    for (const EntityId id : ids) {
        Entity *e = store.find(id);
        if (e == nullptr) {
            continue;
        }
        const game::MobDef *def = mobs.find(e->type);
        if (def == nullptr) {
            continue; // not a mob - the drop pass owns it
        }
        if (e->health <= 0.0) {
            result.dead.push_back(id);
            continue;
        }
        const game::EntityDef &physics_def = types.def_of(e->type);
        const auto [cx, cz] = voxel::Chunk::chunk_coords(static_cast<int>(std::floor(e->position.x)),
                                                         static_cast<int>(std::floor(e->position.z)));
        if (!world.chunk_loaded(cx, cz)) {
            continue; // frozen: no timers, no perception, no physics
        }
        ++e->age;
        if (e->ai.rng == 0) {
            // A mob that arrived without spawn_mob (a test fixture, a save from a
            // future card) still gets a deterministic stream.
            e->ai.rng = world_seed ^ (static_cast<std::uint64_t>(id) * 0xD1B54A32D192ED03ULL);
        }

        mob_detail::MobContext ctx{*e, *def, physics_def, world, rules, mobs, difficulty, actor, store, types, result};
        mob_detail::advance_timers(ctx);
        mob_detail::perceive(ctx);
        const game::GoalScanResult scan =
            game::scan_goals(e->ai.running_goals, def->ordered_goals(), mob_detail::build_verdicts(ctx));
        e->ai.running_goals = scan.running;

        // The scan's verdicts become state changes here, in priority order: stops
        // first (a goal that is giving up releases what it held), then starts. The
        // scan itself touches nothing (game/mob_goal.hpp).
        for (const game::GoalEntry &entry : def->ordered_goals()) {
            if (scan.stopped[static_cast<std::size_t>(entry.kind)]) {
                mob_detail::on_goal_stopped(ctx, entry.kind);
            }
        }
        for (const game::GoalEntry &entry : def->ordered_goals()) {
            if (scan.started[static_cast<std::size_t>(entry.kind)]) {
                mob_detail::on_goal_started(ctx, entry.kind);
            }
        }

        // EVERY running goal is applied, in priority order. The steering channel
        // inside `steering` is the only exclusive resource, and the priority order
        // is what decides who gets it (apply_goal takes it only if it is free).
        mob_detail::Steering steering;
        for (const game::GoalEntry &entry : def->ordered_goals()) {
            if (game::goal_running(e->ai.running_goals, entry.kind)) {
                mob_detail::apply_goal(ctx, entry.kind, steering);
            }
        }
        if (steering.has) {
            const glm::dvec2 direction = mob_detail::navigate_step(*e, physics_def, world, rules, steering.destination);
            mob_detail::step_mob_motion(*e, physics_def, world, direction, def->move_speed * steering.speed_scale);
            mob_detail::track_progress(*e, rules);
        } else {
            mob_detail::step_mob_motion(*e, physics_def, world, {0.0, 0.0}, 0.0);
        }
        if (e->health <= 0.0) {
            result.dead.push_back(id);
        }
    }
    // A mob can be killed by another mob earlier in the walk and again noticed by
    // its own iteration; the caller must not erase (and pay loot) twice.
    std::sort(result.dead.begin(), result.dead.end());
    result.dead.erase(std::unique(result.dead.begin(), result.dead.end()), result.dead.end());
    return result;
}

} // namespace opencraft::server
