#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "opencraft/game/item_registry.hpp"
#include "opencraft/voxel/block_registry.hpp"

namespace opencraft::game {

// ── Harvest rules (docs/research/01 §5.2-5.3, §9 Tiers) ────────────────────
// A block needs a TOOL TIER to give anything up. The scale is research/01 §9's
// 采集等级 column, 0-based exactly as that sheet writes it (木 0 / 石 1 / 铁 2 /
// 钻 3 / 合金 4), and the same scale holds the two ends of the comparison:
//
//   * a block's REQUIREMENT is the lowest tier that can take it
//     (harvest_level below). Stone's is 0 - the wooden pick, the sheet's lowest
//     tool - and the ores climb from there;
//   * a miner's TIER is the held item's (ItemDef::mining_tier).
//
// The bare hand is not on the sheet: it is `kNoTool` (-1), below even the wooden
// pick, so "needs any tool at all" and "needs a better tool than this one" are
// the same comparison - `tier >= requirement` - and need no second flag. That is
// what makes ⚖ 徒手挖石头 7.5 秒且无掉落 and ⚖ 木镐挖石 1.125 秒且掉落 both true
// with one rule (docs/01 §4, and the T-D60 card's §4.1 table).
//
// Blocks a hand CAN take (dirt, sand, log, planks, glass, leaves...) report
// kHandHarvestable, which is the hand's own value: the comparison then passes
// for a hand and for every tool.
//
// Bare hands are the default and were the whole of T008's launch scope; T-D60
// added the tool side (ItemDef's two fields), which is why every entry point
// below still has a bare-handed form and why a bare-handed call is unchanged
// from what it did before that card.

// The hand's own "tier", and at the same time the requirement a block reports
// when a hand is good enough for it. Same value as item_registry's kNoTool -
// it is the same fact seen from the block's side, so it is the same number.
inline constexpr int kHandHarvestable = opencraft::game::kNoTool;

// Requirements above every tool tier (bedrock). The registry marks those blocks
// unbreakable by hardness, and both the mining machine and this table refuse
// them; the value only has to be out of reach of every tier the sheet defines.
inline constexpr int kUnmineable = 100;

// The lowest tool tier that can take `block_id`: kHandHarvestable for the blocks
// a bare hand can harvest (and for unknown ids / air), the tier named by the
// per-block table below otherwise, kUnmineable for blocks the registry marks
// unbreakable.
[[nodiscard]] int harvest_level(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id);

// Bare hands: tier kNoTool, so this is exactly "the requirement is no higher
// than a hand's".
[[nodiscard]] bool can_harvest_by_hand(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id);

// What the miner is swinging (T-D60 C-6). Both fields come off the held item's
// ItemDef; the defaults are the bare hand's values, so MiningTool{} mines
// exactly as the T008 machine did.
struct MiningTool {
    int tier = kNoTool; // §9 Tiers 采集等级 (木 0 / 石 1 / 铁 2 / 钻 3 …)
    double speed = 1.0; // §5.1 挖掘速度倍率 (徒手 1 / 木 2 / 石 4 …)
};

// The tool a held stack provides, read from one place (the def) so the tick and
// the tests cannot disagree about which item is a pickaxe.
[[nodiscard]] inline MiningTool mining_tool_of(const ItemDef &def) {
    return {def.mining_tier, def.mining_speed};
}

// True when this tool's tier reaches the block's requirement - the single rule
// behind BOTH the drop and the choice of divisor: an under-tiered tool mines at
// multiplier 1 in the ÷100 branch and leaves nothing behind (research/01 §5.3).
[[nodiscard]] bool can_harvest_with(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id,
                                    MiningTool tool);

// ── Mining progress state machine (docs/01 §4 ⚖, docs/research/01 §5.1) ────
// Tick-driven (20 TPS), pure inputs -> outputs, no wall clock, no world
// access: the caller resolves the targeted block id via raycast and passes
// it in. Per tick the miner accumulates "damage" on the targeted block:
//     damage_per_tick = speed / hardness / (canHarvest ? 30 : 100)
// where `speed` is the tool's multiplier when the tier reaches the block and 1
// otherwise (a gear's speed does not help on a block it cannot take). Predicted
// ticks = ceil(1 / damage_per_tick); a predicted time <= 0.05 s (1 tick) breaks
// instantly and skips the 6-tick between-blocks delay. Switching targets or
// releasing the button resets accumulated progress. Bare hand on stone
// (hardness 1.5, requirement 0 > kNoTool) => 1 / 1.5 / 100 => 150 ticks => 7.5 s,
// no drops; a wooden pick (tier 0, speed 2) => 2 / 1.5 / 30 => 23 ticks.
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
    //
    // The two-argument form below is a BARE-HANDED swing, unchanged from T008:
    // same numbers, same tick counts (a tool whose tier reaches the block but
    // whose multiplier is 1 mines exactly as fast as a hand, which is why the
    // default exists rather than an if).
    [[nodiscard]] MiningTickResult tick(const glm::ivec3 &target, std::uint16_t block_id, bool targeting,
                                        bool mining_held);
    [[nodiscard]] MiningTickResult tick(const glm::ivec3 &target, std::uint16_t block_id, bool targeting,
                                        bool mining_held, MiningTool tool);

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
