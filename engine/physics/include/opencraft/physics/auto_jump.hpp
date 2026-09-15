#pragma once

#include <algorithm>
#include <cmath>

#include "opencraft/physics/block_source.hpp"
#include "opencraft/physics/input_state.hpp"
#include "opencraft/physics/physics_config.hpp"
#include "opencraft/physics/player_state.hpp"

// T-D14: Auto-Jump (Minecraft Java Edition 1.10+, 16w20a;
// docs/research/08-mc-auto-jump-mechanics.md §2-3, §5).
//
// WHY this exists and not a bigger step height (research/08 §5, T-D14 card):
// the player's step_height stays 0.6 by ruling (T-D8.ruling.md S-1), so a full
// 1-block ledge still requires a jump — that IS original MC behaviour.
// Auto-Jump supplies the jump without the keypress: a client-side sense-decide
// pass that runs in the INPUT stage (before `step_player` integrates) and, when
// the geometry ahead says a jump will land on the obstacle, flips
// `InputState::jump`. The existing jump branch in `player_physics.cpp` then does
// everything else, so jump physics — including the sprint +0.2 boost and the
// golden replay numbers — is shared with manual jumping, not duplicated.
//
// ── SCOPE ────────────────────────────────────────────────────────────────────
// Pure decision function only. Injection lives at the call site. No new virtuals
// on `IBlockSource` (T-D7 froze the adapter contract), no change to
// `step_player`. This is a client-side input-stage predicate, not a physics
// force: the server-authority story is M3 and MC itself drives auto-jump from
// the client's input tick.
//
// ── THE GEOMETRY (research/08 §2.2) ──────────────────────────────────────────
// Three checks, in block space, using `solid_at` only (full cubes — Auto-Jump
// reasons about solid geometry, never about sub-block partial heights, which
// belong to step-assist and the future slab content):
//
//   1. Foot probe   — the player's body box swept forward along the input
//                     direction by `scan_distance`. No solid overlapped →
//                     nothing to jump → false.
//   2. Obstacle     — among the columns blocking that sweep that lie AHEAD of
//                     the body (the leading-edge contact faces), the nearest
//                     one by axis distance sets the obstacle height
//                     ΔH = H_top − Y_feet. Trigger iff
//                     step_height < ΔH ≤ max_obstacle_height.
//   3. Clearance    — above that obstacle's top face, `clearance_height` of
//                     headroom must be free over the landing region, or the
//                     jump would bang the head on a hang-down stair / low
//                     ceiling.
//
// Plus the gatekeepers of research/08 §2.1 and the wall-alignment veto of §2.3:
// strafing along a wall must not fire — the move direction is nearly parallel to
// the blocking face (see `wall_normal_epsilon`).
//
// ── WHY THE SCAN DISTANCE IS 1.0 ─────────────────────────────────────────────
// Not a guess: at 20 TPS the fastest ground step is the sprint cruise,
// 5.612 m/s ≈ 0.281 b/tick. `scan_distance` is how far AHEAD of the body the
// probe reaches, so 1.0 blocks is ≥ 3× the per-tick displacement it guards:
// the column about to be hit is always inside the sweep, and a jump injected
// this tick crosses it next tick without the player eating the wall first. The
// upper bound (a near obstacle must not reach so far that it jumps for a wall
// two columns away) is 1.25: 1.0 stays clear of that band too.
//
// ── WHY FIRST-BLOCKING-COLUMN RATHER THAN A FALL-THROUGH LOOP ────────────────
// A column that blocks the sweep but fails the window (too tall, or its
// clearance is bad) is the obstacle the player actually faces — the jump would
// be wasted on a farther one. So the faced column is decided once and its
// verdict is final; there is no "try the next column behind it". This mirrors
// step-assist's candidate logic in intent (the lowest/nearest usable answer),
// with the opposite failure mode: step-assist picks the lowest LIFT, Auto-Jump
// refuses to jump for a column it cannot clear.

