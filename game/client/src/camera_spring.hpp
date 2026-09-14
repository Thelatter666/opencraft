#pragma once

#include <algorithm>
#include <cmath>

// T-D13: render-camera vertical spring (docs/research/09 §5.1). Pure math so
// headless tests can assert critical damping, rise time, frame-rate invariance
// and the ground invariant; main.cpp only wires the state in.
//
// WHY: the physics step-assist (T-D8) lifts the player's AABB by up to 0.6
// blocks inside ONE tick (engine/physics/src/player_physics.cpp:617). The
// camera hard-followed the physics position, so that lift arrived as a
// single-frame jump -- the user's "像瞬移上去的一样". This filters the vertical
// component only.
//
// ── SCOPE: vertical only ────────────────────────────────────────────────────
// X/Z are deliberately NOT filtered. A spring on the horizontal axes would lag
// mouse and strafe input (docs/research/09 §5.1 describes a *vertical*
// decoupling only). `CameraFilter` copies X/Z through untouched, so "no
// horizontal lag" is structural rather than tuned.
//
// ── EQUATION (docs/research/09 §5.1) ────────────────────────────────────────
//     y'' + 2 ζ ω y' + ω² (y - r) = 0        r = physics-derived eye target
// The damping ratio is critical, ζ = 1.0, which removes oscillation and
// overshoot: the step response is 1 - (1 + ωt)e^{-ωt}, monotone and never
// crossing the target exactly once.
//
// ── WHY THE EXACT SOLUTION AND NOT EULER ────────────────────────────────────
// The task card suggested semi-implicit Euler. Measured, that integrator is
// UNSTABLE at the frame rates this client actually runs at: with ω = 26 rad/s
// and dt = 1/30 s the product ω·dt = 0.867 sits outside the stability region,
// and the step response diverges to -9562 blocks with a +8446 block overshoot
// (report §3). A critically damped spring is exactly solvable, so `advance()`
// uses the closed form: unconditionally stable, monotone and overshoot-free for
// ANY dt (verified from 8 fps up), reproducing the continuous solution to
// ~1e-12. The cost is one exp() per piece, unmeasurable next to a frame of
// rendering.
//
// For a target that moves linearly at slope m over the piece -- and the client's
// partial-tick LERP is piecewise linear, so this is the exact input shape:
//     x = y - r,   x'' + 2ω x' + ω² x = -2 ω m
//     particular   x_p = -2m/ω
//     homogeneous  (A + B t) e^{-ωt}
// which is the closed form below. With m = 0 it reduces to the step response.
//
// ── OMEGA (the one tuning number) ───────────────────────────────────────────
// The card asks for a 0.15-0.25 s visual transition on a 0.6-block lift. For a
// step the 95% rise time is u/ω, with u the root of (1 + u)e^{-u} = 0.05, i.e.
// u = 4.7439. At ω = 26 rad/s that is 0.1825 s, and measured through the
// client's 50 ms partial-tick ramp the 95% point lands at 0.205-0.213 s --
// inside the window with margin at both ends. ω = 26 was picked from that sweep
// rather than guessed: ω = 22 gives 0.246 s (slow edge, reads floaty) and
// ω = 30 gives 0.188 s (fast edge, thin margin).

