#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "opencraft/physics/player_physics.hpp"
#include "physics_test_world.hpp"

using opencraft::physics::BlockShape;
using opencraft::physics::EntityKind;
using opencraft::physics::IBlockSource;
using opencraft::physics::InputState;
using opencraft::physics::MoveResult;
using opencraft::physics::PhysicsConfig;
using opencraft::physics::PlayerState;
using opencraft::physics::Pose;
using opencraft::physics::step_player;
using physics_test::BoxWorld;
using physics_test::flat_world;

// T-D7: MC friction pipeline migration. The pre-T-D7 suite was 163/163 green
// while the player coasted 1.94 blocks after key release (user report
// 2026-09-15), because the old model wrote
//     v = v·drag + dir·target·(1 − drag)
// whose steady state is exactly `target` — drag cancels out, so no
// steady-state assertion could ever see it. These tests therefore assert the
// TRANSIENT behaviour (coast distance, ramp time) and the friction factor
// itself, not just the cruise speeds.

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

// A world whose blocks all report `s` as their slipperiness — the ice hook
// (docs/research/06 §7.1) exercised without needing real ice content.
struct SlipperyWorld final : IBlockSource {
    double s = opencraft::physics::kDefaultSlipperiness;
    int floor_y = 63;

    bool solid_at(int, int wy, int) const override { return wy == floor_y; }

    double slipperiness_at(int, int, int) const override { return s; }
};

// Summary of one run-to-cruise-then-release measurement.
struct RunSummary {
    double ramp_ticks_to_95 = -1.0;
    double coast_blocks = 0.0;
    int coast_ticks = 0;
    double cruise_blocks_per_tick = 0.0;
};

// Ticks for the per-tick displacement to first reach `fraction` of the steady
// displacement, measured after the ramp has been identified separately.
double ramp_ticks_to_fraction(const IBlockSource &world, const PhysicsConfig &cfg, bool sprint, bool sneak,
                              double steady_disp, double fraction, int cruise_ticks) {
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.sprint = sprint;
    in.sneak = sneak;
    double prev = s.position.x;
    for (int t = 1; t <= cruise_ticks; ++t) {
        step_player(s, in, world, cfg);
        const double disp = s.position.x - prev;
        prev = s.position.x;
        if (disp >= fraction * steady_disp) {
            return static_cast<double>(t);
        }
    }
    return -1.0;
}

// Runs to cruise, records the steady per-tick displacement and the ramp time to
// 95% of it, then releases all direction keys and measures the glide.
RunSummary cruise_then_release(const IBlockSource &world, const PhysicsConfig &cfg, bool sprint, bool sneak,
                               int cruise_ticks = 200) {
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.sprint = sprint;
    in.sneak = sneak;

    RunSummary out;
    double prev = s.position.x;
    for (int t = 0; t < cruise_ticks; ++t) {
        step_player(s, in, world, cfg);
        out.cruise_blocks_per_tick = s.position.x - prev;
        prev = s.position.x;
    }
    out.ramp_ticks_to_95 =
        ramp_ticks_to_fraction(world, cfg, sprint, sneak, out.cruise_blocks_per_tick, 0.95, cruise_ticks);

    // Release every direction key and measure how far the entity glides.
    in.forward = false;
    in.sprint = false;
    in.sneak = false;
    const double x_release = s.position.x;
    for (int t = 0; t < 2000; ++t) {
        step_player(s, in, world, cfg);
        ++out.coast_ticks;
        if (s.velocity.x == 0.0) {
            break;
        }
    }
    out.coast_blocks = s.position.x - x_release;
    return out;
}

