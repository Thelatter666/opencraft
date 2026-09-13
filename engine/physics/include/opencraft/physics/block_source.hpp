#pragma once

namespace opencraft::physics {

// Minimal world-query seam between the headless physics and any voxel
// storage. The contract requires only `solid_at`; tests back it with
// hand-built fake worlds, and the runtime adapter (ChunkManager +
// BlockRegistry) arrives with game integration.
struct IBlockSource {
    virtual ~IBlockSource() = default;

    // True when the block at world integer coordinates (wx, wy, wz) blocks
    // entity movement (full-cube collision only in this task).
    [[nodiscard]] virtual bool solid_at(int wx, int wy, int wz) const = 0;

    // True when the block is a liquid (water) for swimming / fall-reset
    // purposes. Defaults to false so a minimal adapter only needs solid_at;
    // the runtime water query is a runtime-adapter concern (T008+).
    [[nodiscard]] virtual bool liquid_at(int wx, int wy, int wz) const {
        (void) wx;
        (void) wy;
        (void) wz;
        return false;
    }
};

} // namespace opencraft::physics
