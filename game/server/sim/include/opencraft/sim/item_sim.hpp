#pragma once

// T-E1: the dropped-item simulation - the rules of research/11 §4.1-4.4 on top
// of the entity store. Header-only, like the store: this card's write scope is
// game/server/sim/**, and the sim library's source list lives one directory up.
//
// Everything here is a pure function of (store, world, rules, registries): the
// caller (server::WorldSim) owns the state and decides WHEN a step runs, this
// decides WHAT a step is. That split is what makes the rules unit-testable
// against a hand-built block world with no chunks, no player and no window.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "opencraft/game/entity_type.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"
#include "opencraft/physics/block_source.hpp"
#include "opencraft/physics/sweep.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk.hpp"

namespace opencraft::server {

// What the drop simulation needs from the world: collision (physics::IBlockSource
// - the same seam the player moves through), the block a cell holds (for the
// environment rules) and whether a chunk is in memory (for the paused timing).
//
// Unloaded chunks are never stepped, so the collision queries below only ever
// see loaded ones; an implementation is free to treat unloaded chunks as solid
// (server::WorldSim does, which is what keeps a walking entity out of the
// streaming void).
struct IItemWorld : physics::IBlockSource {
    [[nodiscard]] virtual std::uint16_t block_at(int wx, int wy, int wz) const = 0;
    [[nodiscard]] virtual bool chunk_loaded(int cx, int cz) const = 0;
};

// Every number the item drop rules use, in one place, each with the section of
// research/11 it comes from. Defaults ARE the shipping values.
//
// The three ⚠ items are the ones T-D36 lists as uncalibrated; they are marked
// here (as the card requires: 两处都在代码注释里写明"待校准，来源见 T-D36") and
// reported, NOT invented.
struct ItemRules {
    // ── lifecycle (research/11 §4.4) ────────────────────────────────────────
    // ⚖ 6000 ticks = 5 minutes. Counted only while the entity's chunk is in
    // memory - see step_items.
    int despawn_ticks = 6000;
    // ⚖ 落入虚空: below (world minimum building height - 64) = -128 in the
    // overworld. Unreachable in today's world (bedrock floor at y = 0, unloaded
    // columns read as solid) and kept anyway: it is the rule, and the world
    // span changes in M3.
    double void_y = -128.0;

    // ── pickup (research/11 §4.2) ───────────────────────────────────────────
    // ⚖ natural drops (block break) 10 ticks; player-thrown 40 ticks. The 40 is
    // INTERFACE ONLY in this card (不做玩家投掷物品) - the field exists so the
    // throwing card does not have to touch the store.
    int pickup_delay_natural = 10;
    int pickup_delay_thrown = 40;

    // ── merging (research/11 §4.3) ──────────────────────────────────────────
    // ⚖ every 40 ticks; every 2 while the drop keeps crossing block boundaries;
    // "after a teleport, immediately" has no teleport to hang off yet.
    int merge_period = 40;
    int merge_period_crossing = 2;
    // ⚖ The merge volume is 0.5 x 0.25 x 0.5 - the box the wiki names. Read as
    // an ABSOLUTE box centred on the drop's own box (so two drops merge when
    // their centres are within ~0.375 blocks horizontally), not as an inflation
    // of it. ⚠ 待校准: the source states the box size and nothing else, and MC's
    // own growth value would read 0.75 rather than 0.375; see the report.
    double merge_box_x = 0.5;
    double merge_box_y = 0.25;
    double merge_box_z = 0.5;

    // ── spawn pop (§4.5; ⚠ no source in research/11) ────────────────────────
    // The drop leaves the broken block's centre with a small upward velocity.
    // ⚠ 待校准: the value is the shape of MC's item pop (a hop you can see), not
    // a number this repository can cite. The horizontal part of that pop is
    // deliberately left out: a jitter would need an RNG in the authority's
    // spawn path, and nothing in the card needs it.
    double spawn_velocity_y = 0.2;

