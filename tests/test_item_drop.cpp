// T-E1: the dropped item's rules, headless - research/11 §4.1-4.4 on top of the
// entity store, with a hand-built block world instead of chunks.
//
// This is where the card's acceptance items 2 (physics), 4 (pickup geometry),
// 5 (merging), 6 (despawn / paused timer) and 7 (environment destruction) are
// asserted. The authority-side half (dig -> drop -> pickup request) lives in
// test_drop_authority.cpp; the on-machine half in docs/qa/T-E1-2026-09-17/.

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include "opencraft/game/entity_type.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/pickup.hpp"
#include "opencraft/physics/physics_config.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/sim/item_sim.hpp"
#include "opencraft/voxel/block_registry.hpp"

namespace {

namespace gam = opencraft::game;
namespace srv = opencraft::server;
namespace voxel = opencraft::voxel;
namespace phy = opencraft::physics;

// A block world for the item rules: inclusive integer regions carrying a block
// id (0 = air), plus a set of chunk columns that are NOT in memory.
//
// Blocks listed as passable do not collide (fire, lava): the default
// IBlockSource derives `shape_top_at` from `solid_at`, so this one flag is
// enough to make a non-solid block behave like one.
class BlockWorld final : public srv::IItemWorld {
public:
    BlockWorld &fill(int x0, int x1, int y0, int y1, int z0, int z1, std::uint16_t block) {
        regions_.push_back({x0, x1, y0, y1, z0, z1, block});
        return *this;
    }

    // One layer whose top face is `top_y`.
    BlockWorld &floor_at(int top_y, std::uint16_t block) { return fill(-4, 20, top_y - 1, top_y - 1, -4, 20, block); }

    BlockWorld &passable(std::uint16_t block) {
        passable_.insert(block);
        return *this;
    }

    void set_loaded(int cx, int cz, bool loaded) {
        if (loaded) {
            unloaded_.erase({cx, cz});
        } else {
            unloaded_.insert({cx, cz});
        }
    }

    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override {
        std::uint16_t found = 0;
        for (const Region &region : regions_) {
            if (wx >= region.x0 && wx <= region.x1 && wy >= region.y0 && wy <= region.y1 && wz >= region.z0 &&
                wz <= region.z1) {
                found = region.block;
            }
        }
        return found;
    }

    [[nodiscard]] bool solid_at(int wx, int wy, int wz) const override {
        const std::uint16_t id = block_at(wx, wy, wz);
        return id != 0 && !passable_.contains(id);
    }

    [[nodiscard]] bool liquid_at(int wx, int wy, int wz) const override { return block_at(wx, wy, wz) == liquid_; }

    [[nodiscard]] bool chunk_loaded(int cx, int cz) const override { return !unloaded_.contains({cx, cz}); }

    void set_liquid(std::uint16_t block) { liquid_ = block; }

private:
    struct Region {
        int x0, x1, y0, y1, z0, z1;
        std::uint16_t block;
    };

    std::vector<Region> regions_;
    std::set<std::pair<int, int>> unloaded_;
    std::set<std::uint16_t> passable_;
    std::uint16_t liquid_ = 0;
};

// Everything a rule test needs: the two registries, the rules, a world and a
// store. The launch registries are used on purpose - block and item ids come
// from the shipping content, not from a fixture's imagination.
struct DropFixture {
    gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    gam::ItemRegistry items = gam::ItemRegistry::create_default();
    voxel::BlockRegistry blocks = voxel::BlockRegistry::create_default();
    srv::ItemRules rules{};
    BlockWorld world;
    srv::EntityStore store;
    std::uint16_t item_type = types.id_of("item");

    // Not nodiscard: half the rules do not care WHICH drop they acted on, only
    // how many are left (the store's own spawn() keeps its nodiscard).
    srv::EntityId spawn(double x, double y, double z, std::uint16_t item, int count = 1, int pickup_delay = 0) {
        srv::Entity entity;
        entity.type = item_type;
        entity.position = {x, y, z};
        entity.stack = gam::ItemStack::of(item, count);
        entity.health = types.def_of(item_type).max_health;
        entity.pickup_delay = pickup_delay;
        entity.merge_timer = rules.merge_period;
        entity.last_block = glm::ivec3(glm::floor(entity.position));
        return store.spawn(entity);
    }

    [[nodiscard]] const srv::Entity &get(srv::EntityId id) const {
        const srv::Entity *entity = store.find(id);
        REQUIRE(entity != nullptr);
        return *entity;
    }

