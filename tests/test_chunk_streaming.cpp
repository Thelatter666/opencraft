// T-D4: tests for the authoritative streaming cycle (server::WorldSim::stream).
//
// The card's judgeable claims are all here: a chunk that leaves the view is
// really released, a released *dirty* chunk writes its edits to the save first
// and comes back with them, the hysteresis band absorbs a player pacing over
// the edge, and the light storage goes with the blocks. The real-machine half
// of the "edits survive the round trip" claim (walk away, walk back, look at
// it) lives in docs/qa/T-D4-2026-09-16/.
//
// Since T-D4 the per-chunk loader is private and stream() is the world
// management verb, so every window below is built through a StreamRequest -
// which is the point: these tests drive the same entry point the client's
// frame loop and its autosave window drive.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "opencraft/core/byte_buffer.hpp"
#include "opencraft/game/protocol.hpp"
#include "opencraft/sim/world_sim.hpp"
#include "opencraft/storage/world_save.hpp"
#include "opencraft/voxel/chunk.hpp"

namespace {

namespace core = opencraft::core;
namespace fs = std::filesystem;
namespace gam = opencraft::game;
namespace srv = opencraft::server;

// Eye offset of the standing pose (client_config's kEyeStanding).
constexpr double kEyeStanding = 1.62;

// One streaming call: the square window of `radius` chunks around (cx, cz),
// releasing only beyond `unload_radius`, with the given generation budget.
[[nodiscard]] gam::StreamRequest window(int cx, int cz, int radius, int unload_radius, int budget) {
    gam::StreamRequest req;
    req.center_cx = cx;
    req.center_cz = cz;
    req.generate_radius = radius;
    req.unload_radius = unload_radius;
    req.generate_budget = budget;
    return req;
}

[[nodiscard]] bool listed(const std::vector<std::pair<int, int>> &chunks, int cx, int cz) {
    return std::find(chunks.begin(), chunks.end(), std::pair<int, int>{cx, cz}) != chunks.end();
}

[[nodiscard]] std::vector<std::uint8_t> bytes_of(const core::ByteBuffer &buffer) {
    return {buffer.data(), buffer.data() + buffer.size()};
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

// Feet position standing on column (wx, wz) of the world the sim generated.
[[nodiscard]] glm::dvec3 feet_on(const srv::WorldSim &sim, int wx, int wz) {
    return {wx + 0.5, static_cast<double>(sim.surface_height(wx, wz)), wz + 0.5};
}

[[nodiscard]] gam::ActorPose standing_on(const srv::WorldSim &sim, int wx, int wz) {
    return gam::ActorPose{feet_on(sim, wx, wz), 1.8, kEyeStanding};
}

fs::path temp_dir(const std::string &name) {
    const fs::path dir = fs::temp_directory_path() / ("opencraft_streaming_test_" + name);
    fs::remove_all(dir);
    return dir;
}

// Every chunk of the square window around (cx, cz).
[[nodiscard]] std::vector<std::pair<int, int>> square(int cx, int cz, int radius) {
    std::vector<std::pair<int, int>> chunks;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            chunks.emplace_back(cx + dx, cz + dz);
        }
    }
    return chunks;
}

} // namespace

TEST_CASE("streaming: the window is generated nearest-first, once, inside the budget") {
    srv::WorldSim sim;

    // Budget 1: exactly the centre chunk, and it is the one the request named.
    const gam::StreamResult first = sim.stream(window(0, 0, 1, 1, 1));
    REQUIRE(first.loaded_chunks.size() == 1);
    CHECK(first.loaded_chunks.front() == std::pair<int, int>{0, 0});
    CHECK(first.loaded_total == 1);
    CHECK(sim.chunk_ready(0, 0));

    // Budget 4: the four chunks at Chebyshev distance 1 - the square's next
    // ring, and nothing further: the budget is a hard stop, not a suggestion.
    const gam::StreamResult second = sim.stream(window(0, 0, 1, 1, 4));
    REQUIRE(second.loaded_chunks.size() == 4);
    for (const auto &[cx, cz] : second.loaded_chunks) {
        CHECK(std::max(cx < 0 ? -cx : cx, cz < 0 ? -cz : cz) == 1);
    }
    CHECK(second.loaded_total == 5);

    // A wider budget finishes the 3x3, and asking again loads nothing: a
    // resident chunk is never re-generated - that would throw its edits away.
    CHECK(sim.stream(window(0, 0, 1, 1, 9)).loaded_chunks.size() == 4);
    const gam::StreamResult again = sim.stream(window(0, 0, 1, 1, 9));
    CHECK(again.loaded_chunks.empty());
    CHECK(again.unloaded_chunks.empty());
    CHECK(again.loaded_total == 9);

    for (const auto &[cx, cz] : square(0, 0, 1)) {
        CHECK(sim.chunk_ready(cx, cz));
    }
}

