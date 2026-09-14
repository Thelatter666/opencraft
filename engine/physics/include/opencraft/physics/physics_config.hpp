#pragma once

#include <cstdint>

namespace opencraft::physics {

// Entity types the shared movement routine can be parameterised for. T-D7 only
// needs the two below; M2 adds the real content (boat / minecart / mob / arrow /
// item) by extending this enum together with `PhysicsConfig::for_entity`.
//
// The point of this enum is architectural: physics parameters are INSTANTIATED
// PER ENTITY TYPE, not carried in one global singleton. A falling block's
// gravity is half the player's (0.04 vs 0.08) and its damping is 0.98 instead
// of 0.91 (docs/research/07 §1.4); boats reach 72.73 m/s on blue ice (§7.4).
// One flat config shared by every entity cannot express that, which is why
// T-D7 is treated as a foundation decision rather than a player-only tweak.
enum class EntityKind : std::uint8_t {
    Player = 0,
    FallingBlock = 1,
};

// Central knob board for the tick-level movement simulation. Units: positions
// in blocks, velocities in blocks/tick (×20 for m/s), accelerations in
// blocks/tick².
//
// ── SCOPE: one instance per entity type ─────────────────────────────────────
// This is a plain copyable value and every routine in this module takes it as a
// parameter. Callers own their instance(s): one per entity type (see
// `for_entity`), or one per entity if a later card needs per-instance
// variation. Nothing here is a process-wide singleton, so two entities with
// different parameters cannot interfere.
//
// ── HORIZONTAL MODEL (T-D7): MC friction pipeline ───────────────────────────
// The pre-T-D7 model stored a target speed and wrote
//     v = v·drag + dir·target·(1 − drag)
// which makes the steady state exactly `target` — and therefore hides `drag`
// from every steady-state assertion. The 163-test suite was green while a 0.9
// ground retention made the player coast 1.92 blocks after key release. The
// model below is Minecraft's, where retention and acceleration are independent
// quantities (docs/research/06 §1.1/§2):
//
//     k      = horizontal_drag × S          (S = slipperiness below the feet)
//     v_move = v + a·dir                    (a from ground_accel/air_accel)
//     move by v_move
//     v      = v_move × k                    (damping AFTER the displacement)
//
// The steady state v* = a / (1 − k) is thus an emergent property rather than an
// input, and is sensitive to both k and a. `k` is multiplicatively decoupled
// into a block term (S) and an entity term (horizontal_drag) so that "one
// entity on different blocks" and "different entities on one block" are both
// expressible (docs/research/07 §7.2).
struct PhysicsConfig {
    // Identity of the entity type these parameters describe. Informational
    // only: no routine branches on it.
    EntityKind kind = EntityKind::Player;

    // ── Horizontal friction pipeline ────────────────────────────────────────
    // Entity air resistance — the entity-owned half of the retention factor.
    // ⚖ MC 1.8.9 air momentum retention is 0.91/tick (docs/research/06 §1.1
    // step 6). Ground retention is k = horizontal_drag × S = 0.546 on default
    // blocks. A falling block uses 0.98 instead; see `for_entity`.
    double horizontal_drag = 0.91;
    // Ground acceleration factor (MC's movement factor). MC applies
    // 0.1 × mode × (0.6/S)³ on the ground: the CUBE is load-bearing — it is
    // what makes ice "slower to reach speed, slower to lose it" rather than
    // simply faster (docs/research/06 §7.1). Replacing it with a linear
    // (0.6/S) gets the ice feel wrong.
    double ground_accel = 0.1;
    // Air acceleration: a fixed 0.02 × mode per tick, independent of speed
    // attribute and of the block below (docs/research/06 §2.2).
    double air_accel = 0.02;
    // Mode multipliers folded into the acceleration: sprint ×1.3, sneak ×0.3.
    // Note the deliberate asymmetry that produces 45° strafing: sneak scales
    // the INPUT vector while sprint scales the ACCELERATION, so a sneak-strafe
    // diagonal gains √2 while a sprint-strafe diagonal gains only 1/0.98
    // (docs/research/06 §3.2). Sneaking also cancels sprinting (MC), so the two
    // never multiply in practice.
    double sprint_multiplier = 1.3;
    double sneak_multiplier = 0.3;
    // Input pre-scale applied to the raw −1/0/1 axes BEFORE normalisation. Sole
    // cause of the 45° Strafe gain: (0.98, 0) has magnitude 0.98 < 1 and is
    // clamped up to 1, while (0.98, 0.98) has magnitude 1.386 and is not
    // (docs/research/06 §3.2).
    double input_scale = 0.98;
    // Per-component momentum cut-off: |v| below this snaps to 0, keeping the
    // tail of the glide finite. ⚖ 0.003 is the 1.9+ value and is exactly the
    // value docs/01 §2's 1.2522 jump apex is derived from — a 0.005 cut-off
    // yields 1.2492 and would break the spec (T-D7 ruling A-5). Deliberately
    // NOT Minestom's 1e-6 epsilon, which would erase the 1.8/1.9 apex
    // difference entirely (docs/research/07 §7.2).
    double momentum_threshold = 0.003;

