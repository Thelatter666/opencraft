// T-E1: the drop on the authoritative side - what server::WorldSim does with it.
//
// test_item_drop.cpp tests the rules against a hand-built block world; this file
// tests the wiring the card's acceptance items 3 and 4 are about: a successful
// dig leaves a drop behind, the drop belongs to the authority (the client only
// reads it), and the PickUp request hands one over only when everything the
// authority can check says it may.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "opencraft/game/pickup.hpp"
#include "opencraft/game/protocol.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/sim/item_sim.hpp"
#include "opencraft/sim/world_sim.hpp"
#include "opencraft/voxel/chunk.hpp"

namespace {

namespace gam = opencraft::game;
namespace srv = opencraft::server;
namespace voxel = opencraft::voxel;

// The eye offset of the standing pose (the client's client_config kEyeStanding);
// the tests send exactly what main.cpp sends.
constexpr double kEyeStanding = 1.62;

void load_spawn_area(srv::WorldSim &sim) {
    gam::StreamRequest req;
    req.generate_radius = 1;
    req.unload_radius = 1;
    req.generate_budget = 9;
    CHECK(sim.stream(req).loaded_total == 9);
}

[[nodiscard]] gam::ActorPose pose_at(double x, double y, double z) {
    return gam::ActorPose{{x, y, z}, 1.8, kEyeStanding};
}

[[nodiscard]] gam::ActionRequest request(gam::ActionKind kind, glm::ivec3 target, gam::ActorPose actor,
                                         std::uint16_t item = 0) {
    gam::ActionRequest req;
    req.kind = kind;
    req.target = target;
    req.actor = actor;
    req.item_or_block = item;
    return req;
}

// A drop the test can talk about: the single live entity.
[[nodiscard]] const srv::Entity &only_drop(const srv::WorldSim &sim) {
    const std::vector<srv::EntityId> ids = sim.entities().live_ids();
    REQUIRE(ids.size() == 1);
    const srv::Entity *entity = sim.entities().find(ids.front());
    REQUIRE(entity != nullptr);
    return *entity;
}

} // namespace

TEST_CASE("authority: a successful dig leaves the block's item behind at the broken cell") {
    srv::WorldSim sim;
    load_spawn_area(sim);

    // Dig the surface block of the column next to the origin, with the actor
    // standing on the floor that is left behind (the pose the player ends up in
    // after the block goes).
    const int surface = sim.surface_height(1, 0);
    const glm::ivec3 target{1, surface - 1, 0};
    const std::uint16_t dug = sim.block_at(target.x, target.y, target.z);
    CHECK(dug != 0);

    const gam::ActionResult result =
        sim.submit(request(gam::ActionKind::Dig, target, pose_at(1.5, surface - 1.0, 0.5)));
    REQUIRE(result.accepted);
    CHECK(sim.block_at(target.x, target.y, target.z) == 0);

    // ⚖ research/11 §4.1-4.4: the drop exists, centred in the cell it came from,
    // with the natural pickup delay and the full 5 HP.
    const srv::Entity &drop = only_drop(sim);
    const std::optional<std::uint16_t> expected = sim.items().item_for_block(dug);
    REQUIRE(expected.has_value());
    CHECK(drop.stack.item == *expected);
    CHECK(drop.stack.count == 1);
    CHECK(drop.type == sim.entity_types().id_of("item"));
    CHECK(drop.position.x == doctest::Approx(1.5));
    CHECK(drop.position.z == doctest::Approx(0.5));
    CHECK(drop.position.y == doctest::Approx(static_cast<double>(target.y) + 0.375));
    CHECK(drop.pickup_delay == 10);
    CHECK(drop.health == 5);

    // A second dig leaves a second drop: the authority does not merge at spawn
    // time (its own 40-tick cadence does that).
    const int surface2 = sim.surface_height(2, 0);
    CHECK(sim.submit(request(gam::ActionKind::Dig, {2, surface2 - 1, 0}, pose_at(2.5, surface2 - 1.0, 0.5))).accepted);
    CHECK(sim.entities().alive_count() == 2);
}

