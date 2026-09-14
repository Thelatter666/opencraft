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

// Runs one continuous sprint script on one world: sprint engaged from tick
// 0, single jump pulses every `jump_period` ticks, returns the
// center-to-center distance of each complete arc. The arc is 12 moves: the
// jump tick plus 11 airborne ticks (measured, see the T-D1 report §3 — R3
// fixed this from a 13-move window that was swallowing the first
// post-landing ground tick). Ground gaps between jumps are long enough that
// ground_drag = 0.9 restores the cruise speed before every takeoff
// (residual deficit < 0.2% at 37 ground ticks), so all sampled arcs are
// statistically identical.
std::vector<double> arc_distances(int jump_period, int total_ticks) {
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    in.sprint = true; // key path engages and holds sprint
    std::vector<double> distances;
    double x_at_takeoff = 0.0;
    int takeoff_tick = -1;
    for (int t = 0; t < total_ticks; ++t) {
        const bool jump_here = (t % jump_period == 0 && t > 0);
        in.jump = jump_here;
        if (jump_here) {
            x_at_takeoff = s.position.x;
            takeoff_tick = t;
        }
        step_player(s, in, world);
        if (takeoff_tick >= 0 && t == takeoff_tick + 11) { // 12th move: landing tick
            distances.push_back(s.position.x - x_at_takeoff);
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
    // Window justification: the wiki's 7.127 m/s is the average speed of a
    // sprint-jump arc. We sample complete arcs from the middle of a fixed
    // script, skipping the first jump so the takeoff acceleration ramp is
    // excluded. Each arc launches from restored cruise speed (see
    // arc_distances), so every sample is drawn from the same distribution.
    //
    // Arc length is 12 moves (jump tick + 11 airborne), MEASURED per the R3
    // ruling — the previous 13-move window appended the first post-landing
    // ground tick (≈0.293 blocks of ground speed) and systematically pulled
    // the average down by ~0.9%.
    const std::vector<double> arcs = arc_distances(50, 400); // jumps at 50,100,...; skip first
    REQUIRE(arcs.size() >= 4);
    double sum = 0.0;
    for (std::size_t i = 1; i < arcs.size(); ++i) {
        sum += arcs[i];
    }
    const double avg_mps = sum / static_cast<double>(arcs.size() - 1) * 20.0 / 12.0;
    CHECK_MESSAGE(std::abs(avg_mps - 7.127) <= 7.127 * 0.01, "arc average " << avg_mps);
}

TEST_CASE("sprint jump clears about four blocks against two for a walk jump") {
    // Wiki metric "jump across up to four blocks" is gap clearance =
    // center-to-center displacement minus the 0.6-block hitbox width, over
    // the same 12-move arc as the average-speed assertion above.
    //
    // NOTE (T-D1 report §3): over a fixed 12-move window the arc average and
    // the clearance are affinely locked — clearance = 0.6 * avg − 0.6. Hitting
    // the ⚖ average of 7.127 exactly would give 3.676 clearance, just under
    // the ⚖ 3.7 floor; conversely "4.03 clearance" demands avg 7.717, i.e.
    // +8.3% and well outside the 1% average band. The calibrated constant is
    // therefore placed to satisfy BOTH ⚖ bands simultaneously rather than
    // either one exactly (measured: avg 7.1845, clearance 3.7107).
    const std::vector<double> arcs = arc_distances(50, 400);
    REQUIRE(arcs.size() >= 2);
    const double sprint_clearance = arcs.back() - 0.6;
    CHECK_MESSAGE(sprint_clearance > 3.7, "sprint clearance " << sprint_clearance);
    CHECK_MESSAGE(sprint_clearance < 4.3, "sprint clearance " << sprint_clearance);

    // Walk jump: same script without the sprint key.
    BoxWorld world = flat_world();
    PlayerState s = spawn_on(0.5, 64.0, 0.5);
    InputState in;
    in.yaw = kYawEast;
    in.forward = true;
    double x0 = 0.0;
    double x1 = 0.0;
    for (int t = 0; t < 200; ++t) {
        in.jump = (t == 60);
        if (t == 60) {
            x0 = s.position.x;
        }
        step_player(s, in, world);
        if (t == 71) { // 12th move, same window as the sprint arc
            x1 = s.position.x;
        }
    }
    CHECK_FALSE(s.sprinting);
    const double walk_clearance = (x1 - x0) - 0.6;
    CHECK_MESSAGE(walk_clearance > 1.9, "walk clearance " << walk_clearance);
    CHECK_MESSAGE(walk_clearance < 2.5, "walk clearance " << walk_clearance);
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
