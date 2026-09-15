#include "opencraft/game/item_registry.hpp"

#include <stdexcept>
#include <utility>

namespace opencraft::game {

namespace {

// ── Launch item set ───────────────────────────────────────────────────────
// Naming is original (docs/04 red lines 2/5: no item names of another game,
// no translations of them). The words are coined from geology / metalworking
// / armour vocabulary instead of the familiar fantasy-mining set.
//
// The first twenty entries are the item forms of the twenty blocks
// BlockRegistry::create_default() ships. They carry no link back to the
// blocks' numeric ids: forming a block from a held item is a client-side
// wiring concern (the next card), and baking a block id into ItemDef here
// would freeze a mapping this card cannot test.
struct LaunchItem {
    const char *id;
    const char *display_name;
    int max_stack;
    EquipSlot equip;
};

constexpr LaunchItem kLaunchItems[] = {
    // Item forms of the twenty launch blocks -- all stack to 64.
    {"loam_clod", "Loam Clod", kStackLimitLarge, EquipSlot::None},
    {"sod_loam", "Sod Loam", kStackLimitLarge, EquipSlot::None},
    {"greyrock", "Greyrock", kStackLimitLarge, EquipSlot::None},
    {"rubble_rock", "Rubble Rock", kStackLimitLarge, EquipSlot::None},
    {"fine_grit", "Fine Grit", kStackLimitLarge, EquipSlot::None},
    {"pebble_grit", "Pebble Grit", kStackLimitLarge, EquipSlot::None},
    {"grit_slab", "Grit Slab", kStackLimitLarge, EquipSlot::None},
    {"timber_log", "Timber Log", kStackLimitLarge, EquipSlot::None},
    {"leaf_canopy", "Leaf Canopy", kStackLimitLarge, EquipSlot::None},
    {"sawn_planks", "Sawn Planks", kStackLimitLarge, EquipSlot::None},
    {"clear_pane", "Clear Pane", kStackLimitLarge, EquipSlot::None},
    {"still_water", "Still Water", kStackLimitLarge, EquipSlot::None},
    {"underrock", "Underrock", kStackLimitLarge, EquipSlot::None},
    {"char_ore", "Char Ore", kStackLimitLarge, EquipSlot::None},
    {"verdigris_ore", "Verdigris Ore", kStackLimitLarge, EquipSlot::None},
    {"ferrous_ore", "Ferrous Ore", kStackLimitLarge, EquipSlot::None},
    {"auric_ore", "Auric Ore", kStackLimitLarge, EquipSlot::None},
    {"lucent_ore", "Lucent Ore", kStackLimitLarge, EquipSlot::None},
    {"rime_block", "Rime Block", kStackLimitLarge, EquipSlot::None},
    {"duskglass", "Duskglass", kStackLimitLarge, EquipSlot::None},

    // Vessels: containers stack to 16 while empty, and a filled one holds
    // fluid so it does not stack at all.
    {"empty_vessel", "Empty Vessel", kStackLimitMedium, EquipSlot::None},
    {"water_vessel", "Water Vessel", kStackLimitSingle, EquipSlot::None},

    // Food and material drafts (no hunger/consumption logic yet).
    {"sunroot", "Sunroot", kStackLimitLarge, EquipSlot::None},
    {"cave_cap", "Cave Cap", kStackLimitLarge, EquipSlot::None},
    {"grain_loaf", "Grain Loaf", kStackLimitLarge, EquipSlot::None},
    {"char_lump", "Char Lump", kStackLimitLarge, EquipSlot::None},
    {"ferrous_bloom", "Ferrous Bloom", kStackLimitLarge, EquipSlot::None},
    {"rime_pearl", "Rime Pearl", kStackLimitMedium, EquipSlot::None},

    // Tools of the first tier (docs/01 §5 names the tiers 木质/岩质/精铁/秘银/
    // 星钻; "timber" is that system's first tier). Durability/damage/harvest
    // level are not modelled yet -- these entries exist so the single-item
    // stack tier has real samples.
    {"timber_chisel", "Timber Chisel Pick", kStackLimitSingle, EquipSlot::None},
    {"timber_hewer", "Timber Hewing Axe", kStackLimitSingle, EquipSlot::None},
    {"timber_spade", "Timber Digging Spade", kStackLimitSingle, EquipSlot::None},
    {"timber_edge", "Timber Edge Blade", kStackLimitSingle, EquipSlot::None},

    // Armour, first tier. Present because the inventory's four armour slots
    // need something that is allowed to occupy them, and because one piece
    // per body part is what makes the slot constraint testable.
    {"timber_headguard", "Timber Headguard", kStackLimitSingle, EquipSlot::Head},
    {"timber_cuirass", "Timber Cuirass", kStackLimitSingle, EquipSlot::Chest},
    {"timber_greaves", "Timber Greaves", kStackLimitSingle, EquipSlot::Legs},
    {"timber_treads", "Timber Treads", kStackLimitSingle, EquipSlot::Feet},
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
    for (const auto &entry : kLaunchItems) {
        registry.register_item(entry.id, {entry.display_name, entry.max_stack, entry.equip});
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

} // namespace opencraft::game
