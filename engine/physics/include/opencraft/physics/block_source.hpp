#pragma once

#include <cstdint>

namespace opencraft::physics {

// MC's default block friction (docs/research/06 §7.1). Air, soul sand, cobweb
// and fluids all report this value; ice is 0.98, packed ice 0.98, blue ice
// 0.989, slime 0.8. Named here because the block query — not the entity config
// — owns the block half of the friction product.
inline constexpr double kDefaultSlipperiness = 0.6;

// Collision shape of one block. Only the two extremes exist in this task;
// slabs, stairs and fluids need a real shape description and arrive with their
// own cards. The enum exists so the seam is in place before those consumers
// land, per docs/research/07 §7.2.
enum class BlockShape : std::uint8_t {
    Empty = 0,
    FullCube = 1,
};

// World-query seam between the headless physics and any voxel storage. Every
// method beyond `solid_at` has a default implementation, so a minimal adapter
// (and the runtime adapter in game/client, which is outside this task's write
// scope) keeps compiling unchanged.
struct IBlockSource {
    virtual ~IBlockSource() = default;

    // True when the block at world integer coordinates (wx, wy, wz) blocks
    // entity movement (full-cube collision only in this task).
    [[nodiscard]] virtual bool solid_at(int wx, int wy, int wz) const = 0;

    // True when the block is a liquid (water) for swimming / fall-reset
    // purposes. Defaults to false so a minimal adapter only needs solid_at.
    [[nodiscard]] virtual bool liquid_at(int wx, int wy, int wz) const {
        (void) wx;
        (void) wy;
        (void) wz;
        return false;
    }

    // Slipperiness of the block — the block half of the friction product
    // `k = entity.horizontal_drag × slipperiness_at(...)` (docs/research/07
    // §7.2). Adapters that model block types override this; the default is MC's
    // ordinary-block value, which keeps a solid-only adapter behaviourally
    // identical to a full one on default terrain.
    [[nodiscard]] virtual double slipperiness_at(int wx, int wy, int wz) const {
        (void) wx;
        (void) wy;
        (void) wz;
        return kDefaultSlipperiness;
    }

    // Collision shape of the block. Derived from `solid_at` by default, which
    // is exactly right while every solid block is a full cube. Adapters with
    // real shapes override it.
    [[nodiscard]] virtual BlockShape shape_at(int wx, int wy, int wz) const {
        return solid_at(wx, wy, wz) ? BlockShape::FullCube : BlockShape::Empty;
    }
};

} // namespace opencraft::physics
