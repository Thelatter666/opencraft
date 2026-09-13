#include "opencraft/game/mining.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace opencraft::game {

namespace {
// Launch blocks that cannot be harvested bare-handed (harvest level >= 1):
// the stone family. Values follow docs/research/01 §5.2/§5.3.
bool requires_tool(std::string_view id) {
    return id == "stone" || id == "cobblestone" || id == "coal_ore" || id == "copper_ore" || id == "iron_ore" ||
           id == "gold_ore" || id == "diamond_ore" || id == "obsidian";
}
} // namespace

int harvest_level(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id) {
    if (!registry.has_numeric(block_id)) {
        return 0;
    }
    const auto &def = registry.def_of(block_id);
    if (def.hardness < 0.0f) {
        return 1; // unbreakable: never mineable by hand
    }
    return requires_tool(registry.string_of(block_id)) ? 1 : 0;
}

bool can_harvest_by_hand(const opencraft::voxel::BlockRegistry &registry, std::uint16_t block_id) {
    return harvest_level(registry, block_id) == 0;
}

MiningTracker::MiningTracker(const opencraft::voxel::BlockRegistry &registry, MiningConfig config)
    : registry_(&registry), config_(config) {}

MiningTickResult MiningTracker::tick(const glm::ivec3 &target, std::uint16_t block_id, bool targeting,
                                     bool mining_held) {
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
        // New target: progress resets (never carried across blocks) and the
        // per-tick damage for this block is derived once (docs/research/01
        // §5.1: damage = canHarvest ? speed/hardness/30 : 1/hardness/100,
        // speed = 1 bare-handed).
        target_ = target;
        has_target_ = true;
        ticks_done_ = 0;
        damage_per_tick_ = 0.0;
        predicted_ticks_ = 0;
        if (registry_->has_numeric(block_id)) {
            const auto &def = registry_->def_of(block_id);
            if (def.hardness > 0.0f) {
                damage_per_tick_ = can_harvest_by_hand(*registry_, block_id) ? 1.0 / def.hardness / 30.0
                                                                             : 1.0 / def.hardness / 100.0;
                predicted_ticks_ = static_cast<int>(std::ceil(1.0 / damage_per_tick_));
            }
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
    const bool instant = predicted_ticks_ <= 1 ||
                         static_cast<double>(predicted_ticks_) / 20.0 <= config_.instant_threshold_seconds;
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