namespace opencraft::physics {

struct AutoJumpConfig {
    // Upper bound of the jump window (research/08 §2.2, H_max = 1.25). The
    // lower bound is NOT a field here: it is the entity's own `step_height`
    // read from `PhysicsConfig` (see `should_auto_jump`), so per-entity
    // configs keep working (mobs step 1.0 and never auto-jump for a 1-block
    // ledge). 1.25 is a literal to keep this header independent of the jump
    // apex constant; a caller wanting spec-exactness may set it to the apex
    // (1.2522). Values above it are harmless in practice: the player's own
    // collision solve decides whether the arc survives, and a too-high
    // obstacle ends as a bump rather than a mount.
    double max_obstacle_height = 1.25;
    // Forward scan distance in blocks (the Foot Probe sweep length). See the
    // "WHY 1.0" note above.
    double scan_distance = 1.0;
    // Headroom required above the obstacle's top face, research/08 §2.2.3.
    double clearance_height = 1.8;
    // research/08 §2.3 wall-alignment tolerance. The blocking face is
    // axis-aligned and the move direction is unit, so |cos θ| = the component
    // of the direction along the face's outward normal. Below this the move is
    // read as "parallel to the wall" and the jump is suppressed. 0.05 ≈ 87.1°
    // off-normal: a face the player actually crashes into sits well above 0.7,
    // so the band only catches grazing strafes. MC ships no published figure
    // (research/08 records the test's shape, not a number); 0.05 is chosen.
    double wall_normal_epsilon = 0.05;
    // Master switch, mirroring MC's own client option. Default ON, per the
    // T-D14 card ("默认开启，对齐 MC").
    bool enabled = true;
};

namespace aj_detail {

// AABB in double, block space (the physics module's private working box,
// duplicated here rather than shared so this header stays self-contained and
// the physics .cpp's internal type does not leak across module boundaries).
struct AjBox {
    double min_x, min_y, min_z, max_x, max_y, max_z;
};

[[nodiscard]] inline AjBox body_box(const PlayerState &s) {
    return AjBox{s.position.x - PlayerState::kHalfWidth,
                 s.position.y,
                 s.position.z - PlayerState::kHalfWidth,
                 s.position.x + PlayerState::kHalfWidth,
                 s.position.y + s.height(),
                 s.position.z + PlayerState::kHalfWidth};
}

// Strict voxel overlap: a box face exactly touching a block face is NOT a
// collision. Same convention as player_physics.cpp::box_collides, full-cube
// only.
[[nodiscard]] inline bool overlaps_solid(const IBlockSource &world, const AjBox &b) {
    const int x0 = static_cast<int>(std::floor(b.min_x));
    const int x1 = static_cast<int>(std::floor(b.max_x));
    const int y0 = static_cast<int>(std::floor(b.min_y));
    const int y1 = static_cast<int>(std::floor(b.max_y));
    const int z0 = static_cast<int>(std::floor(b.min_z));
    const int z1 = static_cast<int>(std::floor(b.max_z));
    for (int bx = x0; bx <= x1; ++bx) {
        for (int by = y0; by <= y1; ++by) {
            for (int bz = z0; bz <= z1; ++bz) {
                if (world.solid_at(bx, by, bz) && b.max_x > bx && b.min_x < bx + 1 && b.max_y > by &&
                    b.min_y < by + 1 && b.max_z > bz && b.min_z < bz + 1) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Top face of the solid column at (cx, cz) that grows contiguously upward from
// the feet's supporting layer. Returns the block-space y of the top surface
// (integer), or the feet layer's own top (a no-op floor) when only that layer
// is solid — the caller judges the resulting ΔH, so "the ground itself" reads
// as ΔH = 0 and never triggers.
[[nodiscard]] inline double column_top(const IBlockSource &world, int cx, int cz, double feet_y, int max_layers) {
    const int base = static_cast<int>(std::floor(feet_y - 1e-9));
    if (!world.solid_at(cx, base, cz)) {
        return static_cast<double>(base); // empty below: surface == base, ΔH < 0
    }
    int top_layer = base;
    for (int i = 1; i < max_layers; ++i) {
        if (world.solid_at(cx, base + i, cz)) {
            top_layer = base + i;
        } else {
            break;
        }
    }
    return static_cast<double>(top_layer) + 1.0;
}

} // namespace aj_detail

// Per-tick auto-jump decision: true when this tick's input, combined with the
// world ahead, says the player should be sent into a jump they did not ask
// for. Pure function, no hidden state (card §1 ①); world access restricted to
// the frozen `solid_at` + `liquid_at` (card §1 ②, §2.2).
[[nodiscard]] inline bool should_auto_jump(const PlayerState &state, const InputState &input, const IBlockSource &world,
                                           const PhysicsConfig &cfg, const AutoJumpConfig &aj = AutoJumpConfig{}) {
    // ── Gatekeepers (research/08 §2.1) ─────────────────────────────────────
    // Cheap booleans before any voxel query, in the doc's matrix order.
    if (!aj.enabled) {
        return false;
    }
    if (!state.on_ground) {
        return false; // grounded only; a jump held across ticks re-fires on
                      // landing through this same gate, matching MC
    }
    if (input.sneak) {
        return false; // sneak is edge protection (docs/01 §2); auto-climbing
                      // over it would defeat that (MC: !isShiftKeyDown)
    }
    if (!input.forward || input.backward) {
        return false; // positive forward impulse only (MC: forwardImpulse > 0;
                      // card gate: !in.forward covers still and backing up)
    }
    {
        // In water, never. Same probe pair as the physics module's is_in_water:
        // the feet block and a mid-body block (good-enough heuristic, T007).
        const int x = static_cast<int>(std::floor(state.position.x));
        const int z = static_cast<int>(std::floor(state.position.z));
        const int feet = static_cast<int>(std::floor(state.position.y + 0.01));
        const int mid = static_cast<int>(std::floor(state.position.y + 1.0));
        if (world.liquid_at(x, feet, z) || world.liquid_at(x, mid, z)) {
            return false; // MC: !isSwimming
        }
    }
    // Riding: no vehicle system exists yet (M2 lands it). MC's !isPassenger
    // gate is therefore vacuously satisfied, not skipped silently — the card's
    // acceptance #1 asks that this be reported, not implemented.

    // ── Move direction ──────────────────────────────────────────────────────
    // Same yaw→direction construction as player_physics.cpp::input_direction
    // (forward = (−sin, −cos); right = (−fz, fx)), including its two
    // determinism conventions: the 1e-12 component snap and normalising the
    // ±1 axis inputs. Auto-jump reasons about the move DIRECTION only, so the
    // physics module's 0.98 pre-scale / clamp-up geometry is irrelevant here.
    double fx = -std::sin(input.yaw);
    double fz = -std::cos(input.yaw);
    if (std::abs(fx) < 1e-12) {
        fx = 0.0;
    }
    if (std::abs(fz) < 1e-12) {
        fz = 0.0;
    }
    const double rx = -fz;
    const double rz = fx;
    const double fwd = (input.forward ? 1.0 : 0.0) - (input.backward ? 1.0 : 0.0);
    const double strafe = (input.right ? 1.0 : 0.0) - (input.left ? 1.0 : 0.0);
    double dir_x = fx * fwd + rx * strafe;
    double dir_z = fz * fwd + rz * strafe;
    const double len_sq = dir_x * dir_x + dir_z * dir_z;
    if (len_sq <= 0.0) {
        return false; // forward held but strafe cancels it: no move, no intent
    }
    const double inv = 1.0 / std::sqrt(len_sq);
    dir_x *= inv;
    dir_z *= inv;

    // ── 1. Foot probe: body box swept forward (research/08 §2.2.1) ─────────
    using namespace aj_detail; // NOLINT: file-scope pure helpers, no state
    const AjBox body = body_box(state);
    const double sd = aj.scan_distance;
    const AjBox swept{body.min_x + std::min(0.0, dir_x * sd), body.min_y, body.min_z + std::min(0.0, dir_z * sd),
                      body.max_x + std::max(0.0, dir_x * sd), body.max_y, body.max_z + std::max(0.0, dir_z * sd)};
    const double feet_y = state.position.y;
    // The FIRST layer the body box overlaps: a full cube there blocks horizontal
    // motion (the layer the player stands ON is below the body box and must not
    // count). +1e-9 so feet flush with an integer surface read the layer above.
    const int probe_layer = static_cast<int>(std::floor(feet_y + 1e-9));
    const int max_layers = static_cast<int>(std::ceil(aj.max_obstacle_height)) + 2;

    // Find the nearest blocking column among those whose LEADING face lies
    // ahead of the body in the move direction (so a column the body already
    // grazes, or one behind it, never counts). Distance is measured to the
    // body's near face on the face's own axis: for an axis-aligned move (the
    // common, and tested, case) this is exact; for diagonals the nearest
    // column on the dominant axis is the one faced first in any reasonable
    // walk geometry. The §2.3 parallel-wall veto is then applied to THIS
    // faced column's normal, not to grazing candidates behind it.
    double best_axis_dist = 1.0 / 0.0;
    int best_cx = 0;
    int best_cz = 0;
    double best_normal = 0.0; // |cos θ| between the move direction and the
                              // faced wall's outward normal (unit dir, unit
                              // axis normal → just the dominant |component|)
    bool found = false;

    const int x0 = static_cast<int>(std::floor(swept.min_x));
    const int x1 = static_cast<int>(std::floor(swept.max_x));
    const int z0 = static_cast<int>(std::floor(swept.min_z));
    const int z1 = static_cast<int>(std::floor(swept.max_z));
    const double eps = 1e-9;

    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) {
            // Strict voxel overlap against the swept box (touching faces are
            // NOT contacts, same convention as the physics module's
            // box_collides): a column merely touching the body's flush face —
            // a player side-surfing a wall with the box exactly at the face —
            // is not in the corridor at all. This check is also what keeps
            // "column ahead on one axis but far behind on the other" out.
            if (!(swept.max_x > cx && swept.min_x < cx + 1 && swept.max_z > cz && swept.min_z < cz + 1)) {
                continue;
            }
            if (!world.solid_at(cx, probe_layer, cz)) {
                continue; // not a blocking column at body height
            }
            // The blocking face and the axis distance to it.
            double axis_dist = 0.0;
            double normal_component = 0.0;
            if (dir_x > 0.0 && static_cast<double>(cx) >= body.max_x - eps) {
                axis_dist = static_cast<double>(cx) - body.max_x;
                normal_component = dir_x;
            } else if (dir_x < 0.0 && static_cast<double>(cx + 1) <= body.min_x + eps) {
                axis_dist = body.min_x - static_cast<double>(cx + 1);
                normal_component = -dir_x;
            } else if (dir_z > 0.0 && static_cast<double>(cz) >= body.max_z - eps) {
                axis_dist = static_cast<double>(cz) - body.max_z;
                normal_component = dir_z;
            } else if (dir_z < 0.0 && static_cast<double>(cz + 1) <= body.min_z + eps) {
                axis_dist = body.min_z - static_cast<double>(cz + 1);
                normal_component = -dir_z;
            } else {
                continue; // column the body already overlaps head-on, or a
                          // pure-corner touch: not a forward-blocking face
            }
            if (axis_dist > sd + eps) {
                continue; // beyond scan reach (belt for the swept bounds)
            }
            if (!found || axis_dist < best_axis_dist - eps) {
                found = true;
                best_axis_dist = axis_dist;
                best_cx = cx;
                best_cz = cz;
                best_normal = normal_component;
            }
        }
    }
    if (!found) {
        return false; // nothing ahead within the scan that the move actually
                      // crashes into: just walk
    }
    // §2.3 veto on the FACED wall only: the nearest blocking column decides,
    // so a grazing candidate behind the wall being walked into must not
    // suppress it. |cos θ| < ε means the move is parallel to the face:
    // sliding along it, no crossing intent, no jump.
    if (std::abs(best_normal) < aj.wall_normal_epsilon) {
        return false;
    }

    // ── 2. Height window (research/08 §2.2.2) ───────────────────────────────
    const double top = column_top(world, best_cx, best_cz, feet_y, max_layers);
    const double dh = top - feet_y;
    if (dh <= cfg.step_height + 1e-9) {
        return false; // step-assist's job, not a jump
    }
    if (dh > aj.max_obstacle_height + 1e-9) {
        return false; // a single jump cannot clear this; do not waste it
    }

    // ── 3. Clearance above the obstacle (research/08 §2.2.3) ────────────────
    // The landing surface is the obstacle's top face. The body, when mounted,
    // occupies y ∈ [top, top + height) at its CURRENT x/z footprint (step-up
    // and jump-land both mount at the feet position, MC semantics): the
    // columns under that footprint must be free for `clearance_height` above
    // `top`. The footprint is the body box, not the swept box — the scan
    // region ahead of a wide wall is solid wall itself and would veto every
    // mount of anything deeper than the scan distance. The obstacle's own
    // columns are automatically clear there: ΔH already ≤ max_obstacle_height
    // and column_top found their contiguous run ending at `top`.
    const double mount_lo = top + 1e-9;
    const double mount_hi = top + aj.clearance_height - 1e-9;
    const AjBox landing{body.min_x, mount_lo, body.min_z, body.max_x, mount_hi, body.max_z};
    if (overlaps_solid(world, landing)) {
        return false; // hang-down stair / low ceiling above the obstacle
    }
    return true;
}

} // namespace opencraft::physics
