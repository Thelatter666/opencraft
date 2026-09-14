// T-D13 evidence harness (QA artifact, NOT part of the deliverable).
//
// Drives the REAL physics (step_player, incl. T-D8 step-assist) and the REAL
// render filter (camera_spring.hpp) and dumps camera Y per render frame, with
// the pre-T-D13 hard-follow camera alongside for comparison.
//
// Design: physics ticks are precomputed on their own 20 Hz timeline, then render
// frames at `fps` replay them through the same partial-tick LERP + kink logic
// main.cpp uses. That mirrors the client exactly (ticks are frame-driven there,
// but the LERP/kink arithmetic is identical).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "camera_spring.hpp"
#include "opencraft/physics/player_physics.hpp"
#include "physics_test_world.hpp"

using opencraft::client::CameraFilter;
using opencraft::physics::InputState;
using opencraft::physics::PhysicsConfig;
using opencraft::physics::PlayerState;
using opencraft::physics::step_player;
using physics_test::BoxWorld;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kYawEast = -kPi / 2.0;
constexpr double kTickS = 0.05;
constexpr double kEyeStanding = 1.62;
constexpr double kEyeSneaking = 1.27;

// Ground at y=64 west of x=10; east of x=10 the collision top is 64 + riser.
// `riser` 0.5 exercises step-assist (T-D8); riser 1.0 is the full block that
// step_height 0.6 deliberately refuses (walks into a wall instead).
BoxWorld terrain(double riser) {
    BoxWorld w;
    w.solid(-256, 9, 63, 63, -256, 256);
    w.solid(10, 256, 63, 63, -256, 256);
    if (riser >= 1.0) {
        w.solid(10, 256, 64, 64, -256, 256); // full extra cube -> 1.0 riser
    } else if (riser > 0.0) {
        w.partial(10, 256, 64, 64, -256, 256, riser); // sub-cube top
    }
    return w;
}

struct TickState {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool on_ground = true;
    bool stepped = false;
    double step_used = 0.0;
};

// Run `ticks` physics ticks with a fixed input program.
std::vector<TickState> simulate(const BoxWorld &world, PlayerState start, int ticks, bool forward, int jump_tick,
                                int sneak_from = -1) {
    PhysicsConfig cfg = PhysicsConfig::for_entity(opencraft::physics::EntityKind::Player);
    PlayerState s = start;
    std::vector<TickState> out;
    out.push_back({s.position.x, s.position.y, s.position.z, s.on_ground, false, 0.0});
    for (int t = 0; t < ticks; ++t) {
        InputState in;
        in.yaw = kYawEast;
        in.forward = forward;
        in.jump = jump_tick >= 0 && t == jump_tick;
        in.sneak = sneak_from >= 0 && t >= sneak_from;
        opencraft::physics::MoveResult res;
        step_player(s, in, world, cfg, &res);
        out.push_back({s.position.x, s.position.y, s.position.z, s.on_ground, res.stepped, res.step_height_used});
    }
    return out;
}

struct Row {
    double t;
    double lerp_y;
    double old_y;
    double new_y;
};

struct Result {
    std::vector<Row> rows;
    bool stepped = false;
    double step_used = 0.0;
    double old_max_jump = 0.0;
    double new_max_jump = 0.0;
    double worst_lerp_gap = 0.0;
};

// Replay the tick sequence as render frames at `fps`.
Result replay(const std::vector<TickState> &ticks, double fps, double eye_height, bool sneak_changes_eye) {
    Result r;
    CameraFilter f;
    f.reset(ticks[0].x, ticks[0].y, ticks[0].z, eye_height);
    int n = 0;
    double acc = 0.0;
    double T = 0.0;
    const double dt = 1.0 / fps;
    const int last = static_cast<int>(ticks.size()) - 1;
    double prev_new = f.y();
    double prev_old = 0.0;
    bool have_prev = false;
    double prev_eye = eye_height;
    while (n < last || acc > 1e-12) {
        const double alpha_before = std::clamp(acc / kTickS, 0.0, 1.0);
        const double kink_dt = (1.0 - alpha_before) * kTickS;
        const bool kink_inside = n < last && kink_dt > 0.0 && kink_dt < dt;
        const double kink_feet = ticks[std::min(n + 1, last)].y;
        const double kink_eye_height = (kink_inside && sneak_changes_eye) ? prev_eye : eye_height;
        acc += dt;
        int fired = 0;
        while (acc >= kTickS - 1e-15 && n < last) {
            acc -= kTickS;
            ++n;
            ++fired;
        }
        if (acc < 0.0) {
            acc = 0.0;
        }
        const int i = std::clamp(n - 1, 0, last);
        const int j = std::clamp(n, 0, last);
        const double a = std::clamp(acc / kTickS, 0.0, 1.0);
        const double lerp_y = ticks[i].y + (ticks[j].y - ticks[i].y) * a + eye_height;
        T += dt;

        const double kink_eye = kink_feet + kink_eye_height;
        if (kink_inside) {
            f.update(ticks[i].x, lerp_y - eye_height, ticks[i].z, eye_height, dt, kink_eye, kink_dt);
        } else {
            f.update(ticks[i].x, lerp_y - eye_height, ticks[i].z, eye_height, dt);
        }
        const double new_y = f.y();
        const double old_y = lerp_y;
        if (have_prev) {
            r.old_max_jump = std::max(r.old_max_jump, std::abs(old_y - prev_old));
            r.new_max_jump = std::max(r.new_max_jump, std::abs(new_y - prev_new));
        }
        prev_new = new_y;
        prev_old = old_y;
        have_prev = true;
        r.worst_lerp_gap = std::max(r.worst_lerp_gap, std::abs(new_y - old_y));
        r.rows.push_back({T, lerp_y, old_y, new_y});
        for (std::size_t k = 0; k < ticks.size(); ++k) {
            if (ticks[k].stepped) {
                r.stepped = true;
                r.step_used = ticks[k].step_used;
            }
        }
    }
    return r;
}