TEST_CASE("authority: the drop settles on the floor and stays there, tick after tick") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const int surface = sim.surface_height(1, 0);
    const glm::ivec3 target{1, surface - 1, 0};
    REQUIRE(sim.submit(request(gam::ActionKind::Dig, target, pose_at(1.5, surface - 1.0, 0.5))).accepted);
    const srv::EntityId id = sim.entities().live_ids().front();

    for (int i = 0; i < 200; ++i) {
        sim.tick();
    }

    const srv::Entity *drop = sim.entities().find(id);
    REQUIRE(drop != nullptr);
    // The floor left under the broken cell is the block BELOW it, whose top face
    // is the broken cell's bottom: the drop falls half a block and stops dead
    // (restitution 0, T-D36 项 1).
    CHECK(drop->position.y == doctest::Approx(static_cast<double>(target.y)));
    CHECK(drop->velocity.x == doctest::Approx(0.0));
    CHECK(drop->velocity.y == doctest::Approx(0.0));
    CHECK(drop->velocity.z == doctest::Approx(0.0));
    CHECK(drop->on_ground);
    CHECK(drop->age == 200);
    CHECK(drop->pickup_delay == 0);

    // Ten more ticks change nothing: no sinking, no drift, no jitter.
    for (int i = 0; i < 10; ++i) {
        sim.tick();
    }
    CHECK(drop->position.x == doctest::Approx(1.5));
    CHECK(drop->position.y == doctest::Approx(static_cast<double>(target.y)));
    CHECK(drop->position.z == doctest::Approx(0.5));
}

TEST_CASE("authority: pickup refuses an unknown id, a wrong item, a live delay and a far actor") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const int surface = sim.surface_height(1, 0);
    const glm::ivec3 target{1, surface - 1, 0};
    REQUIRE(sim.submit(request(gam::ActionKind::Dig, target, pose_at(1.5, surface - 1.0, 0.5))).accepted);
    const srv::Entity &drop = only_drop(sim);
    const srv::EntityId id = drop.id;
    const std::uint16_t item = drop.stack.item;

    // No such entity.
    const gam::ActionResult unknown =
        sim.submit(request(gam::ActionKind::PickUp, {9999, 0, 0}, pose_at(1.5, surface - 1.0, 0.5), item));
    CHECK_FALSE(unknown.accepted);
    CHECK(unknown.reject == gam::ActionReject::UnknownEntity);
    // An entity id of 0 is "no entity", not slot 0.
    CHECK(sim.submit(request(gam::ActionKind::PickUp, {0, 0, 0}, pose_at(1.5, surface - 1.0, 0.5), item)).reject ==
          gam::ActionReject::UnknownEntity);

    // The id exists but holds something else: the client claims the wrong item.
    // This is the check that makes a reused entity slot harmless.
    const std::uint16_t other =
        sim.items().id_of("greyrock") == item ? sim.items().id_of("loam_clod") : sim.items().id_of("greyrock");
    const gam::ActionResult mismatch = sim.submit(
        request(gam::ActionKind::PickUp, {static_cast<int>(id), 0, 0}, pose_at(1.5, surface - 1.0, 0.5), other));
    CHECK_FALSE(mismatch.accepted);
    CHECK(mismatch.reject == gam::ActionReject::EntityItemMismatch);

    // ⚖ 10 ticks: the delay is still running, so nothing is handed over - even
    // though the actor is standing right on the drop.
    const gam::ActionResult too_early = sim.submit(
        request(gam::ActionKind::PickUp, {static_cast<int>(id), 0, 0}, pose_at(1.5, surface - 1.0, 0.5), item));
    CHECK_FALSE(too_early.accepted);
    CHECK(too_early.reject == gam::ActionReject::PickupDelayActive);

    // The delay over, but the actor is nowhere near it.
    for (int i = 0; i < 10; ++i) {
        sim.tick();
    }
    const gam::ActionResult too_far =
        sim.submit(request(gam::ActionKind::PickUp, {static_cast<int>(id), 0, 0}, pose_at(60.5, surface, 0.5), item));
    CHECK_FALSE(too_far.accepted);
    CHECK(too_far.reject == gam::ActionReject::OutOfPickupRange);

    // A refused request must not have consumed anything.
    CHECK(sim.entities().alive_count() == 1);
}

