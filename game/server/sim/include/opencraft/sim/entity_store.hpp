#pragma once

// T-E1: the entity STORAGE - the "where does entity state live" half of the
// entity layer (docs/03 §6: 实体与方块在不同系统面 - entities are NOT in chunk
// state; 实体遍历为确定顺序).
//
// ── STRUCTURE CHOICE (the report asks for this explicitly) ──────────────────
// A dense slot pool: `slots_` is one flat vector, a slot is alive or free, and
// an EntityId is `slot index + 1` (0 is "no entity"). Reasons, in the order
// they mattered:
//
//   1. DETERMINISTIC ITERATION is a spec requirement (docs/03 §6), and it has
//      to be a property of the container, not of every call site. Ascending
//      slot order IS ascending id order, so for_each_entity() is deterministic
//      by construction - an unordered_map (the other obvious choice, since
//      lookup by id is the hot operation) would make iteration order depend on
//      the hash, and a std::map would pay a node allocation per entity.
//   2. The state crosses a wire in M3 (the authority owns entities), so the
//      fewer indirections between "an entity" and "its bytes", the better: one
//      contiguous struct per entity is exactly the payload a snapshot ships.
//   3. Ids are STABLE and REUSED: erase() returns the slot to a free list and a
//      later spawn reuses it. That is why every cross-boundary reference to an
//      entity (the PickUp request) carries the expected item id as well - see
//      protocol.hpp. In-process the reuse is not observable (the client reads
//      and asks inside one tick, and only tick()/submit() mutate the store),
//      but a network channel makes it observable, and the check costs nothing.
//
// What this deliberately is NOT: an ECS. There are no components, no archetypes
// and no query language - the card rules EnTT out (docs/03 §6 mentions it), and
// with one entity kind reachable today the abstraction would be all cost. The
// type registry (game/entity_type.hpp) is what keeps the door open.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "opencraft/game/item_stack.hpp"

namespace opencraft::server {

// Stable handle to a live entity. 0 means "none"; slot indices are 1-based so
// that a default-constructed id is never valid.
using EntityId = std::uint32_t;

// One entity. Everything an entity has is here; the per-type numbers live in
// game::EntityDef and are looked up by `type`.
//
// The position is the CENTRE OF THE FEET (bottom-centre of the AABB), the
// convention physics::PlayerState established - so "the entity stands at y" and
// "the entity's box starts at y" are the same statement for every entity kind.
struct Entity {
    static constexpr EntityId kNoId = 0;

    EntityId id = kNoId;
    std::uint16_t type = 0; // game::EntityTypeRegistry id

    glm::dvec3 position{0.0, 0.0, 0.0}; // feet centre, blocks
    glm::dvec3 velocity{0.0, 0.0, 0.0}; // blocks/tick
    bool on_ground = false;

    // Ticks this entity has been stepped. Counted only while its chunk is in
    // memory, which is precisely the ⚖ despawn rule (research/11 §4.4: 仅在已
    // 加载且处理实体的区块内计时，区块卸载则暂停) - one counter, not two.
    int age = 0;
    // Ticks until it may be picked up (10 natural / 40 thrown, research/11
    // §4.2). Counted down with `age`, so it pauses with it too.
    int pickup_delay = 0;
    // Ticks until the next merge attempt (research/11 §4.3: every 40, or every
    // 2 while the entity keeps crossing block boundaries).
    int merge_timer = 0;
    // The block the entity stood in at the end of the previous tick; the
    // crossing test that re-arms the merge timer above.
    glm::ivec3 last_block{0, 0, 0};
    // Hit points from the type's max_health; environment contact removes them
    // (research/11 §4.4). Nothing else damages an entity in this card - a drop
    // is explicitly NOT attackable.
    int health = 0;

    // Item payload. Only meaningful when the type's carries_item_stack is set.
    game::ItemStack stack{};

