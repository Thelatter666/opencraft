#include "opencraft/physics/player_physics.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "opencraft/physics/sweep.hpp"

namespace opencraft::physics {
namespace {

// The player's own body box: the shared `Box` shape (T-D40) with the player's
// dimensions — fixed half width, pose-dependent height.
[[nodiscard]] Box player_box(const PlayerState &s) {
    return box_of(s.position, PlayerState::kHalfWidth, s.height());
}

// True when some solid block overlaps the box footprint one probe-layer below
// the feet (i.e. the entity would still stand after shifting horizontally).
[[nodiscard]] bool has_support_at(const PlayerState &s, const IBlockSource &world, double offset_x, double offset_z,
                                  double probe_depth) {
    // Height-aware like box_collides (T-D8): standing ON a 0.25 ledge means no
    // full cube is below, and treating that as unsupported would drop the
    // entity through the block it is standing on. The probe layer is below the
    // feet, so any positive top there means the block's surface reaches it.
    const double probe_y = s.position.y - probe_depth;
    const int layer = static_cast<int>(std::floor(probe_y));
    const double min_x = s.position.x - PlayerState::kHalfWidth + offset_x;
    const double max_x = s.position.x + PlayerState::kHalfWidth + offset_x;
    const double min_z = s.position.z - PlayerState::kHalfWidth + offset_z;
    const double max_z = s.position.z + PlayerState::kHalfWidth + offset_z;
    const int x0 = static_cast<int>(std::floor(min_x));
    const int x1 = static_cast<int>(std::floor(max_x));
    const int z0 = static_cast<int>(std::floor(min_z));
    const int z1 = static_cast<int>(std::floor(max_z));
    for (int bx = x0; bx <= x1; ++bx) {
        for (int bz = z0; bz <= z1; ++bz) {
            const double top = world.shape_top_at(bx, layer, bz);
            if (top <= 0.0 || !(max_x > bx && min_x < bx + 1 && max_z > bz && min_z < bz + 1)) {
                continue;
            }
            if (static_cast<double>(layer) + top > probe_y) {
                return true;
            }
        }
    }
    return false;
}

// Sneak edge protection (docs/01 §2 手感项): shrink the horizontal
// displacement in ≤0.05-block steps (x first, then z) until the shifted box
// keeps ground support. Simplified MC backoff — the velocity itself is left
// untouched; only this tick's offset is reduced.
void back_off_from_edge(const PlayerState &s, const IBlockSource &world, const PhysicsConfig &cfg, double &dx,
                        double &dz) {
    const double step = cfg.sneak_edge_step;
    while ((dx != 0.0 || dz != 0.0) && !has_support_at(s, world, dx, dz, cfg.ground_probe_depth)) {
        if (dx != 0.0) {
            dx = std::abs(dx) <= step ? 0.0 : dx - (dx > 0.0 ? step : -step);
        } else {
            dz = std::abs(dz) <= step ? 0.0 : dz - (dz > 0.0 ? step : -step);
        }
    }
}

// Feet block and a mid-body probe decide "in water" (good-enough heuristic
// per the T007 card; exact swim feel is deferred).
[[nodiscard]] bool is_in_water(const IBlockSource &world, const PlayerState &s) {
    const int x = static_cast<int>(std::floor(s.position.x));
    const int z = static_cast<int>(std::floor(s.position.z));
    const int feet = static_cast<int>(std::floor(s.position.y + 0.01));
    const int mid = static_cast<int>(std::floor(s.position.y + 1.0));
    return world.liquid_at(x, feet, z) || world.liquid_at(x, mid, z);
}

// Slipperiness of the block the entity is standing on — the block half of the
// friction product (docs/research/07 §7.2). Sampled one probe-layer below the
// feet so a full-cube floor reports its own value.
[[nodiscard]] double slipperiness_below(const IBlockSource &world, const PlayerState &s, const PhysicsConfig &cfg) {
    const int x = static_cast<int>(std::floor(s.position.x));
    const int z = static_cast<int>(std::floor(s.position.z));
    const int y = static_cast<int>(std::floor(s.position.y - cfg.ground_probe_depth));
    return world.slipperiness_at(x, y, z);
}

// Yaw → normalized horizontal input direction. forward = (-sin, -cos);
// right = forward × up; backward contributes -forward. Components under
// 1e-12 are snapped to zero so axis-aligned yaws (the golden replay only
// uses those) are bit-stable across libm implementations.
[[nodiscard]] std::pair<double, double> input_direction(const InputState &in) {
    double fx = -std::sin(in.yaw);
    double fz = -std::cos(in.yaw);
    if (std::abs(fx) < 1e-12) {
        fx = 0.0;
    }
    if (std::abs(fz) < 1e-12) {
        fz = 0.0;
    }
    const double rx = -fz;
    const double rz = fx;
    const double fwd = (in.forward ? 1.0 : 0.0) - (in.backward ? 1.0 : 0.0);
    const double strafe = (in.right ? 1.0 : 0.0) - (in.left ? 1.0 : 0.0);
    double dx = fx * fwd + rx * strafe;
    double dz = fz * fwd + rz * strafe;
    const double len_sq = dx * dx + dz * dz;
    if (len_sq > 0.0) {
        const double inv = 1.0 / std::sqrt(len_sq);
        dx *= inv;
        dz *= inv;
    }
    return {dx, dz};
}

// MC's input geometry (docs/research/06 §3.2) — the entire origin of 45°
// Strafe. The raw ±1 forward/strafe axes are:
//   1. pre-scaled by `input_scale` (0.98), and by `sneak_multiplier` (0.3)
//      while sneaking — BOTH act on the input vector, unlike sprint;
//   2. combined into n = hypot(strafe, forward);
//   3. clamped UP to 1 when n < 1 — single-axis input (0.98, 0) has n = 0.98
//      and is therefore clamped, so it only reaches 0.98 of full acceleration;
//   4. scaled to the acceleration magnitude, preserving the input direction.
// A diagonal input (0.98, 0.98) has n = 1.386 > 1 and is NOT clamped, which is
// precisely why diagonal movement is 1/0.98 ≈ 2% faster. Because sneak acts on
// the input vector and sprint on the scalar magnitude, a sneak diagonal gains
// √2 (both axes halved, then the clamp applies to the tiny 0.42 magnitude)
// while a sprint diagonal gains only 1/0.98.
//
// Returns the scalar the acceleration magnitude must be multiplied by. The
// direction is unaffected by any of the above, since both axes are scaled
// equally.
[[nodiscard]] double input_magnitude_scale(const InputState &in, const PhysicsConfig &cfg) {
    const double fwd = (in.forward ? 1.0 : 0.0) - (in.backward ? 1.0 : 0.0);
    const double strafe = (in.right ? 1.0 : 0.0) - (in.left ? 1.0 : 0.0);
    const double sneak = in.sneak ? cfg.sneak_multiplier : 1.0;
    const double s = strafe * cfg.input_scale * sneak;
    const double f = fwd * cfg.input_scale * sneak;
    const double n = std::hypot(s, f);
    if (n < 1e-4) {
        return 0.0; // no input: the acceleration term drops out entirely
    }
    const double clamped_n = n < 1.0 ? 1.0 : n;
    return n / clamped_n;
}

// Per-axis clamped moves (docs/research/03 §6.1: Y → X → Z). The geometry now
// lives in `physics::sweep_axis_*` (T-D40) so the player and the entities move
// through one implementation; what stays here is the player's own collision
// RESPONSE — zeroing the velocity component that was just lost.
double move_axis_y(PlayerState &s, const IBlockSource &world, double dy, bool &hit_ground, bool &hit_ceiling) {
    const AxisSweep swept = sweep_axis_y(s.position, PlayerState::kHalfWidth, s.height(), world, dy);
    hit_ground = hit_ground || swept.ground;
    hit_ceiling = hit_ceiling || swept.ceiling;
    return swept.delta;
}

double move_axis_x(PlayerState &s, const IBlockSource &world, double dx, bool &hit_wall) {
    const AxisSweep swept = sweep_axis_x(s.position, PlayerState::kHalfWidth, s.height(), world, dx);
    if (swept.hit) {
        s.velocity.x = 0.0;
        hit_wall = true;
    }
    return swept.delta;
}

double move_axis_z(PlayerState &s, const IBlockSource &world, double dz, bool &hit_wall) {
    const AxisSweep swept = sweep_axis_z(s.position, PlayerState::kHalfWidth, s.height(), world, dz);
    if (swept.hit) {
        s.velocity.z = 0.0;
        hit_wall = true;
    }
    return swept.delta;
}

// ── Step-assist (T-D8; docs/research/06 §6.3) ────────────────────────────────
//
// MC walks up obstacles ≤ `step_height` WITHOUT a jump and WITHOUT losing
// horizontal speed. The algorithm is:
//
//   1. solve the horizontal move flat (no step) → horizontal distance d_flat
//   2. collect every candidate lift height (top faces ≤ step_height above the
//      feet), sorted ascending
//   3. retry the FULL solve at each candidate; stop at the first one that
//      yields a greater horizontal distance than d_flat
//   4. return that result, then fall back down onto the surface
//
// ★ The load-bearing property is step 3's "first", not "best": MC picks the
// LOWEST lift that brings a gain, not the highest lift available. That is what
// lets an entity climb onto an intermediate ledge of a stepped shape instead of
// being flung to its top. Counting "how far I moved" rather than "did I move"
// matters too: a lift that unblocks X but blocks Z travels less than the flat
// solve and must lose, so a step is only taken when it helps.

// Signed horizontal progress along the intended move direction, measured from
// `origin`. Horizontal ONLY: a stepped retry starts at a different height by
// construction, so folding Y into the comparison would make every lift look
// like a gain and silently turn "lowest gainful" into "highest available".
// Signed (not a distance) so a retry that slips backwards behind the origin
// can never beat the flat solve.
[[nodiscard]] double horizontal_progress(const glm::dvec3 &origin, const glm::dvec3 &at, double dir_x, double dir_z) {
    const double len = std::hypot(dir_x, dir_z);
    if (len < 1e-12) {
        return 0.0;
    }
    return ((at.x - origin.x) * dir_x + (at.z - origin.z) * dir_z) / len;
}

// A stepped retry must beat the flat solve by more than this many blocks of
// horizontal progress to count as a gain. Guards against adopting a lift whose
// advantage is pure floating-point noise.
constexpr double kStepGainEpsilon = 1e-12;

// Candidate lift heights in (0, step_height], ascending and deduplicated.
// Collected over the SWEPT footprint — the box union'd with the box at its
// intended destination — because the flat move clamps the entity flush against
// the obstacle, and a flush box no longer overlaps the very block that stopped
// it. That is the classic way to get "no candidates, so no step".
[[nodiscard]] std::vector<double> collect_step_candidates(const IBlockSource &world, const PlayerState &s,
                                                          const PhysicsConfig &cfg, double sweep_dx, double sweep_dz) {
    const Box here = player_box(s);
    PlayerState moved = s;
    moved.position.x += sweep_dx;
    moved.position.z += sweep_dz;
    const Box there = player_box(moved);
    const Box b{std::min(here.min_x, there.min_x), std::min(here.min_y, there.min_y),
                std::min(here.min_z, there.min_z), std::max(here.max_x, there.max_x),
                std::max(here.max_y, there.max_y), std::max(here.max_z, there.max_z)};

    const int x0 = static_cast<int>(std::floor(b.min_x));
    const int x1 = static_cast<int>(std::floor(b.max_x));
    const int z0 = static_cast<int>(std::floor(b.min_z));
    const int z1 = static_cast<int>(std::floor(b.max_z));
    // Only the layer whose bottom is at the feet can present a step-up face.
    // Higher layers would need a lift of ≥ 1 block to be relevant, which no
    // legal step_height reaches — and their bottom faces are ceilings, not
    // floors.
    const int y = static_cast<int>(std::floor(b.min_y));

    std::vector<double> cands;
    for (int bx = x0; bx <= x1; ++bx) {
        for (int bz = z0; bz <= z1; ++bz) {
            const double top = world.shape_top_at(bx, y, bz);
            if (top <= 0.0 || !(b.max_x > bx && b.min_x < bx + 1 && b.max_z > bz && b.min_z < bz + 1)) {
                continue;
            }
            const double lift = (static_cast<double>(y) + top) - b.min_y;
            if (lift > 1e-9 && lift <= cfg.step_height + 1e-9) {
                cands.push_back(lift);
            }
        }
    }
    std::sort(cands.begin(), cands.end());
    cands.erase(std::unique(cands.begin(), cands.end(), [](double a, double c) { return std::abs(a - c) < 1e-9; }),
                cands.end());
    return cands;
}

// Result of one full horizontal solve, plus the post-move state so a winning
// attempt can be adopted wholesale.
struct SubMove {
    PlayerState state{};
    bool hit_x = false;
    bool hit_z = false;
};

// Full horizontal solve: re-applies the tick's remaining dx/dz in the same
// substep shares the main loop uses, per docs/research/03 §6.1. Identical for
// the flat attempt and every stepped retry, so the comparison is
// apples-to-apples — the ONLY difference between them is the starting height.
[[nodiscard]] SubMove resolve_horizontal(const IBlockSource &world, const PlayerState &from, double dx, double dz,
                                         int substeps, int done) {
    SubMove out;
    out.state = from;
    // MC's solve retries the FULL remaining displacement, not one substep's
    // share: the step replaces the whole horizontal move, and comparing a
    // partial-solve distance against a partial flat distance is what would make
    // corner cases pick the wrong ledge.
    const int remaining = substeps - done;
    const double share_x = dx / static_cast<double>(substeps);
    const double share_z = dz / static_cast<double>(substeps);
    for (int k = 0; k < remaining; ++k) {
        move_axis_x(out.state, world, share_x, out.hit_x);
        move_axis_z(out.state, world, share_z, out.hit_z);
    }
    return out;
}

// Highest surface the footprint would rest on within [bottom_y, from_y],
// measured DOWN from `from_y` so a partial block is landed on its actual top
// face rather than on the full block above it. Returns < 0 when the range
// contains nothing to stand on.
[[nodiscard]] double settle_onto_surface(const IBlockSource &world, const PlayerState &s, double bottom_y,
                                         double from_y) {
    Box b = player_box(s);
    b.min_y = bottom_y; // only the footprint and the y-range matter here
    return highest_surface_below(world, b, from_y, bottom_y);
}

// Per-component momentum cut-off (docs/research/06 §2.4/§4.4). This is what
// makes the glide tail finite; 0.003 is the value the ⚖ 1.2522 apex is derived
// from. Judged PER COMPONENT like MC — a magnitude test would alter the
// diagonal-drift behaviour.
void apply_momentum_threshold(PlayerState &s, const PhysicsConfig &cfg) {
    if (std::abs(s.velocity.x) < cfg.momentum_threshold) {
        s.velocity.x = 0.0;
    }
    if (std::abs(s.velocity.y) < cfg.momentum_threshold) {
        s.velocity.y = 0.0;
    }
    if (std::abs(s.velocity.z) < cfg.momentum_threshold) {
        s.velocity.z = 0.0;
    }
}

} // namespace

void step_player(PlayerState &s, const InputState &in, const IBlockSource &world, const PhysicsConfig &cfg,
                 MoveResult *result) {
    if (result != nullptr) {
        *result = MoveResult{};
    }
    s.last_input_sequence = in.sequence;

    // Pose: crouch immediately; standing up requires headroom for the taller
    // box (MC behavior — stay sneaking when the ceiling blocks it).
    if (in.sneak) {
        s.pose = Pose::Sneaking;
    } else if (s.pose == Pose::Sneaking) {
        Box standing = player_box(s);
        standing.max_y = s.position.y + PlayerState::kStandingHeight;
        if (!box_collides(world, standing)) {
            s.pose = Pose::Standing;
        }
    }

    // Ground support re-verification: walking off an edge must be detected
    // before the move (the Y move sees dy == 0 while standing). `has_support_at`
    // is height-aware since T-D8, so standing on a sub-block ledge reads as
    // supported with no special case needed here.
    if (s.on_ground && !has_support_at(s, world, 0.0, 0.0, cfg.ground_probe_depth)) {
        s.on_ground = false;
    }

    const bool water = is_in_water(world, s);

    // ── Sprint state machine (docs/research/05 §1; explicit state per the
    // T-D1 interface contract so headless replay tests can assert it).
    // Stop conditions first (MC checks them every tick): forward released,
    // backward pressed, sneak held, hunger at or below the gate, or a wall hit
    // recorded by the previous tick's move.
    //
    // Sneak is a stop condition because MC gates sprinting on
    // moveForward ≥ 0.8, and sneaking scales the input axes by 0.3 → 0.294,
    // which is below the gate. Without this the two multipliers would stack
    // (sprint ×1.3 on a sneak-scaled input) and the player would SNEAK FASTER
    // than they walk.
    if (s.sprinting &&
        (!in.forward || in.backward || in.sneak || s.hunger <= cfg.sprint_min_hunger || s.collided_horizontally)) {
        s.sprinting = false;
    }
    // Activation. Key path: sprint key held + forward held (works on ground
    // and in air). Double-tap path: a forward press edge while grounded arms
    // a 7-tick window; a second press inside the window engages sprint
    // (requires onGround). MC details not reproduced (documented in the T-D1
    // report): the 1-tick activation delay in air, and the pre-1.9 30-second
    // auto-stop. Per docs/research/05 §1.2 the documented end-conditions are
    // forward-release / backward / hunger / collision — releasing the sprint
    // key is NOT among them, so once engaged the sprint persists while
    // forward is held.
    if (!s.sprinting && s.hunger > cfg.sprint_min_hunger) {
        if (in.sprint && in.forward && !in.backward && !in.sneak) {
            s.sprinting = true;
        } else if (in.forward_press && in.forward && s.on_ground && !in.sprint && !in.sneak) {
            if (s.sprint_toggle_timer > 0) {
                s.sprinting = true;
            } else {
                s.sprint_toggle_timer = cfg.sprint_toggle_window_ticks;
            }
        }
    }
    // Window countdown (MC decrements sprintToggleTimer every tick, before
    // the press check — a press exactly 7 ticks after the first re-arms).
    if (s.sprint_toggle_timer > 0) {
        --s.sprint_toggle_timer;
    }

    // ── Friction factor for this tick ───────────────────────────────────────
    // Slipperiness comes from the block below (the block half) and multiplies
    // the entity's own air resistance (the entity half), per docs/research/07
    // §7.2. Airborne the block term is 1.0 — MC's rule, and the reason a jump
    // carries speed instead of braking.
    const double slipperiness = s.on_ground ? slipperiness_below(world, s, cfg) : 1.0;
    const double friction = cfg.horizontal_drag * slipperiness;
    if (result != nullptr) {
        result->friction = friction;
        result->slipperiness = slipperiness;
    }

    // ── Horizontal acceleration, accumulated but NOT yet damped ─────────────
    // The stored velocity is the post-damping value from the previous tick.
    // This tick's DISPLACEMENT is (stored velocity + this tick's acceleration),
    // and the damping is applied only AFTER the move (docs/research/06 §1.1
    // step 4 vs 6 — the ordering is what the apex and jump distances are
    // derived from).
    if (water) {
        // DEFERRED (out of T-D7 scope): swimming keeps the pre-T-D7
        // stored-target formulation verbatim, where the steady state is exactly
        // `water_cruise_speed` and is therefore independent of `water_drag`.
        const auto [dir_x, dir_z] = input_direction(in);
        const double accel = cfg.water_cruise_speed * (1.0 - cfg.water_drag);
        s.velocity.x = s.velocity.x * cfg.water_drag + dir_x * accel;
        s.velocity.z = s.velocity.z * cfg.water_drag + dir_z * accel;
    } else {
        const double mode = s.sprinting ? cfg.sprint_multiplier : 1.0;
        double magnitude;
        if (s.on_ground) {
            // The CUBE of (0.6/S) is load-bearing: it is what makes ice slow to
            // accelerate but slow to stop, rather than simply faster
            // (docs/research/06 §7.1). A linear term gets the ice feel wrong.
            const double skid = kDefaultSlipperiness / slipperiness;
            magnitude = cfg.ground_accel * mode * skid * skid * skid;
        } else {
            magnitude = cfg.air_accel * mode;
        }
        magnitude *= input_magnitude_scale(in, cfg);
        const auto [dir_x, dir_z] = input_direction(in);
        s.velocity.x += dir_x * magnitude;
        s.velocity.z += dir_z * magnitude;
    }

    double dx = s.velocity.x;
    double dz = s.velocity.z;
    if (s.on_ground && in.sneak) {
        back_off_from_edge(s, world, cfg, dx, dz);
    }

    // Ground jump sets the velocity BEFORE the move (MC order: the first
    // tick's displacement is the full 0.42, which is what makes the apex
    // come out at exactly 1.2522 with the gravity/drag pair). Sprinting adds
    // MC's RAW +0.2 facing impulse (docs/research/06 §5.1) on the jump tick;
    // under this pipeline it hits ⚖ 7.127 m/s by itself.
    if (in.jump && s.on_ground && !water) {
        s.velocity.y = cfg.jump_velocity;
        s.on_ground = false;
        if (s.sprinting) {
            const double fx = -std::sin(in.yaw);
            const double fz = -std::cos(in.yaw);
            s.velocity.x += fx * cfg.sprint_jump_boost;
            s.velocity.z += fz * cfg.sprint_jump_boost;
        }
        dx = s.velocity.x;
        dz = s.velocity.z;
    }

    // Substeps of at most max_substep blocks prevent tunneling
    // (docs/research/03 §6.2); axis order Y → X → Z per substep.
    const double max_component = std::max({std::abs(dx), std::abs(s.velocity.y), std::abs(dz)});
    int substeps = static_cast<int>(std::ceil(max_component / cfg.max_substep));
    if (substeps < 1) {
        substeps = 1;
    }

    bool hit_x = false;
    bool hit_z = false;
    bool hit_ground = false;
    bool hit_ceiling = false;
    bool stepped = false;
    double step_height_used = 0.0;
    for (int i = 0; i < substeps; ++i) {
        bool sub_ground = false;
        bool sub_ceiling = false;
        move_axis_y(s, world, s.velocity.y / substeps, sub_ground, sub_ceiling);
        hit_ground = hit_ground || sub_ground;
        hit_ceiling = hit_ceiling || sub_ceiling;
        if (sub_ground) {
            s.on_ground = true;
            s.velocity.y = 0.0;
            // Fall damage (docs/01 §2 ⚖): floor(fall_distance − 3) HP,
            // measured from the apex to the landing surface; water contact
            // at tick start means the landing is a water landing.
            const double fallen = s.fall_peak_y - s.position.y;
            if (!water && fallen > cfg.fall_damage_offset) {
                s.health -= std::floor(fallen - cfg.fall_damage_offset);
            }
        }
        if (sub_ceiling) {
            s.velocity.y = 0.0;
        }

        // ── Step-assist (docs/research/06 §6.2 step 4: "if onGround and a
        // horizontal collision occurred → try to step"). The window is the
        // horizontal pre-clamp position: MC decides from the position it
        // actually reached, not from where it was clamped to.
        const PlayerState pre = s;
        move_axis_x(s, world, dx / substeps, hit_x);
        move_axis_z(s, world, dz / substeps, hit_z);

        if ((hit_x || hit_z) && pre.on_ground && !water) {
            // Both attempts start from `pre`, the state before this substep's
            // horizontal move: identical inputs, so the comparison isolates the
            // effect of the lift.
            const SubMove flat = resolve_horizontal(world, pre, dx, dz, substeps, i);
            const double d_flat = horizontal_progress(pre.position, flat.state.position, dx, dz);

            const double remaining_frac = static_cast<double>(substeps - i) / static_cast<double>(substeps);
            const std::vector<double> cands =
                collect_step_candidates(world, pre, cfg, dx * remaining_frac, dz * remaining_frac);

            // Ascending candidates → the FIRST gain wins. That is the
            // load-bearing property: the lowest lift that helps, not the
            // highest lift available.
            for (const double lift : cands) {
                PlayerState lifted = pre;
                lifted.position.y += lift;
                const SubMove tried = resolve_horizontal(world, lifted, dx, dz, substeps, i);
                if (horizontal_progress(pre.position, tried.state.position, dx, dz) <= d_flat + kStepGainEpsilon) {
                    continue;
                }
                // Adopt it, then settle back DOWN onto whatever the new
                // footprint rests on, so the entity is never left hovering at
                // the lift. Only falling is allowed here: an upward settle
                // would find a higher face elsewhere in the footprint and
                // teleport the entity onto the top of the block instead of the
                // inner ledge it just mounted. The window is the step itself
                // plus a probe depth.
                s = tried.state;
                hit_x = tried.hit_x;
                hit_z = tried.hit_z;
                // Groundedness is NOT re-derived from has_support_at() here:
                // that probe is false at the lifted height because nothing
                // hangs under the box mid-step. It follows from the settle,
                // i.e. from the block the entity actually stepped onto.
                const double surface = settle_onto_surface(world, s, s.position.y - lift - cfg.ground_probe_depth,
                                                           s.position.y + cfg.ground_probe_depth);
                if (surface >= 0.0) {
                    s.position.y = surface;
                    s.on_ground = true;
                } else {
                    // Nothing under the new footprint within the step window:
                    // the entity cleared the obstacle and is now over a drop.
                    // It must NOT claim to be grounded at the lifted height —
                    // that is precisely the "left hanging in mid-air" failure
                    // mode. Gravity takes it from here next tick.
                    s.on_ground = false;
                }
                // Re-seed the fall-distance reference at the new surface: the
                // step is not a fall, and leaving the stale apex would report
                // phantom fall damage on the next landing.
                s.fall_peak_y = s.position.y;
                s.fall_distance = 0.0;
                s.velocity.y = 0.0;
                stepped = true;
                step_height_used = lift;
                break;
            }
            // A step consumed the rest of this tick's horizontal displacement,
            // so the remaining substeps must not apply it a second time.
            if (stepped) {
                break;
            }
        }
    }
    s.collided_horizontally = hit_x || hit_z;
    // A successful step is NOT a wall hit: the whole point (user report
    // 2026-09-14) is that horizontal speed survives the obstacle.
    if (stepped) {
        s.collided_horizontally = false;
    }

    // Fall-distance bookkeeping, measured against the arc apex so whole-block
    // drops land on exact values (no per-tick accumulation drift).
    if (water || s.on_ground) {
        s.fall_distance = 0.0;
        s.fall_peak_y = s.position.y;
    } else {
        if (s.velocity.y > 0.0) {
            s.fall_peak_y = s.position.y;
            s.fall_distance = 0.0;
        } else {
            if (s.fall_peak_y < s.position.y) {
                s.fall_peak_y = s.position.y;
            }
            s.fall_distance = s.fall_peak_y - s.position.y;
        }
    }

    // ── Damping AFTER the displacement (docs/research/06 §10 point 1) ───────
    if (water) {
        s.velocity.y = s.velocity.y * cfg.water_drag + (in.jump ? cfg.swim_up_accel : -cfg.water_gravity);
        if (s.velocity.y > cfg.water_max_up_speed) {
            s.velocity.y = cfg.water_max_up_speed;
        }
    } else {
        if (s.on_ground) {
            s.velocity.y = 0.0;
        } else {
            s.velocity.y = (s.velocity.y - cfg.gravity) * cfg.vertical_drag;
        }
        s.velocity.x *= friction;
        s.velocity.z *= friction;
    }
    // The cut-off runs after the damping, so the tick's own acceleration is
    // never truncated — only the decaying remainder of past momentum.
    apply_momentum_threshold(s, cfg);

    if (result != nullptr) {
        result->hit_x = hit_x;
        result->hit_y = hit_ground || hit_ceiling;
        result->hit_z = hit_z;
        result->hit_ceiling = hit_ceiling;
        result->landed = hit_ground;
        result->fall_distance = s.fall_distance;
        result->stepped = stepped;
        result->step_height_used = step_height_used;
    }
}

} // namespace opencraft::physics