    // ── collision response ──────────────────────────────────────────────────
    // ★ T-D36 待校准项 1 - 弹性系数 (restitution): the wiki gives NO coefficient
    // for drops (neither the Item (entity) page nor the Entity page), so it is 0
    // here: contact stops the drop instead of bouncing it. A non-zero value is
    // the calibration knob; the collision clamps below multiply by it, so the
    // day the figure is found only this number changes.
    double restitution = 0.0;
    // ★ T-D36 待校准项 2 - 着地摩擦修正: the player's ground retention is
    // horizontal_drag x friction (0.91 x S). Whether a drop gets the same
    // correction is NOT stated by the source; this card applies it with the
    // drop's own drag, i.e. 0.98 x S. That is what step_item_motion computes
    // (def.horizontal_drag x slipperiness) - there is no separate constant to
    // keep in step, and the calibration is "does a drop skid like this on
    // stone", not a number swap.
    //
    // Momentum cut-off: the player snaps |v| < 0.003 to 0 per axis
    // (research/06 §1.1). ⚠ 待校准 whether a drop is truncated the same way -
    // applied here so a drop actually comes to rest instead of creeping for the
    // rest of its 5 minutes.
    double momentum_threshold = 0.003;
    // Displacement is split into substeps of at most this many blocks so a fast
    // drop cannot tunnel through a floor (docs/03 §6.2 - the player's rule).
    double max_substep = 0.5;
    // Depth probed below the feet to find the block whose slipperiness drives
    // this tick's friction (the player's PhysicsConfig value).
    double ground_probe_depth = 0.01;
};

// ── environment damage (research/11 §4.4) ───────────────────────────────────
//
// Block types that destroy a dropped item on contact.
//
// ⚠ The DAMAGING BLOCKS of the mob pathfinder table (岩浆块 / 营火 / 甜浆果丛 /
// 凋零玫瑰 / 细雪, research/11 §1.4.2) are deliberately absent, and that absence
// IS the rule (§4.4's warning: 伤害方块不伤害掉落物). One list - "what destroys a
// drop" - makes the negative rule hold by construction rather than by
// remembering to exclude five names.
//
// ⚠ None of the three exists in the launch block registry, so in-game the rule
// is currently inert; the tests drive it through a registry that does have them.
// The predicate is keyed by STRING id so that the day a block is registered
// under one of these names the rule arms itself with no code change - the same
// reason ItemRegistry resolves its block links by string.
[[nodiscard]] inline bool block_id_destroys_items(const std::string_view block_id) {
    return block_id == "fire" || block_id == "lava" || block_id == "cactus";
}

// The predicate above, resolved once per block id. A per-step string lookup at
// every cell a drop occupies would be a hash per cell per tick (the T-F1
// lesson: a new per-voxel world query needs a cache in front of it); the
// classification is fixed at construction, so this is a bit lookup.
class ItemBlockHazard {
public:
    explicit ItemBlockHazard(const voxel::BlockRegistry &registry) : destroys_(registry.size(), false) {
        for (std::uint16_t id = 0; id < registry.size(); ++id) {
            destroys_[id] = block_id_destroys_items(registry.string_of(id));
        }
    }

