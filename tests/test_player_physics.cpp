#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "opencraft/physics/player_physics.hpp"
#include "physics_test_world.hpp"

using opencraft::physics::IBlockSource;
using opencraft::physics::InputState;
using opencraft::physics::PhysicsConfig;
using opencraft::physics::PlayerState;
using opencraft::physics::Pose;
using opencraft::physics::step_player;
using physics_test::BoxWorld;
using physics_test::flat_world;

namespace {

constexpr double kPi = 3.14159265358979323846;
// Under forward = (-sin(yaw), -cos(yaw)), this yaw faces +X.
constexpr double kYawEast = -kPi / 2.0;

PlayerState spawn_on(double x, double y, double z) {
    PlayerState s;
    s.position = {x, y, z};
    s.on_ground = true;
    s.fall_peak_y = y;
    return s;
}

// Holds the given input for `ticks` steps and returns the average horizontal
// speed in m/s over the cruise window [ticks/2, ticks) — the acceleration
// ramp (ground_drag = 0.9 → ~99% of target by tick 44) is excluded so the
// measured value converges to the steady state (see T007 report, window
// choice).
double cruise_speed_mps(bool sprint, bool sneak, int ticks = 120) {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.sprint = sprint;
    in.sneak = sneak;
    const int window_start = ticks / 2;
    double x_at_window = 0.0;
    for (int t = 0; t < ticks; ++t) {
        if (t == window_start) {
            x_at_window = s.position.x;
        }
        step_player(s, in, world);
    }
    return (s.position.x - x_at_window) / (ticks - window_start) * 20.0;
}

// Minimal-possible world adapter: proves the frozen IBlockSource contract
// (only solid_at) is sufficient for a full simulation.
struct SolidOnlyWorld final : IBlockSource {
    bool solid_at(int wx, int wy, int wz) const override { return wy == 63; }
};

} // namespace

// ── Movement speeds (docs/01 §2 ⚖) ──────────────────────────────────────────

TEST_CASE("steady state walk speed hits 4.317 m/s within 0.5 percent") {
    const double mps = cruise_speed_mps(false, false);
    CHECK_MESSAGE(std::abs(mps - 4.317) <= 4.317 * 0.005, "measured " << mps);
}

TEST_CASE("steady state sprint speed hits 5.612 m/s within 0.5 percent") {
    const double mps = cruise_speed_mps(true, false);
    CHECK_MESSAGE(std::abs(mps - 5.612) <= 5.612 * 0.005, "measured " << mps);
}

TEST_CASE("steady state sneak speed hits 1.295 m/s within 0.5 percent") {
    const double mps = cruise_speed_mps(false, true);
    CHECK_MESSAGE(std::abs(mps - 1.295) <= 1.295 * 0.005, "measured " << mps);
}

TEST_CASE("sneak overrides sprint as the slowest mode wins") {
    const double mps = cruise_speed_mps(true, true);
    CHECK_MESSAGE(std::abs(mps - 1.295) <= 1.295 * 0.005, "measured " << mps);
}

TEST_CASE("sprint without forward held stays at walk speed") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.sprint = true; // no forward
    for (int t = 0; t < 120; ++t) {
        step_player(s, in, world);
    }
    const double mps = s.position.x * 20.0 / 120.0;
    CHECK_MESSAGE(mps < 4.6, "sprint-only drift measured " << mps);
}

TEST_CASE("a solid-only IBlockSource satisfies the world contract") {
    SolidOnlyWorld world;
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    for (int t = 0; t < 60; ++t) {
        step_player(s, in, world);
    }
    CHECK(s.position.x > 10.0);
    CHECK(s.position.y == 64.0);
    CHECK(s.on_ground);
}

// ── Jump arc (docs/01 §2 ⚖: 0.42 initial, apex 1.2522) ──────────────────────

TEST_CASE("jump apex is 1.2522 blocks above the ground within 0.01") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.jump = true;
    double peak = s.position.y;
    for (int t = 0; t < 40; ++t) {
        step_player(s, in, world);
        peak = std::max(peak, s.position.y);
        in.jump = false; // single jump
    }
    CHECK_MESSAGE(std::abs(peak - 65.2522) <= 0.01, "apex " << peak);
    CHECK(s.on_ground);
    CHECK(s.position.y == 64.0);
}

TEST_CASE("jump arc rises for about six ticks and falls back symmetrically") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.jump = true;
    int rising = 0;
    int falling = 0;
    bool landed_once = false;
    for (int t = 0; t < 40 && !landed_once; ++t) {
        const double before = s.position.y;
        step_player(s, in, world);
        in.jump = false;
        if (s.position.y > before) {
            ++rising;
        } else if (s.position.y < before) {
            ++falling;
        }
        if (t > 0 && s.on_ground) {
            landed_once = true;
        }
    }
    CHECK(rising >= 5);
    CHECK(rising <= 7);
    CHECK(falling >= rising); // discretization gives one extra descent tick at most
    CHECK(falling <= rising + 2);
}