// The wiki's sprint-jump figure is an AVERAGE SPEED FOR THE MOVEMENT METHOD,
// so it is defined over a sustained chain: the player re-jumps on the tick it
// lands ("Running start effective? No", footnote "the jump key is released then
// held mid-air every jump"). Returns the centre-to-centre displacement of each
// complete arc; under this cadence every arc is exactly 12 moves (the jump tick
// plus 11 airborne ticks) — the window docs/01 §2 and the T-D1 ruling use.
std::vector<double> chain_arcs(const IBlockSource &world, const PhysicsConfig &cfg, bool sprint, int total_ticks) {
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.sprint = sprint;
    std::vector<double> arcs;
    double x0 = 0.0;
    bool airborne = false;
    for (int t = 0; t < total_ticks; ++t) {
        const bool grounded_at_start = s.on_ground;
        in.jump = grounded_at_start && t > 20;
        if (in.jump) {
            x0 = s.position.x;
        }
        step_player(s, in, world, cfg);
        if (grounded_at_start && !s.on_ground) {
            airborne = true;
        } else if (airborne && s.on_ground) {
            arcs.push_back(s.position.x - x0); // landed: arc complete
            airborne = false;
        }
    }
    return arcs;
}

double steady_arc(const std::vector<double> &arcs) {
    const std::size_t from = arcs.size() / 2;
    double sum = 0.0;
    for (std::size_t i = from; i < arcs.size(); ++i) {
        sum += arcs[i];
    }
    return sum / static_cast<double>(arcs.size() - from);
}

} // namespace

// ── 1. Transient behaviour: the defect this card exists to fix ──────────────

TEST_CASE("releasing walk input glides only about a quarter of a block") {
    // MC target 0.26 blocks (docs/research/06 §2.3); the pre-T-D7 pipeline
    // glided 1.94. The 0.35 ceiling is the card's tolerance.
    BoxWorld world = flat_world();
    const RunSummary r = cruise_then_release(world, PhysicsConfig{}, false, false);
    CHECK_MESSAGE(r.coast_blocks > 0.15, "coast too short: " << r.coast_blocks);
    CHECK_MESSAGE(r.coast_blocks <= 0.35, "coast too long (ice-skating): " << r.coast_blocks);
    CHECK_MESSAGE(std::abs(r.coast_blocks - 0.26) < 0.06, "coast " << r.coast_blocks << " should centre on 0.26");
}

TEST_CASE("walking reaches ninety five percent of cruise within a quarter second") {
    // research/06 §2.3: time constant 1.65 ticks => 5 ticks (0.25 s) to 95%.
    // The pre-T-D7 pipeline took 29 ticks (1.45 s).
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    // Steady per-tick displacement (docs/research/06 §2.3 closed form):
    // a / (1 − k) with a = 0.1 × 0.98 and k = 0.91 × 0.6.
    const double steady_disp = (0.1 * 0.98) / (1.0 - 0.91 * 0.6);
    double prev = s.position.x;
    int ticks = -1;
    for (int t = 1; t <= 100; ++t) {
        step_player(s, in, world);
        const double disp = s.position.x - prev;
        prev = s.position.x;
        if (disp >= 0.95 * steady_disp) {
            ticks = t;
            break;
        }
    }
    CHECK_MESSAGE(ticks > 0, "never reached 95 percent");
    CHECK_MESSAGE(ticks <= 6, "ramp took " << ticks << " ticks (" << ticks / 20.0 << " s)");
}

TEST_CASE("the momentum threshold truncates the glide tail") {
    // Without the cut-off the tail is an infinite geometric series; with it the
    // entity stops in a bounded number of ticks. The pre-T-D7 pipeline never
    // zeroed the velocity at all.
    BoxWorld world = flat_world();
    const RunSummary r = cruise_then_release(world, PhysicsConfig{}, false, false);
    CHECK_MESSAGE(r.coast_ticks <= 12, "coast lasted " << r.coast_ticks << " ticks");
    // Slow-mode coast is shorter still.
    const RunSummary sneak = cruise_then_release(world, PhysicsConfig{}, false, true);
    CHECK(sneak.coast_blocks < r.coast_blocks);
}

// ── 2. The friction factor itself (assertable only since T-D7) ──────────────

