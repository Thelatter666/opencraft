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
