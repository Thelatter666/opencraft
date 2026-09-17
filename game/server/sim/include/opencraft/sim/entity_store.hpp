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
#include "opencraft/game/mob_goal.hpp"

namespace opencraft::server {

// Stable handle to a live entity. 0 means "none"; slot indices are 1-based so
// that a default-constructed id is never valid.
using EntityId = std::uint32_t;

// "No entity", spelled early: MobAi below is declared BEFORE Entity (Entity
// holds a MobAi by value, so the order is forced) and therefore cannot name
// Entity::kNoId. Entity::kNoId is defined as this value, so there is still one
// number with one meaning.
inline constexpr EntityId kNoEntityId = 0;

// The player, as a target of a mob's goals (T-M2).
//
// The player is NOT an entity in this store - T-A1 left the player out of the
// authority's vocabulary on purpose - but a mob still has to be able to say
// "I am chasing THAT". Rather than widen EntityId to an optional pair
// everywhere, the actor gets one reserved id. It cannot collide with a real one:
// ids are slot index + 1, so a store would need 4 billion live slots.
inline constexpr EntityId kActorId = 0xFFFFFFFFu;

// Per-instance mob AI state (T-M2). It sits in every Entity because the store's
// slot layout is one struct per entity (T-E1's deliberate choice - what crosses
// a wire in M3 is one contiguous struct), and the drop rules simply never touch
// it.
//
// Everything here is state that the goal stack writes and the next tick reads.
// It is deliberately flat and copyable: no pointers, no containers - this is a
// snapshot payload.
struct MobAi {
    // What the goal stack has running (game/mob_goal.hpp).
    game::GoalMask running_goals = 0;

    // ── perception (research/11 §1.3.3) ─────────────────────────────────────
    // ⚖ The base game's sensors scan every 20 ticks with a random first delay;
    // R-12 recommends the same for the goal stack, and the reason is
    // behavioural, not just performance: a mob that re-rays every tick locks on
    // the instant you round a corner, which reads as robotic. The timer counts
    // down and is re-staggered per-mob on spawn, so 70 mobs do not all ray on
    // the same tick.
    int perception_timer = 0;
    // The snapshot: does the mob see the actor right now (range AND line of
    // sight), refreshed on the perception schedule.
    bool sees_actor = false;
    // ⚠ The distance is refreshed EVERY tick, not on the 20-tick schedule. It
    // is one subtraction against the raycast that costs a std::function and a
    // voxel walk, and two goals need it per tick: the fuse counts up and down
    // with the distance, and the tempt goal's start/stop thresholds are
    // distances. Only the sight snapshot is allowed to be stale.
    double actor_distance = 0.0;
    bool actor_known = false; // has the authority been told where the player is at all

    // ── targeting ───────────────────────────────────────────────────────────
    EntityId target = kNoEntityId;     // what the mob is going after (may be kActorId)
    EntityId revenge_on = kNoEntityId; // who last hurt it (⚖ 反击)
    int revenge_ticks = 0;             // how long the grudge lasts (⚠ 待校准 knob)
    int attack_cooldown = 0;           // ticks until the next melee hit (⚠ knob)
    int fuse = 0;                      // > 0 while this mob's fuse burns (⚖ 30 ticks)
    // ⚖ research/11 §1.5.1: 受击后的无敌帧 10 tick, during which damage no larger
    // than the last hit is ignored and a larger one settles only the difference
    // - which needs both the window and the amount that opened it.
    int hurt_cooldown = 0;
    double last_hurt_amount = 0.0;

    // ── breeding (research/11 §6.2) ─────────────────────────────────────────
    bool in_love = false;
    int love_ticks = 0;     // ⚖ 求偶超时 30 秒; counts down while looking
    int mating_ticks = 0;   // ⚖ 交配约 2.5 秒 of contact
    int breed_cooldown = 0; // ⚖ 5 分钟
    bool baby = false;
    int growth_ticks = 0; // ⚖ 幼体成长 24000 tick; 0 when adult

    // ── panic / fleeing (research/11 §1.5.7) ────────────────────────────────
    int panic_ticks = 0;
    int flee_check_timer = 0;

    // ── navigation ──────────────────────────────────────────────────────────
    // ⚠ This is the SIMPLIFIED pathfinder the card allows (see mob_sim.hpp's
    // header note): a destination plus a persistent side preference, not a node
    // path. `nav_side` is what turns "blocked, try a detour" into wall
    // following - without it a mob oscillates left/right against a wall.
    glm::dvec3 move_target{0.0, 0.0, 0.0};
    bool has_move_target = false;
    int nav_side = 0;
    int nav_stuck_ticks = 0;
    glm::dvec3 nav_last_position{0.0, 0.0, 0.0};

    // ── presentation ────────────────────────────────────────────────────────
    // The yaw the renderer draws the mob with. It is simulation state rather
    // than a render-only value because the LookAtPlayer goal WRITES it, and
    // "the mob turned to face you" is the observable output of that goal.
    double yaw = 0.0;
    double pitch = 0.0;

    // The RNG stream for this mob, advanced by the goals that need randomness
    // (stroll destination, panic direction). Per-mob rather than global so one
    // mob's decisions do not shift another's.
    std::uint64_t rng = 0;
};

// One entity. Everything an entity has is here; the per-type numbers live in
// game::EntityDef and are looked up by `type`.
//
// The position is the CENTRE OF THE FEET (bottom-centre of the AABB), the
// convention physics::PlayerState established - so "the entity stands at y" and
// "the entity's box starts at y" are the same statement for every entity kind.
struct Entity {
    static constexpr EntityId kNoId = kNoEntityId;

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
    // (research/11 §4.4). A drop is explicitly NOT attackable.
    //
    // ⚠ double, not int (T-M2): a mob's damage is a FRACTION of a point in the
    // sources (research/01 §10.2 gives the zombie 2.5 on easy), and truncating
    // it would silently change a ⚖ number. Every pre-existing use is unaffected
    // - the drop's 5 points and "health <= 0" read the same either way.
    double health = 0.0;

    // Item payload. Only meaningful when the type's carries_item_stack is set.
    game::ItemStack stack{};

    // Mob AI state. Only meaningful when the type's entity_class is Mob - the
    // drop rules never read or write it (T-M2).
    MobAi ai{};

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
