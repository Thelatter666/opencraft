// T-D45: the player's death cycle - the unified damage entry, the death drop,
// the respawn and the two save-file traps the card calls out (§2.4).
//
// What is tested where, and why:
//   * the RULES (clamp, the alive -> dead edge, the respawn values) are pure
//     functions in game/client/src/player_life.hpp, driven directly;
//   * the WIRING (a drop is an authority entity; the authority re-checks the
//     item and the count) is tested against a real server::WorldSim, because
//     "the client asked nicely" is not evidence that a drop exists;
//   * the FALL path is driven through take_fall_damage(), the exact call the
//     logic tick makes after step_player, over the headless BoxWorld the physics
//     tests use;
//   * what a test CANNOT see is the tick's own structure (it polls a real
//     window): "a dead player cannot dig/place/attack/move" is a source-level
//     fact (run_tick's first branch, gated by may_act) plus the on-machine
//     evidence recorded in docs/qa/T-D45-2026-09-18/. This file tests the part
//     that is reachable headlessly: the damaged/dead player's own state.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "mob_test_world.hpp"
#include "opencraft/core/byte_buffer.hpp"
#include "opencraft/game/inventory.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"
#include "opencraft/game/protocol.hpp"
#include "opencraft/physics/input_state.hpp"
#include "opencraft/physics/player_physics.hpp"
#include "opencraft/physics/player_state.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/sim/world_sim.hpp"
#include "opencraft/storage/level_file.hpp"
#include "physics_test_world.hpp"
#include "player_life.hpp"

namespace {

namespace cli = opencraft::client;
namespace gam = opencraft::game;
namespace phy = opencraft::physics;
namespace srv = opencraft::server;
namespace sto = opencraft::storage;

// The client's standing eye offset (client_config.hpp kEyeStanding); the tests
// send the same pose the tick sends.
constexpr double kEyeStanding = 1.62;

// A flat world whose surface top is y = 64 (the shape tests/test_player_physics
// uses for its fall-damage cases).
[[nodiscard]] physics_test::BoxWorld flat_world() {
    physics_test::BoxWorld world;
    world.solid(-16, 16, 0, 63, -16, 16);
    return world;
}

[[nodiscard]] phy::PlayerState standing_at(const double x, const double y, const double z) {
    phy::PlayerState state;
    state.position = {x, y, z};
    state.on_ground = true;
    state.fall_peak_y = y;
    return state;
}

[[nodiscard]] gam::ActorPose pose_at(const glm::dvec3 &feet) {
    gam::ActorPose pose;
    pose.feet = feet;
    pose.height = phy::PlayerState::kStandingHeight;
    pose.eye_height = kEyeStanding;
    return pose;
}

// The spawn area loaded the way main.cpp loads it (chunk (0, 0) and its ring).
void load_spawn_area(srv::WorldSim &sim) {
    gam::StreamRequest req;
    req.generate_radius = 1;
    req.unload_radius = 1;
    req.generate_budget = 9;
    const gam::StreamResult streamed = sim.stream(req);
    REQUIRE(streamed.loaded_total == 9);
}

[[nodiscard]] std::size_t live_drops(const srv::WorldSim &sim) {
    std::size_t count = 0;
    for (const srv::EntityId id : sim.entities().live_ids()) {
        if (sim.entities().find(id) != nullptr) {
            ++count;
        }
    }
    return count;
}

// The item a test drops; "loam_clod" is the launch set's dirt clod (max_stack
// 64, ⚖ docs/01 §5), and a second, different item keeps the item layer's merge
// pass from having an opinion about the count of entities.
constexpr const char *kDropItem = "loam_clod";
constexpr const char *kSecondItem = "greyrock";

} // namespace

// ── §2.1: one entry, clamped at both ends ───────────────────────────────────

TEST_CASE("death: health is clamped to [0, 20] however big the hit is") {
    cli::PlayerLife life;
    double health = cli::kMaxHealth;

    const cli::DamageResult hit = cli::apply_damage(health, life, 30.0, {0.0, 70.0, 0.0});
    CHECK(health == 0.0);
    CHECK(hit.applied);
    CHECK(hit.died);
    CHECK(life.dead);
    CHECK(health >= 0.0); // §5.2: a 30-block fall must not leave a negative

    // Same rule from the other side: nothing pushes the health past the ⚖ 20.
    cli::PlayerLife fresh;
    double full = 15.0;
    const cli::DamageResult healed = cli::apply_damage(full, fresh, -100.0, {0.0, 0.0, 0.0});
    CHECK(full == cli::kMaxHealth);
    CHECK_FALSE(healed.died);
}

