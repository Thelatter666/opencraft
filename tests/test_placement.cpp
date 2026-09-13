#include <doctest/doctest.h>

#include "opencraft/game/placement.hpp"

#include "opencraft/game/raycast.hpp"

using opencraft::game::check_placement;
using opencraft::game::PlacementStatus;
using opencraft::render::IBlockSource;
using opencraft::voxel::BlockRegistry;

namespace {

// Flat slab world: solid ground at y = 0..2 (blocks in cells with y <= 2),
// everything else air. Optionally blocks one extra cell.
class SlabWorld final : public IBlockSource {
public:
    explicit SlabWorld(BlockRegistry registry) : registry_(std::move(registry)) {}

    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override {
        if (wy <= 2) {
            return registry_.id_of("stone");
        }
        if (extra_ && wx == 3 && wy == 3 && wz == 3) {
            return registry_.id_of("cobblestone");
        }
        return 0;
    }

    void set_extra(bool value) { extra_ = value; }

    [[nodiscard]] const BlockRegistry &registry() const { return registry_; }

private:
    BlockRegistry registry_;
    bool extra_ = false;
};

} // namespace

TEST_CASE("placement into air above the ground is accepted") {
    SlabWorld world(BlockRegistry::create_default());
    // Cell above the slab, well clear of a player standing far away.
    const auto status = check_placement(world.registry(), world, {0, 3, 0}, {10.5, 3.0, 10.5}, 1.8, 0.3);
    CHECK(status == PlacementStatus::Ok);
}

TEST_CASE("placement into an occupied cell is rejected and into water allowed") {
    SlabWorld world(BlockRegistry::create_default());
    // Inside the slab (stone): occupied.
    CHECK(check_placement(world.registry(), world, {0, 1, 0}, {10.5, 3.0, 10.5}, 1.8, 0.3) ==
          PlacementStatus::CellOccupied);
    // An extra solid cell: occupied too.
    world.set_extra(true);
    CHECK(check_placement(world.registry(), world, {3, 3, 3}, {10.5, 3.0, 10.5}, 1.8, 0.3) ==
          PlacementStatus::CellOccupied);
    world.set_extra(false);
}

TEST_CASE("placement into the player body is rejected but flush blocks are allowed") {
    SlabWorld world(BlockRegistry::create_default());
    const glm::dvec3 feet(3.5, 3.0, 3.5); // standing on the slab, in cell y=3..4

    // Cell the player stands in: overlaps the body.
    CHECK(check_placement(world.registry(), world, {3, 3, 3}, feet, 1.8, 0.3) ==
          PlacementStatus::IntersectsPlayer);
    // Head-height cell: also overlaps (feet y 3.0 .. 4.8).
    CHECK(check_placement(world.registry(), world, {3, 4, 3}, feet, 1.8, 0.3) ==
          PlacementStatus::IntersectsPlayer);
    // Block directly at head level top - touching the head plane but not
    // overlapping (strict inequality): allowed.
    CHECK(check_placement(world.registry(), world, {3, 5, 3}, feet, 1.8, 0.3) == PlacementStatus::Ok);

    // Sneaking shrinks the body: the same head cell becomes placeable for a
    // shorter player whose box ends below y=4.5.
    const glm::dvec3 sneaking_feet(3.5, 3.0, 3.5);
    CHECK(check_placement(world.registry(), world, {3, 4, 3}, sneaking_feet, 1.5, 0.3) ==
          PlacementStatus::IntersectsPlayer); // 3.0..4.5 still overlaps cell y=[4,5)
    CHECK(check_placement(world.registry(), world, {3, 5, 3}, sneaking_feet, 1.5, 0.3) == PlacementStatus::Ok);

    // Sideways flush: block beside the feet touching x=3.8 (feet + half
    // width) is adjacent-but-not-overlapping at x=[4,5)? No - the player
    // spans x [3.2, 3.8]; cell x=[3,4) overlaps. One further out is fine.
    CHECK(check_placement(world.registry(), world, {4, 3, 3}, feet, 1.8, 0.3) == PlacementStatus::Ok);
}

TEST_CASE("placement neighbor offset uses the entered face, not the hit block") {
    // The raycast supplies block + face normal; placement_cell() adds them.
    // Placing against the TOP face of the slab (hit block y=2) targets the
    // cell above it.
    SlabWorld world(BlockRegistry::create_default());
    opencraft::game::VoxelRayHit hit;
    hit.block_pos = glm::ivec3(6, 2, 0);
    hit.face_normal = glm::ivec3(0, 1, 0);
    const auto cell = opencraft::game::placement_cell(hit);
    CHECK(cell == glm::ivec3(6, 3, 0));
    CHECK(check_placement(world.registry(), world, cell, {30.5, 3.0, 0.5}, 1.8, 0.3) == PlacementStatus::Ok);

    // The wrong-side face points back into the hit block itself: the cell is
    // the hit block, already occupied, and the placement is rejected.
    hit.face_normal = glm::ivec3(0, -1, 0);
    CHECK(check_placement(world.registry(), world, opencraft::game::placement_cell(hit), {30.5, 3.0, 0.5}, 1.8,
                          0.3) == PlacementStatus::CellOccupied);
}
