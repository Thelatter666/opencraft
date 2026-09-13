#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <tuple>
#include <vector>

#include <doctest/doctest.h>

#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk_manager.hpp"
#include "opencraft/voxel/light_engine.hpp"
#include "opencraft/voxel/light_world_adapter.hpp"

using opencraft::voxel::BlockLightProps;
using opencraft::voxel::BlockRegistry;
using opencraft::voxel::Chunk;
using opencraft::voxel::ChunkLightWorld;
using opencraft::voxel::ChunkManager;
using opencraft::voxel::ILightWorld;
using opencraft::voxel::LightEngine;
using opencraft::voxel::LightLevels;

namespace {

constexpr std::uint16_t kAir = 0;
constexpr std::uint16_t kTorch = 1; // transparent, emission 14 (virtual light table)
constexpr std::uint16_t kStone = 2; // opaque, no emission
constexpr std::uint16_t kLamp = 3;  // opaque, emission 12

constexpr int kWorldMaxY = 384;

// Stand-in world for engine tests: a sparse id map over a bounded rectangle.
// Positions outside the rectangle act as opaque world border so light never
// escapes into chunks that no test initializes.
class FakeWorld final : public ILightWorld {
public:
    FakeWorld(int x_min, int x_max, int z_min, int z_max, std::uint16_t fill = kAir)
        : x_min_(x_min), x_max_(x_max), z_min_(z_min), z_max_(z_max), fill_(fill) {}

    void set(int x, int y, int z, std::uint16_t id) { ids_[pack(x, y, z)] = id; }

    [[nodiscard]] std::uint16_t get(int x, int y, int z) const {
        const auto it = ids_.find(pack(x, y, z));
        return it == ids_.end() ? fill_ : it->second;
    }

    void fill_ground(int y = 0) {
        for (int z = z_min_; z < z_max_; ++z) {
            for (int x = x_min_; x < x_max_; ++x) {
                set(x, y, z, kStone);
            }
        }
    }

    [[nodiscard]] BlockLightProps props_at(int wx, int wy, int wz) const override {
        if (wx < x_min_ || wx >= x_max_ || wz < z_min_ || wz >= z_max_ || wy < 0 || wy >= kWorldMaxY) {
            return {false, 0};
        }
        return props_of(get(wx, wy, wz));
    }

    [[nodiscard]] BlockLightProps props_of(std::uint16_t id) const override {
        switch (id) {
        case kTorch:
            return {true, 14};
        case kStone:
            return {false, 0};
        case kLamp:
            return {false, 12};
        default:
            return {true, 0}; // air and anything unknown
        }
    }

private:
    static std::uint64_t pack(int x, int y, int z) {
        return (static_cast<std::uint64_t>(x + 4096) << 24) | (static_cast<std::uint64_t>(y) << 13) |
               static_cast<std::uint64_t>(z + 4096);
    }

    int x_min_;
    int x_max_;
    int z_min_;
    int z_max_;
    std::uint16_t fill_;
    std::unordered_map<std::uint64_t, std::uint16_t> ids_;
};

// World edit helper: the world must already reflect the change when
// on_block_changed runs (documented contract).
void edit(LightEngine &engine, FakeWorld &world, int x, int y, int z, std::uint16_t new_id) {
    const std::uint16_t old_id = world.get(x, y, z);
    world.set(x, y, z, new_id);
    engine.on_block_changed(x, y, z, old_id, new_id);
}

void init_chunks(LightEngine &engine, int cx0, int cx1, int cz0, int cz1) {
    for (int cx = cx0; cx <= cx1; ++cx) {
        for (int cz = cz0; cz <= cz1; ++cz) {
            engine.init_chunk(cx, cz);
        }
    }
}

std::uint8_t sky_at(const LightEngine &engine, int x, int y, int z) {
    return engine.light_at(x, y, z).sky;
}

std::uint8_t block_at(const LightEngine &engine, int x, int y, int z) {
    return engine.light_at(x, y, z).block;
}

struct PressureWorld {
    FakeWorld world{0, 100, 0, 100, kAir};
    std::vector<std::tuple<int, int, int>> torches;
    int pillar_x = 0;
    int pillar_z = 0;
    int pillar_top = 0;
    int plate_x = 0;
    int plate_z = 0;

