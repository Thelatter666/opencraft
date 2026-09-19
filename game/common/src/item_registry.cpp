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
    // T-D46 armour. Both default to 0, which is what every non-armour entry
    // means; only the four `timber_*` pieces below pass them.
    double armor_points = 0.0;
    double armor_toughness = 0.0;
    // T-D59 attack. Both default to 0 as well - the "not a weapon" sentinel
    // (ItemDef's own comment; the fallback is the bare hand). Only the four
    // `timber_*` tools below pass them.
    double attack_damage = 0.0;
    double attack_speed = 0.0;
};

// ⚖ T-D46 ruling C-3: the `timber_*` pieces are the LEATHER tier's equivalent -
// 7 points over the set, split 1/2/3/1 (head/chest/legs/feet), toughness 0.
// Source: docs/research/01 §2 (「皮革 7（1/2/3/1）」; the base game gives leather
// and iron no toughness at all - it arrives with the diamond-tier sets).
//
// ⚠ Deliberately NOT registered here: the iron tier (15 = 2/5/6/2). T-R2's R-2
// ruling is that the launch set carries the leather equivalent and that a later
// content card adds the iron one, and inventing a wood-tier value in between is
// exactly what that ruling forbids.
constexpr double kLeatherTierHead = 1.0;
constexpr double kLeatherTierChest = 2.0;
constexpr double kLeatherTierLegs = 3.0;
constexpr double kLeatherTierFeet = 1.0;
constexpr double kLeatherTierToughness = 0.0;

// ⚖ T-D59 ruling C-4: the four timber tools' attack numbers, which is the item
// set's first tier of weaponry. Source: docs/research/01 §6.1 - its attack-speed
// table AND its 基础伤害 line, the game's own per-KIND values for the first
// tier (剑 4 / 斧 7 / 镐 2 / 锹 2.5 at 1.6 / 0.8 / 1.2 / 1.0).
//
// ⚠ Quoted, NOT composed. §9's Tiers sheet also carries a 伤害加成 column
// (木 0, 石 +1, 铁 +2, 钻 +3 …), but that is a MINING-tier attribute, and the
// two sheets do not add up to each other (the axe's 7 is not "base + 0" under
// any base that also yields the hoe's 1). C-4 says the same thing: use the
// finished values, never the tier column. The tier's remaining columns
// (durability 59, mining speed 2, harvest level 0) still have no home and wait
// for the mining card.
//
// T is derived, never stored: it is 20/attack_speed, so the pick's 16.666…
// stays a double all the way through (protocol.hpp's attack_charge_multiplier).
constexpr double kTimberSwordDamage = 4.0; // §6.1: 剑 木 4   ⇒ T = 12.5
constexpr double kTimberSwordSpeed = 1.6;  // §6.1: 剑 1.6
constexpr double kTimberAxeDamage = 7.0;   // §6.1: 斧 7      ⇒ T = 25
constexpr double kTimberAxeSpeed = 0.8;    // §6.1: 斧 0.8
constexpr double kTimberPickDamage = 2.0;  // §6.1: 镐 2      ⇒ T = 16.666…
constexpr double kTimberPickSpeed = 1.2;   // §6.1: 镐 1.2
constexpr double kTimberSpadeDamage = 2.5; // §6.1: 锹 2.5    ⇒ T = 20
constexpr double kTimberSpadeSpeed = 1.0;  // §6.1: 锹 1.0

// The two armour columns of an entry that passes no armour, spelled out so the
// tool rows below read as "0.0 armour, then the weapon numbers".
constexpr double kNoArmor = 0.0;

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
    // 星钻; "timber" is that system's first tier). T-D59 gave them their ⚖
    // attack numbers (C-4, the constants above) - a timber blade is a real
    // weapon now, and T-D46's "徒手杀一只 20 血生物要 12 秒" is the measurement
    // it is meant to move. Durability and harvest level are still unmodelled:
    // they are the mining card's, not this one's.
    {"timber_chisel", "Timber Chisel Pick", kStackLimitSingle, EquipSlot::None, nullptr, kNoArmor, kNoArmor,
     kTimberPickDamage, kTimberPickSpeed},
    {"timber_hewer", "Timber Hewing Axe", kStackLimitSingle, EquipSlot::None, nullptr, kNoArmor, kNoArmor,
     kTimberAxeDamage, kTimberAxeSpeed},
    {"timber_spade", "Timber Digging Spade", kStackLimitSingle, EquipSlot::None, nullptr, kNoArmor, kNoArmor,
     kTimberSpadeDamage, kTimberSpadeSpeed},
    {"timber_edge", "Timber Edge Blade", kStackLimitSingle, EquipSlot::None, nullptr, kNoArmor, kNoArmor,
     kTimberSwordDamage, kTimberSwordSpeed},

    // Armour, first tier. Present because the inventory's four armour slots
    // need something that is allowed to occupy them, and because one piece
    // per body part is what makes the slot constraint testable. T-D46 gave
    // them the ⚖ leather-tier numbers (C-3) - see the constants above.
    {"timber_headguard", "Timber Headguard", kStackLimitSingle, EquipSlot::Head, nullptr, kLeatherTierHead,
     kLeatherTierToughness},
    {"timber_cuirass", "Timber Cuirass", kStackLimitSingle, EquipSlot::Chest, nullptr, kLeatherTierChest,
     kLeatherTierToughness},
    {"timber_greaves", "Timber Greaves", kStackLimitSingle, EquipSlot::Legs, nullptr, kLeatherTierLegs,
     kLeatherTierToughness},
    {"timber_treads", "Timber Treads", kStackLimitSingle, EquipSlot::Feet, nullptr, kLeatherTierFeet,
     kLeatherTierToughness},
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
        registry.register_item(entry.id, {entry.display_name, entry.max_stack, entry.equip, block, entry.armor_points,
                                          entry.armor_toughness, entry.attack_damage, entry.attack_speed});
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
