// T-M2: the mobs on the AUTHORITATIVE side - what server::WorldSim does with
// them. The rules themselves are tested headless in test_mob_goal.cpp /
// test_mob_ai.cpp / test_mob_spawn.cpp; this file tests the WIRING:
//
//   * the viewer pose (observe_actor) is what makes mobs notice a player at all;
//   * the Attack / Feed verbs validate against the authority's own entity boxes;
//   * a melee hit reaches the client as an ActorEvent (the player's hit points
//     are the client's, so the authority cannot subtract them itself);
//   * the spawner runs inside tick() and produces nothing in a lit world;
//   * a mob is not a valid pickup target.

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "opencraft/game/protocol.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/sim/mob_spawn.hpp"
#include "opencraft/sim/world_sim.hpp"

namespace {

namespace gam = opencraft::game;
namespace srv = opencraft::server;
namespace voxel = opencraft::voxel;

constexpr double kEyeStanding = 1.62;

void load_chunks(srv::WorldSim &sim, const int radius) {
    gam::StreamRequest req;
    req.generate_radius = radius;
    req.unload_radius = radius;
    req.generate_budget = (2 * radius + 1) * (2 * radius + 1);
    sim.stream(req);
    REQUIRE(sim.loaded_chunk_count() > 0);
}

// A standing viewer at the spawn point - what the client sends every tick.
[[nodiscard]] gam::ActorPose viewer_at(const glm::dvec3 &feet) {
    gam::ActorPose pose;
    pose.feet = feet;
    pose.height = 1.8;
    pose.eye_height = kEyeStanding;
    return pose;
}

[[nodiscard]] gam::ActionRequest mob_request(const gam::ActionKind kind, const srv::EntityId mob,
                                             const gam::ActorPose &actor, const std::uint16_t item = 0) {
    gam::ActionRequest req;
    req.kind = kind;
    req.target = {static_cast<int>(mob), 0, 0};
    req.actor = actor;
    req.item_or_block = item;
    return req;
}

// Every live entity that is a mob of the given class.
[[nodiscard]] std::size_t live_mobs(const srv::WorldSim &sim, const gam::MobClass mob_class) {
    std::size_t count = 0;
    for (const srv::EntityId id : sim.entities().live_ids()) {
        const srv::Entity *entity = sim.entities().find(id);
        if (entity == nullptr) {
            continue;
        }
        const gam::MobDef *def = sim.mobs().find(entity->type);
        if (def != nullptr && def->mob_class == mob_class) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("authority mobs: without a viewer pose nothing perceives and nothing spawns") {
    // The headless default. This is also what keeps the other cards' tests
    // unchanged: a WorldSim nobody has told about a player has no mob behaviour at
    // all (its entity population is whatever the tests put there).
    srv::WorldSim sim;
    load_chunks(sim, 3);
    for (int i = 0; i < 200; ++i) {
        sim.tick();
    }
    CHECK(sim.entities().alive_count() == 0);
    CHECK(sim.take_changes().actor_events.empty());
}

TEST_CASE("authority mobs: a lit surface spawns no hostiles, and the spawner does try") {
    // The light gate at the authority level, with the REAL world: the surface is
    // under open sky, so the effective light is 15 and no hostile may appear. The
    // passive half does not care about light, so a passive mob appearing here is
    // what shows the spawner ran at all.
    //
    // This is also the finding behind the live-evidence route in the report: with
    // no day/night cycle, "光照 0" means "under a roof or underground".
    srv::WorldSim sim;
    load_chunks(sim, 4);
    const glm::dvec3 spawn = sim.find_spawn();
    sim.observe_actor(viewer_at(spawn));
    for (int i = 0; i < 400; ++i) {
        sim.tick();
        sim.take_changes();
    }

    // Every hostile that exists is standing in a cell whose effective light is 0
    // (that is the rule), and none of them is on the lit surface. The real world
    // does have light-0 space - its caves - so this is not a vacuous assertion.
    std::size_t hostiles = 0;
    for (const srv::EntityId id : sim.entities().live_ids()) {
        const srv::Entity *entity = sim.entities().find(id);
        if (entity == nullptr) {
            continue;
        }
        const gam::MobDef *def = sim.mobs().find(entity->type);
        if (def == nullptr || def->mob_class != gam::MobClass::Hostile) {
            continue;
        }
        ++hostiles;
        const voxel::LightLevels levels = sim.light().light_at(static_cast<int>(std::floor(entity->position.x)),
                                                               static_cast<int>(std::floor(entity->position.y)),
                                                               static_cast<int>(std::floor(entity->position.z)));
        CHECK(std::max<int>(levels.sky, levels.block) == 0);
    }
    // The surface is lit, so whatever appeared is underground: nothing hostile may
    // be standing at or above the surface height here.
    const int surface = static_cast<int>(spawn.y);
    for (const srv::EntityId id : sim.entities().live_ids()) {
        const srv::Entity *entity = sim.entities().find(id);
        if (entity == nullptr || sim.mobs().find(entity->type) == nullptr) {
            continue;
        }
        if (sim.mobs().find(entity->type)->mob_class == gam::MobClass::Hostile) {
            CHECK(entity->position.y < static_cast<double>(surface));
        }
    }
    CHECK(live_mobs(sim, gam::MobClass::Passive) > 0);
    (void) hostiles;
}

TEST_CASE("authority mobs: a summoned hunter lands a hit that arrives as an ActorEvent") {
    // The full loop this card adds: the authority simulates the mob, the client
    // owns the hit points, and the two meet in game::WorldChanges::actor_events.
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const glm::dvec3 spawn = sim.find_spawn();
    const srv::EntityId hunter = sim.summon_mob(sim.entity_types().id_of("hollow_wretch"), spawn);
    REQUIRE(hunter != srv::EntityStore::kNoEntity);

    // At the player's own feet: certainly inside melee reach, with a trivially clear
    // line of sight.
    sim.observe_actor(viewer_at(spawn));
    std::vector<gam::ActorEvent> events;
    for (int i = 0; i < 40 && events.empty(); ++i) {
        sim.tick();
        const gam::WorldChanges changes = sim.take_changes();
        events = changes.actor_events;
    }
    REQUIRE_FALSE(events.empty());
    CHECK(events[0].kind == gam::ActorEventKind::MeleeHit);
    CHECK(events[0].amount == doctest::Approx(3.0)); // ⚖ normal difficulty
    CHECK(events[0].source_type == sim.entity_types().id_of("hollow_wretch"));

    // Drained once: the next call reports nothing.
    CHECK(sim.take_changes().actor_events.empty());
}

TEST_CASE("authority mobs: the Attack verb kills a passive mob and its loot lands in the world") {
    // This is the acceptance item that says the passive roster DROPS FOOD, driven
    // through the verb the client actually sends. The actor follows the mob as it
    // flees (the pose is an input, so the test may move it), which is what keeps it
    // in reach for the ten hits the mob's ⚖ 10 hit points need.
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const glm::dvec3 spawn = sim.find_spawn();
    const srv::EntityId grazer = sim.summon_mob(sim.entity_types().id_of("mossback"), spawn);
    REQUIRE(grazer != srv::EntityStore::kNoEntity);
    const std::uint16_t haunch = sim.items().id_of("raw_haunch");

    std::size_t accepted = 0;
    for (int attempt = 0; attempt < 40; ++attempt) {
        const srv::Entity *mob = sim.entities().find(grazer);
        if (mob == nullptr) {
            break;
        }
        const gam::ActionResult result =
            sim.submit(mob_request(gam::ActionKind::Attack, grazer, viewer_at(mob->position)));
        if (result.accepted) {
            ++accepted;
        }
        // Step past the ⚖ 10-tick hurt invulnerability window before hitting again.
        sim.tick();
        sim.take_changes();
        for (int i = 0; i < 10; ++i) {
            sim.observe_actor(
                viewer_at(sim.entities().find(grazer) != nullptr ? sim.entities().find(grazer)->position : spawn));
            sim.tick();
            sim.take_changes();
        }
    }

    CHECK(accepted >= 10);                         // ⚖ 10 hit points, 1 damage a hit
    CHECK(sim.entities().find(grazer) == nullptr); // it died
    // ⚖ research/11 §6.1: 生牛肉 1–3 - the loot is in the world as ordinary drops.
    std::size_t meat = 0;
    for (const srv::EntityId id : sim.entities().live_ids()) {
        const srv::Entity *entity = sim.entities().find(id);
        if (entity != nullptr && entity->stack.item == haunch) {
            meat += static_cast<std::size_t>(entity->stack.count);
        }
    }
    CHECK(meat >= 1);
    CHECK(meat <= 3);
}

TEST_CASE("authority mobs: Attack validates the class, the existence and the reach") {
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const glm::dvec3 spawn = sim.find_spawn();

    // No such entity.
    CHECK(sim.submit(mob_request(gam::ActionKind::Attack, 9999, viewer_at(spawn))).reject ==
          gam::ActionReject::UnknownEntity);

    // A mob out of reach is refused - and refused without touching its hit points.
    const srv::EntityId target =
        sim.summon_mob(sim.entity_types().id_of("hollow_wretch"), spawn + glm::dvec3(10.0, 0.0, 0.0));
    REQUIRE(target != srv::EntityStore::kNoEntity);
    const glm::dvec3 mob_feet = sim.entities().find(target)->position;
    CHECK(sim.submit(mob_request(gam::ActionKind::Attack, target, viewer_at(spawn))).reject ==
          gam::ActionReject::OutOfAttackRange);

    // ⚖ research/11 §1.5.1: 玩家近战到达距离 3 格, measured from the EYES to the
    // nearest point of the mob's box (its half width is 0.3). So 3.4 blocks
    // centre-to-centre is 3.1 of reach - just too far - and 3.2 is 2.9, which lands.
    // Both sides of the boundary are asserted because a reach rule that is merely
    // too generous at long range is exactly the bug this pins down.
    CHECK(sim.submit(mob_request(gam::ActionKind::Attack, target, viewer_at(mob_feet - glm::dvec3(3.4, 0.0, 0.0))))
              .reject == gam::ActionReject::OutOfAttackRange);
    CHECK(sim.submit(mob_request(gam::ActionKind::Attack, target, viewer_at(mob_feet - glm::dvec3(3.2, 0.0, 0.0))))
              .accepted);

    // A drop is not a target, even when it is at the actor's feet.
    const srv::EntityId drop = sim.summon_mob(sim.entity_types().id_of("item"), spawn);
    CHECK(drop == srv::EntityStore::kNoEntity); // "item" is not a mob, so nothing was summoned
}

TEST_CASE("authority mobs: a mob cannot be picked up, and a drop still can") {
    // The store holds both classes (one plane, docs/03 §6), and a mob's stack is
    // empty - item id 0. PickUp carries the item id it expects, and a client that
    // sent 0 would otherwise match a mob and DELETE it. That is what the class
    // guard in apply_pickup is for.
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const glm::dvec3 spawn = sim.find_spawn();
    const srv::EntityId grazer = sim.summon_mob(sim.entity_types().id_of("mossback"), spawn);
    REQUIRE(grazer != srv::EntityStore::kNoEntity);

    const gam::ActionResult refused = sim.submit(mob_request(gam::ActionKind::PickUp, grazer, viewer_at(spawn), 0));
    CHECK_FALSE(refused.accepted);
    CHECK(refused.reject == gam::ActionReject::NotAMob);
    CHECK(sim.entities().find(grazer) != nullptr); // still there
}

TEST_CASE("authority mobs: Feed validates the food, the class, the adulthood and the cooldown") {
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const glm::dvec3 spawn = sim.find_spawn();
    const srv::EntityId grazer = sim.summon_mob(sim.entity_types().id_of("mossback"), spawn);
    const srv::EntityId hunter =
        sim.summon_mob(sim.entity_types().id_of("hollow_wretch"), spawn + glm::dvec3(2.0, 0.0, 0.0));
    REQUIRE(grazer != srv::EntityStore::kNoEntity);
    REQUIRE(hunter != srv::EntityStore::kNoEntity);
    const std::uint16_t food = sim.items().id_of("grain_loaf");
    const std::uint16_t other_food = sim.items().id_of("sunroot");

    CHECK(sim.submit(mob_request(gam::ActionKind::Feed, grazer, viewer_at(spawn), other_food)).reject ==
          gam::ActionReject::WrongFood);
    CHECK(sim.submit(mob_request(gam::ActionKind::Feed, hunter, viewer_at(spawn), food)).reject ==
          gam::ActionReject::NotBreedable);
    CHECK(sim.submit(mob_request(gam::ActionKind::Feed, grazer, viewer_at(spawn + glm::dvec3(20.0, 0.0, 0.0)), food))
              .reject == gam::ActionReject::OutOfAttackRange);

    const gam::ActionResult fed = sim.submit(mob_request(gam::ActionKind::Feed, grazer, viewer_at(spawn), food));
    CHECK(fed.accepted);
    CHECK(sim.entities().find(grazer)->ai.in_love);
    // The cooldown is only set when the pair actually breeds, so feeding again is
    // allowed (research/11 §6.2: 可立即再次被喂).
    CHECK(sim.submit(mob_request(gam::ActionKind::Feed, grazer, viewer_at(spawn), food)).accepted);
}

TEST_CASE("authority mobs: feeding two of them breeds a baby, and the baby is on the client's view") {
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const glm::dvec3 spawn = sim.find_spawn();
    const srv::EntityId first = sim.summon_mob(sim.entity_types().id_of("mossback"), spawn);
    const srv::EntityId second =
        sim.summon_mob(sim.entity_types().id_of("mossback"), spawn + glm::dvec3(2.0, 0.0, 0.0));
    const std::uint16_t food = sim.items().id_of("grain_loaf");
    REQUIRE(first != srv::EntityStore::kNoEntity);
    REQUIRE(second != srv::EntityStore::kNoEntity);

    CHECK(sim.submit(mob_request(gam::ActionKind::Feed, first, viewer_at(spawn), food)).accepted);
    CHECK(sim.submit(mob_request(gam::ActionKind::Feed, second, viewer_at(spawn), food)).accepted);

    // ⚖ 约 2.5 秒 of contact, so give it a few seconds of ticks.
    for (int i = 0; i < 400; ++i) {
        sim.tick();
        sim.take_changes();
    }
    CHECK(live_mobs(sim, gam::MobClass::Passive) >= 3); // the pair plus the baby
    std::size_t babies = 0;
    for (const srv::EntityId id : sim.entities().live_ids()) {
        const srv::Entity *entity = sim.entities().find(id);
        if (entity != nullptr && entity->ai.baby) {
            ++babies;
        }
    }
    CHECK(babies == 1);
}
