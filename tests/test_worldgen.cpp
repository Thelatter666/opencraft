#include <doctest/doctest.h>

#include "opencraft/noise/noise_sampler.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using opencraft::voxel::BlockRegistry;
using opencraft::voxel::Chunk;
using opencraft::worldgen::TerrainGenerator;

// Frozen golden seed (spelled "OPENCRAF" in ASCII for recognizability).
constexpr std::uint64_t kGoldenSeed = 0x4F50454E43524146ULL;

// Golden chunk set: spawn plains (0,0), coastline crossing sea level (0,3),
// cave-dense inland (17,-9), open ocean (-10,-10).
constexpr std::array<std::pair<std::int64_t, std::int64_t>, 4> kGoldenChunks{
    std::pair<std::int64_t, std::int64_t>{0, 0},
    {0, 3},
    {17, -9},
    {-10, -10},
};

// FNV-1a 64 over the 98304 block ids in canonical order (y major, then z,
// then x — the chunk's YZX section layout extended to the whole chunk), two
// little-endian bytes per id. Only block ids are hashed, never floats.
constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;

std::uint64_t hash_chunk(const Chunk &chunk) {
    std::uint64_t hash = kFnvOffset;
    for (int ly = 0; ly < Chunk::kSizeY; ++ly) {
        for (int z = 0; z < Chunk::kSizeZ; ++z) {
            for (int x = 0; x < Chunk::kSizeX; ++x) {
                const std::uint16_t id = chunk.get_block(x, ly, z);
                for (int b = 0; b < 2; ++b) {
                    hash = (hash ^ static_cast<std::uint8_t>(id >> (8 * b))) * kFnvPrime;
                }
            }
        }
    }
    return hash;
}

struct Fixture {
    BlockRegistry registry = BlockRegistry::create_default();
    TerrainGenerator generator{kGoldenSeed, registry};

    std::uint16_t water = registry.id_of("water");
    std::uint16_t grass = registry.id_of("grass_block");
    std::uint16_t stone = registry.id_of("stone");
    std::uint16_t bedrock = registry.id_of("bedrock");
    std::uint16_t sand = registry.id_of("sand");
    std::uint16_t gravel = registry.id_of("gravel");

    // True when any cell of the column holds water.
    bool column_has_water(const Chunk &chunk, int x, int z) const {
        for (int ly = 0; ly < Chunk::kSizeY; ++ly) {
            if (chunk.get_block(x, ly, z) == water) {
                return true;
            }
        }
        return false;
    }

    // Topmost non-air, non-water cell of a column; -1 when none exists.
    int surface_local_y(const Chunk &chunk, int x, int z) const {
        for (int ly = Chunk::kSizeY - 1; ly >= 0; --ly) {
            const std::uint16_t id = chunk.get_block(x, ly, z);
            if (id != 0 && id != water) {
                return ly;
            }
        }
        return -1;
    }
};

} // namespace

TEST_CASE("worldgen golden file matches committed chunk hashes") {
    Fixture fx;
    const std::filesystem::path golden_dir = OPENCRAFT_GOLDEN_DIR;
    std::ifstream in(golden_dir / "worldgen_seed0x4F50454E43524146.txt");
    REQUIRE_MESSAGE(in.is_open(), "golden worldgen file is missing");

    std::size_t matched = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::int64_t cx = 0;
        std::int64_t cz = 0;
        unsigned long long expected = 0;
        REQUIRE_EQ(std::sscanf(line.c_str(), "%lld %lld %llx", &cx, &cz, &expected), 3);
        Chunk chunk;
        fx.generator.generate_chunk(cx, cz, chunk);
        const auto actual = static_cast<unsigned long long>(hash_chunk(chunk));
        CHECK_MESSAGE(actual == expected, "golden hash mismatch for chunk (", cx, ",", cz, ")");
        ++matched;
    }
    CHECK(matched == kGoldenChunks.size());
}

TEST_CASE("worldgen same seed regenerates byte-identical chunks") {
    Fixture fx;
    Chunk first;
    Chunk second;
    for (const auto &[cx, cz] : kGoldenChunks) {
        fx.generator.generate_chunk(cx, cz, first);
        fx.generator.generate_chunk(cx, cz, second);
        for (int ly = 0; ly < Chunk::kSizeY; ++ly) {
            for (int z = 0; z < Chunk::kSizeZ; ++z) {
                for (int x = 0; x < Chunk::kSizeX; ++x) {
                    if (first.get_block(x, ly, z) != second.get_block(x, ly, z)) {
                        FAIL("chunk (", cx, ",", cz, ") differs at ", x, ",", ly, ",", z);
                    }
                }
            }
        }
    }
    // A fresh generator instance with the same seed must agree too.
    TerrainGenerator other(kGoldenSeed, fx.registry);
    for (const auto &[cx, cz] : kGoldenChunks) {
        fx.generator.generate_chunk(cx, cz, first);
        other.generate_chunk(cx, cz, second);
        CHECK(hash_chunk(first) == hash_chunk(second));
    }
}

TEST_CASE("worldgen different seeds produce different chunks") {
    Fixture fx;
    TerrainGenerator other(kGoldenSeed + 1, fx.registry);
    Chunk from_golden;
    Chunk from_other;
    for (const auto &[cx, cz] : kGoldenChunks) {
        fx.generator.generate_chunk(cx, cz, from_golden);
        other.generate_chunk(cx, cz, from_other);
        CHECK_NE(hash_chunk(from_golden), hash_chunk(from_other));
    }
}

