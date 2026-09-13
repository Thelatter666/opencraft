#include <memory>

#include <doctest/doctest.h>

#include "opencraft/voxel/chunk_manager.hpp"

using opencraft::voxel::Chunk;
using opencraft::voxel::ChunkManager;

TEST_CASE("chunk manager creates empty chunks on demand") {
    ChunkManager manager;
    CHECK(manager.loaded_count() == 0);
    CHECK(manager.find(0) == nullptr);

    const std::int64_t key = Chunk::chunk_coord(-3, 7);
    Chunk &chunk = manager.get_or_load(key);
    CHECK(manager.loaded_count() == 1);
    CHECK(manager.find(key) == &chunk);
    CHECK(chunk.empty());

    // Same key returns the same instance, not a fresh chunk.
    Chunk &again = manager.get_or_load(key);
    CHECK(&again == &chunk);

    // Writes through one reference are visible through the other.
    again.set_block(1, 2, 3, 9);
    CHECK(chunk.get_block(1, 2, 3) == 9);
}

TEST_CASE("chunk manager accepts integer chunk coordinates") {
    ChunkManager manager;
    Chunk &a = manager.get_or_load(-1, -1);
    Chunk &b = manager.get_or_load(Chunk::chunk_coord(-1, -1));
    CHECK(&a == &b);
    CHECK(manager.find(-1, -1) == &a);
    CHECK(manager.find(-1, 0) == nullptr);
}

TEST_CASE("chunk manager unload drops only the targeted chunk") {
    ChunkManager manager;
    const std::int64_t key_a = Chunk::chunk_coord(0, 0);
    const std::int64_t key_b = Chunk::chunk_coord(16, 0); // world x=16 -> chunk 1
    static_cast<void>(manager.get_or_load(key_a));
    static_cast<void>(manager.get_or_load(key_b));
    CHECK(manager.loaded_count() == 2);

    CHECK(manager.unload(key_a));
    CHECK(manager.find(key_a) == nullptr);
    CHECK(manager.find(key_b) != nullptr);
    CHECK(manager.loaded_count() == 1);

    CHECK_FALSE(manager.unload(key_a)); // already gone
    CHECK_FALSE(manager.unload(Chunk::chunk_coord(99, 99)));

    // Re-request after unload hands out a fresh empty chunk.
    Chunk &fresh = manager.get_or_load(key_a);
    CHECK(fresh.empty());
    CHECK(manager.loaded_count() == 2);
}

TEST_CASE("chunk manager integer overloads take chunk coordinates directly") {
    ChunkManager manager;

    // Chunk (1, 0) is the chunk covering world x in [16, 32); the packed key
    // of that chunk equals Chunk::chunk_coord of any block inside it.
    Chunk &c10 = manager.get_or_load(1, 0);
    CHECK(manager.find(Chunk::chunk_coord(16, 0)) == &c10);
    CHECK(manager.find(Chunk::chunk_coord(31, 15)) == &c10);
    CHECK(manager.find_world(31, 15) == &c10);
    CHECK(manager.find_world(16, 0) == &c10);
    CHECK(manager.find(0, 0) == nullptr); // distinct from chunk (1, 0)

    // Negative chunk coordinates are their own key, no floor division.
    Chunk &cnn = manager.get_or_load(-1, -1);
    CHECK(&manager.get_or_load_world(-16, -16) == &cnn);
    CHECK(&manager.get_or_load_world(-1, -1) == &cnn); // world x=-1 lives in chunk -1
    CHECK(&manager.get_or_load(Chunk::chunk_coord(-16, -16)) == &cnn);
    CHECK(manager.find(-1, -1) == &cnn);
    CHECK(manager.find(-1, 0) == nullptr);

    // Round trip of the pack layout through unpack_chunk_coord, and agreement
    // with chunk_coord applied to the chunk's own origin.
    const auto [cx, cz] = Chunk::unpack_chunk_coord(Chunk::chunk_coord(-5 * 16, 7 * 16));
    CHECK(cx == -5);
    CHECK(cz == 7);
    CHECK(&manager.get_or_load(cx, cz) == &manager.get_or_load_world(-5 * 16, 7 * 16));
}

TEST_CASE("chunk manager get_or_load_world creates the chunk holding the block") {
    ChunkManager manager;
    Chunk &chunk = manager.get_or_load_world(-200, 700);
    const auto [cx, cz] = Chunk::chunk_coords(-200, 700);
    CHECK(manager.find(cx, cz) == &chunk);
    CHECK(chunk.empty());
    chunk.set_block(0, 0, 0, 5);
    CHECK(manager.find_world(-200, 700)->get_block(0, 0, 0) == 5);
}
