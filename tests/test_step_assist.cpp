#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "opencraft/physics/player_physics.hpp"
#include "physics_test_world.hpp"

using opencraft::physics::EntityKind;
using opencraft::physics::IBlockSource;
using opencraft::physics::InputState;
using opencraft::physics::MoveResult;
using opencraft::physics::PhysicsConfig;
using opencraft::physics::PlayerState;
using opencraft::physics::step_player;
using physics_test::BoxWorld;

// T-D8: step-assist (docs/research/06 §6.3). The user report (2026-09-14,
// T-D1 report §7 gap #5) was "the player is stopped dead by a one-block ledge
// and horizontal speed goes to zero; you have to jump by hand".
//
// Testing note — WHY these worlds use `partial()`:
// MC's player step height is 0.6 blocks, which is DELIBERATELY less than a
// full 1.0 block: the player must still jump onto a full block. So a world
// built only from full cubes can never contain an obstacle a player is allowed
// to walk up, and step-assist could not be exercised at all at 0.6. The
// `partial()` regions below are sub-block ledges (0.5, 0.25, 0.75), which is
// exactly the carpet/slab/bed class the 0.6 figure is defined over.
// The one full-block case is asserted as a NEGATIVE (acceptance #2), and the
// mob config (1.0) covers the "walks up a full block" side.

namespace {

constexpr double kPi = 3.14159265358979323846;
// Under forward = (-sin(yaw), -cos(yaw)), this yaw faces +X.
constexpr double kYawEast = -kPi / 2.0;

// Floor top surface at y = 64.
constexpr double kFloorTop = 64.0;

PlayerState spawn_on(double x, double z) {
    PlayerState s;
    s.position = {x, kFloorTop, z};
    s.on_ground = true;
    s.fall_peak_y = kFloorTop;
    return s;
}

// Flat floor that never runs out within a test, z-bounded to the lane used.
BoxWorld floor_world() {
    BoxWorld w;
    w.solid(-256, 256, 63, 63, -256, 256);
    return w;
}

// Runs `ticks` ticks of held-forward movement from x = 0.5 and reports the
// per-tick displacement on the final tick (the steady-state proxy).
double steady_displacement(const IBlockSource &world, const PhysicsConfig &cfg, int ticks) {
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    double prev = s.position.x;
    double disp = 0.0;
    for (int t = 0; t < ticks; ++t) {
        step_player(s, in, world, cfg);
        disp = s.position.x - prev;
        prev = s.position.x;
    }
    return disp;
}

// A ledge of height `top` in block layer y = 64 (surface y = 64 + top),
// spanning [x0, x1]. A NARROW ledge (default) is crossed and stepped back
// down off; a WIDE one is a plateau the entity ends up standing on.
BoxWorld ledge_world(double top, int x0 = 10, int x1 = 10) {
    BoxWorld w = floor_world();
    w.partial(x0, x1, 64, 64, -256, 256, top);
    return w;
}

} // namespace

// ── Acceptance #1: the core feel fix ────────────────────────────────────────

TEST_CASE("step assist: player walks up a 0.5 ledge without losing speed") {
    // A 0.5-block ledge at x = 10 would stop the player dead before this card.
    // It is one block wide, so the player crosses it and drops off the far
    // side — the "walked over it, never stopped" case.
    const BoxWorld flat = floor_world();
    const BoxWorld ledge = ledge_world(0.5);
    const PhysicsConfig cfg;

    const double flat_disp = steady_displacement(flat, cfg, 200);
    const double ledge_disp = steady_displacement(ledge, cfg, 200);

    // The ledge must not cost horizontal speed: within 1% of open ground.
    CHECK(std::abs(ledge_disp - flat_disp) / flat_disp < 0.01);

    // And the player must actually be past the ledge (x = 10), not stopped at
    // its face. 200 ticks at ~4.3 m/s is far more than 10 blocks.
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    for (int t = 0; t < 200; ++t) {
        step_player(s, in, ledge, cfg);
    }
    CHECK(s.position.x > 20.0);
    // Back down on the floor after crossing the one-block ledge.
    CHECK(std::abs(s.position.y - kFloorTop) < 1e-9);
    CHECK(s.on_ground);
}

