#pragma once

// T-E1: the entity TYPE registry - the "what kinds of entity exist" half of the
// entity layer docs/03 §6 has been asking for since M0 (实体管理; 物理参数按实体
// 类型实例化). Before this card the repository had no entity layer at all
// (PM's 2026-09-17 audit: `find engine game -iname "*entit*"` was empty).
//
// Deliberately the same shape as BlockRegistry and ItemRegistry: dense u16
// numeric ids, id 0 reserved, string ids as the content handle, both query
// directions, throwing plus non-throwing lookups, a default launch set built by
// create_default(). Consistency here is worth more than novelty - the next
// entity card (mobs) has to read this the way it already reads the other two.
//
// ── WHY NOT AN ECS (docs/03 §6 names EnTT) ──────────────────────────────────
// The card rules it out, and the reason is structural: every field a
// networked authority has to ship is a field an ECS would hide behind
// component ids. The storage (server::EntityStore) is a dense slot pool and
// this registry is what keeps it extensible by content instead of by schema.
// M3 can revisit once the wire format exists.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "opencraft/game/item_registry.hpp" // game::StringHash (one definition per namespace)

namespace opencraft::game {

// Static description of an entity type. Data only, exactly like BlockDef /
// ItemDef: no per-instance state, no behaviour switch inside.
struct EntityDef {
    std::string display_name;

    // ── collision box, in blocks ────────────────────────────────────────────
    // Feet-bottom-centre origin, the convention physics::PlayerState uses
    // (position IS the bottom centre, not the centre of mass). A dropped item
    // is a 0.25 x 0.25 x 0.25 box (research/11 §4.2), so half_width 0.125 and
    // height 0.25.
    double half_width = 0.125;
    double height = 0.25;

    // ── per-tick motion (research/11 §4.1) ──────────────────────────────────
    // ⚠ NOT the player's numbers, and the difference is load-bearing:
    //   * gravity -0.04, not -0.08 (a falling block's value), and
    //   * horizontal drag 0.98, not 0.91 - which is why a drop slides far
    //     further than a player before it stops.
    // The values live here, on the type, because docs/03 §6 freezes "物理参数
    // 按实体类型实例化，非全局单例": one flat config shared by every entity
    // cannot express a drop and a mob at once.
    double gravity = 0.04;
    double vertical_drag = 0.98;
    double horizontal_drag = 0.98;

    // ── damage (research/11 §4.4) ───────────────────────────────────────────
    // A drop has 5 HP; fire / lava / cactus / explosions remove all of it.
    // Nothing may attack a drop AS A TARGET - that rule needs no field, it
    // needs the absence of a damage path, which is why this card adds none.
    int max_health = 5;

    // True for the entity kinds whose payload is a game::ItemStack. The store
    // holds the stack either way (one struct, one slot layout); this flag is
    // what tells a reader whether the stack means anything.
    bool carries_item_stack = false;
};

// String-id to runtime numeric-id mapping (docs/03 §7), mirroring the other two
// registries: dense u16 ids, id 0 reserved, duplicate/empty ids throw.
class EntityTypeRegistry {
public:
    // 0 is reserved for "no entity" the way BlockRegistry::kAirId is air and
    // ItemRegistry::kEmptyId is an empty cell. It is a real registered entry
    // (string id "empty"), so string_of(0) and def_of(0) stay total.
    static constexpr std::uint16_t kEmptyId = 0;

    // A registry pre-seeded with the reserved empty entry plus the launch
    // entity set (see entity_type.cpp for the table and its provenance).
    [[nodiscard]] static EntityTypeRegistry create_default();

    EntityTypeRegistry();

    // Registers a new type and returns its numeric id. Throws
    // std::invalid_argument on an empty id, a duplicate registration, or a
    // non-positive box; std::overflow_error when the u16 id space is exhausted.
    std::uint16_t register_type(std::string id, EntityDef def);

    [[nodiscard]] bool has_id(std::string_view id) const;
    [[nodiscard]] bool has_numeric(std::uint16_t numeric_id) const;

    // id -> numeric id. nullopt for unknown ids.
    [[nodiscard]] std::optional<std::uint16_t> find_id(std::string_view id) const;
    // Throwing variant; std::out_of_range for unknown ids.
    [[nodiscard]] std::uint16_t id_of(std::string_view id) const;

    // numeric id -> string id. Throwing variant; std::out_of_range for unknown
    // numeric ids.
    [[nodiscard]] const std::string &string_of(std::uint16_t numeric_id) const;

    [[nodiscard]] const EntityDef &def_of(std::uint16_t numeric_id) const;

    [[nodiscard]] std::uint16_t empty() const { return kEmptyId; }

    [[nodiscard]] std::size_t size() const { return defs_.size(); }

private:
    std::unordered_map<std::string, std::uint16_t, StringHash, std::equal_to<>> numeric_by_id_;
    std::vector<std::string> id_by_numeric_;
    std::vector<EntityDef> defs_;
};

} // namespace opencraft::game
