#pragma once

// Client-side interaction/carry state: the hotbar selection, the held item, the
// input edges that drive placement, the targeting + crack overlay and the swing
// / sprint-jump QA counters. Moved out of main.cpp by T-M1 (pure code motion)
// so a logic tick and the renderer share the same named state instead of
// main()'s locals.
//
// Every default is the value main.cpp initialised it to before the split.

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace opencraft::client {

struct InteractionState {
    // ── hotbar / held item ──────────────────────────────────────────────────
    std::array<std::uint16_t, 9> hotbar{}; // keys 1..9; the bucket is kBucketSlot
    int selected_slot = 0;
    // The id the held-item overlay and the placed block use; for the bucket it
    // is the water placeholder (the bucket itself has no block form yet).
    std::uint16_t selected_block = 0;
    // Mirrors selected_slot for the HUD and the render pass (the tick's
    // targeting needs it before the hotbar keys are polled).
    bool bucket_selected = false;
    bool bucket_has_water = false;

    // ── input edges / retry cooldown ────────────────────────────────────────
    bool prev_w = false;
    bool prev_right = false;
    int place_cooldown = 0;

    // ── targeting + mining overlay ──────────────────────────────────────────
    glm::ivec3 target_pos{0, 0, 0};
    bool has_target = false;
    glm::ivec3 crack_pos{0, 0, 0};
    int crack_stage = -1; // -1 = no overlay

    // ── hand swing + T-D1 sprint-jump arc QA counters ───────────────────────
    bool swinging = false;
    double swing_start = -10.0; // glfwGetTime() of the last swing start
    // T-D1 QA evidence (acceptance 6c): measure each sprint-jump arc so the
    // on-machine distance can be compared against the headless ⚖ assertions
    // (arc average 7.127 ±1%, gap clearance ≈4 blocks). An arc opens on the
    // tick a grounded sprint jump leaves the ground and closes on landing.
    bool jump_arc_open = false;
    glm::dvec3 jump_arc_start{0.0, 0.0, 0.0};
    int jump_arc_ticks = 0;
};

} // namespace opencraft::client
