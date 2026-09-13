#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#include "opencraft/voxel/chunk.hpp"

namespace opencraft::voxel {

// Sparse chunk container keyed by the packed (cx, cz) coordinate
// (Chunk::chunk_coord). Generation and disk loading are out of scope here
// (T004/T009): get_or_load() creates an empty chunk when absent, which is the
// seam where the world generator will hook in later.
//
// Coordinate semantics (fixed in T008; T005 defect): the (int, int) overloads
// take CHUNK coordinates (cx, cz) and pack them into the i64 key directly -
// no floor division. get_or_load(1, 0) is the chunk covering world x in
// [16, 32), and negative coordinates work as-is. Use the *_world() entry
// points to address chunks by world BLOCK coordinates; Chunk::chunk_coord()
// remains the world->key primitive and is unchanged.
class ChunkManager {
public:
    // Returns the chunk for the key, creating an empty one if absent.
    [[nodiscard]] Chunk &get_or_load(std::int64_t key);
    // Chunk-coordinate overload: (cx, cz) are chunk coordinates, NOT world
    // block coordinates.
    [[nodiscard]] Chunk &get_or_load(int cx, int cz);
    // World-block-coordinate entry point (wx, wz may be any block position,
    // including negatives): routes through Chunk::chunk_coord.
    [[nodiscard]] Chunk &get_or_load_world(int world_x, int world_z);

    // Returns the chunk if already loaded, nullptr otherwise.
    [[nodiscard]] Chunk *find(std::int64_t key);
    // Chunk-coordinate overload, see get_or_load(int, int).
    [[nodiscard]] Chunk *find(int cx, int cz);
    // World-block-coordinate entry point, see get_or_load_world.
    [[nodiscard]] Chunk *find_world(int world_x, int world_z);

    // Const lookups for read-only consumers (rendering, physics queries).
    [[nodiscard]] const Chunk *find(std::int64_t key) const;
    [[nodiscard]] const Chunk *find(int cx, int cz) const;
    [[nodiscard]] const Chunk *find_world(int world_x, int world_z) const;

    // Drops the chunk from memory if present. Returns true when a chunk was
    // removed (persistence to region files is a later task).
    bool unload(std::int64_t key);

    [[nodiscard]] std::size_t loaded_count() const { return chunks_.size(); }

private:
    // Packs chunk coordinates into the i64 key with the exact bit layout of
    // Chunk::chunk_coord (cx in the high i32, cz in the low i32) minus the
    // world->chunk floor division chunk_coord() performs.
    [[nodiscard]] static std::int64_t pack(int cx, int cz);

    std::unordered_map<std::int64_t, std::unique_ptr<Chunk>> chunks_;
};

} // namespace opencraft::voxel
