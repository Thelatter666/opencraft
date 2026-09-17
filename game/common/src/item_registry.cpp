#include "opencraft/game/item_registry.hpp"

#include <stdexcept>
#include <utility>

#include "opencraft/voxel/block_registry.hpp"

namespace opencraft::game {

namespace {

// ── Launch item set ───────────────────────────────────────────────────────
// Naming is original (docs/04 red lines 2/5: no item names of another game,
// no translations of them). The words are coined from geology / metalworking
// / armour vocabulary instead of the familiar fantasy-mining set.
//
// Every placeable entry names its block by STRING id, resolved in
// create_default(): writing a block's numeric id here would freeze an
// ordering the block registry is free to change, while a name survives
// renumbering and fails loudly (id_of throws) if the block disappears.
struct LaunchItem {
    const char *id;
    const char *display_name;
    int max_stack;
    EquipSlot equip;
    // Block string id this item places, or nullptr when it has no block form.
    const char *block;
};

constexpr LaunchItem kLaunchItems[] = {
    // Item forms of the launch blocks -- all stack to 64. Water has no entry
    // (T-I1 ruling S-3): in this game water is carried in a vessel and is
    // never itself an item, so there is nothing to place.
    {"loam_clod", "Loam Clod", kStackLimitLarge, EquipSlot::None, "dirt"},
    {"sod_loam", "Sod Loam", kStackLimitLarge, EquipSlot::None, "grass_block"},
    {"greyrock", "Greyrock", kStackLimitLarge, EquipSlot::None, "stone"},
    {"rubble_rock", "Rubble Rock", kStackLimitLarge, EquipSlot::None, "cobblestone"},
    {"fine_grit", "Fine Grit", kStackLimitLarge, EquipSlot::None, "sand"},
    {"pebble_grit", "Pebble Grit", kStackLimitLarge, EquipSlot::None, "gravel"},
    {"grit_slab", "Grit Slab", kStackLimitLarge, EquipSlot::None, "sandstone"},
    {"timber_log", "Timber Log", kStackLimitLarge, EquipSlot::None, "log"},
    {"leaf_canopy", "Leaf Canopy", kStackLimitLarge, EquipSlot::None, "leaves"},
    {"sawn_planks", "Sawn Planks", kStackLimitLarge, EquipSlot::None, "planks"},
    {"clear_pane", "Clear Pane", kStackLimitLarge, EquipSlot::None, "glass"},
    {"underrock", "Underrock", kStackLimitLarge, EquipSlot::None, "bedrock"},
    {"char_ore", "Char Ore", kStackLimitLarge, EquipSlot::None, "coal_ore"},
    {"verdigris_ore", "Verdigris Ore", kStackLimitLarge, EquipSlot::None, "copper_ore"},
    {"ferrous_ore", "Ferrous Ore", kStackLimitLarge, EquipSlot::None, "iron_ore"},
    {"auric_ore", "Auric Ore", kStackLimitLarge, EquipSlot::None, "gold_ore"},
    {"lucent_ore", "Lucent Ore", kStackLimitLarge, EquipSlot::None, "diamond_ore"},
    {"rime_block", "Rime Block", kStackLimitLarge, EquipSlot::None, "snow_block"},
    {"duskglass", "Duskglass", kStackLimitLarge, EquipSlot::None, "obsidian"},

    // Vessels: containers stack to 16 while empty, and a filled one holds
    // fluid so it does not stack at all. Both are handled by the client's
    // vessel paths (T-I2), which is why they carry no block form.
    {"empty_vessel", "Empty Vessel", kStackLimitMedium, EquipSlot::None, nullptr},
    {"water_vessel", "Water Vessel", kStackLimitSingle, EquipSlot::None, nullptr},

    // Food and material drafts (no hunger/consumption logic yet).
    {"sunroot", "Sunroot", kStackLimitLarge, EquipSlot::None, nullptr},
    {"cave_cap", "Cave Cap", kStackLimitLarge, EquipSlot::None, nullptr},
    {"grain_loaf", "Grain Loaf", kStackLimitLarge, EquipSlot::None, nullptr},
    {"char_lump", "Char Lump", kStackLimitLarge, EquipSlot::None, nullptr},
    {"ferrous_bloom", "Ferrous Bloom", kStackLimitLarge, EquipSlot::None, nullptr},
    {"rime_pearl", "Rime Pearl", kStackLimitMedium, EquipSlot::None, nullptr},

    // Mob drops (T-M2): what a Mossback leaves behind. research/11 §6.1 gives
    // the base game's counts for the equivalent drops (raw cut 1-3, hide 0-2)
    // and §5.2 its nutrition (3 hunger / 1.8 saturation); the COUNTS are what
    // the loot table uses, while the nutrition waits for the food card -
    // ItemDef has no hunger fields yet and half-building that system here would
    // be exactly the "已实现但不可达" pattern this project keeps paying for.
    {"raw_haunch", "Raw Haunch", kStackLimitLarge, EquipSlot::None, nullptr},
    {"sturdy_hide", "Sturdy Hide", kStackLimitLarge, EquipSlot::None, nullptr},

    // Tools of the first tier (docs/01 §5 names the tiers 木质/岩质/精铁/秘银/
    // 星钻; "timber" is that system's first tier). Durability/damage/harvest
    // level are not modelled yet -- these entries exist so the single-item
    // stack tier has real samples.
    {"timber_chisel", "Timber Chisel Pick", kStackLimitSingle, EquipSlot::None, nullptr},
    {"timber_hewer", "Timber Hewing Axe", kStackLimitSingle, EquipSlot::None, nullptr},
    {"timber_spade", "Timber Digging Spade", kStackLimitSingle, EquipSlot::None, nullptr},
    {"timber_edge", "Timber Edge Blade", kStackLimitSingle, EquipSlot::None, nullptr},

    // Armour, first tier. Present because the inventory's four armour slots
    // need something that is allowed to occupy them, and because one piece
    // per body part is what makes the slot constraint testable.
    {"timber_headguard", "Timber Headguard", kStackLimitSingle, EquipSlot::Head, nullptr},
    {"timber_cuirass", "Timber Cuirass", kStackLimitSingle, EquipSlot::Chest, nullptr},
    {"timber_greaves", "Timber Greaves", kStackLimitSingle, EquipSlot::Legs, nullptr},
    {"timber_treads", "Timber Treads", kStackLimitSingle, EquipSlot::Feet, nullptr},
};

} // namespace

ItemRegistry::ItemRegistry() {
    ItemDef empty_def;
    empty_def.display_name = "Empty";
    empty_def.max_stack = kStackLimitSingle;
    defs_.push_back(std::move(empty_def));
    id_by_numeric_.emplace_back("empty");
    numeric_by_id_.emplace("empty", kEmptyId);
}

ItemRegistry ItemRegistry::create_default() {
    ItemRegistry registry;
    // Resolve the block links against the launch block set. Constructing it
    // here (rather than taking one in) keeps the signature the inventory
    // already depends on; the item set IS the item set of those blocks, and
    // the client's world builds the same default registry, so the ids agree.
    const voxel::BlockRegistry blocks = voxel::BlockRegistry::create_default();
    for (const auto &entry : kLaunchItems) {
        const std::uint16_t block = entry.block == nullptr ? kNoBlock : blocks.id_of(entry.block);
        registry.register_item(entry.id, {entry.display_name, entry.max_stack, entry.equip, block});
    }
    return registry;
}

std::uint16_t ItemRegistry::register_item(std::string id, ItemDef def) {
    if (id.empty()) {
        throw std::invalid_argument("item id must not be empty");
    }
    if (numeric_by_id_.contains(id)) {
        throw std::invalid_argument("item id already registered: " + id);
    }
    if (def.max_stack < 1) {
        throw std::invalid_argument("item max_stack must be at least 1: " + id);
    }
    if (defs_.size() > 0xFFFF) {
        throw std::overflow_error("item registry exhausted (u16 id space)");
    }
    const auto numeric_id = static_cast<std::uint16_t>(defs_.size());
    defs_.push_back(std::move(def));
    id_by_numeric_.push_back(id);
    numeric_by_id_.emplace(id, numeric_id);
    return numeric_id;
}

bool ItemRegistry::has_id(std::string_view id) const {
    return numeric_by_id_.contains(id);
}

bool ItemRegistry::has_numeric(std::uint16_t numeric_id) const {
    return numeric_id < defs_.size();
}

std::optional<std::uint16_t> ItemRegistry::find_id(std::string_view id) const {
    const auto it = numeric_by_id_.find(id);
    if (it == numeric_by_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::uint16_t ItemRegistry::id_of(std::string_view id) const {
    const auto found = find_id(id);
    if (!found) {
        throw std::out_of_range("unknown item id: " + std::string(id));
    }
    return *found;
}

const std::string &ItemRegistry::string_of(std::uint16_t numeric_id) const {
    if (numeric_id >= id_by_numeric_.size()) {
        throw std::out_of_range("unknown item numeric id");
    }
    return id_by_numeric_[numeric_id];
}

const ItemDef &ItemRegistry::def_of(std::uint16_t numeric_id) const {
    if (numeric_id >= defs_.size()) {
        throw std::out_of_range("unknown item numeric id");
    }
    return defs_[numeric_id];
}

std::optional<std::uint16_t> ItemRegistry::item_for_block(const std::uint16_t block) const {
    if (block == kNoBlock) {
        return std::nullopt;
    }
    for (std::uint16_t item = 0; item < defs_.size(); ++item) {
        if (defs_[item].block == block) {
            return item;
        }
    }
    return std::nullopt;
}

} // namespace opencraft::game