TEST_CASE("death: the alive -> dead edge fires once, and later hits are discarded") {
    cli::PlayerLife life;
    double health = 4.0;

    const cli::DamageResult first = cli::apply_damage(health, life, 4.0, {1.0, 64.0, 2.0});
    CHECK(first.died);
    CHECK(life.dead);
    CHECK(life.death_pos == glm::dvec3(1.0, 64.0, 2.0));
    CHECK(health == 0.0);

    // §2.1.2: the melee stream keeps arriving while the player is dead. Every
    // later hit must be discarded WITHOUT raising the edge again (that edge is
    // what drops the inventory) and without driving the health negative.
    for (int i = 0; i < 100; ++i) {
        const cli::DamageResult again = cli::apply_damage(health, life, 3.0, {9.0, 64.0, 9.0});
        CHECK_FALSE(again.died);
        CHECK_FALSE(again.applied);
        CHECK(again.amount == 0.0);
    }
    CHECK(health == 0.0);
    CHECK(life.death_pos == glm::dvec3(1.0, 64.0, 2.0)); // not overwritten by the later hits
}

// ── §5.3: the three damage paths all enter the death state ──────────────────

TEST_CASE("death: the FALL path enters the death state and drops what was carried") {
    physics_test::BoxWorld world = flat_world();
    srv::WorldSim sim;
    load_spawn_area(sim);
    const gam::ItemRegistry &items = sim.items();

    gam::Inventory inventory(items);
    REQUIRE(inventory.set_slot(0, gam::ItemStack::of(items.id_of(kDropItem), 12)));
    REQUIRE(inventory.set_slot(9, gam::ItemStack::of(items.id_of(kSecondItem), 3)));

    cli::PlayerLife life;
    life.respawn_pos = {0.5, 64.0, 0.5};
    cli::GameRules rules;

    // A 30-block fall onto the surface: step_player applies its own ⚖
    // floor(d − 3) (and T-D45's clamp), and the tick reconciles it through the
    // one entry.
    phy::PlayerState state = standing_at(0.5, 94.0, 0.5);
    state.on_ground = false;
    phy::InputState in;
    const double health_before = state.health;
    for (int t = 0; t < 120 && !state.on_ground; ++t) {
        phy::step_player(state, in, world);
    }
    REQUIRE(state.on_ground);
    CHECK(state.health == 0.0); // never negative (acceptance 2)

    const cli::DamageResolution fall =
        cli::take_fall_damage(inventory, sim, rules, state.health, life, health_before, pose_at(state.position));
    CHECK(fall.damage.died);
    CHECK(life.dead);
    CHECK(state.health == 0.0);
    CHECK(life.death_pos == state.position);
    CHECK(fall.drop.dropped == 2);
    CHECK(fall.drop.refused == 0);
    CHECK(live_drops(sim) == 2);
    CHECK(inventory.is_empty());
}

TEST_CASE("death: the mob-event path enters the death state through the same entry") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const gam::ItemRegistry &items = sim.items();

    gam::Inventory inventory(items);
    REQUIRE(inventory.set_slot(0, gam::ItemStack::of(items.id_of(kDropItem), 1)));

    cli::PlayerLife life;
    life.respawn_pos = {0.5, 64.0, 0.5};
    cli::GameRules rules;

    // What the authority reports for a mob hit (ActorEvent.amount) - applied the
    // way the tick applies it, at the position the hit named.
    double health = cli::kMaxHealth;
    const glm::dvec3 hit_at{0.5, 64.0, 0.5};
    const cli::DamageResolution hit =
        cli::take_damage(inventory, sim, rules, health, life, 20.0, hit_at, pose_at(hit_at));
    CHECK(hit.damage.died);
    CHECK(health == 0.0);
    CHECK(life.dead);
    CHECK(life.death_pos == hit_at);
    CHECK(live_drops(sim) == 1);
    CHECK(inventory.is_empty());
}

TEST_CASE("death: a 0-health save re-enters the world alive at the respawn point") {
    // §2.4's second half: health is persisted, the inventory is not, and nothing
    // heals - so a save that stored 0 used to start the session as a corpse.
    sto::LevelData level;
    level.has_player = true;
    level.seed = 0x4F50454E43524146ULL;
    level.health = 0.0;
    level.spawn_x = 8.5;
    level.spawn_y = 71.0;
    level.spawn_z = 8.5;
    level.player_x = 120.0; // died far from the respawn point
    level.player_y = 64.0;
    level.player_z = -300.0;
    level.player_vy = -0.5;

    // The load path, in the order main.cpp runs it.
    cli::PlayerLife life;
    life.respawn_pos = cli::choose_respawn_point(true, level, {0.0, 0.0, 0.0});
    phy::PlayerState state = standing_at(level.player_x, level.player_y, level.player_z);
    state.health = level.health;
    state.velocity = {0.1, level.player_vy, 0.2};

    REQUIRE(cli::save_needs_respawn(level.health));
    cli::respawn_player(state, life);

    CHECK(state.health == cli::kMaxHealth);
    CHECK(state.position == glm::dvec3(8.5, 71.0, 8.5));
    CHECK(state.velocity == glm::dvec3(0.0, 0.0, 0.0));
    CHECK(state.on_ground);
    CHECK_FALSE(life.dead);
    CHECK(life.respawn_pos == glm::dvec3(8.5, 71.0, 8.5));

    // ... and a healthy save is restored as it was, not teleported home.
    CHECK_FALSE(cli::save_needs_respawn(20.0));
    CHECK_FALSE(cli::save_needs_respawn(0.5));
}

