#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "camera_spring.hpp"

using opencraft::client::camera_spring_rise_time_95;
using opencraft::client::CameraFilter;
using opencraft::client::CameraSpring;
using opencraft::client::kCameraSpringMaxDt;
using opencraft::client::kCameraSpringMaxLag;
using opencraft::client::kCameraSpringOmega;
using opencraft::client::kCameraSpringRiseRoot95;
using opencraft::client::kCameraSpringSnapDistance;
using opencraft::client::kCameraSpringZeta;

namespace {

// ⚖ T-D8 player step height: the single-tick lift this filter exists to smooth.
constexpr double kStepHeight = 0.6;
// Client tick period (20 TPS, docs/03 §1).
constexpr double kTick = 0.05;
// The two eye heights from game/client/src/main.cpp.
constexpr double kEyeStanding = 1.62;
constexpr double kEyeSneaking = 1.27;
// Ground the sequence walks on.
constexpr double kGround = 64.0;

// Feet y at each tick boundary. Flat, then `step` from `step_tick` on: the
// physics applies the whole lift in ONE tick, exactly as T-D8 does.
std::vector<double> step_sequence(int ticks, int step_tick, double step) {
    std::vector<double> y(static_cast<std::size_t>(ticks) + 1, kGround);
    for (int i = step_tick; i <= ticks; ++i) {
        y[static_cast<std::size_t>(i)] = kGround + step;
    }
    return y;
}

constexpr std::size_t idx(int i) {
    return static_cast<std::size_t>(i);
}

// Replicates the client main loop's camera path: a fixed-step clock fires
// whole 20 TPS ticks at frame boundaries, the render target is the partial-tick
// LERP, and the spring is told where that LERP bends when a tick lands inside
// the frame. Everything below drives this, so the tests exercise the same
// update sequence main.cpp does.
struct Loop {
    const std::vector<double> &feet;
    double eye_height = kEyeStanding;
    int n = 0;        // ticks executed
    double acc = 0.0; // seconds accumulated toward the next tick

    [[nodiscard]] double feet_at(int i) const { return feet[idx(std::clamp(i, 0, static_cast<int>(feet.size()) - 1))]; }

    // The LERP target at the current clock position, in eye coordinates.
    [[nodiscard]] double lerp_eye() const {
        const double alpha = std::clamp(acc / kTick, 0.0, 1.0);
        return feet_at(n - 1) + (feet_at(n) - feet_at(n - 1)) * alpha + eye_height;
    }

    // Advance `dt` and return one frame of camera motion.
    void frame(CameraFilter &f, double dt) {
        const double alpha_before = std::clamp(acc / kTick, 0.0, 1.0);
        // Where the pending tick will bend the ramp: the value it installs.
        const double kink_eye = feet_at(n) + eye_height;
        const double kink_dt = (1.0 - alpha_before) * kTick;

        acc += dt;
        int fired = 0;
        while (acc >= kTick - 1e-15 && n < static_cast<int>(feet.size()) - 1) {
            acc -= kTick;
            ++n;
            ++fired;
        }
        if (acc < 0.0) {
            acc = 0.0;
        }

        const double end_eye = lerp_eye();
        if (fired == 1 && kink_dt > 0.0 && kink_dt < dt) {
            f.update(0.0, end_eye - eye_height, 0.0, eye_height, dt, kink_eye, kink_dt);
        } else {
            f.update(0.0, end_eye - eye_height, 0.0, eye_height, dt);
        }
    }
};

struct Measured {
    std::vector<double> t;
    std::vector<double> y;
    double rise95 = -1.0;
    double overshoot = 0.0;
    bool monotone = true;
    double max_per_frame = 0.0;
    double worst_lag = 0.0;
};

// Run the loop for `end_time` at `fps`, measuring the eye-target transition.
Measured measure(const std::vector<double> &feet, double fps, double eye_height, double end_time, double step_start) {
    Measured m;
    CameraFilter f;
    f.reset(0.0, feet[0], 0.0, eye_height);
    Loop loop{feet, eye_height};
    const double dt = 1.0 / fps;
    const double base_eye = kGround + eye_height;
    const double top_eye = kGround + kStepHeight + eye_height;
    double T = 0.0;
    double previous = f.y();
    while (T < end_time - 1e-12) {
        const double h = std::min(dt, end_time - T);
        loop.frame(f, h);
        T += h;
        if (m.t.empty() || T > m.t.back() + 1e-12) {
            m.t.push_back(T);
            m.y.push_back(f.y());
            if (T < step_start + 1.5 && f.y() < previous - 1e-12) {
                m.monotone = false;
            }
            m.max_per_frame = std::max(m.max_per_frame, std::abs(f.y() - previous));
            previous = f.y();
            if (m.rise95 < 0.0 && f.y() >= base_eye + kStepHeight * 0.95) {
                m.rise95 = T - step_start;
            }
            m.overshoot = std::max(m.overshoot, f.y() - top_eye);
            m.worst_lag = std::max(m.worst_lag, std::abs(f.y() - loop.lerp_eye()));
        }
    }
    return m;
}

} // namespace

