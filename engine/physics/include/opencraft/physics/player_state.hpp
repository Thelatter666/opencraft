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
    // ⚖ hurt invulnerability window 0.5 s = 10 ticks (research/11 §1.5.1): for
    // 10 ticks after a hit, damage no larger than that hit is ignored and a
    // larger one settles only the difference. T-D46 wired it up (it was a
    // reserved field) and made it an INTEGER tick count - it used to be a
    // `double` of ticks, i.e. the same quantity expressed in a unit the 20 TPS
    // fixed step would drift against.
    //
    // ★ This is damage_mob's rule, copied rather than shared - see
    // game/server/sim/mob_sim.hpp (damage_mob, and MobAi::hurt_cooldown /
    // last_hurt_amount for the mobs' copy of this state). The two exist
    // separately because the mobs are the authority's and the player is the
    // client's (T-A1; T-D46 ruling C-1 keeps it that way until M3), NOT because
    // they may drift: a change to one belongs in the other.
    int invulnerability_ticks = 0;
    // The hit that opened the window above. The pair is what makes "a larger hit
    // settles only the difference" decidable; it is the mobs' last_hurt_amount
    // by another name.
    double last_hurt_amount = 0.0;

    // ── T-D1: explicit sprint state (interface contract: sprint must be
    // assertable in headless replay tests, not buried in the client input
    // layer). hunger defaults to full (20); the full hunger model lands in
    // M2 — for now tests inject values to verify the sprint gate.
    bool sprinting = false;
    // Ticks remaining in the double-tap-forward window (MC arms 7, decrements
    // each tick, a second forward press inside the window engages sprint).
    int sprint_toggle_timer = 0;
    double hunger = 20.0;
    // Set when the move this tick clamped against a wall (X or Z); MC stops
    // sprinting on horizontal collision (isCollidedHorizontally).
    bool collided_horizontally = false;

    // Echo of the last applied InputState::sequence (network prediction
    // groundwork; pure pass-through today).
    std::uint32_t last_input_sequence = 0;

    [[nodiscard]] double height() const { return pose == Pose::Sneaking ? kSneakingHeight : kStandingHeight; }
};

} // namespace opencraft::physics