// ── §5.4: the death is idempotent ───────────────────────────────────────────

TEST_CASE("death: 100 more hits while dead leave the drop count unchanged") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const gam::ItemRegistry &items = sim.items();

    gam::Inventory inventory(items);
    REQUIRE(inventory.set_slot(0, gam::ItemStack::of(items.id_of(kDropItem), 5)));
    REQUIRE(inventory.set_slot(3, gam::ItemStack::of(items.id_of(kSecondItem), 2)));

    cli::PlayerLife life;
    life.respawn_pos = {0.5, 64.0, 0.5};
    cli::GameRules rules;
    double health = cli::kMaxHealth;
    const glm::dvec3 at{0.5, 64.0, 0.5};

    const cli::DamageResolution killing = cli::take_damage(inventory, sim, rules, health, life, 20.0, at, pose_at(at));
    REQUIRE(killing.damage.died);
    REQUIRE(killing.drop.dropped == 2);
    const std::size_t after_death = live_drops(sim);
    REQUIRE(after_death == 2);

    // A mob that keeps swinging: the same call the tick's event loop makes.
    for (int tick = 0; tick < 100; ++tick) {
        const cli::DamageResolution hit = cli::take_damage(inventory, sim, rules, health, life, 1.0, at, pose_at(at));
        CHECK_FALSE(hit.damage.died);
        CHECK(hit.drop.dropped == 0);
        CHECK(hit.drop.refused == 0);
    }
    CHECK(live_drops(sim) == after_death);
    CHECK(health == 0.0);
}

// ── §5.5 / §5.6: the drop count, and the respawn ────────────────────────────

TEST_CASE("death: N filled slots become N authority drops and an empty inventory") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const gam::ItemRegistry &items = sim.items();

    gam::Inventory inventory(items);
    // One stack in a hotbar cell, one in the main section, one in an armour cell
    // and one in the offhand - the four sections the card's §2.3 criterion names.
    REQUIRE(inventory.set_slot(0, gam::ItemStack::of(items.id_of(kDropItem), 64)));
    REQUIRE(inventory.set_slot(20, gam::ItemStack::of(items.id_of(kSecondItem), 7)));
    REQUIRE(inventory.set_slot(gam::kArmorFirstSlot, gam::ItemStack::of(items.id_of("timber_headguard"), 1)));
    REQUIRE(inventory.set_slot(gam::kOffhandFirstSlot, gam::ItemStack::of(items.id_of("timber_edge"), 1)));
    CHECK(inventory.used_slots() == 4);

    cli::PlayerLife life;
    life.respawn_pos = {0.5, 64.0, 0.5};
    cli::GameRules rules;
    double health = cli::kMaxHealth;
    const glm::dvec3 at{0.5, 64.0, 0.5};

    const cli::DamageResolution hit = cli::take_damage(inventory, sim, rules, health, life, 20.0, at, pose_at(at));
    CHECK(hit.drop.dropped == 4);
    CHECK(hit.drop.refused == 0);
    CHECK(inventory.is_empty());
    CHECK(inventory.used_slots() == 0);
    CHECK(live_drops(sim) == 4);

    // Each drop is the stack that left the cell - the authority spawns through
    // the same spawn_item_stack_at() a broken block uses, so the counts travel.
    std::vector<int> counts;
    for (const srv::EntityId id : sim.entities().live_ids()) {
        const srv::Entity *drop = sim.entities().find(id);
        REQUIRE(drop != nullptr);
        counts.push_back(drop->stack.count);
    }
    std::sort(counts.begin(), counts.end());
    REQUIRE(counts.size() == 4);
    CHECK(counts[0] == 1);
    CHECK(counts[1] == 1);
    CHECK(counts[2] == 7);
    CHECK(counts[3] == 64);
}

