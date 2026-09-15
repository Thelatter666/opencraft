// T-D14 Auto-Jump unit tests: the pure decision function `should_auto_jump`
// plus an end-to-end physics integration (does the injected jump actually land
// the player on a 1-block ledge?) and the regression guards the task card's
// acceptance criteria name explicitly (jump apex 1.2522 unchanged; step_height
// 0.6/1.0 unchanged).
#include <doctest/doctest.h>

#include <cmath>

#include "opencraft/physics/auto_jump.hpp"
#include "opencraft/physics/player_physics.hpp"
#include "physics_test_world.hpp"

using opencraft::physics::AutoJumpConfig;
using opencraft::physics::EntityKind;
using opencraft::physics::InputState;
using opencraft::physics::PhysicsConfig;
using opencraft::physics::PlayerState;
using opencraft::physics::should_auto_jump;
using opencraft::physics::step_player;
using physics_test::BoxWorld;

namespace {

// forward = (−sin yaw, −cos yaw): yaw = 0 faces −Z, yaw = π faces +Z, yaw =
// −π/2 faces +X (the golden-replay conventions).
constexpr double kPi = 3.14159265358979323846;
constexpr double kYawSouth = kPi; // +Z

// Ground whose top surface is y = 64.
constexpr double kGround = 64.0;

PlayerState standing_at(double x, double z, double y = kGround) {
    PlayerState s;
    s.position = {x, y, z};
    s.on_ground = true;
    s.fall_peak_y = y;
    return s;
}

InputState forward_input() {
    InputState in;
    in.yaw = kYawSouth;
    in.forward = true;
    return in;
}

// Flat world: one solid layer at y = 63 across the whole lane (±64 so a side-
// walking test can never fall off the edge and confuse the airborne gate).
BoxWorld flat() {
    BoxWorld w;
    w.solid(-64, 64, 63, 63, -64, 64);
    return w;
}

// World with a full-block wall: columns [z0, z1] solid at layers 64..(63+h),
// i.e. top face at y = 64 + h − 1 for a wall of height h blocks placed on the
// ground (h = 1 → one block, top face 65).
BoxWorld wall_world(int z0, int z1, int height) {
    BoxWorld w = flat();
    w.solid(-64, 64, 64, 63 + height, z0, z1);
    return w;
}

} // namespace

