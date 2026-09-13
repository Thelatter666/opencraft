#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "opencraft/game/raycast.hpp"

using opencraft::game::raycast_voxel;
using opencraft::game::target_non_air;
using opencraft::game::VoxelRayHit;
using opencraft::render::IBlockSource;

namespace {

// Sparse in-memory block world: a handful of solid cells in an otherwise
// empty (air) universe. Satisfies the render IBlockSource contract (air for
// anything not explicitly set).
class MapSource final : public IBlockSource {
public:
    void set(int x, int y, int z, std::uint16_t id) { blocks_[key(x, y, z)] = id; }

    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override {
        const auto it = blocks_.find(key(wx, wy, wz));
        return it == blocks_.end() ? 0 : it->second;
    }

private:
    [[nodiscard]] static std::int64_t key(int x, int y, int z) {
        return (static_cast<std::int64_t>(x + 512) << 20) | (static_cast<std::int64_t>(y + 512) << 10) | (z + 512);
    }

    std::unordered_map<std::int64_t, std::uint16_t> blocks_;
};

glm::dvec3 norm(glm::dvec3 v) {
    return glm::normalize(v);
}

} // namespace

TEST_CASE("dda raycast hits a block straight ahead through its -Z face") {
    MapSource world;
    world.set(0, 10, -5, 7); // single stone block five cells in front

    const auto hit = raycast_voxel({0.5, 10.5, 0.5}, {0.0, 0.0, -1.0}, 20.0, world);
    REQUIRE(hit.hit);
    CHECK(hit.block_pos == glm::ivec3(0, 10, -5));
    CHECK(hit.face_normal == glm::ivec3(0, 0, 1)); // entered through +Z side, normal points back at ray
    // Entry boundary at z = -4: distance from 0.5 is 4.5.
    CHECK(hit.t == doctest::Approx(4.5));
}

TEST_CASE("dda raycast detects every face normal on axis-aligned approach") {
    MapSource world;
    const glm::ivec3 block(0, 0, 0);
    world.set(block.x, block.y, block.z, 1);

    const std::vector<std::pair<glm::dvec3, glm::ivec3>> cases = {
        {{1.0, 0.0, 0.0}, {-1, 0, 0}}, {{-1.0, 0.0, 0.0}, {1, 0, 0}}, {{0.0, 1.0, 0.0}, {0, -1, 0}},
        {{0.0, -1.0, 0.0}, {0, 1, 0}}, {{0.0, 0.0, 1.0}, {0, 0, -1}}, {{0.0, 0.0, -1.0}, {0, 0, 1}},
    };
    for (const auto &[dir, expected_normal] : cases) {
        CAPTURE(dir.x);
        CAPTURE(dir.y);
        CAPTURE(dir.z);
        // Start 3 blocks away from the block center, opposite to the travel
        // direction, so the block is in front of the ray.
        const glm::dvec3 origin = glm::dvec3(block) + 0.5 - dir * 3.0;
        const auto hit = raycast_voxel(origin, dir, 10.0, world);
        REQUIRE(hit.hit);
        CHECK(hit.block_pos == block);
        CHECK(hit.face_normal == expected_normal);
        CHECK(hit.t == doctest::Approx(2.5)); // block boundary is 2.5 away from 3.5
    }
}

TEST_CASE("dda raycast crosses cells diagonally and reports the entered face") {
    MapSource world;
    // Slanted ray (no boundary ties): dir (2,1,0)/|.| from (0.5,0.5,0.5)
    // crosses x=1 (t=0.559), then y=1, then x=2; cell (2,1,0) is entered
    // through its -X face at t = (2-0.5)/(2/sqrt(5)) = 0.75*sqrt(5).
    world.set(2, 1, 0, 5);
    const auto hit = raycast_voxel({0.5, 0.5, 0.5}, norm({2.0, 1.0, 0.0}), 30.0, world);
    REQUIRE(hit.hit);
    CHECK(hit.block_pos == glm::ivec3(2, 1, 0));
    CHECK(hit.face_normal == glm::ivec3(-1, 0, 0));
    CHECK(hit.t == doctest::Approx(0.75 * std::sqrt(5.0)));
}