TEST_CASE("ground friction is the product of block friction and entity air resistance") {
    // Multiplicative decoupling (docs/research/07 §7.2): k = 0.91 × S.
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    MoveResult mr;
    step_player(s, in, world, PhysicsConfig{}, &mr);
    CHECK(mr.slipperiness == doctest::Approx(0.6));
    CHECK(mr.friction == doctest::Approx(0.91 * 0.6));
    // The stored velocity after one tick is the move velocity times k.
    CHECK(s.velocity.x == doctest::Approx(0.1 * 0.98 * 0.91 * 0.6));
}

TEST_CASE("airborne friction ignores the block below and keeps ninety one percent") {
    SlipperyWorld world;
    world.s = 0.98; // ice below, but the entity is in the air
    PlayerState s = spawn_on(0.5, 70.0, 0.5);
    s.on_ground = false;
    s.fall_peak_y = 70.0;
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    MoveResult mr;
    step_player(s, in, world, PhysicsConfig{}, &mr);
    CHECK(mr.slipperiness == doctest::Approx(1.0));
    CHECK(mr.friction == doctest::Approx(0.91));
}

// ── 3. Steady states (docs/01 §2 ⚖, unchanged by the migration) ─────────────

TEST_CASE("the migrated pipeline preserves all three spec cruise speeds") {
    BoxWorld world = flat_world();

    struct Expect {
        bool sprint;
        bool sneak;
        double mps;
    };

    for (const Expect &e : {Expect{false, false, 4.317}, Expect{true, false, 5.612}, Expect{false, true, 1.295}}) {
        // Measure the DISPLACEMENT per tick at cruise (the stored velocity is
        // post-damping and therefore k times smaller — that gap is exactly the
        // "damping after displacement" ordering).
        PlayerState s = spawn_on(0.5, 64.0, 0.5);
        InputState in;
        in.yaw = kYawEast;
        in.forward = true;
        in.sprint = e.sprint;
        in.sneak = e.sneak;
        for (int t = 0; t < 100; ++t) {
            step_player(s, in, world);
        }
        const double x0 = s.position.x;
        for (int t = 0; t < 200; ++t) {
            step_player(s, in, world);
        }
        const double mps = (s.position.x - x0) / 200.0 * 20.0;
        CHECK_MESSAGE(std::abs(mps - e.mps) <= e.mps * 0.005, "measured " << mps << " want " << e.mps);
    }
}

TEST_CASE("diagonal input is faster than single axis by the documented one over 0.98") {
    // 45° Strafe (docs/research/06 §3.2): the 0.98 pre-scale is clamped up to 1
    // only for single-axis input, so a diagonal gains 1/0.98 ≈ 2.04%.
    BoxWorld world = flat_world();
    auto cruise = [&](bool diagonal) {
        PlayerState s = spawn_on(0.5, 64.0, 0.5);
        InputState in;
        in.yaw = kYawEast;
        in.forward = true;
        in.right = diagonal;
        for (int t = 0; t < 100; ++t) {
            step_player(s, in, world);
        }
        const double x0 = s.position.x;
        const double z0 = s.position.z;
        for (int t = 0; t < 200; ++t) {
            step_player(s, in, world);
        }
        const double dx = s.position.x - x0;
        const double dz = s.position.z - z0;
        return std::sqrt(dx * dx + dz * dz) / 200.0 * 20.0;
    };
    const double straight = cruise(false);
    const double diagonal = cruise(true);
    CHECK_MESSAGE(std::abs(straight - 4.317) <= 4.317 * 0.005, "straight " << straight);
    CHECK_MESSAGE(diagonal / straight == doctest::Approx(1.0 / 0.98).epsilon(0.002),
                  "diagonal " << diagonal << " straight " << straight);
}

// ── 4. Jump apex (docs/01 §2 ⚖ 1.2522) ─────────────────────────────────────

TEST_CASE("jump apex is unchanged at 1.2522 under the new pipeline") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.jump = true;
    double peak = s.position.y;
    for (int t = 0; t < 40; ++t) {
        step_player(s, in, world);
        peak = std::max(peak, s.position.y);
        in.jump = false;
    }
    CHECK_MESSAGE(std::abs(peak - 65.2522) <= 0.01, "apex " << peak);
}

