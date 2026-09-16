// T-A1: tests for the authoritative side (server::WorldSim).
//
// Three things are worth asserting here and they are the card's acceptance
// items 3 and 4: every illegal request is refused with a reason, an accepted
// request leaves the world in exactly the state a direct engine write would
// (byte for byte), and the push-back the client remeshes from is the chunk set
// the old in-client write produced.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "opencraft/core/byte_buffer.hpp"
#include "opencraft/game/protocol.hpp"
#include "opencraft/game/raycast.hpp"
#include "opencraft/sim/world_sim.hpp"
#include "opencraft/voxel/chunk.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"

namespace {

namespace core = opencraft::core;
namespace gam = opencraft::game;
namespace srv = opencraft::server;
namespace voxel = opencraft::voxel;
namespace worldgen = opencraft::worldgen;

// Eye offset of the standing pose (client_config's kEyeStanding); the tests
// send exactly what main.cpp sends.
constexpr double kEyeStanding = 1.62;

// Feet position standing on column (wx, wz) of the world the sim generated.
[[nodiscard]] glm::dvec3 feet_on(const srv::WorldSim &sim, int wx, int wz) {
    return {wx + 0.5, static_cast<double>(sim.surface_height(wx, wz)), wz + 0.5};
}

[[nodiscard]] gam::ActorPose standing_on(const srv::WorldSim &sim, int wx, int wz) {
    return gam::ActorPose{feet_on(sim, wx, wz), 1.8, kEyeStanding};
}

[[nodiscard]] gam::ActionRequest request(gam::ActionKind kind, glm::ivec3 target, gam::ActorPose actor,
                                         std::uint16_t block = 0) {
    gam::ActionRequest req;
    req.kind = kind;
    req.target = target;
    req.actor = actor;
    req.item_or_block = block;
    return req;
}

[[nodiscard]] std::vector<std::pair<int, int>> sorted(std::vector<std::pair<int, int>> chunks) {
    std::sort(chunks.begin(), chunks.end());
    return chunks;
}

[[nodiscard]] std::vector<std::uint8_t> bytes_of(const core::ByteBuffer &buffer) {
    return {buffer.data(), buffer.data() + buffer.size()};
}

// Loads the spawn chunk and its 4 side neighbors, so rays that leave the column
// still land somewhere loaded. T-D4 made the per-chunk loader private (stream()
// is the world-management verb), so the square is filled through a request;
// radius 1 around the origin is exactly the 3x3 these tests want. (The sim is
// not copyable - the fluid simulation points back into it - so it is filled in
// place.)
void load_spawn_area(srv::WorldSim &sim) {
    gam::StreamRequest req;
    req.generate_radius = 1;
    req.unload_radius = 1;
    req.generate_budget = 9;
    CHECK(sim.stream(req).loaded_total == 9);
}

} // namespace

TEST_CASE("authority: dig breaks the block and pushes exactly the stale chunks back") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const glm::ivec3 target{0, sim.surface_height(0, 0) - 1, 0};
    CHECK(sim.block_at(target.x, target.y, target.z) != 0);

    const gam::ActionResult result = sim.submit(request(gam::ActionKind::Dig, target, standing_on(sim, 0, 0)));
    CHECK(result.accepted);
    CHECK(result.reject == gam::ActionReject::None);
    CHECK(std::string_view(result.reason()).empty());
    CHECK(sim.block_at(target.x, target.y, target.z) == 0);

    // The changed cell sits at lx = lz = 0, so the mesh changes in its own
    // chunk and in the two neighbors the light can still reach: (-1, 0),
    // (0, -1) and the diagonal (-1, -1). The listed order is the write order;
    // the client sorts and dedups, so compare as a set.
    const gam::WorldChanges changes = sim.take_changes();
    CHECK(sorted(changes.dirty_chunks) == std::vector<std::pair<int, int>>{{-1, -1}, {-1, 0}, {0, -1}, {0, 0}});
    CHECK(sim.take_changes().empty()); // drained exactly once
}

