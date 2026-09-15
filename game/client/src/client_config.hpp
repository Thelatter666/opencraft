#pragma once

// Client-side tunables: window size, view/interaction constants and the
// per-frame streaming budgets. Moved verbatim out of main.cpp by T-M1 (pure
// code motion) so the tick and the renderer share one authoritative copy.
//
// ⚖ values are frozen: the split moved them, it did not retune them
// (docs/01 §4).

#include "opencraft/game/inventory.hpp"
#include "opencraft/game/protocol.hpp"

namespace opencraft::client {

inline constexpr int kWindowWidth = 1280;
inline constexpr int kWindowHeight = 720;

// ── view / interaction constants ────────────────────────────────────────────
inline constexpr double kMouseSensitivity = 0.0025;
inline constexpr double kMaxPitch = 1.5533; // ~89 degrees
// ⚖ docs/01 §4: survival block reach. T-A1 gave it a single source of truth -
// the authoritative side re-checks the same number - so the value itself now
// lives with the request vocabulary and this is the client's name for it.
inline constexpr double kReachDistance = game::kReachDistance;
// Base FOV and sprint multiplier live in fov.hpp (T-D1, unit-tested).
inline constexpr int kViewRadius = 6;      // meshed chunk radius around the player
inline constexpr int kGenPerFrame = 2;     // sync-generation budget (docs: <= 2/frame)
inline constexpr int kNewMeshPerFrame = 4; // new-chunk meshing budget/frame

inline constexpr double kEyeStanding = 1.62;
inline constexpr double kEyeSneaking = 1.27;

// ── hotbar layout ───────────────────────────────────────────────────────────
// Keys 1..9 select the nine hotbar cells of the inventory (docs/01 §5
// 快捷栏 9). The T-F1 bucket cell (a tenth, key 0) is gone: the vessels are
// ordinary items in the bar now (T-I2), so the bar is exactly the inventory's
// hotbar section.
inline constexpr int kHotbarSlots = game::kHotbarSlots;

} // namespace opencraft::client
