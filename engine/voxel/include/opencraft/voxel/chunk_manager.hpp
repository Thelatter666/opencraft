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
class ChunkManager {
public:
    // Returns the chunk for the key, creating an empty one if absent.
    [[nodiscard]] Chunk &get_or_load(std::int64_t key);
    [[nodiscard]] Chunk &get_or_load(int chunk_x, int chunk_z);

    // Returns the chunk if already loaded, nullptr otherwise.
    [[nodiscard]] Chunk *find(std::int64_t key);
    [[nodiscard]] Chunk *find(int chunk_x, int chunk_z);

    // Drops the chunk from memory if present. Returns true when a chunk was
    // removed (persistence to region files is a later task).
    bool unload(std::int64_t key);

    [[nodiscard]] std::size_t loaded_count() const { return chunks_.size(); }

private:
    std::unordered_map<std::int64_t, std::unique_ptr<Chunk>> chunks_;
};

} // namespace opencraft::voxel