TEST_CASE("authority: an accepted action leaves the chunk byte-identical to a direct write") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const std::uint16_t stone = sim.registry().id_of("stone");
    // Work the ground block beside the spawn column: the actor's own cell and
    // the one above its head are either solid or inside its AABB, and the two
    // cells find_spawn guarantees to be air are exactly those.
    const glm::dvec3 spawn = sim.find_spawn();
    const glm::ivec3 stand{static_cast<int>(std::floor(spawn.x)), static_cast<int>(std::floor(spawn.y)),
                           static_cast<int>(std::floor(spawn.z))};
    const gam::ActorPose actor{spawn, 1.8, kEyeStanding};
    const glm::ivec3 hole{stand.x + 1, sim.surface_height(stand.x + 1, stand.z) - 1, stand.z};
    REQUIRE(hole.y > 0);

    REQUIRE(sim.submit(request(gam::ActionKind::Dig, hole, actor)).accepted);
    REQUIRE(sim.submit(request(gam::ActionKind::PlaceBlock, hole, actor, stone)).accepted);

    // The reference path is the engine itself: regenerate the same chunk and
    // write the same block into it, with no simulation around it at all.
    const auto [cx, cz] = voxel::Chunk::chunk_coords(hole.x, hole.z);
    voxel::Chunk direct;
    worldgen::TerrainGenerator generator(srv::WorldSim::kSeed, sim.registry());
    generator.generate_chunk(cx, cz, direct);
    direct.set_block(hole.x - cx * voxel::Chunk::kSizeX, hole.y, hole.z - cz * voxel::Chunk::kSizeZ, stone);

    core::ByteBuffer expected;
    direct.serialize(expected);
    core::ByteBuffer actual;
    REQUIRE(sim.serialize_chunk(cx, cz, actual));
    CHECK(bytes_of(actual) == bytes_of(expected));
}

TEST_CASE("authority: a refused request does not touch the world") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    core::ByteBuffer before;
    const std::uint16_t stone = sim.registry().id_of("stone");
    REQUIRE(sim.serialize_chunk(0, 0, before));

    // Placing into the actor's own feet cell: the classic illegal request.
    const glm::dvec3 feet = feet_on(sim, 4, 4);
    const glm::ivec3 body{4, static_cast<int>(feet.y), 4};
    CHECK(sim.block_at(body.x, body.y, body.z) == 0);
    const gam::ActionResult result =
        sim.submit(request(gam::ActionKind::PlaceBlock, body, gam::ActorPose{feet, 1.8, kEyeStanding}, stone));
    CHECK_FALSE(result.accepted);
    CHECK(result.reject == gam::ActionReject::IntersectsActor);
    CHECK_FALSE(std::string_view(result.reason()).empty());

    core::ByteBuffer after;
    REQUIRE(sim.serialize_chunk(0, 0, after));
    CHECK(bytes_of(after) == bytes_of(before));
    CHECK(sim.take_changes().empty()); // nothing was written, so nothing is stale
}