// ── Acceptance 1: critical damping is assertable (monotone, no overshoot) ───

TEST_CASE("camera spring: the damping ratio is critical by construction") {
    CHECK(kCameraSpringZeta == 1.0);
    CHECK(kCameraSpringOmega > 0.0);
}

TEST_CASE("camera spring: a step is approached monotonically and never crossed") {
    // Includes 8 fps to show the closed form has no stability limit, unlike the
    // semi-implicit Euler the card suggested (which diverges at 30 fps here).
    for (double fps : {8.0, 15.0, 30.0, 60.0, 144.0, 1000.0}) {
        CameraSpring s;
        s.reset(0.0);
        const double dt = 1.0 / fps;
        double previous = s.y;
        double peak = s.y;
        bool monotone = true;
        for (int i = 0; i < static_cast<int>(fps * 4.0); ++i) {
            s.advance(kStepHeight, kStepHeight, dt);
            if (s.y < previous - 1e-12) {
                monotone = false;
            }
            previous = s.y;
            peak = std::max(peak, s.y);
        }
        CAPTURE(fps);
        CHECK(monotone);
        CHECK(peak <= kStepHeight + 1e-12);
        CHECK(s.y == doctest::Approx(kStepHeight).epsilon(1e-9));
    }
}

TEST_CASE("camera spring: the damping ratio leaves no oscillation about the target") {
    CameraSpring s;
    s.reset(0.0);
    bool crossed = false;
    for (int i = 0; i < 4000; ++i) {
        s.advance(kStepHeight, kStepHeight, 1.0 / 240.0);
        if (s.y > kStepHeight + 1e-12) {
            crossed = true;
        }
    }
    CHECK_FALSE(crossed);
}

TEST_CASE("camera spring: the analytic update is exact for any dt") {
    // One coarse step must equal many fine ones: the solution is closed form,
    // so the frame rate cannot change the trajectory.
    CameraSpring coarse;
    coarse.reset(0.0);
    coarse.advance(kStepHeight, kStepHeight, 1.0 / 30.0);

    CameraSpring fine;
    fine.reset(0.0);
    for (int i = 0; i < 30000; ++i) {
        fine.advance(kStepHeight, kStepHeight, (1.0 / 30.0) / 30000.0);
    }
    CHECK(coarse.y == doctest::Approx(fine.y).epsilon(1e-9));
    CHECK(coarse.velocity == doctest::Approx(fine.velocity).epsilon(1e-6));
}

// ── Acceptance 2: the 0.6 lift resolves inside 0.15-0.25 s ──────────────────

TEST_CASE("camera spring: the closed form rise time sits inside the target transition window") {
    const double rise = camera_spring_rise_time_95(kCameraSpringOmega);
    CHECK(rise > 0.15);
    CHECK(rise < 0.25);
    CHECK(rise == doctest::Approx(kCameraSpringRiseRoot95 / kCameraSpringOmega));
}

TEST_CASE("camera spring: a 0.6 lift resolves inside the target transition window at 30, 60 and 144 fps") {
    // Measured through the client's real 50 ms partial-tick ramp, not just the
    // pure-step idealisation: the ramp itself already takes one tick.
    const std::vector<double> feet = step_sequence(60, 6, kStepHeight);
    const double step_start = 6.0 * kTick;
    for (double fps : {30.0, 60.0, 144.0}) {
        const Measured m = measure(feet, fps, kEyeStanding, 2.5, step_start);
        CAPTURE(fps);
        CAPTURE(m.rise95);
        CHECK(m.rise95 > 0.15);
        CHECK(m.rise95 < 0.25);
    }
}

