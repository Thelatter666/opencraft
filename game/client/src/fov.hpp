#pragma once

// T-D1: sprint FOV alignment (docs/research/05 §2). Pure math so headless
// tests can assert the transition; main.cpp only wires the state in.
//
// Numbers and formulas below come from docs/research/05 §2; NO decompiled
// source is referenced (compliance red line). Provenance grades:
// - Base FOV 70 ("Normal") and the "FOV Effects" accessibility slider
//   (default 100%, 0% disables): minecraft.wiki/w/Options (accessed
//   2026-09-14) — grade A.
// - Movement-speed attribute: player base 0.1, `minecraft:sprinting`
//   modifier +0.3 with add_multiplied_total (=> x1.3):
//   minecraft.wiki/w/Attribute (accessed 2026-09-14) — grade A.
// - multiplier = (speed / walk_speed + 1) / 2 is a widely-recorded community
//   formula, NOT stated by the main wiki; grade B. Feeding the grade-A
//   attribute values in gives (0.13 / 0.1 + 1) / 2 = 1.15, i.e. a 15% widen —
//   docs/research/01 §1.1's "~10%" was an older estimate. If the PM rules for
//   10%, the only change here is this one constant.
// - The per-update exponential easing (factor 0.5) is likewise grade B
//   community knowledge: monotone, never overshoots, ~99% there in ~7 steps.

namespace opencraft::client {

// ⚖ MC default "Normal" FOV (options.txt fov=70).
inline constexpr float kBaseFov = 70.0f;
// (0.13 / 0.1 + 1) / 2 — sprint movement-speed ratio 1.3 into the formula.
inline constexpr float kSprintFovMultiplier = 1.15f;
// Exponential easing factor per frame (community-documented MC easing).
inline constexpr float kFovEaseFactor = 0.5f;

[[nodiscard]] inline float fov_target(bool sprinting) {
    return sprinting ? kBaseFov * kSprintFovMultiplier : kBaseFov;
}

// One easing step toward the target; monotone (never overshoots) and bounded
// by the target on both approach and retreat.
[[nodiscard]] inline float fov_step(float current, bool sprinting) {
    const float target = fov_target(sprinting);
    return current + (target - current) * kFovEaseFactor;
}

} // namespace opencraft::client