TEST_CASE("authority: dig refuses the targets the mining rules do not allow") {
    srv::WorldSim sim;
    load_spawn_area(sim);

    SUBCASE("air") {
        const glm::ivec3 air{0, sim.surface_height(0, 0) + 1, 0};
        CHECK(sim.block_at(air.x, air.y, air.z) == 0);
        const gam::ActionResult result = sim.submit(request(gam::ActionKind::Dig, air, standing_on(sim, 0, 0)));
        CHECK_FALSE(result.accepted);
        CHECK(result.reject == gam::ActionReject::NothingToDig);
    }

    SUBCASE("bedrock") {
        // The column's floor is bedrock, i.e. hardness < 0 (the registry's
        // unbreakable marker); the actor has to stand next to it to get past
        // the reach gate, which is what digging to the bottom looks like.
        CHECK(sim.registry().def_of(sim.block_at(0, 0, 0)).hardness < 0.0f);
        const gam::ActorPose at_bedrock{glm::dvec3{0.5, 0.0, 0.5}, 1.8, kEyeStanding};
        const gam::ActionResult result = sim.submit(request(gam::ActionKind::Dig, {0, 0, 0}, at_bedrock));
        CHECK_FALSE(result.accepted);
        CHECK(result.reject == gam::ActionReject::Unbreakable);
        CHECK(sim.block_at(0, 0, 0) != 0);
    }

    SUBCASE("outside the world") {
        const gam::ActorPose actor = standing_on(sim, 0, 0);
        CHECK(sim.submit(request(gam::ActionKind::Dig, {0, -1, 0}, actor)).reject == gam::ActionReject::OutOfWorld);
        CHECK(sim.submit(request(gam::ActionKind::Dig, {0, srv::WorldSim::kWorldHeight, 0}, actor)).reject ==
              gam::ActionReject::OutOfWorld);
    }

    SUBCASE("unloaded chunk") {
        const gam::ActionRequest far = request(gam::ActionKind::Dig, {1000, 70, 1000}, standing_on(sim, 0, 0));
        CHECK(sim.submit(far).reject == gam::ActionReject::ChunkNotLoaded);
    }

    SUBCASE("beyond the reach") {
        const glm::ivec3 far{10, sim.surface_height(0, 0), 0};
        const gam::ActionResult result = sim.submit(request(gam::ActionKind::Dig, far, standing_on(sim, 0, 0)));
        CHECK_FALSE(result.accepted);
        CHECK(result.reject == gam::ActionReject::OutOfReach);
        CHECK(sim.block_at(far.x, far.y, far.z) != 0); // the solid target is untouched
    }
}

TEST_CASE("authority: place and pour refuse the cells they may not write") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const std::uint16_t stone = sim.registry().id_of("stone");

    SUBCASE("unknown block id") {
        const glm::ivec3 air{4, sim.surface_height(4, 4), 4};
        const gam::ActorPose actor = standing_on(sim, 4, 4);
        CHECK(sim.submit(request(gam::ActionKind::PlaceBlock, air, actor, 0)).reject ==
              gam::ActionReject::UnknownBlock);
        const auto bogus = static_cast<std::uint16_t>(sim.registry().size() + 5);
        CHECK(sim.submit(request(gam::ActionKind::PlaceBlock, air, actor, bogus)).reject ==
              gam::ActionReject::UnknownBlock);
    }

    SUBCASE("occupied cell") {
        const glm::ivec3 ground{4, sim.surface_height(4, 4) - 1, 4};
        const gam::ActorPose actor = standing_on(sim, 4, 4);
        CHECK(sim.submit(request(gam::ActionKind::PlaceBlock, ground, actor, stone)).reject ==
              gam::ActionReject::CellOccupied);
        // Pouring skips the actor-overlap rule (fluids are non-solid) but not
        // the replaceable rule.
        CHECK(sim.submit(request(gam::ActionKind::PourWater, ground, actor)).reject == gam::ActionReject::CellOccupied);
    }
}

TEST_CASE("authority: scoop refuses a cell without a water source") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    const glm::ivec3 air{4, sim.surface_height(4, 4), 4};
    CHECK(sim.block_at(air.x, air.y, air.z) == 0);
    const gam::ActionResult result = sim.submit(request(gam::ActionKind::ScoopWater, air, standing_on(sim, 4, 4)));
    CHECK_FALSE(result.accepted);
    CHECK(result.reject == gam::ActionReject::NotAWaterSource);
    CHECK_FALSE(std::string_view(result.reason()).empty());
}

TEST_CASE("authority: every refusal code carries a reason") {
    const gam::ActionReject all[] = {
        gam::ActionReject::OutOfWorld,   gam::ActionReject::ChunkNotLoaded,  gam::ActionReject::OutOfReach,
        gam::ActionReject::NothingToDig, gam::ActionReject::Unbreakable,     gam::ActionReject::UnknownBlock,
        gam::ActionReject::CellOccupied, gam::ActionReject::IntersectsActor, gam::ActionReject::NotAWaterSource,
    };
    for (const gam::ActionReject reject : all) {
        CHECK_FALSE(std::string_view(gam::action_reject_reason(reject)).empty());
    }
    CHECK(std::string_view(gam::action_reject_reason(gam::ActionReject::None)).empty());
}