    bool alive = false;
};

// The slot pool. Not copyable-cheap and not meant to be: it is the entity state
// itself. Pointers returned by find() stay valid until the next spawn (erase
// marks a slot, it never moves memory), which the simulation relies on.
class EntityStore {
public:
    static constexpr EntityId kNoEntity = Entity::kNoId;

    // Creates one entity and returns its id; never 0.
    [[nodiscard]] EntityId spawn(const Entity &entity);

    // Kills the entity and frees its slot. False when the id was not live.
    bool erase(EntityId id);

    // Live entity or nullptr. The pointer must not be held across a spawn.
    [[nodiscard]] Entity *find(EntityId id);
    [[nodiscard]] const Entity *find(EntityId id) const;

    [[nodiscard]] std::size_t alive_count() const { return alive_; }

    // Slots in use, live or free - the iteration stride, not the population.
    [[nodiscard]] std::size_t slot_count() const { return slots_.size(); }

    // Visits every live entity in ASCENDING id order (docs/03 §6: 实体遍历为
    // 确定顺序). Erasing the visited entity inside the visitor is allowed: the
    // loop walks the slots by index and re-reads the alive flag per slot, so a
    // slot killed by the visitor is simply skipped. Spawning inside the visitor
    // is NOT allowed (it may reallocate `slots_` while the loop iterates it).
    template <typename Fn>
    void for_each_entity(Fn &&visit) {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].alive) {
                visit(slots_[i]);
            }
        }
    }

    template <typename Fn>
    void for_each_entity(Fn &&visit) const {
        for (const Entity &entity : slots_) {
            if (entity.alive) {
                visit(entity);
            }
        }
    }

    // Every live id, ascending. The readonly-view filter callers (the client,
    // the tests) use this to snapshot what they are about to read, so nothing
    // iterates the store while a submission mutates it.
    [[nodiscard]] std::vector<EntityId> live_ids() const;

    void clear();

private:
    [[nodiscard]] static std::size_t index_of(EntityId id) { return static_cast<std::size_t>(id - 1); }

    std::vector<Entity> slots_;
    // Free slots, most-recently-freed first (LIFO). Reuse is deliberate - see
    // the file header.
    std::vector<EntityId> free_ids_;
    std::size_t alive_ = 0;
};

// ── Implementation ──────────────────────────────────────────────────────────
// Header-only on purpose: the entity layer is new state, not a new library
// target, and this card's write scope is game/server/sim/** (the library's
// source list lives one directory up).

inline EntityId EntityStore::spawn(const Entity &entity) {
    EntityId id = 0;
    if (!free_ids_.empty()) {
        id = free_ids_.back();
        free_ids_.pop_back();
        slots_[index_of(id)] = entity;
    } else {
        id = static_cast<EntityId>(slots_.size()) + 1;
        slots_.push_back(entity);
    }
    slots_[index_of(id)].id = id;
    slots_[index_of(id)].alive = true;
    ++alive_;
    return id;
}

inline bool EntityStore::erase(const EntityId id) {
    if (id == kNoEntity || index_of(id) >= slots_.size() || !slots_[index_of(id)].alive) {
        return false;
    }
    slots_[index_of(id)].alive = false;
    free_ids_.push_back(id);
    --alive_;
    return true;
}

inline Entity *EntityStore::find(const EntityId id) {
    if (id == kNoEntity || index_of(id) >= slots_.size() || !slots_[index_of(id)].alive) {
        return nullptr;
    }
    return &slots_[index_of(id)];
}

inline const Entity *EntityStore::find(const EntityId id) const {
    if (id == kNoEntity || index_of(id) >= slots_.size() || !slots_[index_of(id)].alive) {
        return nullptr;
    }
    return &slots_[index_of(id)];
}

inline std::vector<EntityId> EntityStore::live_ids() const {
    std::vector<EntityId> ids;
    ids.reserve(alive_);
    for (const Entity &entity : slots_) {
        if (entity.alive) {
            ids.push_back(entity.id);
        }
    }
    return ids;
}

inline void EntityStore::clear() {
    slots_.clear();
    free_ids_.clear();
    alive_ = 0;
}

} // namespace opencraft::server