TEST_CASE("streaming: a chunk that leaves the retention window is released") {
    srv::WorldSim sim;
    REQUIRE(sim.stream(window(0, 0, 1, 1, 9)).loaded_total == 9);

    // Three chunks over: the new window [2, 4]^2 is disjoint from [-1, 1]^2, so
    // the whole old square goes and exactly the new one arrives.
    const gam::StreamResult moved = sim.stream(window(3, 0, 1, 1, 9));
    CHECK(moved.unloaded_chunks.size() == 9);
    for (const auto &[cx, cz] : square(0, 0, 1)) {
        CHECK_FALSE(sim.chunk_ready(cx, cz));
        CHECK(listed(moved.unloaded_chunks, cx, cz));
    }
    for (const auto &[cx, cz] : square(3, 0, 1)) {
        CHECK(sim.chunk_ready(cx, cz));
    }

    // The bound is the point: the working set is one window, not the sum of
    // every window the player has ever stood in.
    CHECK(moved.loaded_total == 9);
    CHECK(sim.loaded_chunk_count() == 9);
}

TEST_CASE("streaming: a release drops the chunk's state, and re-entry rebuilds it") {
    // No save attached: this is the mechanics of one release - what leaves
    // memory and what a re-entry recomputes. The persistence that makes the
    // round trip lossless for a player is the next test case.
    srv::WorldSim sim;
    REQUIRE(sim.stream(window(0, 0, 1, 1, 9)).loaded_total == 9);

    const int wx = -8;
    const int wz = -8;
    const int surface = sim.surface_height(wx, wz);
    REQUIRE(sim.block_at(wx, surface - 1, wz) != 0);
    REQUIRE(sim.light().chunk_initialized(-1, -1));

    // Out of the window: the block data reads as air (the documented read
    // contract for an unloaded chunk) and the light storage is gone with it.
    REQUIRE(sim.stream(window(8, 8, 1, 1, 9)).unloaded_chunks.size() == 9);
    CHECK_FALSE(sim.chunk_ready(-1, -1));
    CHECK(sim.block_at(wx, surface - 1, wz) == 0);
    CHECK_FALSE(sim.light().chunk_initialized(-1, -1));

    // Back in: the chunk regenerates and its light is recomputed from scratch
    // (the engine dropped the storage, so init_chunk's seeds and neighbor pulls
    // are the only source - no stale copy survives a release).
    REQUIRE(sim.stream(window(0, 0, 1, 1, 9)).loaded_total == 9);
    CHECK(sim.chunk_ready(-1, -1));
    CHECK(sim.block_at(wx, surface - 1, wz) != 0);
    CHECK(sim.light().chunk_initialized(-1, -1));
    CHECK(sim.light().light_at(wx, surface, wz).sky > 0); // the open cell above the ground
}

