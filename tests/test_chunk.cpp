#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>

#include <doctest/doctest.h>

#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk.hpp"

using opencraft::voxel::BlockRegistry;
using opencraft::voxel::Chunk;

TEST_CASE("chunk dimension constants match the spec") {
    CHECK(Chunk::kSizeX == 16);
    CHECK(Chunk::kSizeY == 384);
    CHECK(Chunk::kSizeZ == 16);
    CHECK(Chunk::kSectionSize == 16);
    CHECK(Chunk::kSectionCount == 24);
}

TEST_CASE("chunk_coord floors negative world coordinates") {
    CHECK(Chunk::chunk_coords(0, 0) == std::pair{0, 0});
    CHECK(Chunk::chunk_coords(15, 15) == std::pair{0, 0});
    CHECK(Chunk::chunk_coords(16, 0) == std::pair{1, 0});
    CHECK(Chunk::chunk_coords(-1, -1) == std::pair{-1, -1});
    CHECK(Chunk::chunk_coords(-16, -16) == std::pair{-1, -1});
    CHECK(Chunk::chunk_coords(-17, 0) == std::pair{-2, 0});
    CHECK(Chunk::chunk_coords(472, -533) == std::pair{29, -34});

    // Packed key must round-trip through unpack for negatives too.
    const std::int64_t key = Chunk::chunk_coord(-1, -533);
    CHECK(Chunk::unpack_chunk_coord(key) == std::pair{-1, -34});
    CHECK(Chunk::unpack_chunk_coord(Chunk::chunk_coord(123456, -123456)) == std::pair{7716, -7716});
}

TEST_CASE("floor_div handles all sign combinations") {
    CHECK(Chunk::floor_div(7, 16) == 0);
    CHECK(Chunk::floor_div(-1, 16) == -1);
    CHECK(Chunk::floor_div(-16, 16) == -1);
    CHECK(Chunk::floor_div(-17, 16) == -2);
    CHECK(Chunk::floor_div(1, -16) == -1);
    CHECK(Chunk::floor_div(-1, -16) == 0);
}

TEST_CASE("fresh chunk reads as air everywhere and is empty") {
    const Chunk chunk;
    CHECK(chunk.empty());
    for (int i = 0; i < Chunk::kSectionCount; ++i) {
        CHECK(chunk.section_empty(i));
    }
    CHECK(chunk.get_block(0, 0, 0) == BlockRegistry::kAirId);
    CHECK(chunk.get_block(15, 383, 15) == BlockRegistry::kAirId);
    CHECK(chunk.get_block(7, 200, 8) == BlockRegistry::kAirId);
}

TEST_CASE("set and get round trip across section boundaries") {
    Chunk chunk;
    const std::uint16_t stone = 3;
    const std::uint16_t dirt = 2;
    chunk.set_block(0, 0, 0, stone);
    chunk.set_block(15, 15, 15, dirt);
    chunk.set_block(0, 16, 0, dirt);     // first block of section 1
    chunk.set_block(15, 383, 15, stone); // last block of section 23
    chunk.set_block(7, 63, 8, stone);    // last block of section 3
    chunk.set_block(7, 64, 8, dirt);     // first block of section 4

    CHECK(chunk.get_block(0, 0, 0) == stone);
    CHECK(chunk.get_block(15, 15, 15) == dirt);
    CHECK(chunk.get_block(0, 16, 0) == dirt);
    CHECK(chunk.get_block(15, 383, 15) == stone);
    CHECK(chunk.get_block(7, 63, 8) == stone);
    CHECK(chunk.get_block(7, 64, 8) == dirt);
    CHECK_FALSE(chunk.section_empty(0));
    CHECK_FALSE(chunk.section_empty(1));
    CHECK_FALSE(chunk.section_empty(4));
    CHECK(chunk.section_empty(2));
    CHECK_FALSE(chunk.empty());
}

TEST_CASE("out of bounds access is rejected") {
    Chunk chunk;
    CHECK_THROWS_AS(chunk.set_block(-1, 0, 0, 1), std::out_of_range);
    CHECK_THROWS_AS(chunk.set_block(16, 0, 0, 1), std::out_of_range);
    CHECK_THROWS_AS(chunk.set_block(0, -1, 0, 1), std::out_of_range);
    CHECK_THROWS_AS(chunk.set_block(0, 384, 0, 1), std::out_of_range);
    CHECK_THROWS_AS(chunk.set_block(0, 0, -1, 1), std::out_of_range);
    CHECK_THROWS_AS(chunk.set_block(0, 0, 16, 1), std::out_of_range);
    CHECK_THROWS_AS([&] { static_cast<void>(chunk.get_block(16, 0, 0)); }(), std::out_of_range);
    CHECK_THROWS_AS([&] { static_cast<void>(chunk.get_block(0, 384, 0)); }(), std::out_of_range);
    CHECK_THROWS_AS([&] { static_cast<void>(chunk.section_empty(-1)); }(), std::out_of_range);
    CHECK_THROWS_AS([&] { static_cast<void>(chunk.section_empty(24)); }(), std::out_of_range);
    CHECK_THROWS_AS([&] { static_cast<void>(chunk.section_stats(24)); }(), std::out_of_range);
}