namespace opencraft::client {

// Damping ratio. The closed form below IS the critically damped case; the
// static_assert in `advance` keeps a future edit from silently invalidating it.
inline constexpr double kCameraSpringZeta = 1.0;
// Natural frequency in rad/s. See the omega note above for how 26 was chosen.
inline constexpr double kCameraSpringOmega = 26.0;
// Upper bound on the camera's distance from its target, in blocks. Two
// constraints fix this value, and they leave a usable window:
//
//   LOWER: measured, the unclamped lag on a 0.6 lift reaches 0.567 blocks
//   (60 fps; see report §3). The bound must sit clear of that, or the clamp
//   would bind on the very motion this filter exists to smooth and flatten the
//   S-curve into a constant-offset linear follow.
//
//   UPPER: the target is the EYE height above the feet (1.62 standing, 1.27
//   sneaking), so the camera is (eye_height - bound) above the feet at worst.
//   The bound must stay below 1.27 to keep the camera above the surface it
//   stands on for every pose.
//
// 0.9 sits inside (0.567, 1.27) with margin on both sides: 0.33 clear of the
// step's own lag, and 0.37 of eye height to spare in the sneaking pose (the
// tight case). Unclamped, a terminal-velocity fall (3.92 blocks/tick =
// 78.4 blocks/s) would park the camera 2v/ω ≈ 6.0 blocks above the player for
// the whole descent; 0.9 keeps that to under a block.
inline constexpr double kCameraSpringMaxLag = 0.9;
// Longest real-time piece fed to one update. The closed form cannot go
// unstable, so this is a policy bound (how far one frame may move the camera
// after a stall), not a stability fix.
inline constexpr double kCameraSpringMaxDt = 0.1;
// Fastest target motion the filter must track without snapping, in blocks per
// second: a terminal-velocity fall, gravity·drag/(1−drag) = 3.92 blocks/tick.
// A CLAMPED frame can therefore move the target by at most
// kCameraSpringMaxDt × this = 0.784 blocks, so anything well above that is a
// reposition rather than motion (see kCameraSpringSnapDistance).
inline constexpr double kCameraSpringMaxTrackedSpeed = 3.92 * 20.0;
// Target discontinuity treated as a reposition and snapped rather than
// filtered, in blocks. Derived from the physical bound above with generous
// margin (20 blocks ≈ 25× the largest motion one clamped frame can contain)
// while staying far below any real spawn / world-load / teleport jump, which
// are hundreds of blocks. Anything smaller would misfire on an ordinary fast
// fall: at 30 fps a terminal fall advances the target ~1.3 blocks per 33 ms
// frame, and the filter is allowed to lag that by kCameraSpringMaxLag, so a
// threshold near 1-2 blocks would snap on every fast fall instead of smoothing
// it.
inline constexpr double kCameraSpringSnapDistance = 20.0;

// Root of (1 + u)e^{-u} = 0.05: the unit step's 95% rise time is u/ω.
inline constexpr double kCameraSpringRiseRoot95 = 4.743922;

[[nodiscard]] inline double camera_spring_rise_time_95(double omega) {
    return kCameraSpringRiseRoot95 / omega;
}

// Critically damped spring-damper on one axis. Copyable value type; the caller
// owns the instance (same convention as the physics config).
struct CameraSpring {
    // Filtered coordinate (blocks).
    double y = 0.0;
    // Its rate of change (blocks/second).
    double velocity = 0.0;

    // Drop the transient: adopt `target` at once with zero velocity. Called on
    // spawn / world load so the camera never glides in from a stale position.
    void reset(double target) {
        y = target;
        velocity = 0.0;
    }

    // Advance one piece, the target moving linearly from `r0` to `r1` over
    // `dt` seconds.
    void advance(double r0, double r1, double dt) {
        static_assert(kCameraSpringZeta == 1.0,
                      "advance() implements the critically damped case; another ratio needs its own solution");
        if (!(dt > 0.0) || !std::isfinite(r1)) {
            return; // a stalled or NaN clock must not move the camera
        }
        dt = std::min(dt, kCameraSpringMaxDt);
        // A step far too large for the filter is a reposition, not motion.
        if (std::abs(y - r1) > kCameraSpringSnapDistance) {
            reset(r1);
            return;
        }

        const double w = kCameraSpringOmega;
        const double m = (r1 - r0) / dt; // target slope over this piece
        const double x_p = -2.0 * m / w; // particular solution
        const double a = (y - r0) - x_p; // homogeneous coefficients
        const double b = (velocity - m) + w * a;
        const double e = std::exp(-w * dt);

        y = r1 + x_p + (a + b * dt) * e;
        velocity = m + ((velocity - m) - w * b * dt) * e;

        clamp_lag(r1);
    }