TEST_CASE("death: respawn returns the player to the respawn point, healthy and empty-handed") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const gam::ItemRegistry &items = sim.items();

    gam::Inventory inventory(items);
    REQUIRE(inventory.set_slot(0, gam::ItemStack::of(items.id_of(kDropItem), 4)));

    cli::PlayerLife life;
    life.respawn_pos = {8.5, 71.0, 8.5};
    life.dead = true;
    life.death_pos = {120.0, 30.0, -400.0};
    cli::GameRules rules; // keepInventory off: the default

    phy::PlayerState state = standing_at(life.death_pos.x, life.death_pos.y, life.death_pos.z);
    state.health = 0.0;
    state.velocity = {0.0, -0.5, 0.0};
    state.fall_peak_y = 200.0;
    state.fall_distance = 170.0;

    cli::respawn_player(state, life);
    cli::respawn_clear_inventory(inventory, rules);

    CHECK(state.position == glm::dvec3(8.5, 71.0, 8.5));
    CHECK(state.health == cli::kMaxHealth);
    CHECK(state.velocity == glm::dvec3(0.0, 0.0, 0.0));
    CHECK(state.fall_distance == 0.0);
    CHECK(state.fall_peak_y == 71.0);
    CHECK_FALSE(life.dead);
    CHECK(life.death_pos == glm::dvec3(0.0, 0.0, 0.0));
    CHECK(inventory.is_empty()); // §5.6
}

TEST_CASE("death: keepInventory keeps the cells a death would have emptied") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const gam::ItemRegistry &items = sim.items();

    gam::Inventory inventory(items);
    REQUIRE(inventory.set_slot(0, gam::ItemStack::of(items.id_of(kDropItem), 9)));

    cli::PlayerLife life;
    life.respawn_pos = {0.5, 64.0, 0.5};
    cli::GameRules rules;
    rules.keep_inventory = true;
    double health = cli::kMaxHealth;
    const glm::dvec3 at{0.5, 64.0, 0.5};

    const cli::DamageResolution hit = cli::take_damage(inventory, sim, rules, health, life, 20.0, at, pose_at(at));
    CHECK(hit.damage.died);
    CHECK(life.dead);
    CHECK(hit.drop.dropped == 0);
    CHECK(live_drops(sim) == 0);
    CHECK(inventory.count_of(items.id_of(kDropItem)) == 9); // still in hand

    cli::respawn_clear_inventory(inventory, rules);
    CHECK(inventory.count_of(items.id_of(kDropItem)) == 9); // and still in hand after respawning
}

// ── §5.7: the respawn point does not drift (the §2.4 trap) ──────────────────

TEST_CASE("death: the respawn point survives two save/load round trips unchanged") {
    cli::PlayerLife life;
    life.respawn_pos = {8.5, 71.0, 8.5};

    sto::LevelData level;
    level.has_player = true;
    level.seed = 0x4F50454E43524146ULL;
    level.health = 20.0;
    // The player has wandered far from home since the spawn scan.
    level.player_x = 512.5;
    level.player_y = 70.0;
    level.player_z = -1024.5;

    cli::write_respawn_point(level, life);
    CHECK(level.spawn_x == 8.5);
    CHECK(level.spawn_y == 71.0);
    CHECK(level.spawn_z == 8.5);

    for (int trip = 0; trip < 2; ++trip) {
        opencraft::core::ByteBuffer payload = sto::serialize_level(level);
        level = sto::deserialize_level(payload);

        const glm::dvec3 point = cli::choose_respawn_point(true, level, {0.0, 0.0, 0.0});
        CHECK(point == life.respawn_pos);
        // ... and the SESSION start is the last quit position, which is exactly
        // why the respawn point cannot be read off it.
        CHECK(glm::dvec3(level.player_x, level.player_y, level.player_z) != point);
        // Re-save: the same point goes back in, so it cannot drift one round trip
        // at a time either.
        cli::write_respawn_point(level, life);
    }

    // A save with no usable point (y == 0: never written) falls back to the scan.
    sto::LevelData unwritten;
    unwritten.has_player = true;
    CHECK(cli::choose_respawn_point(true, unwritten, {4.5, 66.0, 4.5}) == glm::dvec3(4.5, 66.0, 4.5));
    // No save at all: also the scan.
    CHECK(cli::choose_respawn_point(false, level, {4.5, 66.0, 4.5}) == glm::dvec3(4.5, 66.0, 4.5));
}

// ── §5.9: a dead player's tick may do nothing ───────────────────────────────

TEST_CASE("death: the tick gate is closed for a dead player") {
    cli::PlayerLife life;
    CHECK(cli::may_act(life));
    life.dead = true;
    CHECK_FALSE(cli::may_act(life)); // the one gate run_tick's interaction half hangs off
}

// ── the authority's side of the drop (the verb the client sends) ────────────