TEST_CASE("palette grows through 4-bit, 8-bit and 16-bit tiers") {
    Chunk chunk;
    // Uniform section: 0 bits, no palette entries beyond the single value.
    auto stats = chunk.section_stats(5);
    CHECK(stats.uniform);
    CHECK(stats.bits_per_entry == 0);
    CHECK(stats.palette_size == 0);

    // First non-air write: materializes at 4 bits with two palette entries.
    chunk.set_block(0, 80, 0, 1);
    stats = chunk.section_stats(5);
    CHECK_FALSE(stats.uniform);
    CHECK(stats.bits_per_entry == 4);
    CHECK(stats.palette_size == 2);

    // Fill up with distinct values: the 17th distinct entry exceeds the
    // 4-bit capacity (16 slots) and forces growth to 8 bits.
    for (std::uint16_t id = 2; id <= 16; ++id) {
        chunk.set_block(id % 16, 80 + (id / 16), id % 16, id);
    }
    CHECK(chunk.section_stats(5).bits_per_entry == 8);
    CHECK(chunk.section_stats(5).palette_size == 17);

    // The 18th entry still fits in 8 bits.
    chunk.set_block(3, 80, 3, 17);
    CHECK(chunk.section_stats(5).bits_per_entry == 8);
    CHECK(chunk.section_stats(5).palette_size == 18);

    // Grow to 16 bits once 256 distinct values are exceeded.
    for (int i = 0; i < 300; ++i) {
        chunk.set_block(i % 16, 81 + (i / 256), (i / 16) % 16, static_cast<std::uint16_t>(100 + i));
    }
    stats = chunk.section_stats(5);
    CHECK(stats.bits_per_entry == 16);
    CHECK(stats.palette_size == 318);

    // All previously written values must survive every growth step.
    CHECK(chunk.get_block(0, 80, 0) == 1);
    CHECK(chunk.get_block(3, 80, 3) == 17);
    CHECK(chunk.get_block(0, 81, 0) == 100);  // i = 0
    CHECK(chunk.get_block(15, 82, 1) == 387); // i = 287
}

TEST_CASE("setting back to the initial value keeps palette width and entries") {
    Chunk chunk;
    chunk.set_block(0, 32, 0, 7);
    chunk.set_block(1, 32, 0, 9);
    chunk.set_block(0, 32, 0, 7); // undo the dirt; section still non-uniform
    CHECK(chunk.section_stats(2).bits_per_entry == 4);
    CHECK(chunk.section_stats(2).palette_size == 3);
    CHECK(chunk.get_block(1, 32, 0) == 9);
    // The chosen policy: no shrink. Documented on the Chunk class; callers
    // cannot observe it through get/set, only through section_stats.
    CHECK_FALSE(chunk.section_empty(2));
}

TEST_CASE("serialization skips fully air sections") {
    auto buffer_for = [](const Chunk &chunk) {
        opencraft::core::ByteBuffer buffer;
        chunk.serialize(buffer);
        return buffer;
    };
    // Fully air chunk: version (4) + non-empty count (1). Every air section
    // contributes zero bytes on the wire.
    CHECK(buffer_for(Chunk{}).size() == 4 + 1);

    // One non-air block materializes section 0 into a 4-bit packed array:
    // version + count + (index 1 + bits 1 + palette size 2 + palette 2x2 +
    // word count 4 + 4096 entries x 4 bits = 2048).
    Chunk one_uniform;
    one_uniform.set_block(0, 0, 0, 5);
    const auto one_size = buffer_for(one_uniform).size();
    CHECK(one_size == 4 + 1 + 1 + 1 + 2 + 4 + 4 + 2048);

    // A second non-empty section adds its own payload; the 22 air sections
    // add nothing beyond their absence.
    Chunk two_uniform;
    two_uniform.set_block(0, 0, 0, 5);
    two_uniform.set_block(0, 200, 0, 6);
    CHECK(buffer_for(two_uniform).size() == one_size + 2060);
}