TEST_CASE("a five thousandth threshold would break the spec apex so three thousandth is used") {
    // T-D7 ruling A-5: 0.003 reproduces the 1.9+ apex exactly; 0.005 gives
    // 1.2492 and would violate docs/01 §2. This guards against a future
    // "harmless" bump of the threshold.
    BoxWorld world = flat_world();
    auto apex_with_threshold = [&](double threshold) {
        PhysicsConfig cfg;
        cfg.momentum_threshold = threshold;
        PlayerState s = spawn_on(0.5, 64.0, 0.5);
        InputState in;
        in.jump = true;
        double peak = s.position.y;
        for (int t = 0; t < 40; ++t) {
            step_player(s, in, world, cfg);
            peak = std::max(peak, s.position.y);
            in.jump = false;
        }
        return peak;
    };
    const double apex_003 = apex_with_threshold(0.003);
    const double apex_005 = apex_with_threshold(0.005);
    CHECK(apex_003 == doctest::Approx(65.252203).epsilon(1e-6));
    CHECK(apex_005 < 65.2500); // 1.2492: measurably lower
    CHECK(PhysicsConfig{}.momentum_threshold == 0.003);
}

// ── 5. Ice slipperiness hook (docs/research/06 §7.1) ────────────────────────

TEST_CASE("ice is slower to reach but keeps speed six times longer than stone") {
    // Counter-intuitive but correct: ice is SLIPPERIER, not FASTER. The
    // acceleration falls with (0.6/S)³ (cube) while retention only rises
    // linearly, so the ice cruise speed is BELOW the default block's
    // (docs/research/06 §7.1).
    // A real default-block floor (a bare BoxWorld with no solid() call is an
    // empty world, i.e. air physics — not "stone").
    const BoxWorld stone = flat_world();
    SlipperyWorld ice;
    ice.s = 0.98;

    auto measured = [](const IBlockSource &world) {
        PlayerState s = spawn_on(0.5, 64.0, 0.5);
        InputState in;
        in.yaw = kYawEast;
        in.forward = true;
        for (int t = 0; t < 400; ++t) {
            step_player(s, in, world);
        }
        const double x0 = s.position.x;
        for (int t = 0; t < 200; ++t) {
            step_player(s, in, world);
        }
        const double cruise = (s.position.x - x0) / 200.0 * 20.0;
        in.forward = false;
        const double x_rel = s.position.x;
        for (int t = 0; t < 4000; ++t) {
            step_player(s, in, world);
            if (s.velocity.x == 0.0) {
                break;
            }
        }
        return std::pair<double, double>{cruise, s.position.x - x_rel};
    };

    const auto [stone_cruise, stone_coast] = measured(stone);
    const auto [ice_cruise, ice_coast] = measured(ice);

    CHECK_MESSAGE(std::abs(stone_cruise - 4.317) <= 4.317 * 0.005, "stone " << stone_cruise);
    // ⚖ 4.157 m/s on ice — below the 4.317 default-block value.
    CHECK_MESSAGE(std::abs(ice_cruise - 4.157) <= 4.157 * 0.01, "ice " << ice_cruise);
    CHECK_MESSAGE(ice_cruise < stone_cruise, "ice must NOT be faster than stone");
    // ⚖ 1.69 blocks of glide, roughly 6.6× the default block.
    CHECK_MESSAGE(std::abs(ice_coast - 1.69) <= 0.2, "ice coast " << ice_coast);
    CHECK_MESSAGE(ice_coast / stone_coast > 5.0, "ice/stone coast ratio " << ice_coast / stone_coast);
}

TEST_CASE("slime blocks are the slowest of the three friction classes") {
    // (0.6/S)³ falls faster than 1/(1−0.91S) rises for S = 0.8, so slime is
    // slower than both stone and ice (docs/research/06 §7.1).
    SlipperyWorld slime;
    slime.s = 0.8;
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    for (int t = 0; t < 400; ++t) {
        step_player(s, in, slime);
    }
    const double x0 = s.position.x;
    for (int t = 0; t < 200; ++t) {
        step_player(s, in, slime);
    }
    const double mps = (s.position.x - x0) / 200.0 * 20.0;
    CHECK_MESSAGE(std::abs(mps - 3.040) <= 3.040 * 0.01, "slime " << mps);
    CHECK(mps < 4.317);
    CHECK(mps < 4.157);
}