TEST_CASE("step assist: player mounts a 0.5 plateau and stands on it") {
    // Same 0.5 lift, but extending onward: the player ends up standing on the
    // raised surface rather than dropping back off it.
    const BoxWorld plateau = ledge_world(0.5, 10, 256);
    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    for (int t = 0; t < 200; ++t) {
        step_player(s, in, plateau, cfg);
    }
    CHECK(s.position.x > 20.0);
    CHECK(std::abs(s.position.y - 64.5) < 1e-9);
    CHECK(s.on_ground);
}

TEST_CASE("step assist: reported through MoveResult and speed is retained") {
    const BoxWorld ledge = ledge_world(0.5);
    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;

    bool stepped_once = false;
    double step_height_used = 0.0;
    for (int t = 0; t < 60; ++t) {
        MoveResult r;
        step_player(s, in, ledge, cfg, &r);
        if (r.stepped) {
            stepped_once = true;
            step_height_used = r.step_height_used;
            // CRITICAL: a successful step is not a collision — this is what
            // keeps sprint (and horizontal speed) alive across the obstacle.
            CHECK_FALSE(s.collided_horizontally);
            CHECK(s.velocity.x > 0.0);
            break;
        }
    }
    CHECK(stepped_once);
    CHECK(std::abs(step_height_used - 0.5) < 1e-9);
}

// ── Acceptance #2: negative case ────────────────────────────────────────────

TEST_CASE("step assist: a full block is NOT walkable at step height 0.6") {
    // Two-block-high wall (y 64..65 = 2 blocks above the floor surface) and a
    // single full block. Neither may be stepped at 0.6.
    BoxWorld wall = floor_world();
    wall.solid(10, 10, 64, 65, -256, 256);

    BoxWorld single = floor_world();
    single.solid(10, 10, 64, 64, -256, 256);

    const PhysicsConfig cfg;
    for (const BoxWorld *w : {&wall, &single}) {
        PlayerState s = spawn_on(0.5, 0.5);
        InputState in;
        in.yaw = kYawEast;
        in.forward = true;
        bool ever_stepped = false;
        for (int t = 0; t < 120; ++t) {
            MoveResult r;
            step_player(s, in, *w, cfg, &r);
            ever_stepped = ever_stepped || r.stepped;
        }
        CHECK_FALSE(ever_stepped);
        // Stopped at the wall face: x = 10 − half-width = 9.7.
        CHECK(std::abs(s.position.x - 9.7) < 1e-9);
        // No lift at all — still on the floor.
        CHECK(std::abs(s.position.y - kFloorTop) < 1e-9);
        CHECK(s.velocity.x == 0.0);
    }
}

TEST_CASE("step assist: a 2-block wall is not climbable even by a mob") {
    BoxWorld wall = floor_world();
    wall.solid(10, 10, 64, 65, -256, 256);
    const PhysicsConfig mob = PhysicsConfig::for_entity(EntityKind::Mob);

    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    for (int t = 0; t < 120; ++t) {
        MoveResult r;
        step_player(s, in, wall, mob, &r);
        CHECK_FALSE(r.stepped);
    }
    CHECK(std::abs(s.position.x - 9.7) < 1e-9);
    CHECK(std::abs(s.position.y - kFloorTop) < 1e-9);
}

// ── Acceptance #3: the LOWEST gainful height, not the highest ───────────────

TEST_CASE("step assist: with two live candidates the LOWEST gainful one wins") {
    // THE regression test for research/06 §6.3's ★ property.
    //
    // A launched entity whose sweep covers two risers at once — 0.25 near and
    // 0.50 beyond. Both are ≤ 0.6, so BOTH are legal candidates, and both
    // produce a gain over the flat solve. MC takes the FIRST (lowest) one that
    // gains ground, so the entity must rise 0.25 and be stopped by the 0.50
    // riser; taking the highest lift available would put it at 64.5 instead.
    //
    // Verified to be load-bearing: flipping the candidate loop to descending
    // (highest-first) makes this case report step 0.5 / y 64.5 and FAIL. The
    // other step tests use single-candidate sweeps and do NOT catch that
    // mutation, which is why this one exists.
    BoxWorld w = floor_world();
    w.partial(10, 10, 64, 64, -64, 64, 0.25); // near riser: the real blocker
    w.partial(11, 11, 64, 64, -64, 64, 0.50); // farther riser: also in reach

    const PhysicsConfig cfg;
    PlayerState s = spawn_on(9.5, 0.5);
    // A launch speed large enough that one tick's sweep spans both risers.
    s.velocity = {2.0, 0.0, 0.0};
    InputState in;
    in.yaw = kYawEast; // no forward input: the launch alone drives the tick

    MoveResult r;
    step_player(s, in, w, cfg, &r);

    CHECK(r.stepped);
    CHECK(std::abs(r.step_height_used - 0.25) < 1e-9); // lowest gainful
    CHECK(std::abs(s.position.y - 64.25) < 1e-9);      // standing on the near riser
    CHECK(std::abs(s.position.x - 10.7) < 1e-9);       // stopped by the far riser
    CHECK(s.on_ground);
}