TEST_CASE("dda raycast respects the max distance cutoff") {
    MapSource world;
    world.set(0, 0, -5, 3);

    // Just inside reach: boundary at 4.5.
    CHECK(raycast_voxel({0.5, 0.5, 0.5}, {0.0, 0.0, -1.0}, 4.6, world).hit);
    // Just outside reach: the 4.5-block survival interaction distance test.
    CHECK_FALSE(raycast_voxel({0.5, 0.5, 0.5}, {0.0, 0.0, -1.0}, 4.4, world).hit);
    // Boundary distance itself still counts.
    CHECK(raycast_voxel({0.5, 0.5, 0.5}, {0.0, 0.0, -1.0}, 4.5, world).hit);
}

TEST_CASE("dda raycast starting inside a targetable block hits it immediately") {
    MapSource world;
    world.set(2, 2, 2, 9);

    const auto hit = raycast_voxel({2.3, 2.7, 2.9}, {1.0, 0.0, 0.0}, 4.5, world);
    REQUIRE(hit.hit);
    CHECK(hit.block_pos == glm::ivec3(2, 2, 2));
    CHECK(hit.face_normal == glm::ivec3(0, 0, 0));
    CHECK(hit.t == doctest::Approx(0.0));
}

TEST_CASE("dda raycast starting exactly on a boundary steps into the next cell") {
    MapSource world;
    world.set(5, 0, 0, 4);

    // Origin at integer x = 4.5: floor is cell 4, ray along +X, block in
    // cell 5 - entered through its -X face.
    const auto hit = raycast_voxel({4.5, 0.5, 0.5}, {1.0, 0.0, 0.0}, 4.5, world);
    REQUIRE(hit.hit);
    CHECK(hit.block_pos == glm::ivec3(5, 0, 0));
    CHECK(hit.face_normal == glm::ivec3(-1, 0, 0));
    CHECK(hit.t == doctest::Approx(0.5));
}

TEST_CASE("dda raycast filter skips liquid cells") {
    MapSource world;
    world.set(0, 0, -2, 12); // water
    world.set(0, 0, -4, 7);  // stone behind the water

    const auto hit = raycast_voxel({0.5, 0.5, 0.5}, {0.0, 0.0, -1.0}, 20.0, world, target_non_air);
    // Default filter stops at the water.
    REQUIRE(hit.hit);
    CHECK(hit.block_pos == glm::ivec3(0, 0, -2));

    // A liquid-excluding filter (the client targeting filter) sees through.
    const auto through = raycast_voxel({0.5, 0.5, 0.5}, {0.0, 0.0, -1.0}, 20.0, world,
                                       [](std::uint16_t id) { return id != 0 && id != 12; });
    REQUIRE(through.hit);
    CHECK(through.block_pos == glm::ivec3(0, 0, -4));
    CHECK(through.face_normal == glm::ivec3(0, 0, 1));
    CHECK(through.t == doctest::Approx(3.5));
}

TEST_CASE("dda raycast finds nothing in empty space and with zero direction") {
    MapSource world;
    CHECK_FALSE(raycast_voxel({0.5, 0.5, 0.5}, {0.0, 0.0, -1.0}, 4.5, world).hit);
    CHECK_FALSE(raycast_voxel({0.5, 0.5, 0.5}, {0.0, 0.0, 0.0}, 4.5, world).hit);
    // Unnormalized direction is normalized internally - same result.
    const auto hit = raycast_voxel({0.5, 0.5, 0.5}, {0.0, 0.0, -10.0}, 4.5, world);
    CHECK_FALSE(hit.hit);
}

TEST_CASE("placement cell offsets the hit block along the entry face normal") {
    opencraft::game::VoxelRayHit hit;
    hit.block_pos = glm::ivec3(10, 64, -3);
    hit.face_normal = glm::ivec3(0, 1, 0);
    CHECK(opencraft::game::placement_cell(hit) == glm::ivec3(10, 65, -3));
    hit.face_normal = glm::ivec3(-1, 0, 0);
    CHECK(opencraft::game::placement_cell(hit) == glm::ivec3(9, 64, -3));
}
