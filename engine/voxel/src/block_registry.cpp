#include "opencraft/voxel/block_registry.hpp"

#include <stdexcept>
#include <utility>

namespace opencraft::voxel {

namespace {
// Hardness values follow docs/research/01 §5.2 (MW "Breaking" table).
constexpr float kHardnessUnbreakable = -1.0f;
} // namespace

BlockRegistry::BlockRegistry() {
    BlockDef air_def;
    air_def.display_name = "Air";
    air_def.solid = false;
    air_def.transparent = true;
    air_def.hardness = 0.0f;
    defs_.push_back(std::move(air_def));
    id_by_numeric_.emplace_back("air");
    numeric_by_id_.emplace("air", kAirId);
}

BlockRegistry BlockRegistry::create_default() {
    BlockRegistry registry;
    registry.register_block("dirt", {"Dirt", true, false, 0.5f});
    registry.register_block("grass_block", {"Grass Block", true, false, 0.6f});
    registry.register_block("stone", {"Stone", true, false, 1.5f});
    registry.register_block("cobblestone", {"Cobblestone", true, false, 2.0f});
    registry.register_block("sand", {"Sand", true, false, 0.5f});
    registry.register_block("gravel", {"Gravel", true, false, 0.6f});
    registry.register_block("sandstone", {"Sandstone", true, false, 0.8f});
    registry.register_block("log", {"Log", true, false, 2.0f});
    registry.register_block("leaves", {"Leaves", true, true, 0.2f});
    registry.register_block("planks", {"Planks", true, false, 2.0f});
    registry.register_block("glass", {"Glass", true, true, 0.3f});
    registry.register_block("water", {"Water", false, true, 100.0f});
    registry.register_block("bedrock", {"Bedrock", true, false, kHardnessUnbreakable});
    registry.register_block("coal_ore", {"Coal Ore", true, false, 3.0f});
    registry.register_block("copper_ore", {"Copper Ore", true, false, 3.0f});
    registry.register_block("iron_ore", {"Iron Ore", true, false, 3.0f});
    registry.register_block("gold_ore", {"Gold Ore", true, false, 3.0f});
    registry.register_block("diamond_ore", {"Diamond Ore", true, false, 3.0f});
    registry.register_block("snow_block", {"Snow Block", true, false, 0.2f});
    registry.register_block("obsidian", {"Obsidian", true, false, 50.0f});
    return registry;
}

std::uint16_t BlockRegistry::register_block(std::string id, BlockDef def) {
    if (id.empty()) {
        throw std::invalid_argument("block id must not be empty");
    }
    if (numeric_by_id_.contains(id)) {
        throw std::invalid_argument("block id already registered: " + id);
    }
    if (defs_.size() > 0xFFFF) {
        throw std::overflow_error("block registry exhausted (u16 id space)");
    }
    const auto numeric_id = static_cast<std::uint16_t>(defs_.size());
    defs_.push_back(std::move(def));
    id_by_numeric_.push_back(id);
    numeric_by_id_.emplace(id, numeric_id);
    return numeric_id;
}

bool BlockRegistry::has_id(std::string_view id) const {
    return numeric_by_id_.contains(id);
}

bool BlockRegistry::has_numeric(std::uint16_t numeric_id) const {
    return numeric_id < defs_.size();
}

std::optional<std::uint16_t> BlockRegistry::find_id(std::string_view id) const {
    const auto it = numeric_by_id_.find(id);
    if (it == numeric_by_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::uint16_t BlockRegistry::id_of(std::string_view id) const {
    const auto found = find_id(id);
    if (!found) {
        throw std::out_of_range("unknown block id: " + std::string(id));
    }
    return *found;
}

const std::string &BlockRegistry::string_of(std::uint16_t numeric_id) const {
    if (numeric_id >= id_by_numeric_.size()) {
        throw std::out_of_range("unknown block numeric id");
    }
    return id_by_numeric_[numeric_id];
}

const BlockDef &BlockRegistry::def_of(std::uint16_t numeric_id) const {
    if (numeric_id >= defs_.size()) {
        throw std::out_of_range("unknown block numeric id");
    }
    return defs_[numeric_id];
}

} // namespace opencraft::voxel