void dump(const std::string &name, const Result &r, double from, double to, bool verbose) {
    printf("\n=== %s ===\n", name.c_str());
    printf("step-assist fired: %s%s\n", r.stepped ? "YES" : "no",
           r.stepped ? (" (lift " + std::to_string(r.step_used) + ")").c_str() : "");
    printf("max per-frame camera dY: OLD(hard follow) %.6f   NEW(spring) %.6f", r.old_max_jump, r.new_max_jump);
    if (r.new_max_jump > 0) {
        printf("   ratio %.2fx", r.old_max_jump / r.new_max_jump);
    }
    printf("\nworst |spring - hard-follow| = %.6f blocks\n", r.worst_lerp_gap);
    if (!verbose) {
        return;
    }
    printf("       t     hard-follow     spring      dOLD      dNEW\n");
    double po = 0.0, pn = 0.0;
    bool hp = false;
    for (const auto &row : r.rows) {
        if (row.t < from || row.t > to) {
            continue;
        }
        const double dold = hp ? std::abs(row.old_y - po) : 0.0;
        const double dnew = hp ? std::abs(row.new_y - pn) : 0.0;
        printf("  %7.4f  %12.6f  %10.6f  %8.5f  %8.5f\n", row.t, row.old_y, row.new_y, dold, dnew);
        po = row.old_y;
        pn = row.new_y;
        hp = true;
    }
}

} // namespace

int main(int argc, char **argv) {
    const bool verbose = argc > 1 && std::string(argv[1]) == "-v";
    const double fps = argc > 2 ? std::stod(argv[2]) : 60.0;

    // ── S1: walk into a 0.5 riser -- the T-D8 step-assist path ─────────────
    {
        PlayerState s;
        s.position = {2.5, 64.0, 0.5};
        s.on_ground = true;
        s.fall_peak_y = 64.0;
        const auto ticks = simulate(terrain(0.5), s, 44, true, -1);
        const Result r = replay(ticks, fps, kEyeStanding, false);
        dump("S1 walk into a 0.5 riser (step-assist fires)", r, 1.60, 1.95, verbose);
    }
    // ── S2: walk into a full block (step_height 0.6 refuses) ───────────────
    {
        PlayerState s;
        s.position = {2.5, 64.0, 0.5};
        s.on_ground = true;
        s.fall_peak_y = 64.0;
        const auto ticks = simulate(terrain(1.0), s, 44, true, -1);
        const Result r = replay(ticks, fps, kEyeStanding, false);
        dump("S2 walk into a 1.0 block (no step, blocked)", r, 1.60, 1.95, false);
    }
    // ── S3: jump on the spot (the reachable in-game vertical motion) ───────
    {
        PlayerState s;
        s.position = {2.5, 64.0, 0.5};
        s.on_ground = true;
        s.fall_peak_y = 64.0;
        const auto ticks = simulate(terrain(0.0), s, 40, false, 3);
        const Result r = replay(ticks, fps, kEyeStanding, false);
        dump("S3 jump on the spot", r, 0.10, 0.60, verbose);
    }
    // ── S4: fall 3 blocks and land ─────────────────────────────────────────
    {
        PlayerState s;
        s.position = {2.5, 67.0, 0.5};
        s.on_ground = false;
        s.fall_peak_y = 67.0;
        const auto ticks = simulate(terrain(0.0), s, 40, false, -1);
        const Result r = replay(ticks, fps, kEyeStanding, false);
        dump("S4 fall 3 blocks and land", r, 0.20, 0.60, verbose);
    }
    printf("\n(all scenarios at %.0f fps)\n", fps);
    return 0;
}
