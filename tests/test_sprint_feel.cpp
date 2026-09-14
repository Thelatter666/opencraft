#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "fov.hpp"
#include "opencraft/physics/player_physics.hpp"
#include "physics_test_world.hpp"

using opencraft::physics::InputState;
using opencraft::physics::PhysicsConfig;
using opencraft::physics::PlayerState;
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

InputState base_input() {
    InputState in;
    in.yaw = kYawEast;
    return in;
}

// Presses forward (edge) on the given ticks, holds forward the rest of the
// time, and reports whether sprint ever engaged / is engaged at the end.
PlayerState double_tap_run(const BoxWorld &world, int first_press, int second_press, int total_ticks,
                           double hunger = 20.0) {
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    s.hunger = hunger;
    for (int t = 0; t < total_ticks; ++t) {
        InputState in = base_input();
        in.forward = true;
        in.forward_press = (t == first_press || t == second_press);
        step_player(s, in, world);
    }
    return s;
}

// T-D7: a sustained sprint-jump CHAIN — re-jump on the tick the entity lands.
// This is the cadence the wiki's 7.127 m/s describes: the table's row is
// "Sprint-jumping, Flat terrain" under a column headed "Average speed" with
// "Running start effective? No", and its footnote reads "assuming the jump key
// is released then held mid-air EVERY JUMP" — i.e. a repeated cycle, which is
// what makes sprint-jumping a mode of transport rather than a single leap.
// Returns the centre-to-centre displacement of each complete arc (12 moves).
std::vector<double> chain_arc_distances(int total_ticks) {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.sprint = true;
    std::vector<double> distances;
    double x_at_takeoff = 0.0;
    bool airborne = false;
    for (int t = 0; t < total_ticks; ++t) {
        const bool grounded_at_start = s.on_ground;
        in.jump = grounded_at_start && t > 20;
        if (in.jump) {
            x_at_takeoff = s.position.x;
        }
        step_player(s, in, world);
        if (grounded_at_start && !s.on_ground) {
            airborne = true;
        } else if (airborne && s.on_ground) {
            distances.push_back(s.position.x - x_at_takeoff);
            airborne = false;
        }
    }
    return distances;
}

} // namespace

// ── Double-tap sprint window (docs/research/05 §1: MC arms a 7-tick timer) ──

TEST_CASE("double tap forward inside the seven tick window engages sprint") {
    BoxWorld world = flat_world();
    // Window spans ticks 0..6 after the first press: press at 0 and 6
    // (gap 6) must engage. The timer decrements before each press check,
    // so a press 6 ticks later still sees timer == 1.
    PlayerState s = double_tap_run(world, 0, 6, 10);
    CHECK(s.sprinting);
}

TEST_CASE("double tap forward one tick past the window re-arms instead") {
    BoxWorld world = flat_world();
    // A press 7 ticks after the first finds the timer expired and re-arms.
    PlayerState s = double_tap_run(world, 0, 7, 10);
    CHECK_FALSE(s.sprinting);
}

TEST_CASE("single forward tap only arms the window without engaging sprint") {
    BoxWorld world = flat_world();
    PlayerState s = double_tap_run(world, 0, -1, 10);
    CHECK_FALSE(s.sprinting);
    // The armed window decays without a second press.
    CHECK(s.sprint_toggle_timer == 0);
}

TEST_CASE("releasing forward disengages sprint") {
    BoxWorld world = flat_world();
    PlayerState s = double_tap_run(world, 0, 2, 10);
    CHECK(s.sprinting);
    InputState in = base_input(); // forward released
    step_player(s, in, world);
    CHECK_FALSE(s.sprinting);
}

TEST_CASE("sprint key with forward engages sprint without any double tap") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in = base_input();
    in.forward = true;
    in.sprint = true;
    step_player(s, in, world);
    CHECK(s.sprinting);
}