    [[nodiscard]] srv::Entity *mut(srv::EntityId id) {
        srv::Entity *entity = store.find(id);
        REQUIRE(entity != nullptr);
        return entity;
    }

    void step(int ticks = 1) {
        const srv::ItemBlockHazard hazard(blocks);
        for (int i = 0; i < ticks; ++i) {
            srv::step_items(store, world, hazard, rules, types, items);
        }
    }
};

// Reference integrator for the PLAYER's execution order (P -> A -> D) with any
// parameter set. Ten lines, no collision: its only job is to be the other
// order, so the assertion below is about the ORDER alone.
[[nodiscard]] double fall_reference_p_a_d(double gravity, double vertical_drag, int ticks) {
    double y = 0.0;
    double v = 0.0;
    for (int i = 0; i < ticks; ++i) {
        y += v;             // P
        v -= gravity;       // A
        v *= vertical_drag; // D
    }
    return -y;
}

constexpr double kFallStartY = 68.0;
constexpr double kFloorTopY = 64.0;

} // namespace

// ── 2. motion: the parameters and the execution order ──────────────────────

TEST_CASE("item drop: the parameters are the drop's own, not the player's") {
    const gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    const gam::EntityDef &drop = types.def_of(types.id_of("item"));
    const phy::PhysicsConfig player{};

    // ⚖ research/11 §4.1: -0.04 vs the player's -0.08, 0.98 vs 0.91. If these
    // ever become equal the per-type parameters are cosmetic.
    CHECK(drop.gravity == doctest::Approx(0.04));
    CHECK(drop.horizontal_drag == doctest::Approx(0.98));
    CHECK(drop.vertical_drag == doctest::Approx(0.98));
    CHECK(drop.gravity != doctest::Approx(player.gravity));
    CHECK(drop.horizontal_drag != doctest::Approx(player.horizontal_drag));
}

TEST_CASE("item drop: acceleration happens BEFORE the move, so the first tick already falls") {
    DropFixture fixture;
    const gam::EntityDef &def = fixture.types.def_of(fixture.item_type);
    // No floor: a free fall, so nothing clamps the first tick.
    const srv::EntityId id = fixture.spawn(8.5, kFallStartY, 8.5, fixture.items.id_of("greyrock"));

    fixture.step();

    // ★ The whole A -> P -> D claim in one assertion: the very first tick has
    // already displaced the drop by gravity. A P -> A -> D entity at rest does
    // not move at all on its first tick.
    CHECK(fixture.get(id).position.y == doctest::Approx(kFallStartY - def.gravity));
    // ... and the damping was applied AFTER that displacement (0.98, the drop's
    // own drag rather than the player's 0.91).
    CHECK(fixture.get(id).velocity.y == doctest::Approx(-def.gravity * def.vertical_drag));
    CHECK(fixture.get(id).velocity.y == doctest::Approx(-0.0392));
}

TEST_CASE("item drop: A -> P -> D falls one gravity step per tick further than P -> A -> D") {
    constexpr int kTicks = 20;
    DropFixture fixture;
    const gam::EntityDef &def = fixture.types.def_of(fixture.item_type);
    const srv::EntityId id = fixture.spawn(8.5, kFallStartY, 8.5, fixture.items.id_of("greyrock"));

    fixture.step(kTicks);

    const double fell = kFallStartY - fixture.get(id).position.y;
    // The same parameters integrated in the player's order fall exactly
    // `gravity x ticks` less: every tick the drop applies one more gravity step
    // before it moves.
    const double reference = fall_reference_p_a_d(def.gravity, def.vertical_drag, kTicks);
    CHECK(fell == doctest::Approx(reference + def.gravity * kTicks));
    CHECK(fell > reference);
}

TEST_CASE("item drop: the player's numbers would land somewhere else entirely") {
    constexpr int kTicks = 20;
    DropFixture fixture;
    const phy::PhysicsConfig player{};
    const srv::EntityId id = fixture.spawn(8.5, kFallStartY, 8.5, fixture.items.id_of("greyrock"));

    fixture.step(kTicks);
    const double drop_fell = kFallStartY - fixture.get(id).position.y;
    const double player_fell = fall_reference_p_a_d(player.gravity, player.vertical_drag, kTicks);

    CHECK(std::abs(drop_fell - player_fell) > 1.0);
}