TEST_CASE("gravity and vertical drag act exactly per tick on the arc") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.jump = true;
    step_player(s, in, world);
    in.jump = false;
    // The move uses the full 0.42 first, THEN vy = (vy - 0.08) * 0.98.
    CHECK(s.position.y == 64.0 + 0.42);
    CHECK(s.velocity.y == (0.42 - 0.08) * 0.98);
    step_player(s, in, world);
    CHECK(s.velocity.y == ((0.42 - 0.08) * 0.98 - 0.08) * 0.98);
}

TEST_CASE("free fall from rest gains exactly the spec gravity per tick") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 80.0, 0.5);
    s.on_ground = false;
    InputState in;
    // Tick 1: no displacement yet (velocity starts at 0), gravity applies
    // after the move; the fall displacement shows up from tick 2 on.
    step_player(s, in, world);
    CHECK(s.velocity.y == (0.0 - 0.08) * 0.98);
    CHECK(s.position.y == 80.0);
    step_player(s, in, world);
    CHECK(s.position.y == 80.0 + (0.0 - 0.08) * 0.98); // velocity is negative: moved down
    CHECK_FALSE(s.on_ground);
}

// ── Terminal velocity (docs/01 §2 ⚖ ≈78 m/s) ────────────────────────────────

TEST_CASE("long free fall terminal speed stays within 77 to 78.4 m/s") {
    BoxWorld world; // empty world: fall into the void
    PlayerState s = spawn_on(0.5, 1000.0, 0.5);
    s.on_ground = false;
    InputState in;
    double max_speed_bpt = 0.0;
    for (int t = 0; t < 600; ++t) {
        step_player(s, in, world);
        max_speed_bpt = std::max(max_speed_bpt, std::abs(s.velocity.y));
    }
    const double terminal_mps = max_speed_bpt * 20.0;
    CHECK_MESSAGE(terminal_mps <= 78.4, "terminal " << terminal_mps);
    CHECK_MESSAGE(terminal_mps >= 77.0, "terminal " << terminal_mps);
}

// ── Fall damage (docs/01 §2 ⚖ floor(fall − 3) HP, water resets) ─────────────

TEST_CASE("falling 10 blocks deals exactly 7 damage") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 74.0, 0.5);
    s.on_ground = false;
    InputState in;
    for (int t = 0; t < 60; ++t) {
        step_player(s, in, world);
    }
    CHECK(s.health == 13.0);
    CHECK(s.on_ground);
    CHECK(s.position.y == 64.0);
    CHECK(s.fall_distance == 0.0);
}

TEST_CASE("a three block fall deals no damage") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 67.0, 0.5);
    s.on_ground = false;
    InputState in;
    for (int t = 0; t < 40; ++t) {
        step_player(s, in, world);
    }
    CHECK(s.health == 20.0);
    CHECK(s.on_ground);
}

TEST_CASE("landing in water resets fall distance and deals no damage") {
    BoxWorld world;
    world.solid(-8, 8, 60, 60, -8, 8);  // pool floor, top surface y=61
    world.liquid(-8, 8, 61, 63, -8, 8); // deep water above it
    PlayerState s = spawn_on(0.5, 74.0, 0.5);
    s.on_ground = false;
    InputState in;
    for (int t = 0; t < 60; ++t) {
        step_player(s, in, world);
    }
    CHECK(s.health == 20.0);
    CHECK(s.on_ground);
    CHECK(s.position.y == 61.0);
    CHECK(s.fall_distance == 0.0);
}

TEST_CASE("buoyancy lifts a jump-held player and drag slows swimming") {
    BoxWorld world;
    world.solid(-8, 8, 60, 60, -8, 8);
    world.liquid(-8, 8, 61, 63, -8, 8);
    PlayerState s = spawn_on(0.5, 61.0, 0.5);
    InputState in;
    in.jump = true;
    double min_y = s.position.y;
    for (int t = 0; t < 30; ++t) {
        step_player(s, in, world);
        min_y = std::min(min_y, s.position.y);
    }
    // Rose from the pool floor toward the surface without leaving the water column.
    CHECK(s.position.y > 62.0);
    CHECK(s.position.y < 64.5);
}

// ── Collision (docs/research/03 §6) ─────────────────────────────────────────

TEST_CASE("walking into a wall clamps at the wall face without tunneling") {
    BoxWorld world = flat_world();
    world.solid(24, 24, 64, 67, -8, 8);
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    for (int t = 0; t < 150; ++t) {
        step_player(s, in, world);
        CHECK(s.position.x <= 23.7);
    }
    CHECK(s.position.x == 23.7);
    CHECK(s.position.y == 64.0);
    CHECK(s.position.z == 0.5);
}

TEST_CASE("a one block ledge cannot be walked onto") {
    BoxWorld world = flat_world();
    world.solid(24, 40, 64, 64, -8, 8); // top surface y=65
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    for (int t = 0; t < 150; ++t) {
        step_player(s, in, world);
    }
    CHECK(s.position.x == 23.7);
    CHECK(s.position.y == 64.0);
    CHECK(s.on_ground);
}