// ── 6. Per-entity-type parameter instantiation (the foundation requirement) ─

TEST_CASE("a falling block and a player keep independent gravity configurations") {
    // docs/research/07 §1.4: a falling block's gravity is HALF the player's
    // (0.04 vs 0.08) and it retains 0.98 of its horizontal momentum. Under one
    // global config these cannot coexist; instantiated per entity type they do.
    const PhysicsConfig player = PhysicsConfig::for_entity(EntityKind::Player);
    const PhysicsConfig block = PhysicsConfig::for_entity(EntityKind::FallingBlock);
    CHECK(player.kind == EntityKind::Player);
    CHECK(block.kind == EntityKind::FallingBlock);
    CHECK(player.gravity == 0.08);
    CHECK(block.gravity == 0.04);
    CHECK(player.horizontal_drag == 0.91);
    CHECK(block.horizontal_drag == 0.98);

    // Both simulate side by side without the other's parameters leaking in.
    BoxWorld world;
    PlayerState player_state = spawn_on(0.5, 100.0, 0.5);
    player_state.on_ground = false;
    PlayerState block_state = spawn_on(0.5, 100.0, 0.5);
    block_state.on_ground = false;
    InputState in; // no input: pure vertical drop
    for (int t = 0; t < 100; ++t) {
        step_player(player_state, in, world, player);
        step_player(block_state, in, world, block);
    }
    // Both are accelerating downward; the player's terminal speed is about
    // twice the block's (same retention 0.98, half the gravity).
    const double player_vy = std::abs(player_state.velocity.y);
    const double block_vy = std::abs(block_state.velocity.y);
    CHECK_MESSAGE(player_vy == doctest::Approx(2.0 * block_vy).epsilon(0.01),
                  "player " << player_vy << " block " << block_vy);
    CHECK(player_state.health == 20.0);
    // The configurations themselves were not mutated by the simulation.
    CHECK(PhysicsConfig::for_entity(EntityKind::Player).gravity == 0.08);
    CHECK(PhysicsConfig::for_entity(EntityKind::FallingBlock).gravity == 0.04);
}

TEST_CASE("a caller-supplied config leaves the default config untouched") {
    // Configs are values, not a process-wide singleton: simulating with a
    // modified one must not affect anything else.
    const double before = PhysicsConfig{}.gravity;
    PhysicsConfig custom;
    custom.gravity = 0.5;
    BoxWorld world;
    PlayerState s = spawn_on(0.5, 100.0, 0.5);
    s.on_ground = false;
    InputState in;
    for (int t = 0; t < 10; ++t) {
        step_player(s, in, world, custom);
    }
    CHECK(PhysicsConfig{}.gravity == before);
    CHECK(custom.gravity == 0.5);
}

// ── 7. MoveResult out-parameter (interface frozen for M2) ───────────────────

TEST_CASE("move result reports wall hits without any signature break") {
    // This call is the FROZEN four-argument form: it must still compile and
    // behave exactly as before the result object existed.
    BoxWorld world = flat_world();
    world.solid(24, 24, 64, 67, -8, 8);
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    MoveResult mr;
    for (int t = 0; t < 150; ++t) {
        step_player(s, in, world, PhysicsConfig{}, &mr);
    }
    CHECK(mr.hit_x);
    CHECK_FALSE(mr.hit_ceiling);
    CHECK(s.position.x == 23.7);

    // Omitting the out-parameter keeps working (defaulted).
    PlayerState t2 = spawn_on(0.5, 64.0, 0.5);
    for (int t = 0; t < 150; ++t) {
        step_player(t2, in, world);
    }
    CHECK(t2.position.x == 23.7);
}

