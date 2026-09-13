#include <cstdint>
#include <stdexcept>

#include <doctest/doctest.h>

#include "opencraft/voxel/light_storage.hpp"

using opencraft::voxel::LightLevels;
using opencraft::voxel::LightStorage;

TEST_CASE("light storage constants match the chunk layout") {
    CHECK(LightStorage::kSizeX == 16);
    CHECK(LightStorage::kSizeY == 384);
    CHECK(LightStorage::kSizeZ == 16);
    CHECK(LightStorage::kVolume == 16U * 384U * 16U);
}

TEST_CASE("fresh light storage reads as zero everywhere") {
    const LightStorage storage(3, -4);
    CHECK(storage.sky(0, 0, 0) == 0);
    CHECK(storage.sky(15, 383, 15) == 0);
    CHECK(storage.block_light(7, 200, 8) == 0);
    CHECK(storage.chunk_x() == 3);
    CHECK(storage.chunk_z() == -4);
}

TEST_CASE("sky and block channels are independent 4-bit nibbles") {
    LightStorage storage;
    storage.set_sky(1, 2, 3, 15);
    storage.set_block_light(1, 2, 3, 7);
    CHECK(storage.sky(1, 2, 3) == 15);
    CHECK(storage.block_light(1, 2, 3) == 7);

    storage.set_block_light(1, 2, 3, 8);
    CHECK(storage.sky(1, 2, 3) == 15);
    CHECK(storage.block_light(1, 2, 3) == 8);

    storage.set_sky(1, 2, 3, 1);
    CHECK(storage.sky(1, 2, 3) == 1);
    CHECK(storage.block_light(1, 2, 3) == 8);

    // Levels are masked to 4 bits.
    storage.set_sky(0, 0, 0, 0xFF);
    storage.set_block_light(0, 0, 0, 0x1F);
    CHECK(storage.sky(0, 0, 0) == 15);
    CHECK(storage.block_light(0, 0, 0) == 15);
}

TEST_CASE("world-coordinate access respects the chunk origin") {
    LightStorage storage(-2, 5);
    // World x = -32 + 3 = -29, z = 80 + 9 = 89.
    storage.set_sky_world(-29, 100, 89, 13);
    storage.set_block_light_world(-29, 100, 89, 4);
    CHECK(storage.sky_world(-29, 100, 89) == 13);
    CHECK(storage.block_light_world(-29, 100, 89) == 4);
    CHECK(storage.sky(3, 100, 9) == 13);
    CHECK(storage.block_light(3, 100, 9) == 4);
}

TEST_CASE("out-of-bounds access throws") {
    LightStorage storage;
    CHECK_THROWS_AS(static_cast<void>(storage.sky(16, 0, 0)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(storage.sky(-1, 0, 0)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(storage.sky(0, 384, 0)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(storage.block_light(0, 0, 16)), std::out_of_range);
    CHECK_THROWS_AS(storage.set_sky(0, -1, 0, 5), std::out_of_range);
    CHECK_THROWS_AS(storage.set_block_light(0, 0, -1, 5), std::out_of_range);
}

TEST_CASE("clear resets every cell") {
    LightStorage storage(0, 0);
    storage.set_sky(5, 100, 5, 15);
    storage.set_block_light(6, 101, 6, 9);
    storage.clear();
    CHECK(storage.sky(5, 100, 5) == 0);
    CHECK(storage.block_light(6, 101, 6) == 0);
}

TEST_CASE("serialize and deserialize round trip preserves both channels") {
    LightStorage storage(-7, 9);
    storage.set_sky(0, 0, 0, 15);
    storage.set_block_light(0, 0, 0, 3);
    storage.set_sky(15, 383, 15, 9);
    storage.set_block_light(15, 383, 15, 14);
    storage.set_sky(8, 100, 4, 6);
    storage.set_block_light(8, 100, 4, 6);

    opencraft::core::ByteBuffer buffer;
    storage.serialize(buffer);
    buffer.rewind();
    const LightStorage restored = LightStorage::deserialize(buffer);
    CHECK(buffer.remaining() == 0);
    CHECK(restored.sky(0, 0, 0) == 15);
    CHECK(restored.block_light(0, 0, 0) == 3);
    CHECK(restored.sky(15, 383, 15) == 9);
    CHECK(restored.block_light(15, 383, 15) == 14);
    CHECK(restored.sky(8, 100, 4) == 6);
    CHECK(restored.block_light(8, 100, 4) == 6);
}

TEST_CASE("deserialize rejects unknown format versions") {
    opencraft::core::ByteBuffer buffer;
    buffer.write_u32(99);
    CHECK_THROWS_AS(static_cast<void>(LightStorage::deserialize(buffer)), std::runtime_error);
}
