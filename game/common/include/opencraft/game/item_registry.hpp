#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "opencraft/game/protocol.hpp"

namespace opencraft::game {

// Identical in shape to voxel::StringHash: a transparent hash so find_id()
// can be handed a string_view without materialising a std::string. Kept
// local to this namespace so the item layer does not have to include the
// block layer's header (the two registries are independent).
struct StringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
};

// Equipment slots an item may occupy (docs/01 §5: 4 armour slots + offhand).
// `None` means "not equippable": the item only ever lives in the storage
// sections (hotbar/main) or the offhand.
//
// Why this exists in T-I1: the inventory must reject a non-armour item in an
// armour slot, which is not decidable from a max stack size. Harvest tier and
// durability of docs/01 §5 are still not modelled -- the mining card adds those;
// T-D59 added the two ATTACK numbers below.
enum class EquipSlot : std::uint8_t {
    None = 0,
    Head,
    Chest,
    Legs,
    Feet,
};

// ── Stack limits (docs/01 §5 ⚖: 64 / 16 / 1) ──────────────────────────────
inline constexpr int kStackLimitLarge = 64;
inline constexpr int kStackLimitMedium = 16;
inline constexpr int kStackLimitSingle = 1;

// True for the three tiers docs/01 §5 declares. register_item() only rejects
// max_stack < 1 -- a later card may need a fourth tier, and forcing the value
// here would turn a content question into a breaking interface change. The
// default set stays inside the three documented tiers (asserted in tests).
[[nodiscard]] constexpr bool is_standard_stack_limit(int max_stack) {
    return max_stack == kStackLimitLarge || max_stack == kStackLimitMedium || max_stack == kStackLimitSingle;
}

// ── Item -> block link (T-I2, T-I1 ruling S-2) ─────────────────────────────
//
// The sentinel for "this item is not placed as a block" (food, materials,
// tools, armour, containers). Deliberately NOT 0: block id 0 is air, a legal
// value, so 0 would make "places air" and "places nothing" indistinguishable.
// Tests assert it differs from BlockRegistry::kAirId; this header stays free
// of the block layer's header, exactly like StringHash above.
inline constexpr std::uint16_t kNoBlock = 0xFFFF;

// Static description of an item type. Data only: no per-stack state, no
// components/NBT (M2c content cards add what they actually need).
struct ItemDef {
    std::string display_name;
    int max_stack = kStackLimitLarge;
    EquipSlot equip = EquipSlot::None;
    // The block this item places, or kNoBlock. Read-only: fixed when the item
    // is registered, never written afterwards (S-2). These are BLOCK ids, so
    // they are only meaningful against the block registry the item set was
    // built for -- see ItemRegistry::create_default().
    std::uint16_t block = kNoBlock;

    // ── T-D46 armour (the four fields above are unchanged) ───────────────────
    // ⚖ docs/research/01 §2: the armour POINTS a worn piece contributes. The
    // leather set is 7 over its four pieces (1/2/3/1), iron 15 (2/5/6/2); the
    // launch set's `timber_*` pieces carry the leather numbers (T-D46 ruling
    // C-3). 0.0 for everything that is not a piece of armour, which is every
    // other item in the registry.
    //
    // double, not int: the reduction formula divides, and the player's own hit
    // points are a double for the same reason (physics/player_state.hpp) - one
    // type for the quantity avoids a cast at every use.
    double armor_points = 0.0;
    // ⚖ The `min(toughness, 20)` term of the same formula. Leather AND iron are
    // both 0 in the base game (toughness arrives with the diamond-tier sets), so
    // this is 0.0 for every registered item -- the field exists because the
    // formula has the term and a later set will use it, not because anything
    // shipping reads a non-zero value.
    double armor_toughness = 0.0;