    void build() {
        world.fill_ground();
        std::mt19937 rng(42U);
        auto pick = [&](int lo, int hi) { return static_cast<int>(rng() % static_cast<unsigned>(hi - lo + 1)) + lo; };
        // Keep structures apart (and away from the protected sun columns) so
        // the sampled expectations below stay exact.
        std::vector<std::pair<int, int>> occupied{{50, 50}, {20, 30}};
        auto free_spot = [&](int x, int z) {
            for (const auto &spot : occupied) {
                if (std::abs(x - spot.first) <= 5 && std::abs(z - spot.second) <= 5) {
                    return false;
                }
            }
            return true;
        };
        // Solid pillars: 1x1 columns of stone.
        for (int i = 0; i < 30; ++i) {
            const int x = pick(3, 96);
            const int z = pick(3, 96);
            if (!free_spot(x, z)) {
                continue;
            }
            occupied.emplace_back(x, z);
            const int h = pick(3, 8);
            for (int y = 1; y <= h; ++y) {
                world.set(x, y, z, kStone);
            }
            if (pillar_top == 0) {
                pillar_x = x;
                pillar_z = z;
                pillar_top = h;
            }
        }
        // Floating 3x3 slabs at y=6 casting skylight shadow.
        for (int i = 0; i < 8; ++i) {
            const int x = pick(5, 90);
            const int z = pick(5, 90);
            if (!free_spot(x, z)) {
                continue;
            }
            occupied.emplace_back(x, z);
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    world.set(x + dx, 6, z + dz, kStone);
                }
            }
            if (plate_x == 0) {
                plate_x = x;
                plate_z = z;
            }
        }
        // Torches on open ground, kept 6 apart so their 5x5 sampling pockets
        // never overwrite each other.
        for (int guard = 0; guard < 10000 && torches.size() < 12; ++guard) {
            const int x = pick(5, 94);
            const int z = pick(5, 94);
            if (!free_spot(x, z)) {
                continue;
            }
            occupied.emplace_back(x, z);
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dz = -2; dz <= 2; ++dz) {
                    world.set(x + dx, 1, z + dz, kAir);
                    world.set(x + dx, 2, z + dz, kAir);
                }
            }
            world.set(x, 1, z, kTorch);
            torches.emplace_back(x, 1, z);
        }
        // Guaranteed clear sun columns away from the random structures.
        for (int y = 1; y < 64; ++y) {
            world.set(50, y, 50, kAir);
            world.set(20, y, 30, kAir);
        }
    }
};

} // namespace

TEST_CASE("flat ground has full skylight above and zero inside solids") {
    FakeWorld world(-16, 32, -16, 32);
    world.fill_ground();
    LightEngine engine(world);
    init_chunks(engine, -1, 1, -1, 1);

    CHECK(sky_at(engine, 5, 1, 5) == 15);
    CHECK(sky_at(engine, 5, 100, 5) == 15);
    CHECK(sky_at(engine, 5, 383, 5) == 15);
    CHECK(sky_at(engine, 5, 0, 5) == 0);
    CHECK(block_at(engine, 5, 1, 5) == 0);
    CHECK(engine.pending_count() == 0);
}

TEST_CASE("skylight gradient under an overhang") {
    FakeWorld world(-16, 32, -16, 32);
    world.fill_ground();
    // 12x12 plate at y=3.
    for (int x = 2; x <= 13; ++x) {
        for (int z = 2; z <= 13; ++z) {
            world.set(x, 3, z, kStone);
        }
    }
    LightEngine engine(world);
    init_chunks(engine, -1, 1, -1, 1);

    // Manhattan distance to the nearest open column drives the gradient.
    CHECK(sky_at(engine, 8, 1, 8) == 9);
    CHECK(sky_at(engine, 8, 2, 8) == 9);
    CHECK(sky_at(engine, 7, 1, 7) == 9);
    CHECK(sky_at(engine, 2, 2, 2) == 14);
    CHECK(sky_at(engine, 13, 1, 13) == 14);
    CHECK(sky_at(engine, 2, 1, 8) == 14);
    CHECK(sky_at(engine, 5, 4, 5) == 15); // above the plate
    CHECK(sky_at(engine, 5, 3, 5) == 0);  // the plate itself
    CHECK(sky_at(engine, 5, 0, 5) == 0);  // ground
    CHECK(engine.pending_count() == 0);
}