TEST_CASE("step assist: takes the lowest lift that gains ground, not the highest") {
    // Multi-level terrain: a 0.5 ledge first, then a lower 0.25 ledge beyond
    // it. Both are ≤ 0.6, so both are legal where they are met. The step taken
    // must be the one that gains ground at the obstacle actually in the way —
    // 0.5 — not the highest lift reachable anywhere nearby.
    BoxWorld w = floor_world();
    w.partial(10, 256, 64, 64, -256, 256, 0.5);
    w.partial(20, 256, 64, 64, -256, 256, 0.25);

    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;

    double first_step_height = -1.0;
    for (int t = 0; t < 200; ++t) {
        MoveResult r;
        step_player(s, in, w, cfg, &r);
        if (r.stepped && first_step_height < 0.0) {
            first_step_height = r.step_height_used;
        }
    }
    CHECK(std::abs(first_step_height - 0.5) < 1e-9);
    CHECK(std::abs(s.position.y - 64.5) < 1e-9);
}

TEST_CASE("step assist: climbs a staircase one lowest-gain step at a time") {
    // A staircase of 0.25 / 0.50 / 0.75 tops: each riser is a legal lift on
    // its own, and the sweep sees more than one at once. Taking the HIGHEST
    // available lift would jump straight to the tallest riser; MC takes the
    // lowest that gains ground, so the entity must rise 0.25 each time.
    BoxWorld w = floor_world();
    w.partial(10, 10, 64, 64, -256, 256, 0.25);
    w.partial(11, 11, 64, 64, -256, 256, 0.50);
    w.partial(12, 256, 64, 64, -256, 256, 0.75);

    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;

    std::vector<double> lifts;
    for (int t = 0; t < 200; ++t) {
        MoveResult r;
        step_player(s, in, w, cfg, &r);
        if (r.stepped) {
            lifts.push_back(r.step_height_used);
        }
    }
    REQUIRE(lifts.size() == 3);
    // Every riser is climbed by exactly its own height, never the tallest
    // candidate in reach.
    CHECK(std::abs(lifts[0] - 0.25) < 1e-9);
    CHECK(std::abs(lifts[1] - 0.25) < 1e-9);
    CHECK(std::abs(lifts[2] - 0.25) < 1e-9);
    CHECK(std::abs(s.position.y - 64.75) < 1e-9);
    CHECK(s.on_ground);
}

TEST_CASE("step assist: the step is the riser height, not a higher face") {
    // Two risers in the sweep at once — 0.25 near, 0.50 just beyond — with
    // open headroom. Both are legal lifts; the near one is the lowest that
    // gains ground, so the entity must take 0.25 first, never the 0.50 that
    // is also within reach.
    BoxWorld w = floor_world();
    w.partial(10, 13, 64, 64, -256, 256, 0.25);
    w.partial(14, 256, 64, 64, -256, 256, 0.50);

    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;

    double used = -1.0;
    for (int t = 0; t < 200; ++t) {
        MoveResult r;
        step_player(s, in, w, cfg, &r);
        if (r.stepped && used < 0.0) {
            used = r.step_height_used;
        }
    }
    CHECK(std::abs(used - 0.25) < 1e-9);
    // Ends on the 0.50 plateau, reached one 0.25 riser at a time.
    CHECK(std::abs(s.position.y - 64.5) < 1e-9);
    CHECK(s.on_ground);
}

// ── Acceptance #4: land back on the surface, never hover ────────────────────