TEST_CASE("authority: DropItems spawns the stack, and re-checks item, count and chunk") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const gam::ItemRegistry &items = sim.items();
    const std::uint16_t loam = items.id_of(kDropItem);

    const auto request = [&](const std::uint16_t item, const int count, const glm::dvec3 &feet) {
        gam::ActionRequest req;
        req.kind = gam::ActionKind::DropItems;
        req.target = {count, 0, 0}; // borrowed for the count, see protocol.hpp
        req.item_or_block = item;
        req.actor = pose_at(feet);
        return req;
    };

    // Accepted: the drop appears at the actor's middle, in the loaded chunk.
    const int surface = sim.surface_height(0, 0);
    const glm::dvec3 feet{0.5, static_cast<double>(surface), 0.5};
    const gam::ActionResult ok = sim.submit(request(loam, 30, feet));
    CHECK(ok.accepted);
    REQUIRE(live_drops(sim) == 1);
    {
        const srv::Entity *drop = sim.entities().find(sim.entities().live_ids().front());
        REQUIRE(drop != nullptr);
        CHECK(drop->stack.item == loam);
        CHECK(drop->stack.count == 30);
        CHECK(drop->pickup_delay == sim.item_rules().pickup_delay_natural);
    }

    // The empty id and an id past the table are UnknownItem.
    CHECK(sim.submit(request(gam::ItemRegistry::kEmptyId, 1, feet)).reject == gam::ActionReject::UnknownItem);
    CHECK(sim.submit(request(static_cast<std::uint16_t>(items.size() + 40), 1, feet)).reject ==
          gam::ActionReject::UnknownItem);

    // The count is re-derived against the item's own ⚖ stack limit.
    CHECK(sim.submit(request(loam, 0, feet)).reject == gam::ActionReject::BadStackCount);
    CHECK(sim.submit(request(loam, -5, feet)).reject == gam::ActionReject::BadStackCount);
    CHECK(sim.submit(request(loam, 65, feet)).reject == gam::ActionReject::BadStackCount);
    CHECK(sim.submit(request(items.id_of("timber_edge"), 2, feet)).reject ==
          gam::ActionReject::BadStackCount);             // max_stack 1
    CHECK(sim.submit(request(loam, 64, feet)).accepted); // exactly the limit

    // A chunk that is not resident is refused: a drop there would be frozen.
    const glm::dvec3 far{4096.5, 64.0, 4096.5};
    CHECK(sim.submit(request(loam, 1, far)).reject == gam::ActionReject::ChunkNotLoaded);

    // Nothing above generated a drop it should not have.
    CHECK(live_drops(sim) == 2);
}

// ── T-D46: combat, the player's side ────────────────────────────────────────
//
// These live in this file because player_life.hpp is the file they drive: it
// owns the ONE damage entry (T-D45) and now owns the three combat rules too -
// the hurt window, the armour reduction and the knockback. What a test CANNOT
// see is the tick's wiring (it polls a real window); that part is covered
// on-machine in docs/qa/T-D46-2026-09-19/.

// The four armour cells, filled from the launch kit's pieces.
void wear_timber_set(gam::Inventory &inventory, const gam::ItemRegistry &items) {
    REQUIRE(inventory.set_slot(gam::armor_slot_index(gam::ArmorSlot::Head),
                               gam::ItemStack::of(items.id_of("timber_headguard"), 1)));
    REQUIRE(inventory.set_slot(gam::armor_slot_index(gam::ArmorSlot::Chest),
                               gam::ItemStack::of(items.id_of("timber_cuirass"), 1)));
    REQUIRE(inventory.set_slot(gam::armor_slot_index(gam::ArmorSlot::Legs),
                               gam::ItemStack::of(items.id_of("timber_greaves"), 1)));
    REQUIRE(inventory.set_slot(gam::armor_slot_index(gam::ArmorSlot::Feet),
                               gam::ItemStack::of(items.id_of("timber_treads"), 1)));
}

TEST_CASE("combat: a second hit inside the window is immune, a bigger one settles the excess") {
    // ⚖ research/11 §1.5.1 through the player's window (contract ① / §5.2 1-2).
    phy::PlayerState state;
    CHECK(state.invulnerability_ticks == 0);
    CHECK(cli::settle_hurt(state, 3.0) == doctest::Approx(3.0)); // the window was closed
    CHECK(state.invulnerability_ticks == cli::kHurtInvulnerabilityTicks);
    CHECK(state.last_hurt_amount == doctest::Approx(3.0));

    // Same amount, smaller amount: nothing at all.
    CHECK(cli::settle_hurt(state, 3.0) == doctest::Approx(0.0));
    CHECK(cli::settle_hurt(state, 1.0) == doctest::Approx(0.0));
    CHECK(state.last_hurt_amount == doctest::Approx(3.0)); // an absorbed hit does not move the bar

    // Bigger: only the difference lands, and the bar moves to the new amount.
    CHECK(cli::settle_hurt(state, 5.0) == doctest::Approx(2.0));
    CHECK(state.last_hurt_amount == doctest::Approx(5.0));
    CHECK(state.invulnerability_ticks == cli::kHurtInvulnerabilityTicks); // re-armed
}

