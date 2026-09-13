#pragma once

#include <functional>

#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk_manager.hpp"
#include "opencraft/voxel/light_engine.hpp"

namespace opencraft::voxel {

// ILightWorld implementation backed by a ChunkManager + BlockRegistry, the
// wiring the game layer will use. LightEngine itself never touches this class,
// keeping the engine testable against a bare fake.
//
// Transparency comes from the registry's BlockDef::transparent flag. Emission
// is injected as a block-id -> level table (the launch registry has no light
// sources; tests/mods supply virtual ones like a 14-level torch).
class ChunkLightWorld final : public ILightWorld {
public:
    using EmissionFn = std::function<std::uint8_t(std::uint16_t block_id)>;

    // `emission` may be null/empty; missing ids then read as level 0. When a
    // position's chunk is not loaded it answers as transparent air, matching
    // LightEngine's deferred-offer model for uninitialized chunks.
    ChunkLightWorld(ChunkManager &chunks, const BlockRegistry &registry, EmissionFn emission);

    [[nodiscard]] BlockLightProps props_at(int world_x, int world_y, int world_z) const override;
    [[nodiscard]] BlockLightProps props_of(std::uint16_t block_id) const override;

private:
    ChunkManager &chunks_;
    const BlockRegistry &registry_;
    EmissionFn emission_;
};

} // namespace opencraft::voxel
