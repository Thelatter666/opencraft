#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace opencraft::physics {

// Body pose. The collision box does not change with the view angle
// (docs/01 §2 ⚖); only crouching changes its height.
enum class Pose : std::uint8_t {
    Standing = 0,
    Sneaking = 1,
};

// Full kinematic state of the simulated player. Position is the CENTER OF
// THE FEET (bottom-center of the AABB), doubles throughout so the golden
// replay is bit-stable. Velocities are in blocks/tick.
struct PlayerState {
    // ── ⚖ body dimensions (docs/01 §2): standing 0.6×1.8, sneaking 0.6×1.5.
    static constexpr double kHalfWidth = 0.3;
    static constexpr double kStandingHeight = 1.8;
    static constexpr double kSneakingHeight = 1.5;

    glm::dvec3 position{0.0, 0.0, 0.0}; // feet-center
    glm::dvec3 velocity{0.0, 0.0, 0.0}; // blocks/tick

    bool on_ground = false;
    // Distance fallen during the current descent (apex − current y, see
    // fall_peak_y); reset on ground and in water.
    double fall_distance = 0.0;
    // y of the highest point of the current airborne arc; the reference the
    // fall distance is measured against (avoids per-tick accumulation error).
    double fall_peak_y = 0.0;

    double yaw = 0.0;
    double pitch = 0.0;

    Pose pose = Pose::Standing;

    // Health (20 HP = 10 hearts, docs/01 §3). Only fall damage writes it in
    // this task; armor/regen belong to later systems.
    double health = 20.0;
    // ⚖ hurt invulnerability window 0.5 s = 10 ticks. Field reserved only —
    // no combat system exists yet, nothing decrements it (T007 card).
    double invulnerability_ticks = 0.0;

    // Echo of the last applied InputState::sequence (network prediction
    // groundwork; pure pass-through today).
    std::uint32_t last_input_sequence = 0;

    [[nodiscard]] double height() const { return pose == Pose::Sneaking ? kSneakingHeight : kStandingHeight; }
};

} // namespace opencraft::physics