TEST_CASE("combat: the hurt window is 10 ticks of integer counting") {
    // §5.2 3: the boundary, counted in TICKS - which is also what shows the
    // counter is an integer tick count and not a seconds-valued double
    // (contract ②).
    phy::PlayerState state;
    CHECK(cli::settle_hurt(state, 3.0) == doctest::Approx(3.0)); // hit on tick 0

    // Ticks 1..9: each one advances the window and then settles a hit, which is
    // the order the live tick uses (world_tail) and the mobs use (advance_timers
    // before the goals).
    for (int tick = 1; tick <= 9; ++tick) {
        cli::tick_hurt_window(state);
        INFO("tick " << tick);
        CHECK(cli::settle_hurt(state, 3.0) == doctest::Approx(0.0));
    }
    // Tick 10 counts the window down to zero, so the same hit settles in full
    // again: the window spans 10 ticks INCLUDING the tick the hit landed on.
    cli::tick_hurt_window(state);
    CHECK(state.invulnerability_ticks == 0);
    CHECK(cli::settle_hurt(state, 3.0) == doctest::Approx(3.0));
}

TEST_CASE("combat: the player's window IS damage_mob's rule, tick for tick") {
    // ★ The C-1 cost, pinned. The player's window (client) and the mobs' window
    // (the authority) are two implementations of one rule, and this test drives
    // both through the SAME schedule of hits one tick at a time and requires the
    // damage that lands to agree at every step. It is what "must stay in sync"
    // means operationally: if either side's ordering or comparison changes, this
    // fails even though each side's own test still passes.
    mobtest::MobFixture fixture;
    const srv::EntityId target = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    phy::PlayerState state;

    // Amounts chosen to exercise all three branches repeatedly: immune (≤ last),
    // excess (> last, inside the window) and a full settle (window closed).
    constexpr double kAmounts[] = {5.0, 3.0, 8.0, 8.0, 1.0,  12.0, 4.0, 2.0, 5.0,
                                   3.0, 8.0, 8.0, 1.0, 12.0, 4.0,  2.0, 5.0};
    double player_health = cli::kMaxHealth;
    for (std::size_t tick = 0; tick < sizeof(kAmounts) / sizeof(kAmounts[0]); ++tick) {
        if (tick > 0) {
            // One tick of the world for each side, in each side's own order.
            (void) fixture.step(std::nullopt, gam::Difficulty::Normal, 1);
            cli::tick_hurt_window(state);
        }
        const double amount = kAmounts[tick];
        const double before = fixture.get(target).health;
        (void) srv::damage_mob(fixture.store, fixture.mobs, target, amount, srv::kActorId, fixture.rules);
        const double mob_lost = before - fixture.get(target).health;
        const double player_lost = cli::settle_hurt(state, amount);
        INFO("tick " << tick << " amount " << amount);
        CHECK(player_lost == doctest::Approx(mob_lost));
        player_health -= player_lost;
    }
    // The schedule settles 5 (t0) + 3 (t2, the excess over 5) + 4 (t5, over 8) +
    // 2 (t15, the window having closed) + 3 (t16, over 2) = 17 over seventeen
    // ticks; both sides must have lost exactly that.
    CHECK(fixture.get(target).health == doctest::Approx(3.0)); // 20 - 17
    CHECK(player_health == doctest::Approx(cli::kMaxHealth - 17.0));
}

