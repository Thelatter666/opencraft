#include "opencraft/voxel/chunk_manager.hpp"

namespace opencraft::voxel {

Chunk &ChunkManager::get_or_load(std::int64_t key) {
    const auto it = chunks_.find(key);
    if (it != chunks_.end()) {
        return *it->second;
    }
    auto chunk = std::make_unique<Chunk>();
    Chunk &ref = *chunk;
    chunks_.emplace(key, std::move(chunk));
    return ref;
}

Chunk &ChunkManager::get_or_load(int chunk_x, int chunk_z) {
    return get_or_load(Chunk::chunk_coord(chunk_x, chunk_z));
}

Chunk *ChunkManager::find(std::int64_t key) {
    const auto it = chunks_.find(key);
    return it == chunks_.end() ? nullptr : it->second.get();
}

Chunk *ChunkManager::find(int chunk_x, int chunk_z) {
    return find(Chunk::chunk_coord(chunk_x, chunk_z));
}

bool ChunkManager::unload(std::int64_t key) {
    return chunks_.erase(key) > 0;
}

} // namespace opencraft::voxel
