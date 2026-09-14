#pragma once

#include "opencraft/physics/block_source.hpp"
#include "opencraft/physics/input_state.hpp"
#include "opencraft/physics/physics_config.hpp"
#include "opencraft/physics/player_state.hpp"

namespace opencraft::physics {

// Optional per-tick collision/movement report. Step output that consumers
// (step sounds, particles, landing effects, mob AI, fall-damage UI) would
// otherwise have to re-derive from before/after states — or force another
// signature change on `step_player` to obtain. Written into a caller-owned
// out-parameter, so the frozen four-argument call keeps working unchanged.
struct MoveResult {
    // True when this tick's move was clamped on that axis. `hit_y` covers both
    // "landed on a floor" and "bumped a ceiling"; `hit_ceiling` distinguishes
    // the latter.
    bool hit_x = false;
    bool hit_y = false;
    bool hit_z = false;
    bool hit_ceiling = false;

    // True when ground contact was established during THIS tick (a fresh
    // landing), as opposed to the entity already standing at tick start.
    bool landed = false;

    // Distance fallen since the apex of the current arc, in blocks, as of the
    // END of this tick. Mirrors `PlayerState::fall_distance`, reported so a
    // landing consumer can read the drop height without diffing states.
    double fall_distance = 0.0;

    // Friction factor applied this tick (k = horizontal_drag × S) and the
    // slipperiness sampled for it. Diagnostic: lets a test assert the block
    // query was consulted without re-deriving the pipeline.
    double friction = 0.0;
    double slipperiness = 0.0;

    // T-D8: true when step-assist walked the entity up an obstacle this tick.
    // Distinct from `hit_x`/`hit_z`: a successful step is NOT a collision — the
    // obstacle was climbed, so horizontal speed is retained.
    bool stepped = false;
    // Height the step lifted the entity by, in blocks (0 when `stepped` is
    // false). Report-only: lets a consumer tell a carpet from a bed without
    // re-deriving the terrain query.
    double step_height_used = 0.0;
};

// Advances the simulation by EXACTLY one tick (50 ms at 20 TPS). Pure function
// of (state, input, world, config): no RNG, no wall clock, only double +−×÷
// plus std::floor on the hot path (sin/cos appear once per tick on the input
// yaw; axis-aligned angles are snapped so results are platform-stable).
// Determinism is an acceptance criterion (T007 #5).
//
// The first four parameters are FROZEN (T-D7 interface contract): existing
// callers keep compiling and behaving. The optional fifth parameter is the
// extension point — `MoveResult` exists so M2 can add interactions (step
// sounds, landing effects, mob sensing) without growing the signature again.
void step_player(PlayerState &state, const InputState &input, const IBlockSource &world,
                 const PhysicsConfig &config = PhysicsConfig{}, MoveResult *result = nullptr);

} // namespace opencraft::physics