TEST_CASE("item drop: terminal velocity is 1.96 blocks/tick = 39.2 m/s") {
    DropFixture fixture;
    const gam::EntityDef &def = fixture.types.def_of(fixture.item_type);
    // High enough that 1000 ticks of fall stay above the void line.
    const srv::EntityId id = fixture.spawn(8.5, 4000.0, 8.5, fixture.items.id_of("greyrock"));

    for (int i = 0; i < 1000; ++i) {
        fixture.step();
    }

    // ⚖ research/11 §4.1: 39.2 m/s = 1.96 blocks/tick, DERIVED from the two
    // parameters instead of stored as a third constant that can go stale:
    //   gravity x vertical_drag / (1 - vertical_drag) = 0.04 x 0.98 / 0.02.
    const double derived = def.gravity * def.vertical_drag / (1.0 - def.vertical_drag);
    CHECK(derived == doctest::Approx(1.96));
    CHECK(derived * 20.0 == doctest::Approx(39.2));
    // 0.98^1000 = 1.6e-9, so the approach is well inside this tolerance.
    CHECK(fixture.get(id).velocity.y == doctest::Approx(-derived).epsilon(1e-6));
}

TEST_CASE("item drop: damping is 0.98 vertically and 0.98 horizontally in the air") {
    DropFixture fixture;
    const gam::EntityDef &def = fixture.types.def_of(fixture.item_type);
    const srv::EntityId id = fixture.spawn(8.5, 100.0, 8.5, fixture.items.id_of("greyrock"));
    fixture.mut(id)->velocity = {1.0, 0.0, -1.0};

    fixture.step();

    // The displaced distance uses the PRE-damping velocity (docs/03 §6 contract:
    // 阻尼施加在位移之后), so one tick at 1.0 block/tick moves a whole block and
    // only then decays to 0.98.
    CHECK(fixture.get(id).position.x == doctest::Approx(9.5).epsilon(1e-9));
    CHECK(fixture.get(id).velocity.x == doctest::Approx(0.98));
    CHECK(fixture.get(id).velocity.z == doctest::Approx(-0.98));
    CHECK(def.horizontal_drag == doctest::Approx(0.98)); // the drop's value, not the player's 0.91
}

TEST_CASE("item drop: ground contact stops it dead (restitution 0, T-D36 项 1)") {
    DropFixture fixture;
    fixture.world.floor_at(static_cast<int>(kFloorTopY), fixture.blocks.id_of("stone"));
    const srv::EntityId id = fixture.spawn(8.5, kFallStartY, 8.5, fixture.items.id_of("greyrock"));

    fixture.step(60); // four blocks of fall, then the floor

    CHECK(fixture.get(id).position.y == doctest::Approx(kFloorTopY));
    CHECK(fixture.get(id).velocity.y == doctest::Approx(0.0));
    // ⚠ T-D36 项 1: 0 because the wiki gives no coefficient for a drop. A
    // calibrated value is expected to make this assertion fail - that is what
    // "待校准" has to look like in the code.
    CHECK(fixture.rules.restitution == doctest::Approx(0.0));

    // No bounce: the following ticks keep it exactly on the surface.
    fixture.mut(id)->velocity.x = 1.0;
    fixture.step(4);
    CHECK(fixture.get(id).position.y == doctest::Approx(kFloorTopY));
    CHECK(fixture.get(id).velocity.y == doctest::Approx(0.0));
}

TEST_CASE("item drop: ground retention is horizontal_drag x slipperiness (T-D36 项 2)") {
    DropFixture fixture;
    fixture.world.floor_at(static_cast<int>(kFloorTopY), fixture.blocks.id_of("stone"));
    const gam::EntityDef &def = fixture.types.def_of(fixture.item_type);
    const srv::EntityId id = fixture.spawn(8.5, kFloorTopY, 8.5, fixture.items.id_of("greyrock"));

    fixture.step(); // one tick on the floor: lands, and on_ground is set by the move
    CHECK(fixture.get(id).on_ground);
    fixture.mut(id)->velocity.x = 1.0;
    fixture.step();

    // ⚠ T-D36 项 2: the source does not say whether a drop gets the player's
    // on-ground friction correction. This card applies it with the drop's own
    // drag, i.e. 0.98 x S, and S is 0.6 on an ordinary block.
    const double expected = def.horizontal_drag * phy::kDefaultSlipperiness;
    CHECK(expected == doctest::Approx(0.588));
    CHECK(fixture.get(id).velocity.x == doctest::Approx(expected).epsilon(1e-9));

    // And it comes to rest instead of creeping for the rest of its five minutes.
    fixture.step(200);
    CHECK(fixture.get(id).velocity.x == doctest::Approx(0.0));
    CHECK(fixture.get(id).position.x > 10.0);
    CHECK(fixture.get(id).position.x < 15.0);
}