TEST_CASE("move result reports a jump landing with its fall distance") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 74.0, 0.5);
    s.on_ground = false; // falling 10 blocks
    InputState in;
    MoveResult mr;
    bool saw_landing = false;
    for (int t = 0; t < 60; ++t) {
        step_player(s, in, world, PhysicsConfig{}, &mr);
        if (mr.landed) {
            saw_landing = true;
            CHECK(mr.hit_y);
            CHECK(mr.fall_distance == 0.0); // reset on landing
            break;
        }
    }
    CHECK(saw_landing);
    CHECK(s.health == 13.0); // floor(10 − 3)
}

TEST_CASE("move result flags a ceiling bump as a vertical hit without landing") {
    BoxWorld world = flat_world();
    world.solid(-8, 8, 66, 66, -8, 8); // ceiling 2 above the feet
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.jump = true;
    MoveResult mr;
    bool saw_ceiling = false;
    for (int t = 0; t < 30; ++t) {
        step_player(s, in, world, PhysicsConfig{}, &mr);
        in.jump = false;
        if (mr.hit_ceiling) {
            saw_ceiling = true;
            CHECK(mr.hit_y);
            CHECK_FALSE(mr.landed);
            break;
        }
    }
    CHECK(saw_ceiling);
}

// ── 8. Block shape seam (interface first, concrete shapes later) ────────────

TEST_CASE("the block shape query defaults to solid means full cube") {
    BoxWorld world = flat_world();
    CHECK(world.shape_at(0, 63, 0) == BlockShape::FullCube);
    CHECK(world.shape_at(0, 64, 0) == BlockShape::Empty);

    // A world that reports a full cube without being solid demonstrates that
    // collision consults the shape, not `solid_at`.
    struct ShapeOnlyWorld final : IBlockSource {
        bool solid_at(int, int, int) const override { return false; }

        BlockShape shape_at(int, int wy, int) const override {
            return wy == 63 ? BlockShape::FullCube : BlockShape::Empty;
        }
    } shape_only;

    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    for (int t = 0; t < 20; ++t) {
        step_player(s, in, shape_only);
    }
    CHECK(s.position.y == 64.0); // held up by the block shape
    CHECK(s.on_ground);
}

// ── 9. Sprint-jump (docs/01 §2 ⚖) under the migrated pipeline ───────────────

TEST_CASE("sprint jump chain average is 7.127 m/s with MC raw plus 0.2") {
    // The wiki figure is an AVERAGE SPEED FOR THE MOVEMENT METHOD (a sustained
    // chain: the entity re-jumps the tick it lands — "Running start effective?
    // No", footnote "the jump key is released then held mid-air every jump").
    // Under the migrated pipeline MC's RAW +0.2 lands on it: measured 7.1268
    // m/s, i.e. 0.003% off, with NO calibration constant.
    //
    // T-D1 had to calibrate 0.1842 to hit this, because it sampled an ISOLATED
    // jump 50 ticks apart (a cold arc: 38 cruise ticks + one jump), whose
    // average is only 6.05 m/s here. That cadence measures "mostly sprinting",
    // not "sprint-jumping as a method".
    BoxWorld world = flat_world();
    const PhysicsConfig cfg; // raw MC constants: sprint_jump_boost = 0.2
    CHECK(cfg.sprint_jump_boost == 0.2);

    const std::vector<double> arcs = chain_arcs(world, cfg, /*sprint=*/true, 4000);
    REQUIRE(arcs.size() >= 8);
    const double disp = steady_arc(arcs);
    const double avg_mps = disp * 20.0 / 12.0;
    CHECK_MESSAGE(std::abs(avg_mps - 7.127) <= 7.127 * 0.01, "chain arc average " << avg_mps);
    CHECK_MESSAGE(std::abs(avg_mps - 7.127) <= 7.127 * 0.005, "should land much closer than 1 percent: " << avg_mps);
}