TEST_CASE("double tap forward in mid air does not engage sprint") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 70.0, 0.5);
    s.on_ground = false; // falling over flat ground
    s.fall_peak_y = 70.0;
    for (int t = 0; t < 8; ++t) {
        InputState in = base_input();
        in.forward = true;
        in.forward_press = (t == 0 || t == 5);
        step_player(s, in, world);
        CHECK_FALSE(s.sprinting);
    }
}

TEST_CASE("sprint key path also engages sprint in mid air") {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 70.0, 0.5);
    s.on_ground = false;
    s.fall_peak_y = 70.0;
    InputState in = base_input();
    in.forward = true;
    in.sprint = true;
    step_player(s, in, world);
    CHECK(s.sprinting);
}

// ── Sprint gates (docs/research/05 §5: hunger > 6 required) ─────────────────

TEST_CASE("hunger at the gate of six blocks engaging sprint by either path") {
    BoxWorld world = flat_world();
    PlayerState s = double_tap_run(world, 0, 2, 10, 6.0);
    CHECK_FALSE(s.sprinting);
    PlayerState k = spawn_on(0.5, 64.0, 0.5);
    k.hunger = 6.0;
    InputState in = base_input();
    in.forward = true;
    in.sprint = true;
    step_player(k, in, world);
    CHECK_FALSE(k.sprinting);
}

TEST_CASE("hunger just above the gate still allows sprint") {
    BoxWorld world = flat_world();
    PlayerState s = double_tap_run(world, 0, 2, 10, 6.5);
    CHECK(s.sprinting);
    PlayerState k = spawn_on(0.5, 64.0, 0.5);
    k.hunger = 7.0;
    InputState in = base_input();
    in.forward = true;
    in.sprint = true;
    step_player(k, in, world);
    CHECK(k.sprinting);
}

TEST_CASE("hunger dropping to six mid sprint disengages sprint") {
    BoxWorld world = flat_world();
    PlayerState s = double_tap_run(world, 0, 2, 10);
    CHECK(s.sprinting);
    s.hunger = 6.0;
    InputState in = base_input();
    in.forward = true;
    step_player(s, in, world);
    CHECK_FALSE(s.sprinting);
}

// ── Stop conditions ─────────────────────────────────────────────────────────

TEST_CASE("backward input disengages sprint") {
    BoxWorld world = flat_world();
    PlayerState s = double_tap_run(world, 0, 2, 10);
    CHECK(s.sprinting);
    InputState in = base_input();
    in.forward = true;
    in.backward = true; // W and S both held: MC moveForward sums to zero
    step_player(s, in, world);
    CHECK_FALSE(s.sprinting);
}

TEST_CASE("colliding with a wall disengages sprint on the next tick") {
    BoxWorld world = flat_world();
    world.solid(24, 24, 64, 67, -8, 8);
    PlayerState s = double_tap_run(world, 0, 2, 10);
    CHECK(s.sprinting);
    bool saw_wall = false;
    for (int t = 0; t < 200 && s.sprinting; ++t) {
        InputState in = base_input();
        in.forward = true;
        step_player(s, in, world);
        saw_wall = saw_wall || s.collided_horizontally;
    }
    CHECK(saw_wall);
    CHECK_FALSE(s.sprinting);
    CHECK(s.position.x == doctest::Approx(23.7));
}

// ── Sprint jump (docs/01 §2 ⚖: arc average 7.127 m/s, ≈4 block clearance) ───