TEST_CASE("item drop: a wall stops it and the substeps keep a fast drop from tunnelling") {
    DropFixture fixture;
    fixture.world.floor_at(static_cast<int>(kFloorTopY), fixture.blocks.id_of("stone"));
    fixture.world.fill(12, 12, static_cast<int>(kFloorTopY), static_cast<int>(kFloorTopY) + 3, -4, 20,
                       fixture.blocks.id_of("stone"));
    const srv::EntityId id = fixture.spawn(8.5, kFloorTopY, 8.5, fixture.items.id_of("greyrock"));
    fixture.mut(id)->velocity.x = 5.0; // 5 blocks/tick in one move

    fixture.step(20);

    CHECK(fixture.get(id).position.x == doctest::Approx(12.0 - 0.125));
    CHECK(fixture.get(id).position.y == doctest::Approx(kFloorTopY));
    CHECK(fixture.get(id).velocity.x == doctest::Approx(0.0));
}

// ── 4. pickup geometry ─────────────────────────────────────────────────────

TEST_CASE("pickup box: horizontal faces are inclusive, vertical faces are exclusive") {
    gam::ActorPose actor;
    actor.feet = {8.5, kFloorTopY, 8.5};
    actor.height = 1.8;

    const double half = phy::PlayerState::kHalfWidth;
    const double drop_half = 0.125;

    const auto box_at = [&](double x, double y, double z) {
        return std::pair<glm::dvec3, glm::dvec3>{{x - drop_half, y, z - drop_half},
                                                 {x + drop_half, y + 0.25, z + drop_half}};
    };

    // Inside the box: trivially true.
    const auto [inside_min, inside_max] = box_at(9.5, kFloorTopY, 9.5);
    CHECK(gam::pickup_box_contains(actor, inside_min, inside_max));

    // Horizontal: a box whose outer face sits EXACTLY on the boundary is still
    // inside (⚖ ≤ 1 block).
    const double boundary_x = actor.feet.x + half + gam::kPickupReachHorizontal + drop_half;
    const auto [edge_min, edge_max] = box_at(boundary_x, kFloorTopY, 8.5);
    CHECK(edge_min.x == doctest::Approx(actor.feet.x + half + 1.0));
    CHECK(gam::pickup_box_contains(actor, edge_min, edge_max));
    // A millimetre further out: not inside.
    const auto [past_min, past_max] = box_at(boundary_x + 0.001, kFloorTopY, 8.5);
    CHECK_FALSE(gam::pickup_box_contains(actor, past_min, past_max));

    // Vertical: a box whose lower face sits EXACTLY on the upper boundary is NOT
    // inside (⚖ < 0.5 blocks). The drop stands on the actor's head here.
    const double top_y = actor.feet.y + actor.height + gam::kPickupReachVertical;
    const auto [above_min, above_max] = box_at(8.5, top_y, 8.5);
    CHECK(above_min.y == doctest::Approx(actor.feet.y + 1.8 + 0.5));
    CHECK_FALSE(gam::pickup_box_contains(actor, above_min, above_max));
    const auto [near_min, near_max] = box_at(8.5, top_y - 0.001, 8.5);
    CHECK(gam::pickup_box_contains(actor, near_min, near_max));

    // The same at the bottom: half a block below the feet is the limit, and
    // touching it does not count.
    const auto [below_min, below_max] = box_at(8.5, actor.feet.y - gam::kPickupReachVertical - 0.25, 8.5);
    CHECK_FALSE(gam::pickup_box_contains(actor, below_min, below_max));
    const auto [close_min, close_max] = box_at(8.5, actor.feet.y - gam::kPickupReachVertical - 0.25 + 0.001, 8.5);
    CHECK(gam::pickup_box_contains(actor, close_min, close_max));
}

TEST_CASE("pickup box: the sneaking pose shrinks it, as the source describes") {
    const gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    const gam::EntityDef &def = types.def_of(types.id_of("item"));
    srv::Entity drop;
    drop.position = {8.5, kFloorTopY, 8.5};
    const auto [box_min, box_max] = srv::entity_box_corners(drop, def);

    gam::ActorPose standing;
    standing.feet = {8.5, kFloorTopY, 8.5};
    standing.height = phy::PlayerState::kStandingHeight;
    gam::ActorPose sneaking = standing;
    sneaking.height = phy::PlayerState::kSneakingHeight;

    CHECK(gam::pickup_box_contains(standing, box_min, box_max));
    CHECK(gam::pickup_box_contains(sneaking, box_min, box_max)); // a drop at the feet is reachable either way

    // A drop 2.05 blocks up: inside the standing box (feet + 1.8 + 0.5 = 2.3),
    // outside the sneaking one (feet + 1.5 + 0.5 = 2.0) - 拾取盒随玩家碰撞盒变化.
    drop.position.y = kFloorTopY + 2.05;
    const auto [high_min, high_max] = srv::entity_box_corners(drop, def);
    CHECK(gam::pickup_box_contains(standing, high_min, high_max));
    CHECK_FALSE(gam::pickup_box_contains(sneaking, high_min, high_max));
}

