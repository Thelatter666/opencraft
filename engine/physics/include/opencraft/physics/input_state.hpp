#pragma once

#include <cstdint>

namespace opencraft::physics {

// Abstract, GLFW-free per-tick input sample. `sequence` is reserved for
// client-side prediction / reconciliation later (T-net); step_player only
// echoes it into PlayerState::last_input_sequence for now.
struct InputState {
    // View direction. Radians; yaw follows the engine convention
    // forward = (-sin(yaw), -cos(yaw)) so yaw = -π/2 faces +X, yaw = 0
    // faces -Z, yaw = π faces +Z. Pitch is carried but unused by movement.
    double yaw = 0.0;
    double pitch = 0.0;

    bool forward = false;
    bool left = false;
    bool right = false;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;

    std::uint32_t sequence = 0;
};

} // namespace opencraft::physics