TEST_SUITE("auto_jump") {
    TEST_CASE("should_auto_jump true at a 1-block wall ahead") {
        auto world = wall_world(11, 12, 1);
        PlayerState s = standing_at(8.5, 9.8); // body face z = 10.1, wall face z = 11 → within the 1.0 scan
        CHECK(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("should_auto_jump false when disabled") {
        auto world = wall_world(11, 12, 1);
        PlayerState s = standing_at(8.5, 9.8);
        AutoJumpConfig off;
        off.enabled = false;
        CHECK_FALSE(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, off));
    }

    TEST_CASE("should_auto_jump false when airborne") {
        auto world = wall_world(11, 12, 1);
        PlayerState s = standing_at(8.5, 9.8);
        s.on_ground = false;
        CHECK_FALSE(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("should_auto_jump false when sneaking") {
        auto world = wall_world(11, 12, 1);
        PlayerState s = standing_at(8.5, 9.8);
        InputState in = forward_input();
        in.sneak = true;
        CHECK_FALSE(should_auto_jump(s, in, world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("should_auto_jump false when in water") {
        auto world = wall_world(11, 12, 1);
        world.liquid(0, 16, 64, 70, 0, 16); // flood the lane ahead of the feet
        PlayerState s = standing_at(8.5, 9.8);
        CHECK_FALSE(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("should_auto_jump false on flat ground (nothing to jump)") {
        auto world = flat();
        PlayerState s = standing_at(8.5, 9.8);
        CHECK_FALSE(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("should_auto_jump false without forward input") {
        auto world = wall_world(11, 12, 1);
        PlayerState s = standing_at(8.5, 9.8);
        InputState still; // no forward
        CHECK_FALSE(should_auto_jump(s, still, world, PhysicsConfig{}, AutoJumpConfig{}));
        InputState back;
        back.yaw = kYawSouth;
        back.backward = true;
        CHECK_FALSE(should_auto_jump(s, back, world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("wall too tall (2 blocks) does not trigger") {
        auto world = wall_world(11, 12, 2);
        PlayerState s = standing_at(8.5, 9.8);
        CHECK_FALSE(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("far obstacle outside the scan range does not trigger") {
        auto world = wall_world(11, 12, 1);
        // Body at z = 8.5 (max_z = 8.8) faces the wall but the 1.0 scan
        // (z ≤ 9.8) does not reach it: the player must get closer before the
        // decision flips. This is the scan-distance bound, not the window.
        PlayerState s = standing_at(8.5, 8.5);
        CHECK_FALSE(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("obstacle at exactly the jump ceiling (1.25 b) triggers") {
        auto world = wall_world(11, 12, 1);
        world.partial(0, 16, 65, 65, 11, 12, 0.25); // adds a 0.25 cap: top face 65.25 → ΔH = 1.25
        PlayerState s = standing_at(8.5, 9.8);
        CHECK(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("sub-step obstacle (0.5 b) does not trigger (step-assist's job)") {
        auto world = flat();
        world.partial(0, 16, 64, 64, 11, 12, 0.5); // top 64.5 → ΔH = 0.5
        PlayerState s = standing_at(8.5, 9.8);
        CHECK_FALSE(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("low ceiling above the obstacle (no headroom) does not trigger") {
        auto world = wall_world(11, 12, 1);
        world.solid(-16, 16, 65, 66, 11, 12); // hang-down: the very column to be mounted is capped
        PlayerState s = standing_at(8.5, 9.8);
        CHECK_FALSE(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("grazing along a wall does not trigger") {
        // research/08 §2.3, the "most easily missed" case: a long wall running
        // east–west (z ∈ [11, 12], full block) with the player pressed against
        // its near face and walking ALONG it (+X, yaw = −π/2). The move is
        // parallel to the wall face, so it must not auto-jump — the classic
        // failure mode is jumping while side-surfing a corridor.
        BoxWorld w = flat();
        w.solid(-64, 64, 64, 64, 11, 12); // two-deep wall: behind-near columns
                                          // never open a clean mount face
        InputState in;
        in.yaw = -kPi / 2.0; // +X
        in.forward = true;
        PlayerState flush = standing_at(8.5, 10.7); // body max_z = 11.0, flush to the wall
        CHECK_FALSE(should_auto_jump(flush, in, w, PhysicsConfig{}, AutoJumpConfig{}));
        // And from a hair off the face (0.05 b), inside the scan range: still
        // parallel, still no jump.
        PlayerState near = standing_at(8.5, 10.65);
        CHECK_FALSE(should_auto_jump(near, in, w, PhysicsConfig{}, AutoJumpConfig{}));
        // Control: the same flush position walking HEAD-ON into the wall
        // (dir +Z) does trigger — the suppression is the alignment, not the
        // proximity.
        CHECK(should_auto_jump(standing_at(8.5, 10.7), forward_input(), w, PhysicsConfig{}, AutoJumpConfig{}));
    }

    TEST_CASE("step-assist height mob does not auto-jump at 1.0") {
        // A Mob config has step_height = 1.0, so a 1.0-block obstacle is AT its
        // step ceiling (≤ step_height): no jump needed.
        auto world = wall_world(11, 12, 1);
        PhysicsConfig mob = PhysicsConfig::for_entity(EntityKind::Mob);
        PlayerState s = standing_at(8.5, 9.8); // ΔH = 1.0 == mob.step_height
        CHECK_FALSE(should_auto_jump(s, forward_input(), world, mob, AutoJumpConfig{}));
        // Sanity: the same world DOES auto-jump for the player config (0.6),
        // proving the gate reads cfg.step_height and not a hidden constant.
        CHECK(should_auto_jump(s, forward_input(), world, PhysicsConfig{}, AutoJumpConfig{}));
    }
}

TEST_SUITE("auto_jump_integration") {
    TEST_CASE("walking into a 1-block ledge with auto-jump wiring lands on top") {
        // End-to-end: the client's wiring (flip in.jump when
        // should_auto_jump is true) must carry the player over a 1-block wall
        // and end grounded on top of it, WITHOUT the user pressing space.
        auto world = wall_world(11, 16, 1); // wall from z = 11, top face y = 65
        PlayerState s = standing_at(8.5, 6.5);
        InputState in = forward_input();
        PhysicsConfig cfg;
        for (int t = 0; t < 200; ++t) {
            in.jump = should_auto_jump(s, in, world, cfg, AutoJumpConfig{});
            step_player(s, in, world, cfg);
            if (s.on_ground && s.position.y > kGround + 0.5) {
                break;
            }
        }
        CHECK(s.on_ground);
        // Feet land exactly on the wall's top face (y = 65). Exact equality is
        // the right assertion here (an `Approx` epsilon makes a near-miss read
        // as a hit); the landing is clamped to the face, not integrated past it.
        CHECK(s.position.y == doctest::Approx(65.0)); // standing on the wall
        CHECK(s.position.z > 11.0);                   // actually crossed the face
    }

    TEST_CASE("without the wiring the same walk is stopped by the wall") {
        auto world = wall_world(11, 16, 1);
        PlayerState s = standing_at(8.5, 6.5);
        InputState in = forward_input();
        in.jump = false; // the difference: no auto-jump injection
        PhysicsConfig cfg;
        for (int t = 0; t < 200; ++t) {
            step_player(s, in, world, cfg);
        }
        CHECK(s.on_ground);
        CHECK(s.position.y < 64.5); // still on the ground, never climbed
        CHECK(s.position.z < 11.0); // stopped at the wall face
    }
}

TEST_SUITE("auto_jump_regression_guards") {
    // The card freezes these: Auto-Jump must not perturb the physics model it
    // borrows. These are the same invariants the golden replay already locks,
    // restated locally so this file is self-evidently guard-bearing.
    TEST_CASE("jump apex unchanged at 1.2522 blocks") {
        auto world = flat();
        PlayerState s = standing_at(8.5, 9.8);
        InputState in;
        in.jump = true;
        double peak = s.position.y;
        for (int t = 0; t < 40; ++t) {
            step_player(s, in, world, PhysicsConfig{});
            peak = std::max(peak, s.position.y);
            in.jump = false; // single hop
        }
        // The apex is an exact golden value: `== Approx` asserts it within the
        // epsilon; a strict `>` would fail on exact equality (65.0 > 65.0 is
        // false), which is what the first run of this file caught.
        CHECK(peak - kGround == doctest::Approx(1.2522).epsilon(0.0001));
    }

    TEST_CASE("step_height frozen: player 0.6, mob 1.0") {
        CHECK(PhysicsConfig::for_entity(EntityKind::Player).step_height == doctest::Approx(0.6));
        CHECK(PhysicsConfig::for_entity(EntityKind::Mob).step_height == doctest::Approx(1.0));
    }
}