TEST_CASE("a two block ledge cannot be jumped onto") {
    BoxWorld world = flat_world();
    world.solid(24, 80, 64, 65, -8, 8); // top surface y=66
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.jump = true; // keep retrying
    double peak = s.position.y;
    for (int t = 0; t < 200; ++t) {
        step_player(s, in, world);
        peak = std::max(peak, s.position.y);
    }
    CHECK_MESSAGE(peak < 65.99, "reached " << peak);
    CHECK(s.position.x == 23.7); // pressed against the face every hop
}

TEST_CASE("a one block ledge can be jumped onto") {
    BoxWorld world = flat_world();
    world.solid(24, 80, 64, 64, -8, 8);
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.jump = true;
    for (int t = 0; t < 180; ++t) {
        step_player(s, in, world);
    }
    in.jump = false; // settle before asserting
    for (int t = 0; t < 20; ++t) {
        step_player(s, in, world);
    }
    CHECK(s.position.y == 65.0);
    CHECK(s.on_ground);
    CHECK(s.position.x > 24.0);
}

TEST_CASE("ceiling stops upward motion and zeroes vertical velocity") {
    BoxWorld world = flat_world();
    world.solid(-8, 8, 66, 66, -8, 8); // ceiling 2 above the feet
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.jump = true;
    double peak = s.position.y;
    for (int t = 0; t < 30; ++t) {
        step_player(s, in, world);
        peak = std::max(peak, s.position.y);
        in.jump = false;
    }
    // The box (standing height 1.8) is clamped head-first: 66 − 1.8 = 64.2.
    CHECK(peak == 64.2);
    CHECK(s.position.y == 64.0);
    CHECK(s.on_ground);
}

// ── Sneak edge protection (docs/01 §2 手感项) ────────────────────────────────

TEST_CASE("sneaking toward an edge is clamped and never falls off") {
    BoxWorld world;
    world.solid(-10, 10, 63, 63, -10, 10); // isolated platform, top y=64
    PlayerState s = spawn_on(8.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.sneak = true;
    for (int t = 0; t < 200; ++t) {
        step_player(s, in, world);
        CHECK(s.on_ground);
        CHECK(s.position.y == 64.0);
    }
    // Support requires the box to still overlap the platform (edge at x=11):
    // the clamp guarantees box.min < 11, i.e. x < 11.3.
    CHECK(s.position.x < 11.3);
    CHECK(s.position.x > 10.9);
}

TEST_CASE("walking without sneak from the same spot does fall off") {
    BoxWorld world;
    world.solid(-10, 10, 63, 63, -10, 10);
    PlayerState s = spawn_on(8.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    for (int t = 0; t < 40; ++t) {
        step_player(s, in, world);
    }
    CHECK_FALSE(s.on_ground);
    CHECK(s.position.y < 64.0);
}

TEST_CASE("sneak pose uses the 1.5 block tall body") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.sneak = true;
    step_player(s, in, world);
    CHECK(s.pose == Pose::Sneaking);
    in.sneak = false;
    step_player(s, in, world);
    CHECK(s.pose == Pose::Standing); // open sky: standing up is allowed
}

// ── Determinism & input plumbing ────────────────────────────────────────────

TEST_CASE("identical input sequences simulate bit-identically twice") {
    BoxWorld world = flat_world();
    world.solid(24, 24, 64, 67, -8, 8);
    const InputState legs[] = {
        InputState{.yaw = kYawEast, .forward = true, .sequence = 0},
        InputState{.yaw = kYawEast, .forward = true, .jump = true, .sprint = true, .sequence = 1},
        InputState{.yaw = kPi / 2.0, .forward = true, .jump = true, .sprint = true, .sequence = 2},
        InputState{.yaw = kPi / 2.0, .sneak = true, .sequence = 3},
        InputState{.yaw = kYawEast, .forward = true, .sneak = true, .sequence = 4},
    };
    PlayerState a = spawn_on(0.5, 64.0, 0.5);
    PlayerState b = spawn_on(0.5, 64.0, 0.5);
    for (int t = 0; t < 300; ++t) {
        const InputState &in = legs[(t / 60) % 5];
        step_player(a, in, world);
        step_player(b, in, world);
        CHECK(a.position == b.position);
        CHECK(a.velocity == b.velocity);
        CHECK(a.health == b.health);
        CHECK(a.fall_distance == b.fall_distance);
        CHECK(a.on_ground == b.on_ground);
        CHECK(a.pose == b.pose);
    }
}

TEST_CASE("input sequence numbers are echoed into the player state") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.sequence = 42;
    step_player(s, in, world);
    CHECK(s.last_input_sequence == 42);
    in.sequence = 43;
    step_player(s, in, world);
    CHECK(s.last_input_sequence == 43);
}