TEST_CASE("entity box: the corners bracket a 0.25 cube above the feet") {
    const gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    const gam::EntityDef &def = types.def_of(types.id_of("item"));
    srv::Entity entity;
    entity.position = {8.5, 64.0, 8.5};
    const auto [box_min, box_max] = srv::entity_box_corners(entity, def);

    CHECK(box_min.x == doctest::Approx(8.5 - def.half_width));
    CHECK(box_max.x == doctest::Approx(8.5 + def.half_width));
    CHECK(box_min.y == doctest::Approx(64.0));
    CHECK(box_max.y == doctest::Approx(64.0 + def.height));
    // ⚖ research/11 §4.2: 0.25 x 0.25 x 0.25.
    CHECK(box_max.x - box_min.x == doctest::Approx(0.25));
    CHECK(box_max.y - box_min.y == doctest::Approx(0.25));
    CHECK(box_max.z - box_min.z == doctest::Approx(0.25));
}

// ── 5. merging ─────────────────────────────────────────────────────────────

TEST_CASE("item drop merge: the larger stack keeps and absorbs the smaller one") {
    DropFixture fixture;
    const std::uint16_t greyrock = fixture.items.id_of("greyrock"); // max_stack 64
    const srv::EntityId small = fixture.spawn(8.5, kFloorTopY, 8.5, greyrock, 3);
    const srv::EntityId big = fixture.spawn(8.6, kFloorTopY, 8.6, greyrock, 9);

    fixture.step(40); // ⚖ the merge timer is 40 ticks

    CHECK(fixture.store.alive_count() == 1);
    CHECK(fixture.store.find(small) == nullptr);
    const srv::Entity &survivor = fixture.get(big);
    CHECK(survivor.stack.item == greyrock);
    CHECK(survivor.stack.count == 12); // ⚖ 数量多的那个保留并增加计数
}

TEST_CASE("item drop merge: the survivor takes the longer remaining timers") {
    DropFixture fixture;
    fixture.world.floor_at(static_cast<int>(kFloorTopY), fixture.blocks.id_of("stone"));
    const std::uint16_t greyrock = fixture.items.id_of("greyrock");
    const srv::EntityId elder = fixture.spawn(8.5, kFloorTopY, 8.5, greyrock, 1);
    const srv::EntityId younger = fixture.spawn(8.6, kFloorTopY, 8.6, greyrock, 5);

    fixture.step(30);
    CHECK(fixture.get(elder).age == 30);
    CHECK(fixture.get(younger).age == 30);
    // The larger stack survives (5 > 1) and would be at age 40 at merge time;
    // the other one is pretending to be 5 ticks old. The loser also carries the
    // LONGER pickup delay, so the survivor can only end up with it by taking it.
    fixture.mut(younger)->age = 5;
    fixture.mut(younger)->pickup_delay = 40;
    fixture.mut(elder)->pickup_delay = 100;
    fixture.step(10); // the merge fires on tick 40

    CHECK(fixture.store.alive_count() == 1);
    CHECK(fixture.store.find(elder) == nullptr);
    const srv::Entity &survivor = fixture.get(younger);
    CHECK(survivor.stack.count == 6);
    // ⚖ 计时器取剩余时间更长的那个: the smaller age IS the longer remaining life.
    // At merge time the ages are 40 (elder) and 15 (younger).
    CHECK(survivor.age == 15);
    // The pickup timer follows the same "longer remaining wins" rule: 100 - 10
    // beats 40 - 10, and it is the LOSER's value that the survivor kept.
    CHECK(survivor.pickup_delay == 90);
}

TEST_CASE("item drop merge: outside the 0.5 x 0.25 x 0.5 box nothing merges") {
    DropFixture fixture;
    const std::uint16_t greyrock = fixture.items.id_of("greyrock");
    fixture.world.floor_at(static_cast<int>(kFloorTopY), fixture.blocks.id_of("stone"));
    fixture.spawn(8.5, kFloorTopY, 8.5, greyrock, 1);
    fixture.spawn(9.5, kFloorTopY, 8.5, greyrock, 1); // a block apart

    fixture.step(40);

    CHECK(fixture.store.alive_count() == 2);
}

