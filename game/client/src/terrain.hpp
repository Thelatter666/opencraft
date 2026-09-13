#pragma once

#include "opencraft/render/mesher.hpp"
#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk_manager.hpp"

namespace opencraft::client {

// Procedurally composed demo terrain (task T005 acceptance: 5x5 chunks of
// rolling terrain plus a carved pit, independent of T004's generator). Fills
// a ChunkManager directly at construction: bedrock floor, stone body with ore
// specks, dirt, grass/sand surface depending on the water level, water-filled
// basins, a dry-land pit and a few trees. Everything is deterministic.
class TestTerrain {
public:
    // Chunks are built for chunk coordinates [-2, 2] x [-2, 2].
    static constexpr int kRadius = 2;
    static constexpr int kWaterLevel = 8;

    TestTerrain();

    [[nodiscard]] voxel::BlockRegistry &registry() { return registry_; }

    [[nodiscard]] voxel::ChunkManager &chunks() { return chunks_; }

    // Ground surface height of a column (top solid block y).
    [[nodiscard]] int ground_height(int wx, int wz) const;

private:
    void build_column(int wx, int wz);
    void set_world_block(int wx, int wy, int wz, std::uint16_t id);

    voxel::BlockRegistry registry_;
    voxel::ChunkManager chunks_;
};

// render::IBlockSource adapter over the loaded ChunkManager. Unloaded chunks
// and out-of-world y serve air (the documented IBlockSource contract), so the
// world edge stays visible. Keeps a one-chunk positive cache since meshing
// queries are spatially coherent.
class ChunkSource final : public render::IBlockSource {
public:
    explicit ChunkSource(voxel::ChunkManager &chunks) : chunks_(chunks) {}

    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override;

private:
    voxel::ChunkManager &chunks_;
    mutable std::int64_t cached_key_ = 0;
    mutable const voxel::Chunk *cached_chunk_ = nullptr;
};

} // namespace opencraft::client
