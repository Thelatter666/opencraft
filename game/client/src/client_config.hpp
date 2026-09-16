#pragma once

// Client-side tunables: window size, view/interaction constants and the
// per-frame streaming budgets. Moved verbatim out of main.cpp by T-M1 (pure
// code motion) so the tick and the renderer share one authoritative copy.
//
// ⚖ values are frozen: the split moved them, it did not retune them
// (docs/01 §4).

#include <cmath>

#include <glm/glm.hpp>

#include "opencraft/game/inventory.hpp"
#include "opencraft/game/protocol.hpp"
#include "opencraft/voxel/chunk.hpp"

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

// ── streaming windows (T-D4) ────────────────────────────────────────────────
// The client names these and the authority enforces them (game::StreamRequest).
// Generation keeps one ring more than the meshed radius, so the chunks at the
// view edge mesh against lit neighbors (the T004 order).
inline constexpr int kGenRadius = kViewRadius + 1;
// Hysteresis: how much further than the generation window a chunk must be
// before the authority releases it. Measured in chunks, so `kUnloadRadius` is a
// 2-chunk (32-block) slack band all around the working set.
//
// Why 2: pacing back and forth over a boundary is the normal case (the player
// walks one chunk out and returns), and one chunk of slack would already cover
// that - but the slack also decides when dirty chunks are written, and every
// boundary crossing with zero hysteresis re-saves a whole 15-chunk row. Two
// chunks means any back-and-forth shorter than three chunks (48 blocks) loads
// nothing, releases nothing and writes nothing, while the resident set stays a
// fixed 19x19 square (361 chunks) instead of growing without bound.
inline constexpr int kUnloadHysteresis = 2;
inline constexpr int kUnloadRadius = kGenRadius + kUnloadHysteresis;

inline constexpr double kEyeStanding = 1.62;
inline constexpr double kEyeSneaking = 1.27;

// One streaming request for a viewer standing at `feet`, with the windows above
// and this call's generation budget. The frame loop sends it with the
// per-frame budget; the tick's autosave window sends the same request with no
// budget and `persist` set. Both ends of that pairing live here, so the two
// call sites cannot drift apart.
[[nodiscard]] inline game::StreamRequest make_stream_request(const glm::dvec3 &feet, int generate_budget = 0) {
    const auto [cx, cz] =
        voxel::Chunk::chunk_coords(static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.z)));
    game::StreamRequest req;
    req.center_cx = cx;
    req.center_cz = cz;
    req.generate_radius = kGenRadius;
    req.unload_radius = kUnloadRadius;
    req.generate_budget = generate_budget;
    return req;
}

// ── hotbar layout ───────────────────────────────────────────────────────────
// Keys 1..9 select the nine hotbar cells of the inventory (docs/01 §5
// 快捷栏 9). The T-F1 bucket cell (a tenth, key 0) is gone: the vessels are
// ordinary items in the bar now (T-I2), so the bar is exactly the inventory's
// hotbar section.
inline constexpr int kHotbarSlots = game::kHotbarSlots;

} // namespace opencraft::client