TEST_CASE("item drop merge: different items, unstackable items and an overflowing sum never merge") {
    DropFixture fixture;
    const std::uint16_t greyrock = fixture.items.id_of("greyrock");
    const std::uint16_t loam = fixture.items.id_of("loam_clod");
    const std::uint16_t chisel = fixture.items.id_of("timber_chisel"); // max_stack 1

    // Different item (and far enough apart that only the item decides).
    fixture.spawn(8.0, kFloorTopY, 8.0, greyrock, 1);
    fixture.spawn(8.6, kFloorTopY, 8.6, loam, 1);
    // ⚖ 可堆叠: a 1-stack item (tools, armour) never merges.
    fixture.spawn(8.7, kFloorTopY, 8.7, chisel, 1);
    fixture.spawn(8.8, kFloorTopY, 8.8, chisel, 1);
    // Same item, in range of each other, but 40 + 40 > the 64 limit: the card
    // does NOT allow MC's partial merge, so neither is touched.
    const srv::EntityId big_a = fixture.spawn(10.5, kFloorTopY, 10.5, greyrock, 40);
    const srv::EntityId big_b = fixture.spawn(10.6, kFloorTopY, 10.6, greyrock, 40);

    fixture.step(40);

    CHECK(fixture.store.alive_count() == 6);
    CHECK(fixture.get(big_a).stack.count == 40);
    CHECK(fixture.get(big_b).stack.count == 40);
}

TEST_CASE("item drop merge: crossing a block boundary re-arms the timer to 2 ticks") {
    DropFixture fixture;
    fixture.world.floor_at(static_cast<int>(kFloorTopY), fixture.blocks.id_of("stone"));
    const std::uint16_t greyrock = fixture.items.id_of("greyrock");
    // Without the crossing the two would merge on tick 40 (every other merge
    // test asserts exactly that); with it, on tick 4.
    const srv::EntityId mover = fixture.spawn(8.95, kFloorTopY, 8.5, greyrock, 1);
    const srv::EntityId partner = fixture.spawn(9.2, kFloorTopY, 8.5, greyrock, 1);
    fixture.mut(mover)->velocity.x = 0.1;

    fixture.step();
    // Still in its spawn cell (x = 9.05 is inside block 8): no crossing yet, the
    // timer just ticks down.
    CHECK(fixture.get(mover).last_block.x == 8);
    CHECK(fixture.get(mover).merge_timer == 39);

    fixture.step();
    // It crossed into block 9, so the timer is clamped to 2 (⚖ 跨越方块边界时可每
    // 2 tick 处理一次) instead of waiting out the full period.
    CHECK(fixture.get(mover).last_block.x == 9);
    CHECK(fixture.get(mover).merge_timer <= 2);

    // Tick 4 is therefore enough for the merge to have happened.
    fixture.step(2);
    CHECK(fixture.store.alive_count() == 1);
}

TEST_CASE("item drop: spawn_item_drop puts the drop in the cell, and water leaves nothing") {
    DropFixture fixture;
    const std::uint16_t stone = fixture.blocks.id_of("stone");
    const srv::EntityId id =
        srv::spawn_item_drop(fixture.store, fixture.types, fixture.items, fixture.rules, glm::ivec3{4, 30, 6}, stone);

    REQUIRE(id != srv::EntityStore::kNoEntity);
    const srv::Entity &drop = fixture.get(id);
    CHECK(drop.stack.item == fixture.items.item_for_block(stone).value());
    CHECK(drop.stack.count == 1);
    CHECK(drop.position.x == doctest::Approx(4.5));
    CHECK(drop.position.z == doctest::Approx(6.5));
    // Centred in the cell it came from - half the drop's height below the cell
    // centre - so it falls the last half block onto the floor.
    CHECK(drop.position.y == doctest::Approx(30.375));
    CHECK(drop.pickup_delay == 10); // ⚖ a natural drop waits 10 ticks
    CHECK(drop.health == 5);
    CHECK(drop.type == fixture.item_type);

    // ⚖ Water has no item form (T-I1 ruling S-3), so it leaves nothing behind.
    CHECK_FALSE(fixture.items.item_for_block(fixture.blocks.id_of("water")).has_value());
    CHECK(srv::spawn_item_drop(fixture.store, fixture.types, fixture.items, fixture.rules, glm::ivec3{4, 31, 6},
                               fixture.blocks.id_of("water")) == srv::EntityStore::kNoEntity);
    CHECK(fixture.store.alive_count() == 1);
}