    // ── T-D59 attack (the four fields above are unchanged) ──────────────────
    // 0.0 is the "not a weapon" sentinel and it is not a neutral value: it means
    // "fall back to the bare hand", which is kPunchDamage 1.0 and
    // kPunchAttackSpeed 4.0. Read them through attack_damage_of/attack_speed_of
    // below rather than testing the sentinel at the call site - the fallback is
    // one rule and belongs in one place.
    //
    // Both are doubles like armor_points, for the same reason: attack_speed is
    // divided into a tick period (a pick's T is 16.666…, which an int would
    // round) and attack_damage is multiplied by a fractional charge.
    //
    // ⚖ research/01 §6.1's own damage/speed tables, NOT the §9 Tiers sheet: a
    // tier's 伤害加成 is a mining-tier attribute, and the base game's per-weapon
    // damage is not that column plus a base. The four timber tools carry the
    // base game's numbers for their kinds (item_registry.cpp).
    double attack_damage = 0.0;
    double attack_speed = 0.0;
};

// ── the "not a weapon" fallback, resolved in one place ─────────────────────
// Everything that is not a tool - food, blocks, a vessel, armour, the empty
// hand itself - swings and hits exactly as a bare hand does. The numbers are
// protocol.hpp's, so the item layer and the authority cannot drift apart about
// what a fist is worth.

[[nodiscard]] constexpr double attack_damage_of(const ItemDef &def) {
    return def.attack_damage > 0.0 ? def.attack_damage : kPunchDamage;
}

[[nodiscard]] constexpr double attack_speed_of(const ItemDef &def) {
    return def.attack_speed > 0.0 ? def.attack_speed : kPunchAttackSpeed;
}

// String-id to runtime numeric-id mapping (docs/03 §7), mirroring the
// BlockRegistry conventions: dense u16 ids, id 0 reserved, duplicate/empty
// ids throw, both query directions, transparent string_view lookup.
//
// Item ids are separate from block ids: an item is a thing a player can hold,
// a block is a thing in the world. The two id spaces are independent, so a
// block's numeric id must never be used to index this registry.
class ItemRegistry {
public:
    // 0 is reserved for "nothing in hand" / the empty slot, mirroring
    // BlockRegistry::kAirId. It is a real registered entry (string id
    // "empty"), so string_of(0) and def_of(0) stay total.
    static constexpr std::uint16_t kEmptyId = 0;

    // A registry pre-seeded with the reserved empty entry plus the launch
    // item set (see item_registry.cpp for the table and its provenance).
    [[nodiscard]] static ItemRegistry create_default();

    ItemRegistry();

    // Registers a new item and returns its numeric id. Throws
    // std::invalid_argument on an empty id, a duplicate registration, or a
    // max_stack < 1; std::overflow_error when the u16 id space is exhausted.
    std::uint16_t register_item(std::string id, ItemDef def);

    [[nodiscard]] bool has_id(std::string_view id) const;
    [[nodiscard]] bool has_numeric(std::uint16_t numeric_id) const;

    // id -> numeric id. nullopt for unknown ids.
    [[nodiscard]] std::optional<std::uint16_t> find_id(std::string_view id) const;
    // Throwing variant; std::out_of_range for unknown ids.
    [[nodiscard]] std::uint16_t id_of(std::string_view id) const;

    // numeric id -> string id. Throwing variant; std::out_of_range for
    // unknown numeric ids.
    [[nodiscard]] const std::string &string_of(std::uint16_t numeric_id) const;

    [[nodiscard]] const ItemDef &def_of(std::uint16_t numeric_id) const;

    // The item a block leaves behind when it is broken, or nullopt when the
    // block has none (air, water - research/01 §7 / T-I1 ruling S-3: water is
    // carried in a vessel and is not an item). T-E1's drop rule: the authority
    // asks this when a dig succeeds.
    //
    // Scans in ascending item id so an item set that maps two items to one
    // block still answers deterministically; the launch set is 1:1. The block
    // id is a BLOCK numeric id, meaningful only against the block registry this
    // item set was built for (see create_default).
    [[nodiscard]] std::optional<std::uint16_t> item_for_block(std::uint16_t block) const;

    [[nodiscard]] std::uint16_t empty() const { return kEmptyId; }

    [[nodiscard]] std::size_t size() const { return defs_.size(); }

private:
    std::unordered_map<std::string, std::uint16_t, StringHash, std::equal_to<>> numeric_by_id_;
    std::vector<std::string> id_by_numeric_;
    std::vector<ItemDef> defs_;
};

} // namespace opencraft::game