    // ── Vertical ────────────────────────────────────────────────────────────
    // ⚖ docs/01 §2: jump initial velocity 0.42 blocks/tick, apex 1.2522 blocks
    // with the gravity/drag pair below (move-then-integrate order).
    double jump_velocity = 0.42;
    // ⚖ docs/01 §2: gravity 0.08 blocks/tick², applied after the move as
    // vy = (vy − gravity) × vertical_drag.
    double gravity = 0.08;
    // ⚖ docs/01 §2: vertical damping ×0.98/tick. Terminal velocity is derived:
    // gravity × vertical_drag / (1 − vertical_drag) = 3.92 blocks/tick
    // = 78.4 m/s (approached from below).
    double vertical_drag = 0.98;

    // Sprint-jump impulse: +0.2 blocks/tick along the FACING direction on the
    // jump tick (docs/research/06 §5.1). This is MC's RAW constant — under the
    // friction pipeline above it reproduces the ⚖ 7.127 m/s arc average by
    // itself, so the 0.1842 stand-in the old pipeline required is retired
    // (T-D7 report §4 records the window口径 this number is defined over).
    double sprint_jump_boost = 0.2;

    // ── Sprint state machine (docs/research/05 §1) ──────────────────────────
    // Double-tap-forward window in ticks (MC arms sprintToggleTimer = 7).
    int sprint_toggle_window_ticks = 7;
    // ⚖ Sprinting requires hunger > 6 (cannot engage or maintain at ≤ 6).
    double sprint_min_hunger = 6.0;

    // ── Fall damage ─────────────────────────────────────────────────────────
    // ⚖ docs/01 §2: fall damage = floor(fall_distance − 3) HP.
    double fall_damage_offset = 3.0;

    // ── Fluid block: CARRIED OVER FROM T007, OUT OF T-D7 SCOPE ──────────────
    // Water keeps the pre-T-D7 "stored target steady state" formulation
    // verbatim; swimming is explicitly outside T-D7's scope and a real MC fluid
    // model is a separate card. `water_cruise_speed` is the dry-land walk
    // cruise speed (4.317 m/s) times the old `water_speed_mult = 0.5`, written
    // as a literal so this deferred block does not depend on retired fields.
    double water_cruise_speed = 4.317 / 20.0 * 0.5;
    double water_drag = 0.8;
    double water_gravity = 0.02;
    double swim_up_accel = 0.05;
    double water_max_up_speed = 0.15;

    // ── Collision ───────────────────────────────────────────────────────────
    // Displacement is split into substeps of at most this many blocks to
    // prevent tunneling (docs/research/03 §6.2). To be pressure-tested against
    // the 3.64 blocks/tick blue-ice boat speed in debt T-D10.
    double max_substep = 0.5;
    // Sneak edge protection backoff granularity per iteration (chosen).
    double sneak_edge_step = 0.05;
    // Depth probed below the feet when re-verifying ground support and when
    // locating the block whose slipperiness drives this tick's friction.
    double ground_probe_depth = 0.01;

    // Instantiate the parameter set for an entity type. Returns a fresh value on
    // every call; callers keep their own instance per entity type.
    [[nodiscard]] static PhysicsConfig for_entity(EntityKind kind) {
        PhysicsConfig c;
        c.kind = kind;
        switch (kind) {
        case EntityKind::FallingBlock:
            // docs/research/07 §1.4: a falling block accelerates at half the
            // player's gravity and retains 0.98 (0.91) of its horizontal
            // momentum — i.e. it has no dry-land friction at all. Falling blocks
            // do not walk, so the mode/input constants are irrelevant to them,
            // but they are left at their defaults rather than zeroed so an
            // accidental reuse degrades predictably.
            c.gravity = 0.04;
            c.vertical_drag = 0.98;
            c.horizontal_drag = 0.98;
            break;
        case EntityKind::Player:
            break; // defaults ARE the player
        }
        return c;
    }
};

} // namespace opencraft::physics