// ── 6. despawn and the paused timer ────────────────────────────────────────

TEST_CASE("item drop: despawns at 6000 ticks, and the timer pauses in an unloaded chunk") {
    DropFixture fixture;
    fixture.world.floor_at(static_cast<int>(kFloorTopY), fixture.blocks.id_of("stone"));
    const srv::EntityId id = fixture.spawn(8.5, kFloorTopY, 8.5, fixture.items.id_of("greyrock"));
    CHECK(fixture.rules.despawn_ticks == 6000); // ⚖ 5 minutes

    fixture.step(5999);
    REQUIRE(fixture.store.find(id) != nullptr);
    CHECK(fixture.get(id).age == 5999);

    // ★ While the chunk is not in memory NOTHING advances - the counter IS the
    // timer, so there is no second clock to keep in step (research/11 §4.4:
    // 区块卸载则暂停).
    fixture.world.set_loaded(0, 0, false);
    CHECK_FALSE(fixture.world.chunk_loaded(0, 0));
    fixture.step(100);
    CHECK(fixture.store.find(id) != nullptr);
    CHECK(fixture.get(id).age == 5999);

    // Reloaded, the very next tick takes it over the line.
    fixture.world.set_loaded(0, 0, true);
    fixture.step();
    CHECK(fixture.store.find(id) == nullptr);
    CHECK(fixture.store.alive_count() == 0);
}

TEST_CASE("item drop: a drop below the void line disappears at once") {
    DropFixture fixture;
    const srv::EntityId id = fixture.spawn(8.5, fixture.rules.void_y - 1.0, 8.5, fixture.items.id_of("greyrock"));
    CHECK(fixture.rules.void_y == doctest::Approx(-128.0)); // ⚖ 主世界 Y = -128
    fixture.step();
    CHECK(fixture.store.find(id) == nullptr);
}

// ── 7. environment destruction ─────────────────────────────────────────────

TEST_CASE("item environment: the rule table names fire, lava and cactus and nothing else") {
    CHECK(srv::block_id_destroys_items("fire"));
    CHECK(srv::block_id_destroys_items("lava"));
    CHECK(srv::block_id_destroys_items("cactus"));
    // ⚠ The pathfinder "damage" blocks (research/11 §1.4.2) must NOT be here -
    // that is the rule §4.4 warns about, not an omission.
    CHECK_FALSE(srv::block_id_destroys_items("magma_block"));
    CHECK_FALSE(srv::block_id_destroys_items("campfire"));
    CHECK_FALSE(srv::block_id_destroys_items("sweet_berry_bush"));
    CHECK_FALSE(srv::block_id_destroys_items("wither_rose"));
    CHECK_FALSE(srv::block_id_destroys_items("powder_snow"));
    CHECK_FALSE(srv::block_id_destroys_items("water"));

    // The launch block registry arms none of them (no fire / lava / cactus block
    // exists yet), which is exactly why the tests build their own registry.
    const voxel::BlockRegistry launch = voxel::BlockRegistry::create_default();
    const srv::ItemBlockHazard launch_hazard(launch);
    for (std::uint16_t id = 0; id < launch.size(); ++id) {
        CHECK_FALSE(launch_hazard.destroys(id));
    }
}