TEST_CASE("torch light falls off radially in an enclosed cave and is blocked by walls") {
    FakeWorld world(-16, 48, -16, 48, kStone);
    for (int x = 10; x <= 20; ++x) {
        for (int z = 10; z <= 20; ++z) {
            for (int y = 1; y <= 4; ++y) {
                world.set(x, y, z, kAir);
            }
        }
    }
    world.set(15, 2, 15, kTorch);
    LightEngine engine(world);
    init_chunks(engine, 0, 1, 0, 1);

    CHECK(block_at(engine, 15, 2, 15) == 14);
    CHECK(block_at(engine, 14, 2, 15) == 13);
    CHECK(block_at(engine, 16, 2, 15) == 13); // crosses the chunk border
    CHECK(block_at(engine, 15, 3, 15) == 13);
    CHECK(block_at(engine, 15, 1, 15) == 13);
    CHECK(block_at(engine, 13, 2, 15) == 12);
    CHECK(block_at(engine, 14, 2, 14) == 12);
    CHECK(block_at(engine, 15, 4, 15) == 12);
    CHECK(block_at(engine, 10, 2, 15) == 9);
    CHECK(block_at(engine, 10, 1, 10) == 3);
    CHECK(block_at(engine, 20, 4, 20) == 2);
    CHECK(block_at(engine, 9, 2, 15) == 0);  // wall
    CHECK(block_at(engine, 15, 5, 15) == 0); // ceiling
    CHECK(sky_at(engine, 15, 2, 15) == 0);   // underground: no skylight
    CHECK(engine.pending_count() == 0);

    SUBCASE("removing the emitter darkens the whole cave") {
        edit(engine, world, 15, 2, 15, kAir);
        CHECK(block_at(engine, 15, 2, 15) == 0);
        CHECK(block_at(engine, 14, 2, 15) == 0);
        CHECK(block_at(engine, 16, 2, 15) == 0);
        CHECK(block_at(engine, 10, 1, 10) == 0);

        SUBCASE("a new emitter lights the cave from its own position") {
            edit(engine, world, 12, 2, 12, kTorch);
            CHECK(block_at(engine, 12, 2, 12) == 14);
            CHECK(block_at(engine, 15, 2, 15) == 8); // manhattan distance 6 from the new torch
            CHECK(block_at(engine, 10, 2, 10) == 10);
        }
    }
}

TEST_CASE("placing a block cuts a tunnel light path and removal re-infills it") {
    FakeWorld world(-16, 48, -16, 48, kStone);
    for (int x = 5; x <= 25; ++x) {
        world.set(x, 2, 15, kAir);
    }
    world.set(10, 2, 15, kTorch);
    LightEngine engine(world);
    init_chunks(engine, 0, 1, 0, 0);

    CHECK(block_at(engine, 15, 2, 15) == 9);
    CHECK(block_at(engine, 16, 2, 15) == 8);
    CHECK(block_at(engine, 20, 2, 15) == 4);
    CHECK(block_at(engine, 23, 2, 15) == 1);
    CHECK(block_at(engine, 24, 2, 15) == 0);

    SUBCASE("opaque block darkens everything downstream") {
        edit(engine, world, 15, 2, 15, kStone);
        CHECK(block_at(engine, 15, 2, 15) == 0);
        CHECK(block_at(engine, 14, 2, 15) == 10);
        CHECK(block_at(engine, 16, 2, 15) == 0);
        CHECK(block_at(engine, 20, 2, 15) == 0);

        SUBCASE("removing the block lets the light flow back in") {
            edit(engine, world, 15, 2, 15, kAir);
            CHECK(block_at(engine, 15, 2, 15) == 9);
            CHECK(block_at(engine, 16, 2, 15) == 8);
            CHECK(block_at(engine, 20, 2, 15) == 4);
            CHECK(block_at(engine, 24, 2, 15) == 0);
        }
    }
}