TEST_CASE("worldgen spawn chunk has grass under air and a sealed floor") {
    Fixture fx;
    Chunk chunk;
    fx.generator.generate_chunk(0, 0, chunk);

    bool saw_grass = false;
    for (int z = 0; z < Chunk::kSizeZ; ++z) {
        for (int x = 0; x < Chunk::kSizeX; ++x) {
            const int top = fx.surface_local_y(chunk, x, z);
            REQUIRE(top >= 0);
            // Surface block is grass; everything above it is air.
            CHECK_EQ(chunk.get_block(x, top, z), fx.grass);
            saw_grass = saw_grass || chunk.get_block(x, top, z) == fx.grass;
            for (int ly = top + 1; ly < Chunk::kSizeY; ++ly) {
                if (chunk.get_block(x, ly, z) != 0) {
                    FAIL("non-air above surface at ", x, ",", ly, ",", z);
                }
            }
            // No water anywhere in the spawn chunk, so no flooded surface.
            CHECK_FALSE(fx.column_has_water(chunk, x, z));
        }
    }
    CHECK(saw_grass);

    // Y=-64 is solid bedrock, Y=-58 carries none.
    for (int z = 0; z < Chunk::kSizeZ; ++z) {
        for (int x = 0; x < Chunk::kSizeX; ++x) {
            CHECK_EQ(chunk.get_block(x, 0, z), fx.bedrock);
            for (int ly = 6; ly < Chunk::kSizeY; ++ly) {
                if (chunk.get_block(x, ly, z) == fx.bedrock) {
                    FAIL("bedrock above Y=-58 at ", x, ",", ly, ",", z);
                }
            }
        }
    }
}

TEST_CASE("worldgen water columns never surface as grass") {
    Fixture fx;
    Chunk chunk;
    fx.generator.generate_chunk(0, 3, chunk); // coastline chunk

    bool saw_water = false;
    bool saw_land = false;
    for (int z = 0; z < Chunk::kSizeZ; ++z) {
        for (int x = 0; x < Chunk::kSizeX; ++x) {
            if (!fx.column_has_water(chunk, x, z)) {
                saw_land = true;
                continue;
            }
            saw_water = true;
            const int top = fx.surface_local_y(chunk, x, z);
            REQUIRE(top >= 0);
            const std::uint16_t surface = chunk.get_block(x, top, z);
            CHECK_NE(surface, fx.grass);
            CHECK((surface == fx.sand || surface == fx.gravel));
        }
    }
    CHECK(saw_water);
    CHECK(saw_land); // genuinely crosses the sea level
}

TEST_CASE("worldgen leaves no floating water") {
    Fixture fx;
    for (const auto &[cx, cz] : kGoldenChunks) {
        Chunk chunk;
        fx.generator.generate_chunk(cx, cz, chunk);
        for (int ly = 1; ly < Chunk::kSizeY; ++ly) {
            for (int z = 0; z < Chunk::kSizeZ; ++z) {
                for (int x = 0; x < Chunk::kSizeX; ++x) {
                    if (chunk.get_block(x, ly, z) == fx.water && chunk.get_block(x, ly - 1, z) == 0) {
                        FAIL("floating water at ", x, ",", ly, ",", z, " in (", cx, ",", cz, ")");
                    }
                }
            }
        }
    }
}

TEST_CASE("worldgen carves caves into deep terrain") {
    Fixture fx;
    Chunk chunk;
    fx.generator.generate_chunk(17, -9, chunk);
    // Air below Y=-5 can only come from the cave carvers (surface pockets are
    // sealed and the surface is far above).
    std::size_t cave_air = 0;
    constexpr int kDeepLocalY = -5 - TerrainGenerator::kMinWorldY;
    for (int ly = 0; ly < kDeepLocalY; ++ly) {
        for (int z = 0; z < Chunk::kSizeZ; ++z) {
            for (int x = 0; x < Chunk::kSizeX; ++x) {
                if (chunk.get_block(x, ly, z) == 0) {
                    ++cave_air;
                }
            }
        }
    }
    CHECK_MESSAGE(cave_air > 500, "cave air cells below Y=-5: ", cave_air);
}

TEST_CASE("worldgen generator is thread safe") {
    Fixture fx;
    constexpr std::int64_t kCx = 3;
    constexpr std::int64_t kCz = 5;
    Chunk reference;
    fx.generator.generate_chunk(kCx, kCz, reference);
    const auto reference_hash = hash_chunk(reference);

    std::vector<bool> ok(4, true);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < ok.size(); ++i) {
        threads.emplace_back([&, i] {
            Chunk chunk;
            // Two regenerations of the same chunk from a shared generator plus
            // an unrelated chunk in between — all interleaved across threads.
            fx.generator.generate_chunk(kCx, kCz, chunk);
            ok[i] = ok[i] && hash_chunk(chunk) == reference_hash;
            fx.generator.generate_chunk(kCx + static_cast<std::int64_t>(i) + 1, kCz, chunk);
            fx.generator.generate_chunk(kCx, kCz, chunk);
            ok[i] = ok[i] && hash_chunk(chunk) == reference_hash;
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    for (std::size_t i = 0; i < ok.size(); ++i) {
        CHECK_MESSAGE(ok[i], "thread ", i, " produced divergent output");
    }
}