TEST_CASE("item environment: a drop in fire or lava or against a cactus is destroyed") {
    voxel::BlockRegistry registry;
    const std::uint16_t fire = registry.register_block("fire", {"Fire", false, true, 0.0f});
    const std::uint16_t lava = registry.register_block("lava", {"Lava", false, true, 100.0f, true});
    const std::uint16_t cactus = registry.register_block("cactus", {"Cactus", true, true, 0.4f});
    const std::uint16_t stone = registry.register_block("stone", {"Stone", true, false, 1.5f});
    const srv::ItemBlockHazard hazard(registry);
    CHECK(hazard.destroys(fire));
    CHECK(hazard.destroys(lava));
    CHECK(hazard.destroys(cactus));
    CHECK_FALSE(hazard.destroys(stone));
    CHECK_FALSE(hazard.destroys(0));

    const gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    const gam::ItemRegistry items = gam::ItemRegistry::create_default();
    const srv::ItemRules rules{};
    const std::uint16_t item_type = types.id_of("item");
    const auto add_drop = [&](srv::EntityStore &store, double x, double y, double z) {
        srv::Entity entity;
        entity.type = item_type;
        entity.position = {x, y, z};
        entity.stack = gam::ItemStack::of(items.id_of("greyrock"), 1);
        entity.health = types.def_of(item_type).max_health;
        return store.spawn(entity);
    };

    // Fire and lava are not solid, so the drop sinks into the cell and burns.
    for (const std::uint16_t hot : {fire, lava}) {
        BlockWorld world;
        world.floor_at(static_cast<int>(kFloorTopY), hot).passable(hot);
        world.set_liquid(lava);
        srv::EntityStore store;
        const srv::EntityId id = add_drop(store, 8.5, kFloorTopY, 8.5);
        for (int i = 0; i < 4; ++i) {
            srv::step_items(store, world, hazard, rules, types, items);
        }
        CHECK(store.find(id) == nullptr);
    }

    // A cactus is solid: the drop is destroyed where its box overlaps the cell.
    BlockWorld cactus_world;
    cactus_world.floor_at(static_cast<int>(kFloorTopY), stone);
    cactus_world.fill(9, 9, static_cast<int>(kFloorTopY), static_cast<int>(kFloorTopY) + 1, 8, 8, cactus);
    srv::EntityStore cactus_store;
    const srv::EntityId in_cactus = add_drop(cactus_store, 9.5, kFloorTopY, 8.5);
    srv::step_items(cactus_store, cactus_world, hazard, rules, types, items);
    CHECK(cactus_store.find(in_cactus) == nullptr);
}

TEST_CASE("item environment: a damage block that is not on the list leaves the drop alone") {
    // ⚖ research/11 §4.4's warning, asserted rather than assumed: a magma block
    // damages LIVING entities and does not damage items. The drop stands on one
    // for twenty ticks and keeps every point of its 5 HP.
    voxel::BlockRegistry registry;
    const std::uint16_t magma = registry.register_block("magma_block", {"Magma Block", true, false, 0.5f});
    const srv::ItemBlockHazard hazard(registry);
    CHECK_FALSE(hazard.destroys(magma));

    const gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    const gam::ItemRegistry items = gam::ItemRegistry::create_default();
    const srv::ItemRules rules{};
    const std::uint16_t item_type = types.id_of("item");

    BlockWorld world;
    world.floor_at(static_cast<int>(kFloorTopY), magma);
    srv::EntityStore store;
    srv::Entity entity;
    entity.type = item_type;
    entity.position = {8.5, kFloorTopY, 8.5};
    entity.stack = gam::ItemStack::of(items.id_of("greyrock"), 1);
    entity.health = types.def_of(item_type).max_health;
    const srv::EntityId id = store.spawn(entity);

    for (int i = 0; i < 20; ++i) {
        srv::step_items(store, world, hazard, rules, types, items);
    }
    REQUIRE(store.find(id) != nullptr);
    CHECK(store.find(id)->health == 5);
}

TEST_CASE("item environment: an explosion destroys the drops inside its radius only") {
    DropFixture fixture;
    const std::uint16_t greyrock = fixture.items.id_of("greyrock");
    const srv::EntityId near_drop = fixture.spawn(8.5, kFloorTopY, 8.5, greyrock);
    const srv::EntityId far_drop = fixture.spawn(20.5, kFloorTopY, 8.5, greyrock);

    const std::size_t destroyed =
        srv::destroy_items_in_radius(fixture.store, fixture.types, glm::dvec3{8.5, kFloorTopY, 8.5}, 3.0);

    CHECK(destroyed == 1);
    CHECK(fixture.store.find(near_drop) == nullptr);
    CHECK(fixture.store.find(far_drop) != nullptr);
    // Nothing left in range, nothing destroyed.
    CHECK(srv::destroy_items_in_radius(fixture.store, fixture.types, glm::dvec3{8.5, kFloorTopY, 8.5}, 3.0) == 0);
}

TEST_CASE("item drop: nothing attacks a drop, so a thousand ticks next to it change nothing") {
    // ⚖ 不可被玩家或生物攻击 needs no code - it needs the ABSENCE of a damage
    // path, which is what this asserts.
    DropFixture fixture;
    fixture.world.floor_at(static_cast<int>(kFloorTopY), fixture.blocks.id_of("stone"));
    const srv::EntityId id = fixture.spawn(8.5, kFloorTopY, 8.5, fixture.items.id_of("greyrock"), 1, 10);

    fixture.step(1000);

    REQUIRE(fixture.store.find(id) != nullptr);
    CHECK(fixture.get(id).health == 5);
    CHECK(fixture.get(id).stack.count == 1);
    CHECK(fixture.get(id).pickup_delay == 0);
}
