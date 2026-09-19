#include "opencraft/game/mining.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace opencraft::game {

namespace {
// The per-block requirement table (T-D60 C-6), on research/01 §9's 0-based tool
// scale: the LOWEST tier that can take the block. Everything not listed is
// kHandHarvestable (dirt, sand, gravel, log, planks, glass, leaves, snow, the
// two crafted blocks...) - the T008 default that "the registry does not know
// this id" and "a hand is enough" are the same answer.
//
// ⚖ The values are the base game's own tool gates, one tier each:
//   stone family     0  木镐 (the sheet's lowest tool; ★ see the note below)
//   coal ore         0  木镐
//   copper ore       1  石镐
//   iron ore         1  石镐
//   gold ore         2  铁镐
//   diamond ore      2  铁镐
//   obsidian         3  钻镐
//
// ★ stone family = 0, NOT 1: the card's §4.1 table (the normative arithmetic
// this card is accepted against) has 木镐挖 stone succeed in 1.125 s with a
// cobblestone drop, and the whole progression chain - 木镐 → 圆石 → 石镐 - runs
// through it. With the hand below the sheet (kNoTool) that row and 徒手挖石
// 7.5 秒且无掉落 hold together; raising stone to 1 would make the wooden pick
// useless and leave the chain unclosable.
// ★ gold = 2 is the card's CONSERVATIVE value (an iron pick), not the value
// research/01:138 gives (which puts gold with iron at 石镐): the base game
// requires an iron pick for gold ore, and PM ruled the stricter reading on
// purpose. Flagged 待校准 on the card for T-D36's校准 list; do not "fix" it to
// 1 without that measurement.
// ★ copper = 1 (石镐) is the base game's gate too; research/01:138 does not list
// copper at all (it predates the 铜档), so the value is the sheet's own 铜 tier
// and is the one entry the card's table does not name.
[[nodiscard]] int requirement_of(std::string_view id) {
    if (id == "stone" || id == "cobblestone" || id == "coal_ore") {
        return 0;
    }
    if (id == "copper_ore" || id == "iron_ore") {
        return 1;
    }
    if (id == "gold_ore" || id == "diamond_ore") {
        return 2;
    }
    if (id == "obsidian") {
        return 3;
    }
    return kHandHarvestable;
}
} // namespace

int harvest_level(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id) {
    if (!registry.has_numeric(block_id)) {
        return kHandHarvestable;
    }
    const auto &def = registry.def_of(block_id);
    if (def.hardness < 0.0f) {
        return kUnmineable; // unbreakable: no tier reaches it
    }
    return requirement_of(registry.string_of(block_id));
}

bool can_harvest_by_hand(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id) {
    return harvest_level(registry, block_id) <= kHandHarvestable;
}

bool can_harvest_with(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id, MiningTool tool) {
    return tool.tier >= harvest_level(registry, block_id);
}

MiningTracker::MiningTracker(const opencraft::voxel::BlockRegistry &registry, MiningConfig config)
    : registry_(&registry), config_(config) {
}

MiningTickResult MiningTracker::tick(const glm::ivec3 &target, std::uint16_t block_id, bool targeting,
                                     bool mining_held) {
    return tick(target, block_id, targeting, mining_held, MiningTool{});
}

MiningTickResult MiningTracker::tick(const glm::ivec3 &target, std::uint16_t block_id, bool targeting, bool mining_held,
                                     MiningTool tool) {
    MiningTickResult result;

    if (!targeting || !mining_held) {
        // No target or released button: progress does not survive. The
        // between-blocks delay still counts down while idle (MC behavior).
        has_target_ = false;
        ticks_done_ = 0;
        if (delay_ticks_ > 0) {
            --delay_ticks_;
        }
        return result;
    }

    if (!has_target_ || target != target_) {
        // New target: progress resets (never carried across blocks).
        target_ = target;
        has_target_ = true;
        ticks_done_ = 0;
    }
    // The per-tick damage is derived from (block, tool) on every tick rather
    // than only when the target changes: swapping to a pickaxe half-way through
    // a block must speed the rest of it up (the base game does the same), and
    // for a fixed tool the arithmetic is the same number it always was, so the
    // T008 bare-handed results are unchanged tick for tick.
    damage_per_tick_ = 0.0;
    predicted_ticks_ = 0;
    if (registry_->has_numeric(block_id)) {
        const auto &def = registry_->def_of(block_id);
        if (def.hardness > 0.0f) {
            const bool harvest = can_harvest_with(*registry_, block_id, tool);
            const double speed = harvest ? tool.speed : 1.0;
            damage_per_tick_ = speed / static_cast<double>(def.hardness) / (harvest ? 30.0 : 100.0);
            predicted_ticks_ = static_cast<int>(std::ceil(1.0 / damage_per_tick_));
        }
    }

    if (!registry_->has_numeric(block_id)) {
        return result;
    }
    const auto &def = registry_->def_of(block_id);

    if (def.hardness < 0.0f) {
        return result; // unbreakable
    }

    // Instant mining: predicted time <= 0.05 s (1 tick), including hardness-0
    // blocks. Skips the crack overlay AND the between-blocks delay.
    const bool instant =
        predicted_ticks_ <= 1 || static_cast<double>(predicted_ticks_) / 20.0 <= config_.instant_threshold_seconds;
    if (instant && ticks_done_ == 0) {
        result.broke = true;
        result.instant = true;
        has_target_ = false;
        ticks_done_ = 0;
        return result;
    }

    if (delay_ticks_ > 0) {
        // Between-blocks delay: exactly config_.consecutive_delay_ticks of
        // blocked ticks after every non-instant break.
        --delay_ticks_;
        return result;
    }

    // Integer tick counting: the block breaks on the ceil(1/damage)-th
    // accumulation tick. (Accumulating floating-point damage and comparing
    // against 1.0 breaks at 16 ticks for dirt: 15 x 0.0666... = 0.99999...
    // < 1.0 in IEEE doubles. The spec counts ticks, so count ticks.)
    ++ticks_done_;
    if (ticks_done_ >= predicted_ticks_) {
        result.broke = true;
        delay_ticks_ = config_.consecutive_delay_ticks; // ⚖ 6-tick delay, never after instant breaks
        ticks_done_ = 0;
        has_target_ = false;
        return result;
    }

    result.progress = static_cast<float>(static_cast<double>(ticks_done_) * damage_per_tick_);
    result.crack_stage = std::min(9, ticks_done_ * 10 / std::max(1, predicted_ticks_));
    return result;
}

void MiningTracker::reset() {
    has_target_ = false;
    ticks_done_ = 0;
}

} // namespace opencraft::game