TEST_CASE("authority: every cell the client's own ray can hit is accepted") {
    // The guard on this card's riskiest rule: the authority's reach test must
    // never refuse something the client's raycast aimed at with the same reach.
    // The eye is derived the same way on both sides (feet + eye offset).
    srv::WorldSim sim;
    load_spawn_area(sim);
    const glm::dvec3 feet = feet_on(sim, 0, 0);
    const gam::ActorPose actor{feet, 1.8, kEyeStanding};
    const glm::dvec3 eye = feet + glm::dvec3(0.0, kEyeStanding, 0.0);

    int checked = 0;
    for (double yaw = 0.0; yaw < 6.28; yaw += 0.13) {
        for (double pitch = -1.2; pitch <= 1.2; pitch += 0.2) {
            const glm::dvec3 dir{-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
            const gam::VoxelRayHit hit = gam::raycast_voxel(eye, dir, gam::kReachDistance, sim);
            if (!hit.hit) {
                continue;
            }
            const std::uint16_t id = sim.block_at(hit.block_pos.x, hit.block_pos.y, hit.block_pos.z);
            if (id == 0 || sim.registry().def_of(id).hardness < 0.0f) {
                continue; // air and bedrock are not diggable targets anyway
            }
            ++checked;
            const gam::ActionResult result = sim.submit(request(gam::ActionKind::Dig, hit.block_pos, actor));
            REQUIRE(result.accepted);
            // Undo, so the sweep keeps looking at the world the rays were
            // cast into (each hit block is put back as the same block id).
            static_cast<void>(sim.submit(request(gam::ActionKind::PlaceBlock, hit.block_pos, actor, id)));
            static_cast<void>(sim.take_changes());
        }
    }
    CHECK(checked > 100); // the sweep really did look at the world
}

TEST_CASE("authority: pour, fluid tick and scoop work through the channel") {
    srv::WorldSim sim;
    load_spawn_area(sim);
    // Stand on the scanned spawn column and work one cell into the ground
    // below it: dig the ground block, then pour into the cell it left.
    const glm::dvec3 spawn = sim.find_spawn();
    const glm::ivec3 stand{static_cast<int>(std::floor(spawn.x)), static_cast<int>(std::floor(spawn.y)),
                           static_cast<int>(std::floor(spawn.z))};
    const gam::ActorPose actor{spawn, 1.8, kEyeStanding};
    const glm::ivec3 hole{stand.x, stand.y - 1, stand.z};

    REQUIRE(sim.submit(request(gam::ActionKind::Dig, hole, actor)).accepted);
    CHECK(sim.block_at(hole.x, hole.y, hole.z) == 0);
    static_cast<void>(sim.take_changes());

    // Pour into the cell above the hole: it is air and replaceable.
    REQUIRE(sim.submit(request(gam::ActionKind::PourWater, stand, actor)).accepted);
    CHECK(sim.is_water_source(stand.x, stand.y, stand.z));
    CHECK(sim.fluid_height_at(stand.x, stand.y, stand.z) > 0.0f);
    // The fluid layer's own stale chunks are flushed by the simulation step,
    // not by the request (same order the in-client write had).
    CHECK(sim.take_changes().empty());

    // Scooping works on a source and refuses the cell once it is gone.
    REQUIRE(sim.submit(request(gam::ActionKind::ScoopWater, stand, actor)).accepted);
    CHECK_FALSE(sim.is_water_source(stand.x, stand.y, stand.z));
    CHECK(sim.block_at(stand.x, stand.y, stand.z) == 0);
    CHECK(sim.submit(request(gam::ActionKind::ScoopWater, stand, actor)).reject == gam::ActionReject::NotAWaterSource);

    // A source over an opening falls: five ticks later the cell below is wet,
    // and the simulation has pushed the chunks it changed back to the client.
    static_cast<void>(sim.take_changes());
    REQUIRE(sim.submit(request(gam::ActionKind::PourWater, stand, actor)).accepted);
    for (int i = 0; i < 6; ++i) {
        sim.tick();
    }
    CHECK(sim.fluid_height_at(hole.x, hole.y, hole.z) > 0.0f);
    CHECK_FALSE(sim.take_changes().empty());
}