TEST_CASE("floating block casts a skylight shadow and removal restores the sun column") {
    FakeWorld world(-16, 32, -16, 32);
    world.fill_ground();
    LightEngine engine(world);
    init_chunks(engine, -1, 1, -1, 1);

    edit(engine, world, 8, 5, 8, kStone);
    CHECK(sky_at(engine, 8, 5, 8) == 0);  // the new block
    CHECK(sky_at(engine, 8, 4, 8) == 14); // shadow column, lit from the sides
    CHECK(sky_at(engine, 8, 2, 8) == 14);
    CHECK(sky_at(engine, 8, 1, 8) == 14);
    CHECK(sky_at(engine, 8, 6, 8) == 15); // above the block stays full sun
    CHECK(sky_at(engine, 9, 5, 8) == 15);
    CHECK(sky_at(engine, 7, 5, 8) == 15);

    edit(engine, world, 8, 5, 8, kAir);
    CHECK(sky_at(engine, 8, 4, 8) == 15);
    CHECK(sky_at(engine, 8, 1, 8) == 15);
    CHECK(engine.pending_count() == 0);
}

TEST_CASE("cross-chunk torch light converges regardless of init order") {
    // Order A: source chunk first.
    {
        FakeWorld world(0, 32, 0, 16);
        world.fill_ground();
        world.set(15, 1, 8, kTorch);
        LightEngine engine(world);
        engine.init_chunk(0, 0);
        CHECK(block_at(engine, 15, 1, 8) == 14);
        CHECK(sky_at(engine, 16, 1, 8) == 0); // neighbor has no light data yet
        CHECK(engine.pending_count() > 0);

        engine.init_chunk(1, 0);
        CHECK(sky_at(engine, 16, 1, 8) == 15);
        CHECK(block_at(engine, 16, 1, 8) == 13);
        CHECK(block_at(engine, 20, 1, 8) == 9);
        CHECK(block_at(engine, 28, 1, 8) == 1);
        CHECK(block_at(engine, 30, 1, 8) == 0);
        CHECK(engine.pending_count() == 0);
    }
    // Order B: receiving chunk first.
    {
        FakeWorld world(0, 32, 0, 16);
        world.fill_ground();
        world.set(15, 1, 8, kTorch);
        LightEngine engine(world);
        engine.init_chunk(1, 0);
        CHECK(sky_at(engine, 16, 1, 8) == 15);
        CHECK(block_at(engine, 16, 1, 8) == 0);
        CHECK(engine.pending_count() > 0);

        engine.init_chunk(0, 0);
        CHECK(sky_at(engine, 16, 1, 8) == 15);
        CHECK(block_at(engine, 16, 1, 8) == 13);
        CHECK(block_at(engine, 20, 1, 8) == 9);
        CHECK(block_at(engine, 15, 1, 8) == 14);
        CHECK(engine.pending_count() == 0);
    }
}

TEST_CASE("pressure smoke: many structures and edits converge with an empty deferred queue") {
    PressureWorld p;
    p.build();
    LightEngine engine(p.world);
    init_chunks(engine, 0, 6, 0, 6);
    CHECK(engine.pending_count() == 0);

    for (const auto &torch : p.torches) {
        const auto [tx, ty, tz] = torch;
        CHECK(block_at(engine, tx, ty, tz) == 14);
        CHECK(block_at(engine, tx + 1, ty, tz) == 13);
        CHECK(block_at(engine, tx - 1, ty, tz) == 13);
        CHECK(block_at(engine, tx, ty, tz + 1) == 13);
        CHECK(block_at(engine, tx, ty, tz - 1) == 13);
        CHECK(block_at(engine, tx + 2, ty, tz) == 12);
    }
    CHECK(sky_at(engine, 50, 1, 50) == 15);
    CHECK(sky_at(engine, 50, 63, 50) == 15);
    CHECK(sky_at(engine, 20, 1, 30) == 15);
    CHECK(sky_at(engine, p.pillar_x, p.pillar_top, p.pillar_z) == 0);
    CHECK(sky_at(engine, p.pillar_x, p.pillar_top + 1, p.pillar_z) == 15);
    // Under the first slab center: two steps from the open columns around it.
    CHECK(sky_at(engine, p.plate_x, 5, p.plate_z) == 13);
    CHECK(sky_at(engine, p.plate_x, 1, p.plate_z) == 13);

    // Random edit storm: place and remove blocks and torches.
    std::mt19937 rng(7U);
    auto pick = [&](int lo, int hi) { return static_cast<int>(rng() % static_cast<unsigned>(hi - lo + 1)) + lo; };
    for (int i = 0; i < 60; ++i) {
        const int x = pick(10, 89);
        const int y = pick(1, 15);
        const int z = pick(10, 89);
        if (p.world.get(x, y, z) != kAir) {
            continue;
        }
        edit(engine, p.world, x, y, z, kStone);
        edit(engine, p.world, x, y, z, kAir);
    }
    for (int i = 0; i < 10; ++i) {
        const int x = pick(10, 89);
        const int z = pick(10, 89);
        if (p.world.get(x, 1, z) != kAir) {
            continue;
        }
        edit(engine, p.world, x, 1, z, kTorch);
        CHECK(block_at(engine, x, 1, z) == 14);
        edit(engine, p.world, x, 1, z, kAir);
        CHECK(block_at(engine, x, 1, z) <= 13); // own emission gone, neighbors may still light it
    }
    CHECK(engine.pending_count() == 0);
    // Existing torches are unaffected by the edit storm.
    for (const auto &torch : p.torches) {
        const auto [tx, ty, tz] = torch;
        CHECK(block_at(engine, tx, ty, tz) == 14);
    }
}