TEST_CASE("camera spring: the lift is smeared over many frames, not taken in one") {
    // This is the user-visible fix. The old hard-follow camera moved the whole
    // 0.6 within a single frame; the filtered camera must never take a step
    // anywhere near that large.
    const std::vector<double> feet = step_sequence(60, 6, kStepHeight);
    for (double fps : {30.0, 60.0, 144.0}) {
        const Measured m = measure(feet, fps, kEyeStanding, 2.5, 6.0 * kTick);
        CAPTURE(fps);
        CAPTURE(m.max_per_frame);
        CHECK(m.max_per_frame < 0.25); // vs 0.6 for the unfiltered camera
        CHECK(m.monotone);
        CHECK(m.overshoot <= 1e-12);
    }
}

TEST_CASE("camera spring: a 0.6 lift is smoothed at least as much as an unfiltered camera") {
    // Sanity that the assertion above has teeth: the unfiltered value really is
    // 0.6 in one frame, so a threshold of 0.25 is a real constraint.
    const double unfiltered_single_frame_jump = kStepHeight;
    CHECK(unfiltered_single_frame_jump > 0.25);
}

// ── Acceptance 3: horizontal passes through exactly ─────────────────────────

TEST_CASE("camera filter: X and Z are the interpolated physics values exactly") {
    CameraFilter f;
    f.reset(1.0, kGround, 2.0, kEyeStanding);
    const double xs[] = {-123.456789, 0.0, 3.5, 1000000.25};
    const double zs[] = {987.654321, -0.125, 0.0, -1000000.75};
    for (std::size_t i = 0; i < 4; ++i) {
        for (double dt : {1.0 / 144.0, 1.0 / 60.0, 1.0 / 30.0, 0.1}) {
            f.update(xs[i], kGround + 0.3, zs[i], kEyeStanding, dt);
            CHECK(f.x() == xs[i]); // bit-exact: no spring, no easing
            CHECK(f.z() == zs[i]);
        }
    }
}

TEST_CASE("camera filter: a horizontal jump reaches the camera on the same frame") {
    // A spring on X/Z would smear this over several frames and read as input
    // lag; the horizontal axes must not be filtered at all.
    CameraFilter f;
    f.reset(0.0, kGround, 0.0, kEyeStanding);
    f.update(50.0, kGround, -50.0, kEyeStanding, 1.0 / 60.0);
    CHECK(f.x() == 50.0);
    CHECK(f.z() == -50.0);
}

// ── Acceptance 4: the camera never sinks below its target (no ground clip) ──

TEST_CASE("camera spring: the tracking error never exceeds the configured lag") {
    // Adversarial: random target thrash with random frame times, including
    // stalls longer than the dt clamp.
    unsigned long long rng = 0x9E3779B97F4A7C15ULL;
    const auto next = [&rng]() {
        rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((rng >> 11) & 0xFFFFFFU) / static_cast<double>(0x1000000U);
    };
    double worst = 0.0;
    for (int trial = 0; trial < 40; ++trial) {
        CameraSpring s;
        double target = kGround + kEyeStanding;
        s.reset(target);
        for (int i = 0; i < 1000; ++i) {
            target += (next() - 0.5) * 2.0;
            s.advance(target, target, 0.001 + next() * 0.2);
            worst = std::max(worst, std::abs(s.y - target));
        }
    }
    CAPTURE(worst);
    CHECK(worst <= kCameraSpringMaxLag + 1e-9);
}

TEST_CASE("camera spring: the lag bound keeps the eye above the surface for every pose") {
    // The target is the eye height above the feet, so a lag of L leaves the
    // camera (eye_height - L) above the feet. It must stay positive or the
    // camera would clip into the floor it is standing on.
    CHECK(kEyeStanding - kCameraSpringMaxLag > 0.0);
    CHECK(kEyeSneaking - kCameraSpringMaxLag > 0.0);
}

