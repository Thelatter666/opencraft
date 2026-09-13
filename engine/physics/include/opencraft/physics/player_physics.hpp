#pragma once

#include "opencraft/physics/block_source.hpp"
#include "opencraft/physics/input_state.hpp"
#include "opencraft/physics/physics_config.hpp"
#include "opencraft/physics/player_state.hpp"

namespace opencraft::physics {

// Advances the simulation by EXACTLY one tick (50 ms at 20 TPS). Pure
// function of (state, input, world, config): no RNG, no wall clock, only
// double +−×÷ plus std::floor on the hot path (sin/cos appear once per tick
// on the input yaw; axis-aligned angles are snapped so results are
// platform-stable). Determinism is an acceptance criterion (T007 #5).
void step_player(PlayerState &state, const InputState &input, const IBlockSource &world,
                 const PhysicsConfig &config = PhysicsConfig{});

} // namespace opencraft::physics