    // Advance one render frame whose target path BENDS once, `dt_kink` into the
    // frame: the target runs r0 -> rk over dt_kink, then rk -> r1 over the rest.
    //
    // The client's partial-tick LERP is exactly this shape (research/09 §1.1):
    // a linear ramp toward the tick's state, bending where the tick lands. Both
    // pieces are known at the frame that straddles the boundary, so reproducing
    // the bend rather than chording across it makes the trajectory EXACTLY
    // frame-rate independent (measured: identical to 1e-9 at coincident times,
    // against ~7e-3 blocks for a single chording ramp). A frame with no tick
    // inside uses the single-piece overload instead.
    void advance(double r0, double rk, double dt_kink, double r1, double dt) {
        if (!(dt > 0.0) || !std::isfinite(r1)) {
            return;
        }
        // Degenerate splits fall back to one piece: no kink inside the frame, a
        // kink at either end, or a frame long enough that the clamp would cut
        // the first piece short (a stall -- the bend is not worth modelling).
        dt = std::min(dt, kCameraSpringMaxDt);
        if (!(dt_kink > 0.0) || dt_kink >= dt) {
            advance(r0, r1, dt);
            return;
        }
        advance(r0, rk, dt_kink);
        advance(rk, r1, dt - dt_kink);
    }

private:
    // Bound the tracking error. Pinning the position alone would leave
    // forbidden energy in `velocity`, so the component pointing away from the
    // target is dropped with it.
    void clamp_lag(double target) {
        const double offset = y - target;
        if (offset > kCameraSpringMaxLag) {
            y = target + kCameraSpringMaxLag;
            velocity = std::min(velocity, 0.0);
        } else if (offset < -kCameraSpringMaxLag) {
            y = target - kCameraSpringMaxLag;
            velocity = std::max(velocity, 0.0);
        }
    }
};

// The camera position for one render frame: X/Z pass through exactly, the eye Y
// runs through the spring. Kept here rather than inline in main.cpp so that
// "no horizontal lag" and "never below the target" are asserted by unit tests
// on the real code path.
struct CameraFilter {
    CameraSpring vertical;
    // Eye target handed to the spring last update, so the target can be
    // described as the linear ramp it actually is between frames.
    double target_y = 0.0;

    // Place the camera at its target immediately (no transient). The spawn and
    // world-load entry point.
    void reset(double px, double feet_y, double pz, double eye_height) {
        world_x = px;
        world_z = pz;
        target_y = feet_y + eye_height;
        vertical.reset(target_y);
    }

    // px/pz/feet_y are the interpolated physics values for this frame, eye_height
    // the pose-dependent eye offset, dt the real frame duration in seconds.
    void update(double px, double feet_y, double pz, double eye_height, double dt) {
        update_impl(px, feet_y, pz, eye_height, dt, 0.0, 0.0, false);
    }

    // Same, for a frame that straddles a physics tick: the eye target is
    // `mid_eye_y` at `mid_dt` seconds into the frame (the tick boundary), which
    // is where the partial-tick ramp bends.
    void update(double px, double feet_y, double pz, double eye_height, double dt, double mid_eye_y, double mid_dt) {
        update_impl(px, feet_y, pz, eye_height, dt, mid_eye_y, mid_dt, true);
    }

    // Interpolated physics X/Z, passed through exactly (never filtered).
    [[nodiscard]] double x() const { return world_x; }

    [[nodiscard]] double z() const { return world_z; }

    // Filtered eye height.
    [[nodiscard]] double y() const { return vertical.y; }

private:
    void update_impl(double px, double feet_y, double pz, double eye_height, double dt, double mid_eye_y, double mid_dt,
                     bool has_mid) {
        world_x = px; // horizontal pass-through: exact, by construction
        world_z = pz;
        const double next = feet_y + eye_height;
        if (has_mid) {
            vertical.advance(target_y, mid_eye_y, mid_dt, next, dt);
        } else {
            vertical.advance(target_y, next, dt);
        }
        target_y = next;
    }

    double world_x = 0.0;
    double world_z = 0.0;
};

} // namespace opencraft::client