TEST_CASE("camera spring: the lag bound is clear of the step it must not clamp") {
    // Two-sided constraint. Below: the unclamped lag on a 0.6 lift reaches
    // ~0.57 blocks, so a tighter bound would flatten the S-curve it exists to
    // produce. Above: the bound has to stay under the sneaking eye height.
    const std::vector<double> feet = step_sequence(60, 6, kStepHeight);
    double worst = 0.0;
    for (double fps : {30.0, 60.0, 144.0}) {
        worst = std::max(worst, measure(feet, fps, kEyeStanding, 2.5, 6.0 * kTick).worst_lag);
    }
    CAPTURE(worst);
    CAPTURE(kCameraSpringMaxLag);
    CHECK(worst < kCameraSpringMaxLag);        // the clamp never binds on a step
    CHECK(kCameraSpringMaxLag < kEyeSneaking); // and cannot clip the camera
}

TEST_CASE("camera spring: a long fall trails the player but stays inside the bound") {
    const double v_terminal = 3.92 * 20.0; // blocks per second
    const double feet_top = kGround + 40.0;
    const double land_time = 40.0 / v_terminal;
    for (double fps : {30.0, 60.0, 144.0}) {
        CameraSpring s;
        s.reset(feet_top + kEyeStanding);
        const double dt = 1.0 / fps;
        double t = 0.0;
        double worst_above = 0.0;
        double worst_below = 0.0;
        while (t < land_time + 1.0) {
            const double feet = t < land_time ? feet_top - v_terminal * t : kGround;
            const double target = feet + kEyeStanding;
            s.advance(target, target, dt);
            t += dt;
            worst_above = std::max(worst_above, s.y - target);
            worst_below = std::max(worst_below, target - s.y);
        }
        CAPTURE(fps);
        CAPTURE(worst_above);
        CAPTURE(worst_below);
        // On the way down the camera trails ABOVE the physics eye (the eye
        // cannot end up inside the ground), and both directions are bounded.
        CHECK(worst_above > 0.0);
        CHECK(worst_above <= kCameraSpringMaxLag + 1e-9);
        CHECK(worst_below <= kCameraSpringMaxLag + 1e-9);
        CHECK(s.y == doctest::Approx(kGround + kEyeStanding).epsilon(1e-6));
    }
}

// ── Acceptance 5: frame-rate independence ──────────────────────────────────

TEST_CASE("camera spring: 30, 60 and 144 fps agree at coincident simulated times") {
    // Six hertz is a common divisor of 30, 60 and 144, so k/6 is an exact frame
    // boundary at all three rates. The response is a linear time-invariant ODE
    // driven by a target that depends only on elapsed time, so an exact
    // integrator must give identical values there.
    const std::vector<double> feet = step_sequence(60, 6, kStepHeight);
    std::vector<Measured> runs;
    for (double fps : {30.0, 60.0, 144.0}) {
        runs.push_back(measure(feet, fps, kEyeStanding, 2.5, 6.0 * kTick));
    }
    const auto at = [](const Measured &m, double t) {
        for (std::size_t i = 0; i < m.t.size(); ++i) {
            if (std::abs(m.t[i] - t) < 1e-12) {
                return m.y[i];
            }
        }
        return std::nan("");
    };
    for (int k = 1; k <= 14; ++k) {
        const double t = k / 6.0;
        const double a = at(runs[0], t);
        const double b = at(runs[1], t);
        const double c = at(runs[2], t);
        if (std::isnan(a) || std::isnan(b) || std::isnan(c)) {
            continue;
        }
        CAPTURE(k);
        CAPTURE(t);
        CAPTURE(a);
        CAPTURE(b);
        CAPTURE(c);
        CHECK(a == doctest::Approx(b).epsilon(1e-9));
        CHECK(a == doctest::Approx(c).epsilon(1e-9));
    }
}

TEST_CASE("camera spring: a stalled clock cannot move the camera") {
    CameraSpring s;
    s.reset(kGround);
    const double y = s.y;
    s.advance(kGround + kStepHeight, kGround + kStepHeight, 0.0);
    CHECK(s.y == y);
    s.advance(kGround + kStepHeight, kGround + kStepHeight, -1.0);
    CHECK(s.y == y);
    s.advance(kGround + kStepHeight, kGround + kStepHeight, std::nan(""));
    CHECK(s.y == y);
}