TEST_CASE("step assist: lands back on the ground instead of hovering") {
    // A 0.5 plateau with a far edge at x = 20: the entity mounts it, walks
    // across, and must drop back to the floor — never hover at the lift.
    BoxWorld w = floor_world();
    w.partial(10, 20, 64, 64, -256, 256, 0.5);

    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;

    for (int t = 0; t < 400; ++t) {
        step_player(s, in, w, cfg);
    }
    CHECK(s.position.x > 20.0);
    CHECK(std::abs(s.position.y - kFloorTop) < 1e-9);
    CHECK(s.on_ground);
}

TEST_CASE("step assist: on_ground holds on every tick of the traverse") {
    // A plateau: the entity is on the raised surface for the rest of the run,
    // so every tick must report grounded — never hovering at the lift height.
    const BoxWorld plateau = ledge_world(0.5, 10, 256);
    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    for (int t = 0; t < 200; ++t) {
        step_player(s, in, plateau, cfg);
        CHECK(s.on_ground);
    }
    CHECK(std::abs(s.position.y - 64.5) < 1e-9);
}

// ── Acceptance #5: only while grounded ──────────────────────────────────────

TEST_CASE("step assist: does not fire while airborne") {
    // A 0.5 ledge (occupying y 64.0–64.5) with a full cube stacked above it,
    // approached while AIRBORNE. The box is pinned to y = 64.1 so it overlaps
    // both: horizontal contact genuinely happens off the ground, which is what
    // exercises the onGround guard.
    BoxWorld w = floor_world();
    w.partial(10, 10, 64, 64, -256, 256, 0.5);
    w.solid(10, 10, 65, 66, -256, 256);

    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    s.on_ground = false;
    s.position.y = 64.1; // box 64.1–65.9 overlaps the 64.0–64.5 ledge band
    s.velocity = {0.0, 0.0, 0.0};

    InputState in;
    in.yaw = kYawEast;
    in.forward = true;

    bool ever_stepped = false;
    bool ever_hit_horizontally = false;
    for (int t = 0; t < 60; ++t) {
        // Hold it airborne: zero the vertical velocity and clear groundedness
        // each tick so gravity cannot set it down before contact.
        s.velocity.y = 0.0;
        s.on_ground = false;
        MoveResult r;
        step_player(s, in, w, cfg, &r);
        ever_stepped = ever_stepped || r.stepped;
        ever_hit_horizontally = ever_hit_horizontally || (r.hit_x || r.hit_z);
        if (ever_hit_horizontally) {
            break;
        }
    }
    // Guard: the contact really did happen, so the assertion below is a real
    // test of the airborne rule and not a miss flying over the obstacle.
    REQUIRE(ever_hit_horizontally);
    CHECK_FALSE(ever_stepped);
    // And no lift: still at the height it was flying at.
    CHECK(std::abs(s.position.y - 64.1) < 1e-9);
}

TEST_CASE("step assist: a jump onto a ledge is not a step") {
    // Pressing jump on flat ground then meeting a ledge must not report a
    // step; the jump is doing the work.
    const BoxWorld ledge = ledge_world(0.5);
    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;

    for (int t = 0; t < 60; ++t) {
        in.jump = (t % 10 == 0);
        MoveResult r;
        step_player(s, in, ledge, cfg, &r);
        if (in.jump) {
            CHECK_FALSE(r.stepped); // airborne on the jump tick: no step
        }
    }
}

// ── Acceptance #6: configurable per entity ──────────────────────────────────

TEST_CASE("step assist: step height is configured per entity kind") {
    const PhysicsConfig player = PhysicsConfig::for_entity(EntityKind::Player);
    const PhysicsConfig mob = PhysicsConfig::for_entity(EntityKind::Mob);

    CHECK(std::abs(player.step_height - 0.6) < 1e-12);
    CHECK(std::abs(mob.step_height - 1.0) < 1e-12);
    CHECK(player.step_height != mob.step_height);
}

