#pragma once

namespace opencraft::physics {

// Central knob board for the tick-level player simulation. Every tunable
// constant lives here; the defaults ARE the spec values (docs/01 §2, marked
// ⚖ there). Units: positions in blocks, velocities in blocks/tick
// (multiply by 20 for m/s), accelerations in blocks/tick².
//
// Horizontal movement model (docs/research/01 §1.1): per tick
//     v = (v * drag) + dir * accel,   accel = target_speed * (1 - drag)
// so the steady-state speed is exactly target_speed:
//     v* = accel / (1 - drag) = target_speed.
// ground_drag = 0.9 is a chosen feel constant (≈0.5 s to reach ~99% of the
// target and to stop after releasing input); it does not affect the
// steady-state values, which are the ⚖ quantities. Derivation + verification
// windows are recorded in the T007 developer report.
struct PhysicsConfig {
    // ⚖ docs/01 §2: walk 4.317 m/s → 0.21585 blocks/tick.
    double walk_speed = 4.317 / 20.0;
    // ⚖ docs/01 §2: sprint 5.612 m/s → 0.2806 blocks/tick.
    double sprint_speed = 5.612 / 20.0;
    // ⚖ docs/01 §2: sneak 1.295 m/s → 0.06475 blocks/tick.
    double sneak_speed = 1.295 / 20.0;

    // Per-tick horizontal velocity retention while on ground (chosen, see
    // struct comment). Steady state is drag-independent by construction.
    double ground_drag = 0.9;
    // Airborne horizontal retention (docs/research/05 §4, mcpk.wiki
    // Horizontal Movement Formulas, accessed 2026-09-14): MC air momentum is
    // 0.91/tick — jumps carry speed, they don't brake.
    double air_drag = 0.91;
    // Airborne acceleration, FIXED per tick regardless of walk/sprint mode
    // (docs/research/05 §4: mcpk.wiki Horizontal Movement Formulas, accessed
    // 2026-09-14 — air accel is 0.02 blocks/tick² in MC, not tied to the
    // speed attribute). Air steady state = 0.02 / (1 − 0.91) = 0.2222
    // blocks/tick ≈ 4.444 m/s, slightly ABOVE walk speed: a sprint jump
    // decays toward 4.444 m/s mid-air instead of down to walking pace
    // (T007's walk-anchored air model, replaced here per T-D1 research).
    double air_accel = 0.02;

    // Sprint-jump impulse added to horizontal velocity along the FACING
    // direction at the jump tick when sprinting (docs/research/05 §3: MC
    // applies +0.2 blocks/tick, mcpk.wiki Sprinting, accessed 2026-09-14).
    //
    // ── CALIBRATION VALUE for this engine's simplified pipeline ───────────
    // 0.1842 is NOT MC's raw constant; it is the value that makes the ⚖
    // observables come out right through OUR pipeline (ground_drag 0.9 instead
    // of MC's 0.546 friction, and a fixed air_accel expressed in the uniform
    // "steady state" form). Chosen as the 4-decimal value maximising the
    // smaller of the two normalised ⚖ margins on a measured scan; it yields:
    //     arc average 7.1867 m/s (+0.84%, inside the ⚖ 7.127 ±1% band)
    //     gap clearance 3.7120 blocks (inside the ⚖ 3.7–4.3 band; a
    //     walk jump clears 2.016 by the same measure)
    // The two bands are affinely locked over a 12-move window
    // (clearance = 0.6·avg − 0.6), so 7.127 and "4.03 clearance" cannot both
    // be hit; the measured feasible band on this constant is
    // [0.18237, 0.18525]. Full scan, feasible interval and the lock algebra:
    // docs/tasks/T-D1.report.md §3.
    //
    // ── RETIREMENT CONDITION ─────────────────────────────────────────────
    // This constant retires in T-D7 (MC friction-pipeline migration, RG-R5).
    // Once friction is 0.546/0.91 with input decay 0.98 and the 0.005
    // momentum threshold, MC's RAW +0.2 blocks/tick must hit 7.127 ±1% by
    // itself; T-D7's acceptance criterion is exactly that. Do not carry
    // 0.1840 across that migration.
    double sprint_jump_boost = 0.1842;

    // Sprint state machine constants (docs/research/05 §1):
    // double-tap-forward window in ticks (MC arms sprintToggleTimer = 7,
    // mcpk.wiki Sprinting, accessed 2026-09-14; a second press inside the
    // window engages sprint, one tick later re-arms instead).
    int sprint_toggle_window_ticks = 7;
    // ⚖ docs/research/01 §1.1 + wiki Sprinting (accessed 2026-09-14):
    // sprinting requires hunger > 6 (cannot engage or maintain at ≤ 6).
    double sprint_min_hunger = 6.0;

    // ⚖ docs/01 §2: jump initial velocity 0.42 blocks/tick, apex 1.2522
    // blocks with the gravity/drag pair below (move-then-integrate order).
    double jump_velocity = 0.42;
    // ⚖ docs/01 §2: gravity 0.08 blocks/tick², applied after the move as
    // vy = (vy - gravity) * vertical_drag.
    double gravity = 0.08;
    // ⚖ docs/01 §2: vertical velocity damping ×0.98 per tick. Terminal
    // velocity is derived: gravity * vertical_drag / (1 - vertical_drag)
    // = 3.92 blocks/tick = 78.4 m/s (approached from below).
    double vertical_drag = 0.98;

    // Water is "good enough" per the T007 card (docs/research/01 §1.4 puts MC
    // swimming around 2 m/s): reduced horizontal target (≈2.16 m/s), strong
    // vertical drag, mild buoyant gravity, and a swim-up accel while jump is
    // held. All chosen approximations; precise swim feel is deferred.
    double water_speed_mult = 0.5;
    double water_drag = 0.8;
    double water_gravity = 0.02;
    double swim_up_accel = 0.05;
    double water_max_up_speed = 0.15;

    // ⚖ docs/01 §2: fall damage = floor(fall_distance - 3) HP.
    double fall_damage_offset = 3.0;

    // Collision substep bound (docs/research/03 §6.2): displacement is split
    // into substeps of at most this many blocks to prevent tunneling.
    double max_substep = 0.5;
    // Sneak edge protection backoff granularity per iteration (chosen,
    // MC-style ≤0.05 shrink steps).
    double sneak_edge_step = 0.05;
    // Depth probed below the feet when re-verifying ground support.
    double ground_probe_depth = 0.01;
};

} // namespace opencraft::physics
