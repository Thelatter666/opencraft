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