TEST_CASE("single block update performance sampling") {
    PressureWorld p;
    p.build();
    LightEngine engine(p.world);

    using clock = std::chrono::steady_clock;
    const auto init_start = clock::now();
    init_chunks(engine, 0, 6, 0, 6);
    const auto init_us = std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - init_start).count();

    std::mt19937 rng(11U);
    auto pick = [&](int lo, int hi) { return static_cast<int>(rng() % static_cast<unsigned>(hi - lo + 1)) + lo; };
    double total_us = 0.0;
    double worst_us = 0.0;
    int samples = 0;
    for (int i = 0; i < 120; ++i) {
        const int x = pick(15, 85);
        const int z = pick(15, 85);
        const int y = pick(1, 10);
        const std::uint16_t new_id = (i % 2 == 0) ? kTorch : kStone;
        if (p.world.get(x, y, z) != kAir) {
            continue;
        }
        const auto t0 = clock::now();
        edit(engine, p.world, x, y, z, new_id);
        edit(engine, p.world, x, y, z, kAir);
        const double us =
            static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - t0).count());
        total_us += us;
        worst_us = std::max(worst_us, us);
        ++samples;
    }
    std::printf("[perf] init 7x7 chunks total=%lld us (avg=%.3f ms/chunk)\n", static_cast<long long>(init_us),
                static_cast<double>(init_us) / 1000.0 / 49.0);
    std::printf("[perf] block edit pair (place+remove) avg=%.1f us worst=%.1f us over %d samples\n",
                samples > 0 ? total_us / samples : 0.0, worst_us, samples);
    CHECK(engine.pending_count() == 0);
    CHECK(samples > 0);
}

TEST_CASE("ChunkLightWorld adapter wires ChunkManager and registry") {
    BlockRegistry registry = BlockRegistry::create_default();
    registry.register_block("test_torch", {"Test Torch", true, true, 0.0f});
    const std::uint16_t torch_id = registry.id_of("test_torch");
    const std::uint16_t stone_id = registry.id_of("stone");

    ChunkManager chunks;
    Chunk &chunk = chunks.get_or_load(0, 0);
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            chunk.set_block(x, 0, z, stone_id);
        }
    }
    chunk.set_block(8, 1, 8, torch_id);

    ChunkLightWorld world(chunks, registry, [&](std::uint16_t id) { return id == torch_id ? 14 : 0; });
    LightEngine engine(world);
    engine.init_chunk(0, 0);

    CHECK(block_at(engine, 8, 1, 8) == 14);
    CHECK(block_at(engine, 7, 1, 8) == 13);
    CHECK(sky_at(engine, 8, 1, 8) == 15);
    CHECK(sky_at(engine, 5, 1, 5) == 15);
    CHECK(sky_at(engine, 5, 0, 5) == 0);

    // Block edit through the adapter: the stone shadows cells behind it but
    // light reroutes over the top.
    chunk.set_block(7, 1, 8, stone_id);
    engine.on_block_changed(7, 1, 8, registry.air(), stone_id);
    CHECK(block_at(engine, 7, 1, 8) == 0);
    CHECK(block_at(engine, 7, 2, 8) == 12);
    CHECK(block_at(engine, 6, 1, 8) == 10);
}