TEST_CASE("sprint jump arc average speed is 7.127 m/s within one percent") {
    // T-D7 rewrote this assertion's SAMPLING CADENCE. The ⚖ target is
    // unchanged; what changed is which motion it is measured on.
    //
    // The wiki's 7.127 m/s is the average speed of "Sprint-jumping" as a
    // MOVEMENT METHOD: its table lists the row under a column headed "Average
    // speed in m/s", answers "Running start effective? No", and footnotes the
    // elevation variants with "assuming the jump key is released then held
    // mid-air EVERY JUMP" — a repeated cycle. So the measurement is a sustained
    // chain of jumps, each launched the tick the previous one lands.
    //
    // T-D1 instead sampled an ISOLATED jump every 50 ticks (38 cruise ticks
    // between leaps) and calibrated sprint_jump_boost = 0.1842 to drag that
    // average up to 7.127. That cadence mostly measures SPRINTING with the odd
    // jump in it: under the migrated pipeline it reads 6.05 m/s, and no raw MC
    // constant reproduces 7.127 there (it would need ~0.33, i.e. 65% above
    // MC's). Over the chain cadence MC's RAW +0.2 gives 7.1268 m/s — 0.003%
    // off the ⚖ value, with no calibration at all (T-D7 acceptance item 4).
    const std::vector<double> arcs = chain_arc_distances(4000);
    REQUIRE(arcs.size() >= 8);
    // Drop the ramp-in arcs; the chain converges within ~10 jumps.
    const std::size_t from = arcs.size() / 2;
    double sum = 0.0;
    for (std::size_t i = from; i < arcs.size(); ++i) {
        sum += arcs[i];
    }
    const double avg_mps = sum / static_cast<double>(arcs.size() - from) * 20.0 / 12.0;
    CHECK_MESSAGE(std::abs(avg_mps - 7.127) <= 7.127 * 0.01, "arc average " << avg_mps);
    CHECK_MESSAGE(std::abs(avg_mps - 7.127) <= 7.127 * 0.005, "raw MC +0.2 should land far closer: " << avg_mps);
}

TEST_CASE("sprint jump clears well past a walk jump over the same window") {
    // T-D7 note: the absolute clearance assertion is no longer a separate ⚖
    // band. Over a fixed 12-move window the clearance is AFFINELY LOCKED to the
    // average (clearance = 0.6 × avg − 0.6, docs/01 §2 "仿射锁定"), so the
    // wiki's own 7.127 mathematically IMPLIES 3.6762 — just under the 3.7 floor
    // that the same spec line carries. The two cannot both hold, and T-D7's
    // card instructs reproducing 7.127 rather than retuning to flatter a
    // number. The clearance is therefore asserted as the lock plus a sanity
    // band, and the conflict is reported to the PM (T-D7 report §5).
    const std::vector<double> arcs = chain_arc_distances(4000);
    REQUIRE(arcs.size() >= 8);
    const std::size_t from = arcs.size() / 2;
    double sum = 0.0;
    for (std::size_t i = from; i < arcs.size(); ++i) {
        sum += arcs[i];
    }
    const double disp = sum / static_cast<double>(arcs.size() - from);
    const double avg_mps = disp * 20.0 / 12.0;
    const double sprint_clearance = disp - 0.6;
    CHECK_MESSAGE(std::abs(sprint_clearance - (0.6 * avg_mps - 0.6)) < 1e-9, "affine lock violated");
    CHECK_MESSAGE(sprint_clearance > 3.60, "sprint clearance " << sprint_clearance);
    CHECK_MESSAGE(sprint_clearance < 3.75, "sprint clearance " << sprint_clearance);

    // Walk jump: same script without the sprint key, same chain cadence and
    // same 12-move window.
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    double walk_displacement = 0.0;
    {
        std::vector<double> walk_arcs;
        double x0 = 0.0;
        bool airborne = false;
        for (int t = 0; t < 4000; ++t) {
            const bool grounded_at_start = s.on_ground;
            in.jump = grounded_at_start && t > 20;
            if (in.jump) {
                x0 = s.position.x;
            }
            step_player(s, in, world);
            if (grounded_at_start && !s.on_ground) {
                airborne = true;
            } else if (airborne && s.on_ground) {
                walk_arcs.push_back(s.position.x - x0);
                airborne = false;
            }
        }
        REQUIRE(walk_arcs.size() >= 8);
        double wsum = 0.0;
        for (std::size_t i = walk_arcs.size() / 2; i < walk_arcs.size(); ++i) {
            wsum += walk_arcs[i];
        }
        walk_displacement = wsum / static_cast<double>(walk_arcs.size() - walk_arcs.size() / 2);
    }
    CHECK_FALSE(s.sprinting);
    const double walk_clearance = walk_displacement - 0.6;
    CHECK_MESSAGE(walk_clearance > 1.5, "walk clearance " << walk_clearance);
    CHECK_MESSAGE(walk_clearance < 2.1, "walk clearance " << walk_clearance);
    const bool clears_much_further = sprint_clearance > walk_clearance + 1.3;
    CHECK_MESSAGE(clears_much_further, "sprint must reach well past a walk jump");
}