TEST_CASE("authority: pickup hands the whole stack over and the drop is gone afterwards") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const int surface = sim.surface_height(1, 0);
    const glm::ivec3 target{1, surface - 1, 0};
    REQUIRE(sim.submit(request(gam::ActionKind::Dig, target, pose_at(1.5, surface - 1.0, 0.5))).accepted);
    const srv::Entity &drop = only_drop(sim);
    const srv::EntityId id = drop.id;
    const std::uint16_t item = drop.stack.item;

    for (int i = 0; i < 10; ++i) {
        sim.tick(); // the natural delay elapses
    }

    // The actor stands on the floor the drop is resting on - after ten ticks the
    // drop has fallen the half block onto the cell's bottom face and stopped.
    const gam::ActionResult picked = sim.submit(
        request(gam::ActionKind::PickUp, {static_cast<int>(id), 0, 0}, pose_at(1.5, surface - 1.0, 0.5), item));
    CHECK(picked.accepted);
    CHECK(sim.entities().alive_count() == 0);

    // And it is really gone: the same id cannot hand over a second stack, even
    // if the slot were reused (the item check catches that too).
    CHECK(sim.submit(
                 request(gam::ActionKind::PickUp, {static_cast<int>(id), 0, 0}, pose_at(1.5, surface - 1.0, 0.5), item))
              .reject == gam::ActionReject::UnknownEntity);
}

TEST_CASE("authority: the item registry belongs to the world, and every launch block maps 1:1") {
    srv::WorldSim sim;
    // The drop path reads the same registry the client does (world.items()), so
    // the mapping has to be consistent with the block layer it was built from.
    for (std::uint16_t block = 0; block < sim.registry().size(); ++block) {
        const std::optional<std::uint16_t> item = sim.items().item_for_block(block);
        if (!item.has_value()) {
            // Only water and air have no item form in the launch set (T-I1
            // ruling S-3: water is carried in a vessel, never as an item).
            const std::string &id = sim.registry().string_of(block);
            CHECK((id == "water" || id == "air"));
            continue;
        }
        // Exactly the item whose block form it is, and the mapping is 1:1.
        CHECK(sim.items().def_of(*item).block == block);
        CHECK(sim.items().item_for_block(sim.items().def_of(*item).block) == item);
    }
    // The reserved sentinel is not a block.
    CHECK_FALSE(sim.items().item_for_block(gam::kNoBlock).has_value());
}

TEST_CASE("authority: an unloaded chunk freezes the drop, despawn timer included") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const int surface = sim.surface_height(1, 0);
    const glm::ivec3 target{1, surface - 1, 0};
    REQUIRE(sim.submit(request(gam::ActionKind::Dig, target, pose_at(1.5, surface - 1.0, 0.5))).accepted);
    const srv::EntityId id = sim.entities().live_ids().front();
    sim.tick();
    const int age_before = sim.entities().find(id)->age;
    CHECK(age_before == 1);

    // T-D4's release sweep, driven through its own verb: name a viewer far away
    // and keep nothing beyond its own chunk.
    gam::StreamRequest release;
    release.center_cx = 1000;
    release.center_cz = 1000;
    release.generate_radius = 0;
    release.unload_radius = 0;
    release.generate_budget = 0;
    CHECK(sim.stream(release).unloaded_chunks.size() == 9);
    CHECK(sim.loaded_chunk_count() == 0);

    for (int i = 0; i < 100; ++i) {
        sim.tick();
    }

    // ⚖ research/11 §4.4: 仅在已加载且处理实体的区块内计时，区块卸载则暂停. The
    // entity keeps its slot while frozen; entity persistence to disk is a later
    // card (this drop would be written with its chunk one day).
    const srv::Entity *drop = sim.entities().find(id);
    REQUIRE(drop != nullptr);
    CHECK(drop->age == age_before);

    // Back in memory, the counter resumes where it stopped.
    load_spawn_area(sim);
    sim.tick();
    CHECK(sim.entities().find(id)->age == age_before + 1);
}