TEST_CASE("combat: armour reduces a hit by research/01 §2.1's formula") {
    // §5.2 4: bare vs one piece vs the full set, against the arithmetic the card
    // worked out (§4.1) rather than against a re-run of the same expression.
    const gam::ItemRegistry items = gam::ItemRegistry::create_default();
    gam::Inventory bare(items);
    gam::Inventory head_only(items);
    gam::Inventory full(items);
    REQUIRE(head_only.set_slot(gam::armor_slot_index(gam::ArmorSlot::Head),
                               gam::ItemStack::of(items.id_of("timber_headguard"), 1)));
    wear_timber_set(full, items);

    CHECK(cli::equipped_armor(bare).points == doctest::Approx(0.0));
    CHECK(cli::equipped_armor(head_only).points == doctest::Approx(1.0));
    CHECK(cli::equipped_armor(full).points == doctest::Approx(7.0));
    CHECK(cli::equipped_armor(full).toughness == doctest::Approx(0.0));

    // No armour: the hit passes through untouched.
    CHECK(cli::damage_after_armor(3.0, cli::DamageType::Melee, cli::equipped_armor(bare)) == doctest::Approx(3.0));
    // One point: max(1/5, 1 − 4×3/8) = 0.2 ⇒ 0.8% ⇒ 2.976.
    CHECK(cli::damage_after_armor(3.0, cli::DamageType::Melee, cli::equipped_armor(head_only)) ==
          doctest::Approx(2.976));
    // ⚖ The card's own table (§4.1), leather 7 / toughness 0, damage ⇒ what lands:
    CHECK(cli::damage_after_armor(1.0, cli::DamageType::Melee, cli::equipped_armor(full)) == doctest::Approx(0.74));
    CHECK(cli::damage_after_armor(2.5, cli::DamageType::Melee, cli::equipped_armor(full)) == doctest::Approx(1.925));
    CHECK(cli::damage_after_armor(3.0, cli::DamageType::Melee, cli::equipped_armor(full)) == doctest::Approx(2.34));
    CHECK(cli::damage_after_armor(4.5, cli::DamageType::Melee, cli::equipped_armor(full)) == doctest::Approx(3.645));
    CHECK(cli::damage_after_armor(20.0, cli::DamageType::Melee, cli::equipped_armor(full)) == doctest::Approx(18.88));
    // The reductions themselves, so a reader sees the 26/23/22/19% ladder.
    CHECK(cli::armor_reduction(7.0, 0.0, 1.0) == doctest::Approx(0.26));
    CHECK(cli::armor_reduction(7.0, 0.0, 3.0) == doctest::Approx(0.22));
    CHECK(cli::armor_reduction(7.0, 0.0, 4.5) == doctest::Approx(0.19));
    // The tier distinction §4.1 asks for: the same 3.0 against the IRON set is
    // 54%, so the two tiers cannot be confused - computed from the numbers, not
    // from a registered iron piece (this card registers none, C-3).
    CHECK(cli::armor_reduction(15.0, 0.0, 3.0) == doctest::Approx(0.54));
}

TEST_CASE("combat: a huge hit falls back to the armour/5 floor, not the 80% ceiling") {
    // §5.2 5 / research/01 §2.1's BreakingPoint: max(7/5, 7 − 4×1000/8) = 1.4,
    // so a 1000-damage hit is reduced by 5.6% - NOT by the 80% the ceiling would
    // give. This is the assertion that pins the lower branch of the formula.
    const gam::ItemRegistry items = gam::ItemRegistry::create_default();
    gam::Inventory full(items);
    wear_timber_set(full, items);
    const cli::ArmorTotals armor = cli::equipped_armor(full);

    CHECK(cli::armor_reduction(armor.points, armor.toughness, 1000.0) == doctest::Approx(0.056));
    CHECK(cli::damage_after_armor(1000.0, cli::DamageType::Melee, armor) == doctest::Approx(944.0));
    // …and explicitly not the ceiling: 20/25 = 0.8 would have left 200.
    CHECK(cli::damage_after_armor(1000.0, cli::DamageType::Melee, armor) > 200.0);
    // The ceiling itself is still reachable - with enough armour the outer min()
    // is what binds (20 points is the ⚖ cap of the armour scale).
    CHECK(cli::armor_reduction(30.0, 0.0, 1.0) == doctest::Approx(0.8));
}

TEST_CASE("combat: exempt damage bypasses armour, so a fall costs the same in full timber_*") {
    // §5.2 6 / ⑤ (research/01 §2.3): 摔落绕过护甲. Driven through the real fall
    // entry, so this fails if the exemption is ever removed - the fall path runs
    // the same reduction call with an exempt type rather than skipping it.
    const gam::ItemRegistry items = gam::ItemRegistry::create_default();
    gam::Inventory bare(items);
    gam::Inventory full(items);
    wear_timber_set(full, items);

    srv::WorldSim sim;
    cli::GameRules rules;

    // What the physics step left behind: 10 points of fall damage, reconciled
    // through take_fall_damage the way the tick does it.
    cli::PlayerLife bare_life;
    double bare_health = 20.0 - 10.0;
    const cli::DamageResolution bare_fall =
        cli::take_fall_damage(bare, sim, rules, bare_health, bare_life, 20.0, pose_at({0.5, 64.0, 0.5}));
    REQUIRE(bare_fall.damage.applied);
    CHECK(bare_fall.damage.amount == doctest::Approx(10.0));

    cli::PlayerLife armored_life;
    double armored_health = 20.0 - 10.0;
    const cli::DamageResolution armored_fall =
        cli::take_fall_damage(full, sim, rules, armored_health, armored_life, 20.0, pose_at({0.5, 64.0, 0.5}));
    REQUIRE(armored_fall.damage.applied);
    CHECK(armored_fall.damage.amount == doctest::Approx(bare_fall.damage.amount)); // 同额
    CHECK(armored_health == doctest::Approx(bare_health));

    // The exemption is a property of the TYPE, and the other three listed by ⑤
    // behave the same way; a melee hit through the same call is reduced.
    CHECK(cli::bypasses_armor(cli::DamageType::Fall));
    CHECK(cli::bypasses_armor(cli::DamageType::Suffocation));
    CHECK(cli::bypasses_armor(cli::DamageType::Void));
    CHECK(cli::bypasses_armor(cli::DamageType::Starvation));
    CHECK_FALSE(cli::bypasses_armor(cli::DamageType::Melee));
    CHECK_FALSE(cli::bypasses_armor(cli::DamageType::Explosion));
    CHECK(cli::damage_after_armor(10.0, cli::DamageType::Melee, cli::equipped_armor(full)) < 10.0);
}

