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
    // Airborne horizontal retention (chosen after docs/research/01 §1.2:
    // MC air momentum 0.91/tick — jumps carry speed, they don't brake).
    double air_drag = 0.91;
    // Multiplier on the walking acceleration available while airborne. With
    // accel = walk_speed * (1 - air_drag) * air_control the AIR steady state
    // is exactly walk_speed: airborne you can maintain walking pace but not
    // gain on it, and a sprint decays toward walk speed mid-jump (MC-like).
    // The acceleration magnitude itself is ~10x weaker than ground (drag 0.91
    // vs 0.9 but target walked from walk_speed, not the mode speed).
    double air_control = 1.0;

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