TEST_CASE("streaming: a released chunk is written to the save first and comes back with the edits") {
    const fs::path root = temp_dir("unload_roundtrip");
    opencraft::storage::WorldSave save(root, "world");
    srv::WorldSim sim;
    sim.attach_save(&save);
    REQUIRE(sim.stream(window(0, 0, 1, 1, 9)).loaded_total == 9);

    // A dug hole and a poured water source, both inside chunk (-1, -1).
    const int wx = -8;
    const int wz = -8;
    const int surface = sim.surface_height(wx, wz);
    const glm::ivec3 hole{wx, surface - 1, wz};
    const glm::ivec3 wet{wx + 1, surface - 1, wz};
    const gam::ActorPose actor = standing_on(sim, wx, wz);
    REQUIRE(sim.submit(request(gam::ActionKind::Dig, hole, actor)).accepted);
    REQUIRE(sim.submit(request(gam::ActionKind::Dig, wet, actor)).accepted);
    REQUIRE(sim.submit(request(gam::ActionKind::PourWater, wet, actor)).accepted);
    CHECK(sim.block_at(hole.x, hole.y, hole.z) == 0); // the hole the dig left
    CHECK(sim.is_water_source(wet.x, wet.y, wet.z));
    CHECK(save.is_dirty(-1, -1));

    // Leaving releases it, and the dirty flag is gone: the release path wrote
    // the chunk out synchronously before dropping its state.
    const gam::StreamResult moved = sim.stream(window(3, 0, 1, 1, 9));
    CHECK(listed(moved.unloaded_chunks, -1, -1));
    CHECK_FALSE(sim.chunk_ready(-1, -1));
    CHECK_FALSE(save.is_dirty(-1, -1));
    CHECK(save.load_chunk(-1, -1).has_value());

    // A clean chunk of the same window was released without a write: untouched
    // chunks regenerate deterministically, so they never reach the disk.
    CHECK_FALSE(save.load_chunk(-1, 1).has_value());

    // Coming back: the chunk is loaded from disk, and both edits are still
    // there - the hole is still a hole, the poured source is still a source.
    const gam::StreamResult back = sim.stream(window(0, 0, 1, 1, 9));
    CHECK(listed(back.loaded_chunks, -1, -1));
    CHECK(sim.chunk_ready(-1, -1));
    CHECK(sim.block_at(hole.x, hole.y, hole.z) == 0);
    CHECK(sim.is_water_source(wet.x, wet.y, wet.z));
    CHECK(sim.fluid_height_at(wet.x, wet.y, wet.z) > 0.0f);

    // ... and what is in memory now is byte-identical to what the release wrote,
    // which worldgen could not have produced (it has no hole).
    core::ByteBuffer resident;
    REQUIRE(sim.serialize_chunk(-1, -1, resident));
    const std::optional<std::vector<std::uint8_t>> on_disk = save.load_chunk(-1, -1);
    REQUIRE(on_disk.has_value());
    CHECK(bytes_of(resident) == *on_disk);
}

TEST_CASE("streaming: the hysteresis band absorbs a player pacing over the edge") {
    // Two chunks of hysteresis: generation out to radius 2, release beyond 4.
    constexpr int kRadius = 2;
    constexpr int kUnload = 4;
    constexpr int kBudget = 25;

    srv::WorldSim sim;
    REQUIRE(sim.stream(window(0, 0, kRadius, kUnload, kBudget)).loaded_total == 25);

    // Pacing over the generation edge, two chunks (32 blocks) out and back.
    // Chunks ahead of the player do stream in (that is streaming), but nothing
    // is ever released, so no chunk has to be generated a second time: no
    // load/unload churn, which is exactly what the card asks for.
    //
    // The band's width is the limit on how far the pacing may roam: chunks
    // generated around c are kept while |c_now - c| <= unload_radius - radius,
    // so a spread of two chunks is free and a spread of four is not (the last
    // step below walks out of the band on purpose).
    const std::pair<int, int> path[] = {{1, 0}, {2, 0}, {1, 0}, {0, 0}, {1, 0}, {2, 0}, {1, 0}, {0, 0}};
    std::vector<std::pair<int, int>> seen = square(0, 0, kRadius); // the opening window
    int newly_generated = 0;
    for (const auto &[cx, cz] : path) {
        const gam::StreamResult step = sim.stream(window(cx, cz, kRadius, kUnload, kBudget));
        CHECK(step.unloaded_chunks.empty());
        for (const auto &chunk : step.loaded_chunks) {
            CHECK_FALSE(listed(seen, chunk.first, chunk.second)); // never generated twice
            seen.push_back(chunk);
            ++newly_generated;
        }
    }
    CHECK(newly_generated > 0); // the pacing did move the generation window

    // The counterfactual: with no hysteresis the very same pacing churns. One
    // chunk over releases the trailing column, and stepping back regenerates
    // it - the load/release cycle the band exists to prevent.
    srv::WorldSim tight;
    REQUIRE(tight.stream(window(0, 0, kRadius, kRadius, kBudget)).loaded_total == 25);
    const gam::StreamResult out = tight.stream(window(1, 0, kRadius, kRadius, kBudget));
    REQUIRE_FALSE(out.unloaded_chunks.empty());
    const std::pair<int, int> trailing = out.unloaded_chunks.front();
    const gam::StreamResult home = tight.stream(window(0, 0, kRadius, kRadius, kBudget));
    CHECK(listed(home.loaded_chunks, trailing.first, trailing.second));

    // Leaving for good still releases, and the working set stays bounded by the
    // retention square rather than by everywhere the player has been.
    const gam::StreamResult away = sim.stream(window(7, 0, kRadius, kUnload, kBudget));
    CHECK(listed(away.unloaded_chunks, 0, 0));
    CHECK_FALSE(sim.chunk_ready(0, 0));
    CHECK(away.loaded_total == sim.loaded_chunk_count());
    CHECK(away.loaded_total <= static_cast<std::size_t>((2 * kUnload + 1) * (2 * kUnload + 1)));
}