TEST_CASE("combat: a hit pushes the player ⚖ 0.4 away from the attacker, then friction eats it") {
    // §5.2 7's player half (⑥ / C-4's convention, written down): the impulse is
    // an INITIAL SPEED of 0.4 blocks/tick written into the stored velocity at the
    // end of the tick that took the hit, so the NEXT step_player displaces the
    // full 0.4 before the damping runs - and then decays.
    physics_test::BoxWorld world = flat_world();
    phy::PlayerState state = standing_at(0.5, 64.0, 0.5);
    const glm::dvec3 attacker{0.5, 64.0, 1.5}; // straight +Z of the player
    cli::push_away(state, attacker, cli::kKnockbackSpeed);
    CHECK(state.velocity.x == doctest::Approx(0.0));
    CHECK(state.velocity.z == doctest::Approx(-0.4));
    CHECK(state.velocity.y == doctest::Approx(0.0)); // C-4: horizontal only

    const glm::dvec3 before = state.position;
    phy::InputState idle;
    phy::step_player(state, idle, world);
    // The whole initial speed, on the first tick - this is the convention the
    // card asks to be pinned (§7.4): the "0.4" IS a speed and the first tick
    // really moves that far.
    CHECK(state.position.z - before.z == doctest::Approx(-0.4));

    // Damping is applied after the move (0.91 x 0.6 on ordinary ground), so the
    // total travel is 0.4 / (1 - 0.546); the geometric tail is cut by the
    // momentum threshold, which is where the small deficit comes from.
    for (int i = 0; i < 40; ++i) {
        phy::step_player(state, idle, world);
    }
    const double pushed = before.z - state.position.z;
    INFO("pushed " << pushed << " blocks");
    CHECK(pushed > 0.85);
    CHECK(pushed < 0.89);
    CHECK(state.position.x == doctest::Approx(before.x)); // no sideways drift
}

TEST_CASE("combat: an attacker on top of the player pushes nothing (no direction to push)") {
    physics_test::BoxWorld world = flat_world();
    phy::PlayerState state = standing_at(0.5, 64.0, 0.5);
    cli::push_away(state, state.position, cli::kKnockbackSpeed);
    CHECK(state.velocity == glm::dvec3(0.0, 0.0, 0.0));
}

TEST_CASE("combat: the chain end to end - Blastbud's 4.5 needs 5 bare hits and 6 in timber_*") {
    // §4.1's 手感口径, asserted through the three real calls in the order the
    // tick makes them (window, armour, the one damage entry) rather than through
    // the formula alone. It is also the answer to §7.6's question about the
    // bare-handed feel.
    const gam::ItemRegistry items = gam::ItemRegistry::create_default();
    const auto hits_to_kill = [&](gam::Inventory &inventory) {
        phy::PlayerState state;
        cli::PlayerLife life;
        int hits = 0;
        // 20 ticks apart: a Blastbud's own melee cadence, and well clear of the
        // 10-tick window - so every hit is a full one and the count is about the
        // armour alone.
        while (!life.dead && hits < 40) {
            if (hits > 0) {
                for (int tick = 0; tick < 20; ++tick) {
                    cli::tick_hurt_window(state);
                }
            }
            const double settled = cli::settle_hurt(state, 4.5);
            REQUIRE(settled == doctest::Approx(4.5));
            const double landed =
                cli::damage_after_armor(settled, cli::DamageType::Melee, cli::equipped_armor(inventory));
            (void) cli::apply_damage(state.health, life, landed, {0.5, 64.0, 0.5});
            ++hits;
        }
        return hits;
    };

    gam::Inventory bare(items);
    gam::Inventory full(items);
    wear_timber_set(full, items);
    CHECK(hits_to_kill(bare) == 5); // 20 / 4.5   = 4.44
    CHECK(hits_to_kill(full) == 6); // 20 / 3.645 = 5.49 - one more hit per kill
}
