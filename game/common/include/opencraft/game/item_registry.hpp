#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
// armour slot, which is not decidable from a max stack size. Durability /
// damage / harvest tier of docs/01 §5 are deliberately NOT modelled -- a
// later card adds them.
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

// Static description of an item type. Data only: no per-stack state, no
// components/NBT (M2c content cards add what they actually need).
struct ItemDef {
    std::string display_name;
    int max_stack = kStackLimitLarge;
    EquipSlot equip = EquipSlot::None;
};

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

    [[nodiscard]] std::uint16_t empty() const { return kEmptyId; }

    [[nodiscard]] std::size_t size() const { return defs_.size(); }

private:
    std::unordered_map<std::string, std::uint16_t, StringHash, std::equal_to<>> numeric_by_id_;
    std::vector<std::string> id_by_numeric_;
    std::vector<ItemDef> defs_;
};

} // namespace opencraft::game
