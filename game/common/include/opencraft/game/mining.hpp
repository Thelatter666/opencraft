#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "opencraft/voxel/block_registry.hpp"

namespace opencraft::game {

// ── Harvest rules (docs/research/01 §5.2-5.3) ──────────────────────────────
// Launch scope (T008) is bare hands only: multiplier 1. The registry's
// BlockDef carries hardness but no tool tier, so the "requires a tool" set
// lives here as the block ids that need harvest level >= 1 (stone family).
// When tools arrive (M2+) this table grows into a per-block tier attribute.

// 0 = harvestable by hand, 1+ = needs at least that tool tier. Unknown ids
// and air return 0; blocks the registry marks unbreakable report level 1 so
// they can never be mined bare-handed.
[[nodiscard]] int harvest_level(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id);

// Bare hands: multiplier 1, so canHarvest == (harvest_level == 0).
[[nodiscard]] bool can_harvest_by_hand(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id);

// ── Mining progress state machine (docs/01 §4 ⚖, docs/research/01 §5.1) ────
// Tick-driven (20 TPS), pure inputs -> outputs, no wall clock, no world
// access: the caller resolves the targeted block id via raycast and passes
// it in. Per tick the miner accumulates "damage" on the targeted block:
//     damage_per_tick = canHarvest ? speed / hardness / 30 : 1 / hardness / 100
// with speed = 1 (bare hands). Predicted ticks = ceil(1 / damage_per_tick);
// a predicted time <= 0.05 s (1 tick) breaks instantly and skips the
// 6-tick between-blocks delay. Switching targets or releasing the button
// resets accumulated progress. Bare hand on stone (hardness 1.5, not
// harvestable) => 1 / 1.5 / 100 => 150 ticks => 7.5 s, no drops.
struct MiningConfig {
    int consecutive_delay_ticks = 6;         // ⚖ docs/01 §4: 6-tick between-blocks delay
    double instant_threshold_seconds = 0.05; // ⚖ docs/01 §4: predicted <= 0.05 s mines instantly
};

// Per-tick result of the mining state machine.
struct MiningTickResult {
    bool broke = false;    // the targeted block broke on this tick
    bool instant = false;  // broke with no crack stages (predicted <= 0.05 s)
    float progress = 0.0f; // accumulated damage on the current target, 0..1
    int crack_stage = 0;   // 0..9 overlay stage derived from progress
};

class MiningTracker {
public:
    explicit MiningTracker(const opencraft::voxel::BlockRegistry &registry, MiningConfig config = {});

    // Advances the machine by one tick. `block_id` is the registry id of the
    // targeted block (ignored when targeting is false), `target` its cell.
    [[nodiscard]] MiningTickResult tick(const glm::ivec3 &target, std::uint16_t block_id, bool targeting,
                                        bool mining_held);

    // Accumulated progress on the current target (drives the crack overlay).
    [[nodiscard]] float progress() const {
        return has_target_ ? static_cast<float>(static_cast<double>(ticks_done_) * damage_per_tick_) : 0.0f;
    }

    // Clears accumulated progress and drops the target (e.g. on pause).
    void reset();

private:
    const opencraft::voxel::BlockRegistry *registry_;
    MiningConfig config_;

    glm::ivec3 target_{0, 0, 0};
    bool has_target_ = false;
    // Integer tick accounting on the current target (see mining.cpp: the
    // break tick index is ceil(1/damage), counted in whole ticks so IEEE
    // rounding of the damage sum can never delay a break).
    int ticks_done_ = 0;
    double damage_per_tick_ = 0.0;
    int predicted_ticks_ = 0;
    int delay_ticks_ = 0; // ticks until the next block may start taking damage
};

} // namespace opencraft::game