TEST_CASE("streaming: the persistence window is part of the request") {
    const fs::path root = temp_dir("persist_window");
    opencraft::storage::WorldSave save(root, "world");
    srv::WorldSim sim;
    sim.attach_save(&save);
    REQUIRE(sim.stream(window(0, 0, 1, 1, 9)).loaded_total == 9);

    // Edit, then ask for the persist window with no generation budget: the
    // chunk stays resident (nothing is leaving the window) and is written
    // anyway - this is the path the tick's autosave cadence now takes.
    const int wx = 2;
    const int wz = 2;
    const glm::ivec3 target{wx, sim.surface_height(wx, wz) - 1, wz};
    REQUIRE(sim.submit(request(gam::ActionKind::Dig, target, standing_on(sim, wx, wz))).accepted);
    CHECK(save.is_dirty(0, 0));

    gam::StreamRequest autosave = window(0, 0, 1, 1, 0);
    autosave.persist = true;
    const gam::StreamResult result = sim.stream(autosave);
    CHECK(result.persisted_chunks == 1);
    CHECK(result.loaded_chunks.empty()); // budget 0: this call generated nothing
    CHECK(result.unloaded_chunks.empty());
    CHECK(sim.chunk_ready(0, 0)); // still resident, edit still in memory
    CHECK(sim.block_at(target.x, target.y, target.z) == 0);
    CHECK_FALSE(save.is_dirty(0, 0));

    // No save attached: the same request is a no-op rather than an error.
    srv::WorldSim memory_only;
    REQUIRE(memory_only.stream(window(0, 0, 1, 1, 9)).loaded_total == 9);
    gam::StreamRequest no_save = window(0, 0, 1, 1, 0);
    no_save.persist = true;
    CHECK(memory_only.stream(no_save).persisted_chunks == 0);
}

TEST_CASE("streaming: an untouched chunk released and regenerated is byte-identical") {
    // The premise behind "clean chunks never reach the disk": regeneration is
    // deterministic, so dropping an unedited chunk loses nothing.
    const fs::path root = temp_dir("deterministic");
    opencraft::storage::WorldSave save(root, "world");
    srv::WorldSim sim;
    sim.attach_save(&save);
    REQUIRE(sim.stream(window(0, 0, 1, 1, 9)).loaded_total == 9);

    core::ByteBuffer before;
    REQUIRE(sim.serialize_chunk(1, 1, before));
    REQUIRE(sim.stream(window(8, 8, 1, 1, 9)).unloaded_chunks.size() == 9);
    REQUIRE(sim.stream(window(0, 0, 1, 1, 9)).loaded_total == 9);
    core::ByteBuffer after;
    REQUIRE(sim.serialize_chunk(1, 1, after));
    CHECK(bytes_of(after) == bytes_of(before));
    CHECK_FALSE(save.load_chunk(1, 1).has_value()); // regenerated, not reloaded
}