    [[nodiscard]] bool destroys(const std::uint16_t block_id) const {
        return block_id < destroys_.size() && destroys_[block_id];
    }

private:
    std::vector<bool> destroys_;
};

// The AABB of an entity of this type, from its feet-centre position -
// `physics::box_of` (T-D40), so the drop is described by the same box type and
// the same arithmetic the player and every future mob use.
[[nodiscard]] inline physics::Box entity_box(const Entity &entity, const game::EntityDef &def) {
    return physics::box_of(entity.position, def.half_width, def.height);
}

// The same box in the (min, max) dvec3 form the shared pickup predicate speaks
// (game/pickup.hpp) - what a request would carry over a wire. One conversion
// here rather than one per call site, so the authority and the client cannot
// disagree about which corner is which.
[[nodiscard]] inline std::pair<glm::dvec3, glm::dvec3> entity_box_corners(const Entity &entity,
                                                                          const game::EntityDef &def) {
    const physics::Box box = entity_box(entity, def);
    return {glm::dvec3{box.min_x, box.min_y, box.min_z}, glm::dvec3{box.max_x, box.max_y, box.max_z}};
}

namespace item_detail {

// Slipperiness of the block under the drop - the block half of the friction
// product (docs/03 §6 / research/07 §7.2). Probed one layer below the feet.
[[nodiscard]] inline double slipperiness_below(const physics::IBlockSource &world, const Entity &e,
                                               const ItemRules &rules) {
    return world.slipperiness_at(static_cast<int>(std::floor(e.position.x)),
                                 static_cast<int>(std::floor(e.position.y - rules.ground_probe_depth)),
                                 static_cast<int>(std::floor(e.position.z)));
}

} // namespace item_detail

// One tick of a drop's motion: ★ A → P → D, in that order.
//
// ⚠ The execution order is the ONE thing that differs from the player, and the
// card calls it out because getting it wrong moves the landing spot
// (research/11 §4.1):
//
//     player / mob : position → accelerate → damp      (P → A → D)
//     drop         : accelerate → position → damp      (A → P → D)
//
// Both satisfy the docs/03 §6 friction contract (阻尼施加在位移之后); the drop
// merely accelerates BEFORE the displacement instead of after it. It is visible
// in the very first tick from rest: the drop has already fallen gravity
// (0.04 blocks) where a player in the same state has not moved at all.
inline void step_item_motion(Entity &e, const game::EntityDef &def, const physics::IBlockSource &world,
                             const ItemRules &rules) {
    // ── A: acceleration ─────────────────────────────────────────────────────
    e.velocity.y -= def.gravity;

    // ── P: displacement, per-axis clamped, Y → X → Z, in substeps ───────────
    e.on_ground = false;
    const double max_component = std::max({std::abs(e.velocity.x), std::abs(e.velocity.y), std::abs(e.velocity.z)});
    int substeps = static_cast<int>(std::ceil(max_component / rules.max_substep));
    if (substeps < 1) {
        substeps = 1;
    }
    for (int i = 0; i < substeps; ++i) {
        const physics::AxisSweep y =
            physics::sweep_axis_y(e.position, def.half_width, def.height, world, e.velocity.y / substeps);
        // ★ restitution (T-D36 项 1) is the whole collision response: 0 stops
        // the drop dead on contact, a calibrated value would bounce it back.
        // It stays HERE, not in the shared sweep: the player answers the same
        // clamp by zeroing the component instead (T-D40).
        if (y.hit) {
            e.velocity.y = -e.velocity.y * rules.restitution;
        }
        const physics::AxisSweep x =
            physics::sweep_axis_x(e.position, def.half_width, def.height, world, e.velocity.x / substeps);
        if (x.hit) {
            e.velocity.x = -e.velocity.x * rules.restitution;
        }
        const physics::AxisSweep z =
            physics::sweep_axis_z(e.position, def.half_width, def.height, world, e.velocity.z / substeps);
        if (z.hit) {
            e.velocity.z = -e.velocity.z * rules.restitution;
        }
        if (y.ground) {
            e.on_ground = true;
        }
    }

    // ── D: damping, AFTER the displacement (docs/03 §6 contract) ────────────
    // Ground retention is horizontal_drag x S and the air value is
    // horizontal_drag alone (S ≡ 1.0 in the air, the player's rule) - the
    // T-D36 校准项 2 sits in that product, see ItemRules.
    const double slipperiness = e.on_ground ? item_detail::slipperiness_below(world, e, rules) : 1.0;
    const double horizontal = def.horizontal_drag * slipperiness;
    e.velocity.x *= horizontal;
    e.velocity.z *= horizontal;
    e.velocity.y *= def.vertical_drag;

    if (std::abs(e.velocity.x) < rules.momentum_threshold) {
        e.velocity.x = 0.0;
    }
    if (std::abs(e.velocity.y) < rules.momentum_threshold) {
        e.velocity.y = 0.0;
    }
    if (std::abs(e.velocity.z) < rules.momentum_threshold) {
        e.velocity.z = 0.0;
    }
}

// ── the passes ──────────────────────────────────────────────────────────────

namespace item_detail {

// Does the drop's box overlap a block that destroys it (research/11 §4.4)?
[[nodiscard]] inline bool touches_hazard(const IItemWorld &world, const ItemBlockHazard &hazard,
                                         const physics::Box &b) {
    const int x0 = static_cast<int>(std::floor(b.min_x));
    const int x1 = static_cast<int>(std::floor(b.max_x));
    const int y0 = static_cast<int>(std::floor(b.min_y));
    const int y1 = static_cast<int>(std::floor(b.max_y));
    const int z0 = static_cast<int>(std::floor(b.min_z));
    const int z1 = static_cast<int>(std::floor(b.max_z));
    for (int bx = x0; bx <= x1; ++bx) {
        for (int by = y0; by <= y1; ++by) {
            for (int bz = z0; bz <= z1; ++bz) {
                if (b.max_x > bx && b.min_x < bx + 1 && b.max_y > by && b.min_y < by + 1 && b.max_z > bz &&
                    b.min_z < bz + 1 && hazard.destroys(world.block_at(bx, by, bz))) {
                    return true;
                }
            }
        }
    }
    return false;
}

// ⚖ research/11 §4.3: same type, same item, stackable, and the sum must still
// fit the stack limit - the card does NOT allow a partial merge (a pair that
// would overflow is left alone instead of filling one and creating a remainder).
// The merge box is read as an absolute 0.5 x 0.25 x 0.5 volume centred on the
// caller's own box, so the check is asymmetric by construction: `a` is the drop
// whose timer came due. For two 0.25 cubes (the only shape that exists) the
// result is symmetric.
[[nodiscard]] inline bool mergeable(const game::EntityTypeRegistry &types, const game::ItemRegistry &items,
                                    const ItemRules &rules, const Entity &a, const Entity &b) {
    if (a.type != b.type || a.stack.empty() || b.stack.empty() || a.stack.item != b.stack.item) {
        return false;
    }
    const int limit = game::stack_limit_of(items, a.stack.item);
    if (limit <= 1) {
        return false; // ⚖ 可堆叠: a single-unit item (tool, armour) never merges
    }
    if (a.stack.count + b.stack.count > limit) {
        return false; // ⚖ 合并后不超过堆叠上限
    }
    const physics::Box box_a = entity_box(a, types.def_of(a.type));
    const physics::Box box_b = entity_box(b, types.def_of(b.type));
    const double inflate_x = (rules.merge_box_x - (box_a.max_x - box_a.min_x)) * 0.5;
    const double inflate_y = (rules.merge_box_y - (box_a.max_y - box_a.min_y)) * 0.5;
    const double inflate_z = (rules.merge_box_z - (box_a.max_z - box_a.min_z)) * 0.5;
    return box_a.min_x - inflate_x < box_b.max_x && box_a.max_x + inflate_x > box_b.min_x &&
           box_a.min_y - inflate_y < box_b.max_y && box_a.max_y + inflate_y > box_b.min_y &&
           box_a.min_z - inflate_z < box_b.max_z && box_a.max_z + inflate_z > box_b.min_z;
}

// Runs the merge attempts for the drops whose timer came due this tick, in
// ascending id order. Each attempt rescans after a successful merge (the
// survivor's count and position are now different), which also makes the pass
// indifferent to which of a pair was the origin.
inline void merge_pass(EntityStore &store, const game::EntityTypeRegistry &types, const game::ItemRegistry &items,
                       const ItemRules &rules, const std::vector<EntityId> &due) {
    for (const EntityId due_id : due) {
        if (store.find(due_id) == nullptr) {
            continue; // an earlier merge already consumed this drop
        }
        bool merged = true;
        while (merged) {
            merged = false;
            for (const EntityId other_id : store.live_ids()) {
                Entity *self = store.find(due_id);
                Entity *other = store.find(other_id);
                if (self == nullptr || other == nullptr || other_id == due_id) {
                    continue;
                }
                if (!mergeable(types, items, rules, *self, *other)) {
                    continue;
                }
                // ⚖ 数量多的那个保留并增加计数，另一个消失. Equal counts: the lower
                // id keeps, so the outcome does not depend on which of the two
                // the scan happened to start from.
                const bool other_keeps = other->stack.count > self->stack.count ||
                                         (other->stack.count == self->stack.count && other->id < self->id);
                const EntityId keep = other_keeps ? other_id : due_id;
                const EntityId gone = other_keeps ? due_id : other_id;
                Entity *keeper = store.find(keep);
                const Entity *loser = store.find(gone);
                keeper->stack.count += loser->stack.count;
                // ⚖ 计时器取剩余时间更长的那个 (research/11 §4.3): the smaller age IS
                // the longer remaining lifetime. The pickup timer follows the
                // same "longer remaining wins" rule - chosen here, since the
                // source states it for the despawn timer only.
                keeper->age = std::min(keeper->age, loser->age);
                keeper->pickup_delay = std::max(keeper->pickup_delay, loser->pickup_delay);
                store.erase(gone);
                merged = true;
                break;
            }
        }
    }
}

} // namespace item_detail

// One authoritative step over every drop in the store:
//   age / pickup delay / merge timer -> motion -> environment contact ->
//   despawn -> merge attempts.
//
// ⚖ The whole step is skipped for a drop whose chunk is not in memory, which is
// exactly how "计时只在已加载且处理实体的区块内进行，区块卸载则暂停"
// (research/11 §4.4) is implemented: there is no second timer to keep in step,
// the one counter simply does not advance. The drop keeps its slot while it is
// frozen (entity persistence to disk is NOT part of this card).
inline void step_items(EntityStore &store, const IItemWorld &world, const ItemBlockHazard &hazard,
                       const ItemRules &rules, const game::EntityTypeRegistry &types, const game::ItemRegistry &items) {
    std::vector<EntityId> merge_due;
    const std::vector<EntityId> ids = store.live_ids(); // snapshot: the pass erases
    merge_due.reserve(ids.size());
    for (const EntityId id : ids) {
        Entity *e = store.find(id);
        if (e == nullptr) {
            continue;
        }
        const game::EntityDef &def = types.def_of(e->type);
        const auto [cx, cz] = voxel::Chunk::chunk_coords(static_cast<int>(std::floor(e->position.x)),
                                                         static_cast<int>(std::floor(e->position.z)));
        if (!world.chunk_loaded(cx, cz)) {
            continue; // frozen: no age, no timers, no physics
        }
        ++e->age;
        if (e->pickup_delay > 0) {
            --e->pickup_delay;
        }
        if (e->merge_timer > 0) {
            --e->merge_timer;
        }
        // ⚖ 跨越方块边界时可每 2 tick 处理一次 (research/11 §4.3).
        const glm::ivec3 block_now{static_cast<int>(std::floor(e->position.x)),
                                   static_cast<int>(std::floor(e->position.y)),
                                   static_cast<int>(std::floor(e->position.z))};
        if (block_now != e->last_block) {
            e->last_block = block_now;
            e->merge_timer = std::min(e->merge_timer, rules.merge_period_crossing);
        }
        if (e->merge_timer == 0) {
            e->merge_timer = rules.merge_period;
            merge_due.push_back(id);
        }

        step_item_motion(*e, def, world, rules);

        if (item_detail::touches_hazard(world, hazard, entity_box(*e, def))) {
            e->health = 0; // fire / lava / cactus remove every point at once
        }
        if (e->health <= 0 || e->position.y < rules.void_y || e->age >= rules.despawn_ticks) {
            store.erase(id);
        }
    }
    item_detail::merge_pass(store, types, items, rules, merge_due);
}

// Spawns the drop a freshly broken block leaves behind, or kNoEntity when the
// block has no item form (air; water, whose item form does not exist - T-I1
// ruling S-3).
//
// The position is the broken cell's centre, lifted by half the drop's height,
// so the drop sits centred in the cell it came from (§4.5) and falls the last
// half block onto the floor below.
[[nodiscard]] inline EntityId spawn_item_drop(EntityStore &store, const game::EntityTypeRegistry &types,
                                              const game::ItemRegistry &items, const ItemRules &rules,
                                              const glm::ivec3 &block, const std::uint16_t block_id) {
    const std::optional<std::uint16_t> item = items.item_for_block(block_id);
    if (!item.has_value()) {
        return EntityStore::kNoEntity;
    }
    // id_of throws on an unknown name, which is the intended failure mode for
    // content drift (the same contract as resolve_vessel_ids on the client).
    const std::uint16_t item_type = types.id_of("item");
    const game::EntityDef &def = types.def_of(item_type);
    Entity drop;
    drop.type = item_type;
    drop.position = glm::dvec3(block) + glm::dvec3(0.5, 0.5 - def.height * 0.5, 0.5);
    drop.velocity = glm::dvec3(0.0, rules.spawn_velocity_y, 0.0);
    drop.health = def.max_health;
    drop.pickup_delay = rules.pickup_delay_natural;
    drop.merge_timer = rules.merge_period;
    drop.last_block = glm::ivec3(glm::floor(drop.position));
    drop.stack = game::ItemStack::of(*item, 1);
    return store.spawn(drop);
}

// Destroys every drop whose box centre lies within `radius` of `center`
// (research/11 §4.4: 被爆炸摧毁; ⚖ 下界之星免疫爆炸 has no item to attach to in
// this item set). Explosions are not a system yet - no TNT or explosive content
// exists - so this card ships the entry point plus its unit test rather than a
// rule that only lives in prose.
[[nodiscard]] inline std::size_t destroy_items_in_radius(EntityStore &store, const game::EntityTypeRegistry &types,
                                                         const glm::dvec3 &center, const double radius) {
    std::size_t destroyed = 0;
    for (const EntityId id : store.live_ids()) {
        const Entity *e = store.find(id);
        if (e == nullptr) {
            continue;
        }
        const physics::Box box = entity_box(*e, types.def_of(e->type));
        const glm::dvec3 middle{(box.min_x + box.max_x) * 0.5, (box.min_y + box.max_y) * 0.5,
                                (box.min_z + box.max_z) * 0.5};
        if (glm::length(middle - center) <= radius) {
            store.erase(id);
            ++destroyed;
        }
    }
    return destroyed;
}

} // namespace opencraft::server