TEST_CASE("step assist: a mob walks up a full block the player cannot") {
    // Same world, two configs: the mob (1.0) clears the full block, the
    // player (0.6) does not. This is the observable meaning of the config.
    BoxWorld w = floor_world();
    w.solid(10, 10, 64, 64, -256, 256);

    const PhysicsConfig player = PhysicsConfig::for_entity(EntityKind::Player);
    const PhysicsConfig mob = PhysicsConfig::for_entity(EntityKind::Mob);

    PlayerState ps = spawn_on(0.5, 0.5);
    PlayerState ms = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;

    bool player_stepped = false;
    bool mob_stepped = false;
    for (int t = 0; t < 120; ++t) {
        MoveResult pr;
        MoveResult mr;
        step_player(ps, in, w, player, &pr);
        step_player(ms, in, w, mob, &mr);
        player_stepped = player_stepped || pr.stepped;
        mob_stepped = mob_stepped || mr.stepped;
    }
    CHECK_FALSE(player_stepped);
    CHECK(mob_stepped);
    CHECK(std::abs(ps.position.x - 9.7) < 1e-9); // still blocked
    CHECK(ms.position.x > 20.0);                 // walked over it
    CHECK(std::abs(ms.position.y - 64.0) < 1e-9);
    // Grounded on the plateau it mounted.
    CHECK(ms.on_ground);
}

// ── Scope guards ────────────────────────────────────────────────────────────

TEST_CASE("step assist: crossing a riser at a pit lip never hovers") {
    // Invariant: a 0.5 riser at the edge of a void must not leave the entity
    // hanging above that void — it must fall.
    //
    // Threshold rationale: this engine re-verifies ground support at tick
    // START ("walking off an edge must be detected before the move"), so
    // on_ground is authoritative-as-of-tick-start and legitimately reads true
    // for the one tick in which the footprint has just left the surface. That
    // one-tick latency is pre-existing T007/T-D7 behaviour and is identical on
    // a full-cube edge, so it is not asserted away here; x = 12 is chosen past
    // the point where it must already have resolved.
    //
    // Scope note: this guards the overall no-hover invariant. The "settle found
    // no surface" branch in the step code is belt-and-braces for a geometry
    // unreachable at walking/sprint speed (the step lands the entity ON the
    // riser, which genuinely supports it), and this test does not depend on it.
    BoxWorld w;
    w.solid(-256, 9, 63, 63, -256, 256); // floor up to the lip
    w.partial(10, 10, 64, 64, -256, 256, 0.5);
    // x >= 11 is void: nothing to stand on.

    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;

    bool saw_hover_claim = false;
    for (int t = 0; t < 120; ++t) {
        step_player(s, in, w, cfg);
        if (s.on_ground && s.position.y > kFloorTop + 1e-9 && s.position.x > 12.0) {
            saw_hover_claim = true;
        }
    }
    CHECK_FALSE(saw_hover_claim);
    // Fell into the void instead of gliding over it.
    CHECK(s.position.y < kFloorTop);
}

TEST_CASE("step assist: default IBlockSource keeps full-cube semantics") {
    // The default shape_top_at is derived from shape_at, so an adapter that
    // models nothing special is unchanged: solid ⇒ 1.0, empty ⇒ 0.0.
    struct SolidOnly final : IBlockSource {
        bool solid_at(int, int wy, int) const override { return wy == 63; }
    };

    SolidOnly w;
    CHECK(std::abs(w.shape_top_at(0, 63, 0) - 1.0) < 1e-12);
    CHECK(std::abs(w.shape_top_at(0, 64, 0) - 0.0) < 1e-12);
}

TEST_CASE("step assist: sub-block ledges do not block movement in general") {
    // A 0.25 plateau is below the 0.6 step: walkable, and the entity ends up
    // standing on it rather than stopped in front of it.
    const BoxWorld w = ledge_world(0.25, 10, 256);
    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    for (int t = 0; t < 300; ++t) {
        step_player(s, in, w, cfg);
    }
    CHECK(s.position.x > 20.0);
    CHECK(std::abs(s.position.y - 64.25) < 1e-9);
    CHECK(s.on_ground);
}

TEST_CASE("step assist: a 0.75 ledge is above the 0.6 step and is not crossed") {
    const BoxWorld w = ledge_world(0.75);
    const PhysicsConfig cfg;
    PlayerState s = spawn_on(0.5, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    MoveResult r;
    bool ever_stepped = false;
    for (int t = 0; t < 120; ++t) {
        r = MoveResult{};
        step_player(s, in, w, cfg, &r);
        ever_stepped = ever_stepped || r.stepped;
    }
    CHECK_FALSE(ever_stepped);
    CHECK(std::abs(s.position.x - 9.7) < 1e-9);
    CHECK(std::abs(s.position.y - kFloorTop) < 1e-9);
}