TEST_CASE("sprint jump keeps airborne speed above walking pace all arc long") {
    // T007 gap closure: the old air model bled a sprint jump down toward
    // walk speed mid-arc; the new model must keep every airborne tick above
    // the 4.317 m/s walk steady state (docs/research/05 §4).
    //
    // 11 airborne checks, NOT 12 (R3): the jump tick puts the player in the
    // air, then it takes 11 further moves to land — the 12th iteration of
    // this loop would inspect a state that is already back on the ground.
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.sprint = true;
    for (int t = 0; t < 60; ++t) {
        step_player(s, in, world);
    }
    in.jump = true;
    step_player(s, in, world);
    in.jump = false;
    const double walk_bpt = 4.317 / 20.0;
    for (int t = 0; t < 11; ++t) {
        CHECK_FALSE(s.on_ground);
        const double h_speed = std::sqrt(s.velocity.x * s.velocity.x + s.velocity.z * s.velocity.z);
        CHECK_MESSAGE(h_speed > walk_bpt, "air speed " << h_speed << " at air tick " << t);
        step_player(s, in, world);
    }
    CHECK(s.on_ground); // the 12th move lands, closing the 12-move arc
}

TEST_CASE("sprint jump vertical arc is unchanged from the spec jump") {
    // The boost is horizontal only: apex must stay 1.2522.
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.sprint = true;
    for (int t = 0; t < 60; ++t) {
        step_player(s, in, world);
    }
    double peak = s.position.y;
    in.jump = true;
    step_player(s, in, world);
    in.jump = false;
    for (int t = 0; t < 40; ++t) {
        step_player(s, in, world);
        peak = std::max(peak, s.position.y);
    }
    CHECK_MESSAGE(std::abs(peak - 65.2522) <= 0.01, "apex " << peak);
}

// ── FOV easing (game/client/src/fov.hpp, docs/research/05 §2) ───────────────

TEST_CASE("fov transition to sprint target is monotone bounded and reaches base plus fifteen percent") {
    float fov = opencraft::client::kBaseFov;
    const float target = opencraft::client::fov_target(true);
    CHECK_MESSAGE(target == doctest::Approx(80.5f).epsilon(0.001), "sprint target " << target);
    float previous = fov;
    int steps = 0;
    while (std::abs(fov - target) > 0.5f && steps < 20) {
        fov = opencraft::client::fov_step(fov, true);
        CHECK(fov > previous);
        CHECK(fov <= target + 1e-4f);
        previous = fov;
        ++steps;
    }
    CHECK_MESSAGE(steps <= 6, "reached in " << steps << " steps");
    CHECK(fov > opencraft::client::kBaseFov);
}

TEST_CASE("fov returns to base after leaving sprint") {
    float fov = opencraft::client::fov_target(true);
    float previous = fov;
    int steps = 0;
    while (std::abs(fov - opencraft::client::kBaseFov) > 0.5f && steps < 20) {
        fov = opencraft::client::fov_step(fov, false);
        CHECK(fov < previous);
        CHECK(fov >= opencraft::client::kBaseFov - 1e-4f);
        previous = fov;
        ++steps;
    }
    CHECK_MESSAGE(steps <= 6, "returned in " << steps << " steps");
}
