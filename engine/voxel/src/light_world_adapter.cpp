#include "opencraft/voxel/light_world_adapter.hpp"

#include <utility>

namespace opencraft::voxel {

ChunkLightWorld::ChunkLightWorld(ChunkManager &chunks, const BlockRegistry &registry, EmissionFn emission)
    : chunks_(chunks), registry_(registry), emission_(std::move(emission)) {
}

BlockLightProps ChunkLightWorld::props_at(int world_x, int world_y, int world_z) const {
    const Chunk *chunk = chunks_.find(Chunk::chunk_coord(world_x, world_z));
    if (chunk == nullptr) {
        return BlockLightProps{}; // unloaded chunk behaves as transparent air
    }
    const auto [cx, cz] = Chunk::chunk_coords(world_x, world_z);
    const std::uint16_t block_id =
        chunk->get_block(world_x - cx * Chunk::kSizeX, world_y, world_z - cz * Chunk::kSizeZ);
    return props_of(block_id);
}

BlockLightProps ChunkLightWorld::props_of(std::uint16_t block_id) const {
    BlockLightProps props;
    if (!registry_.has_numeric(block_id)) {
        return props; // unknown ids are treated as transparent air
    }
    props.transparent = registry_.def_of(block_id).transparent;
    props.emission = emission_ != nullptr ? emission_(block_id) : 0;
    return props;
}

} // namespace opencraft::voxel