TEST_CASE("camera spring: a frame longer than the clamp is truncated, not integrated in full") {
    CameraSpring truncated;
    truncated.reset(0.0);
    truncated.advance(kStepHeight, kStepHeight, 1.0);
    CameraSpring capped;
    capped.reset(0.0);
    capped.advance(kStepHeight, kStepHeight, kCameraSpringMaxDt);
    CHECK(truncated.y == doctest::Approx(capped.y));
    CHECK(truncated.y <= kStepHeight);
}

// ── Reset behaviour: the card's pause / world switch / teleport warning ─────

TEST_CASE("camera filter: reset adopts the target with no transient") {
    CameraFilter f;
    f.reset(10.0, kGround, -10.0, kEyeStanding);
    CHECK(f.y() == doctest::Approx(kGround + kEyeStanding));
    // A reset after a large jump must not glide in from the old position.
    f.reset(500.0, 200.0, 500.0, kEyeStanding);
    CHECK(f.y() == doctest::Approx(200.0 + kEyeStanding));
    CHECK(f.x() == 500.0);
    CHECK(f.z() == 500.0);
}

TEST_CASE("camera spring: a teleport snaps instead of gliding") {
    CameraSpring s;
    s.reset(kGround);
    s.advance(200.0, 200.0, 1.0 / 60.0);
    CHECK(s.y == doctest::Approx(200.0));
    CHECK(s.velocity == doctest::Approx(0.0));
}

TEST_CASE("camera spring: ordinary fast motion is never mistaken for a teleport") {
    // A terminal-velocity fall advances the target ~1.3 blocks per 30 fps frame,
    // and the filter may lag that by up to kCameraSpringMaxLag. The snap
    // threshold must sit well clear of both, or fast falls would snap instead of
    // smoothing.
    CHECK(kCameraSpringSnapDistance > 2.0 * (kCameraSpringMaxLag + 3.92 * 20.0 / 30.0));
    CameraSpring s;
    s.reset(kGround + kEyeStanding);
    const double dt = 1.0 / 30.0;
    for (int i = 0; i < 300; ++i) {
        const double a = kGround + kEyeStanding - i * 1.3067;
        const double b = a - 1.3067;
        s.advance(a, b, dt);
        CHECK(std::abs(s.y - b) <= kCameraSpringMaxLag + 1e-9);
    }
}

TEST_CASE("camera filter: a suspended simulation holds the camera still") {
    // With no ticks the eye target is constant; a frame with a very long dt
    // (pause, hitch) must not produce motion.
    CameraFilter f;
    f.reset(0.0, kGround, 0.0, kEyeStanding);
    for (int i = 0; i < 200; ++i) {
        f.update(0.0, kGround + kStepHeight, 0.0, kEyeStanding, 1.0 / 60.0);
    }
    const double settled = f.y();
    for (int i = 0; i < 120; ++i) {
        f.update(0.0, kGround + kStepHeight, 0.0, kEyeStanding, 1.0 / 60.0);
    }
    CHECK(f.y() == doctest::Approx(settled).epsilon(1e-9));
    f.update(0.0, kGround + kStepHeight, 0.0, kEyeStanding, 3.0);
    CHECK(f.y() == doctest::Approx(settled).epsilon(1e-6));
}

TEST_CASE("camera filter: a pose change is smoothed over several frames") {
    // Standing 1.62 to sneaking 1.27 changes the target by 0.35; running it
    // through the same channel keeps a crouch from popping.
    CameraFilter f;
    f.reset(0.0, kGround, 0.0, kEyeStanding);
    for (int i = 0; i < 200; ++i) {
        f.update(0.0, kGround, 0.0, kEyeStanding, 1.0 / 60.0);
    }
    const double before = f.y();
    f.update(0.0, kGround, 0.0, kEyeSneaking, 1.0 / 60.0);
    const double moved = std::abs(f.y() - before);
    CHECK(moved > 0.0);
    CHECK(moved < kEyeStanding - kEyeSneaking);
    for (int i = 0; i < 200; ++i) {
        f.update(0.0, kGround, 0.0, kEyeSneaking, 1.0 / 60.0);
    }
    CHECK(f.y() == doctest::Approx(kGround + kEyeSneaking).epsilon(1e-6));
}