TEST_CASE("sprint jump chain clearance follows the affine lock from its average") {
    // docs/01 §2 (T-D1 finding): over a fixed 12-move window the clearance and
    // the average are affinely locked, clearance = 0.6 × avg − 0.6. The wiki's
    // own 7.127 therefore IMPLIES 3.6762, which is just below the 3.7 floor the
    // same spec line carries. The two bands cannot both hold; T-D7 reports this
    // conflict for a PM ruling and reproduces the wiki value, per its card
    // ("+0.2 should naturally hit 7.127; do not retune to flatter a number").
    BoxWorld world = flat_world();
    const PhysicsConfig cfg;
    const std::vector<double> arcs = chain_arcs(world, cfg, /*sprint=*/true, 4000);
    REQUIRE(arcs.size() >= 8);
    const double disp = steady_arc(arcs);
    const double avg_mps = disp * 20.0 / 12.0;
    const double clearance = disp - 0.6;

    CHECK_MESSAGE(std::abs(clearance - (0.6 * avg_mps - 0.6)) < 1e-9,
                  "lock violated: clearance " << clearance << " vs " << (0.6 * avg_mps - 0.6));
    CHECK_MESSAGE(clearance > 3.60, "clearance " << clearance);
    CHECK_MESSAGE(clearance < 3.75, "clearance " << clearance);

    // A walk jump measured over the same window and cadence clears far less:
    // the comparative claim the old test encoded still holds.
    const std::vector<double> walk_arcs = chain_arcs(world, cfg, /*sprint=*/false, 4000);
    REQUIRE(walk_arcs.size() >= 8);
    const double walk_clearance = steady_arc(walk_arcs) - 0.6;
    CHECK_MESSAGE(walk_clearance > 1.5, "walk clearance " << walk_clearance);
    CHECK_MESSAGE(walk_clearance < 2.1, "walk clearance " << walk_clearance);
    CHECK_MESSAGE(clearance > walk_clearance + 1.3, "sprint must clear well past a walk jump");
}

TEST_CASE("a strafe-launched sprint jump is slower than a straight one") {
    // docs/research/06 §5.1: the +0.2 sprint impulse is applied along the
    // FACING direction, not the movement direction, so sprint-jumping while
    // strafing (facing forward, moving diagonally) is genuinely SLOWER than a
    // straight sprint-jump. This is MC behaviour, not a regression — it is why
    // "sidestep" techniques need the facing turned to the movement direction.
    BoxWorld world = flat_world();
    auto arc_disp = [&](bool diagonal) {
        PlayerState s = spawn_on(0.5, 64.0, 0.5);
        InputState in;
        in.yaw = kYawEast;
        in.forward = true;
        in.sprint = true;
        in.right = diagonal;
        double x0 = 0.0;
        double z0 = 0.0;
        for (int t = 0; t < 200; ++t) {
            in.jump = (t == 60);
            if (t == 60) {
                x0 = s.position.x;
                z0 = s.position.z;
            }
            step_player(s, in, world);
            if (t == 71) {
                const double dx = s.position.x - x0;
                const double dz = s.position.z - z0;
                return std::sqrt(dx * dx + dz * dz);
            }
        }
        return 0.0;
    };
    const double straight = arc_disp(false);
    const double diagonal = arc_disp(true);
    CHECK_MESSAGE(diagonal < straight, "strafe launch should lose distance: " << diagonal << " vs " << straight);
    CHECK_MESSAGE(diagonal > 0.9 * straight, "the loss should be modest, not catastrophic");
}

// ── 10. Determinism is preserved (T007 acceptance) ──────────────────────────

TEST_CASE("the migrated pipeline stays bit-identical across repeated runs") {
    BoxWorld world = flat_world();
    world.solid(24, 24, 64, 67, -8, 8);
    PlayerState a = spawn_on(0.5, 64.0, 0.5);
    PlayerState b = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.sprint = true;
    for (int t = 0; t < 300; ++t) {
        in.jump = (t % 40 == 0);
        MoveResult ma;
        MoveResult mb;
        step_player(a, in, world, PhysicsConfig{}, &ma);
        step_player(b, in, world, PhysicsConfig{}, &mb);
        CHECK(a.position == b.position);
        CHECK(a.velocity == b.velocity);
        CHECK(ma.hit_x == mb.hit_x);
        CHECK(ma.friction == mb.friction);
    }
}