TEST_CASE("serialization round trips a randomly filled chunk") {
    auto registry = BlockRegistry::create_default();
    std::mt19937 rng(20260913);
    std::uniform_int_distribution<int> coord_dist(0, 15);
    std::uniform_int_distribution<int> y_dist(0, Chunk::kSizeY - 1);
    const auto max_id = static_cast<std::uint16_t>(registry.size() - 1);
    std::uniform_int_distribution<std::uint16_t> block_dist(0, max_id);

    Chunk chunk;
    // Sparse random edits plus a solid slab, so both empty and dense
    // sections with several palette tiers are exercised.
    for (int i = 0; i < 4000; ++i) {
        chunk.set_block(coord_dist(rng), y_dist(rng), coord_dist(rng), block_dist(rng));
    }
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < 40; ++y) {
                chunk.set_block(x, y, z, block_dist(rng));
            }
        }
    }

    opencraft::core::ByteBuffer buffer;
    chunk.serialize(buffer);
    buffer.rewind();
    const Chunk restored = Chunk::deserialize(buffer);
    CHECK(buffer.remaining() == 0);

    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < Chunk::kSizeY; ++y) {
                if (chunk.get_block(x, y, z) != restored.get_block(x, y, z)) {
                    FAIL("block mismatch at ", x, ", ", y, ", ", z);
                }
            }
        }
    }
}

TEST_CASE("serialization rejects malformed payloads") {
    SUBCASE("wrong version") {
        opencraft::core::ByteBuffer buffer;
        buffer.write_version(999);
        buffer.write_u8(0);
        buffer.rewind();
        CHECK_THROWS_AS([&] { static_cast<void>(Chunk::deserialize(buffer)); }(), std::runtime_error);
    }
    SUBCASE("bad section index") {
        opencraft::core::ByteBuffer buffer;
        buffer.write_version(Chunk::kFormatVersion);
        buffer.write_u8(1);
        buffer.write_u8(24); // out of range
        buffer.rewind();
        CHECK_THROWS_AS([&] { static_cast<void>(Chunk::deserialize(buffer)); }(), std::runtime_error);
    }
    SUBCASE("bad bit width") {
        opencraft::core::ByteBuffer buffer;
        buffer.write_version(Chunk::kFormatVersion);
        buffer.write_u8(1);
        buffer.write_u8(3); // section index
        buffer.write_u8(5); // invalid bit width
        buffer.rewind();
        CHECK_THROWS_AS([&] { static_cast<void>(Chunk::deserialize(buffer)); }(), std::runtime_error);
    }
}

TEST_CASE("full chunk sweep set and get performance") {
    auto registry = BlockRegistry::create_default();
    Chunk chunk;
    const std::uint16_t stone = registry.id_of("stone");
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t sink = 0;
    for (int y = 0; y < Chunk::kSizeY; ++y) {
        for (int z = 0; z < Chunk::kSizeZ; ++z) {
            for (int x = 0; x < Chunk::kSizeX; ++x) {
                chunk.set_block(x, y, z, stone);
            }
        }
    }
    for (int y = 0; y < Chunk::kSizeY; ++y) {
        for (int z = 0; z < Chunk::kSizeZ; ++z) {
            for (int x = 0; x < Chunk::kSizeX; ++x) {
                sink += chunk.get_block(x, y, z);
            }
        }
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
    std::cout << "PERF full-chunk set+get sweep (2 x 98304 ops): " << ms << " ms (sink=" << sink << ")\n";
    CHECK(sink > 0);
    CHECK(ms < 1000.0);
}

TEST_CASE("typical terrain chunk serialized size") {
    auto registry = BlockRegistry::create_default();
    const auto bedrock = registry.id_of("bedrock");
    const auto stone = registry.id_of("stone");
    const auto dirt = registry.id_of("dirt");
    const auto grass = registry.id_of("grass_block");
    const auto water = registry.id_of("water");

    Chunk chunk;
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            chunk.set_block(x, 0, z, bedrock); // world bottom layer
            for (int y = 1; y <= 40; ++y) {
                chunk.set_block(x, y, z, stone);
            }
            for (int y = 41; y <= 44; ++y) {
                chunk.set_block(x, y, z, dirt);
            }
            chunk.set_block(x, 45, z, grass);
            for (int y = 46; y <= 62; ++y) {
                chunk.set_block(x, y, z, water);
            }
            // y 63.. stay air: 14 sections skipped entirely.
        }
    }
    opencraft::core::ByteBuffer buffer;
    chunk.serialize(buffer);
    std::cout << "PERF typical surface chunk serialized size: " << buffer.size() << " bytes (raw, no compression)\n";
    CHECK(buffer.size() > 0);
}
