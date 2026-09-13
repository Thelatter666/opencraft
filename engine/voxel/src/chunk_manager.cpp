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

Chunk &ChunkManager::get_or_load(int cx, int cz) {
    return get_or_load(pack(cx, cz));
}

Chunk &ChunkManager::get_or_load_world(int world_x, int world_z) {
    return get_or_load(Chunk::chunk_coord(world_x, world_z));
}

Chunk *ChunkManager::find(std::int64_t key) {
    const auto it = chunks_.find(key);
    return it == chunks_.end() ? nullptr : it->second.get();
}

Chunk *ChunkManager::find(int cx, int cz) {
    return find(pack(cx, cz));
}

Chunk *ChunkManager::find_world(int world_x, int world_z) {
    return find(Chunk::chunk_coord(world_x, world_z));
}

bool ChunkManager::unload(std::int64_t key) {
    return chunks_.erase(key) > 0;
}

std::int64_t ChunkManager::pack(int cx, int cz) {
    const auto ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx));
    const auto uz = static_cast<std::uint32_t>(cz);
    return static_cast<std::int64_t>((ux << 32) | uz);
}

} // namespace opencraft::voxel
