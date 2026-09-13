#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include <doctest/doctest.h>

#include "opencraft/render/mesher.hpp"
#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk.hpp"

using namespace opencraft;
using render::ChunkPos;
using render::IBlockSource;
using render::MeshData;

namespace {

constexpr std::uint16_t kStone = 3; // BlockRegistry::create_default() ordering
constexpr std::uint16_t kWater = 12;

// In-memory IBlockSource: whole chunks filled with `fill`, plus per-world-
// position overrides (used to punch holes or place single blocks). Outside
// the world vertical range everything is air, matching the documented
// IBlockSource contract.
class TestSource final : public IBlockSource {
public:
    std::unordered_set<std::int64_t> solid_chunks;
    std::unordered_map<std::int64_t, std::uint16_t> overrides;

    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override {
        if (wy < 0 || wy >= render::kChunkSizeY) {
            return 0;
        }
        const auto packed = (static_cast<std::int64_t>(wx) << 40) | (static_cast<std::int64_t>(wz) << 20) | wy;
        if (const auto it = overrides.find(packed); it != overrides.end()) {
            return it->second;
        }
        if (solid_chunks.contains(voxel::Chunk::chunk_coord(wx, wz))) {
            return kStone;
        }
        return 0;
    }
};

[[nodiscard]] std::size_t face_count(const render::MeshBucket &bucket) {
    return bucket.indices.size() / 6;
}

} // namespace

TEST_CASE("mesher: all-air chunk produces no geometry") {
    TestSource source;
    const MeshData mesh = render::build_chunk_mesh(source, {0, 0});

    CHECK(mesh.opaque.vertices.empty());
    CHECK(mesh.opaque.indices.empty());
    CHECK(mesh.translucent.vertices.empty());
    CHECK(mesh.translucent.indices.empty());
}

TEST_CASE("mesher: single isolated block emits all six faces") {
    TestSource source;
    source.overrides.emplace(0, kStone); // block at world (0, 0, 0)
    const MeshData mesh = render::build_chunk_mesh(source, {0, 0});

    REQUIRE(mesh.opaque.vertices.size() == 24); // 6 faces * 4
    REQUIRE(mesh.opaque.indices.size() == 36);  // 6 faces * 6
    CHECK(mesh.translucent.vertices.empty());

    // All vertices stay inside the block's unit cube.
    for (const auto &v : mesh.opaque.vertices) {
        CHECK(v.x <= 1);
        CHECK(v.y <= 1);
        CHECK(v.z <= 1);
    }
}

TEST_CASE("mesher: fully solid chunk keeps exactly the outer surface") {
    TestSource source;
    source.solid_chunks.insert(voxel::Chunk::chunk_coord(0, 0));

    const MeshData mesh = render::build_chunk_mesh(source, {0, 0});

    // Out-of-bounds neighbors are air, so the exposed surface is top +
    // bottom + the four vertical walls.
    const std::size_t expected_faces = 2 * static_cast<std::size_t>(render::kChunkSizeX * render::kChunkSizeZ) +
                                       4 * static_cast<std::size_t>(render::kChunkSizeX * render::kChunkSizeY);
    CHECK(face_count(mesh.opaque) == expected_faces);
    CHECK(mesh.opaque.vertices.size() == expected_faces * 4);
    CHECK(mesh.opaque.indices.size() == expected_faces * 6);
    CHECK(mesh.translucent.vertices.empty());
}

TEST_CASE("mesher: solid neighbor chunk culls boundary faces across chunks") {
    TestSource source;
    source.solid_chunks.insert(voxel::Chunk::chunk_coord(0, 0));
    // chunk_coord takes *world* coordinates: block (16, 0) lives in chunk (1, 0).
    source.solid_chunks.insert(voxel::Chunk::chunk_coord(1 * 16, 0)); // east neighbor

    const ChunkPos pos{0, 0};
    const MeshData both_solid = render::build_chunk_mesh(source, pos);

    // The shared east/west wall is hidden; exposed surface is top + bottom +
    // three walls.
    const std::size_t wall_faces = 3 * static_cast<std::size_t>(render::kChunkSizeX * render::kChunkSizeY);
    const std::size_t cap_faces = 2 * static_cast<std::size_t>(render::kChunkSizeX * render::kChunkSizeZ);
    CHECK(face_count(both_solid.opaque) == wall_faces + cap_faces);

    // Punch a single-block hole into the neighbor: chunk 0 must gain exactly
    // one face (proving the cross-chunk query reached into chunk 1).
    source.overrides.emplace((static_cast<std::int64_t>(16) << 40) | (static_cast<std::int64_t>(5) << 20) | 100, 0);
    const MeshData with_hole = render::build_chunk_mesh(source, pos);
    CHECK(face_count(with_hole.opaque) == wall_faces + cap_faces + 1);
    CHECK(with_hole.opaque.vertices.size() == both_solid.opaque.vertices.size() + 4);
    CHECK(with_hole.opaque.indices.size() == both_solid.opaque.indices.size() + 6);
}

TEST_CASE("mesher: same input meshes byte-identical twice") {
    TestSource source;
    source.solid_chunks.insert(voxel::Chunk::chunk_coord(0, 0));
    source.overrides.emplace((static_cast<std::int64_t>(-1) << 40) | (static_cast<std::int64_t>(-1) << 20) | 64,
                             kWater);
    source.overrides.emplace((static_cast<std::int64_t>(9) << 40) | (static_cast<std::int64_t>(-1) << 20) | 200,
                             kStone);

    const MeshData first = render::build_chunk_mesh(source, {0, 0});
    const MeshData second = render::build_chunk_mesh(source, {0, 0});

    REQUIRE(first == second);
    // Strict byte-level equality on top of the element-wise comparison.
    REQUIRE(memcmp(first.opaque.vertices.data(), second.opaque.vertices.data(),
                   first.opaque.vertices.size() * sizeof(render::MeshVertex)) == 0);
    REQUIRE(memcmp(first.opaque.indices.data(), second.opaque.indices.data(),
                   first.opaque.indices.size() * sizeof(std::uint32_t)) == 0);
    REQUIRE(memcmp(first.translucent.vertices.data(), second.translucent.vertices.data(),
                   first.translucent.vertices.size() * sizeof(render::MeshVertex)) == 0);
}

TEST_CASE("mesher: water meshes into the translucent bucket without a bottom face") {
    TestSource source;
    // One water block floating in air: top + four sides, no bottom.
    source.overrides.emplace(0, kWater);
    const MeshData mesh = render::build_chunk_mesh(source, {0, 0});

    CHECK(mesh.opaque.vertices.empty());
    CHECK(face_count(mesh.translucent) == 5);
    CHECK(mesh.translucent.vertices.size() == 20);

    // Ground under water must keep its face so the water floor is visible:
    // stone at (0,0,0) with water above it at (0,1,0) keeps its top face.
    TestSource with_ground;
    with_ground.overrides.emplace(0, kStone);
    with_ground.overrides.emplace(1, kWater); // world (0, 1, 0)
    const MeshData ground_mesh = render::build_chunk_mesh(with_ground, {0, 0});
    CHECK(face_count(ground_mesh.opaque) == 6); // stone: 5 sides + kept top face
    CHECK(face_count(ground_mesh.translucent) == 5);
}

TEST_CASE("mesher: water-water and stone-water neighbors cull internal faces") {
    TestSource source;
    // Two stacked water blocks (0,0,0) and (0,1,0): the shared face is
    // culled; the lower one emits its four sides, the upper one its four
    // sides plus the exposed top.
    source.overrides.emplace(0, kWater);
    source.overrides.emplace(1, kWater); // world (0, 1, 0)
    const MeshData mesh = render::build_chunk_mesh(source, {0, 0});
    // Lower block: 4 side faces (top culled by the water above, bottom
    // skipped); upper block: 4 side faces + exposed top = 9 faces total.
    CHECK(face_count(mesh.translucent) == 9);
}

TEST_CASE("mesher: translucent classification matches the default block registry") {
    const voxel::BlockRegistry registry = voxel::BlockRegistry::create_default();
    for (std::uint16_t id = 1; id < registry.size(); ++id) {
        CAPTURE(registry.string_of(id));
        CHECK(render::is_translucent_block(id) == registry.def_of(id).transparent);
    }
}

TEST_CASE("mesher: tile index layout is block-major with top/side/bottom slots") {
    CHECK(render::tile_index(0, 0) == 0);
    CHECK(render::tile_index(0, 2) == 2);
    CHECK(render::tile_index(1, 0) == 3);
    CHECK(render::tile_index(12, 1) == 37);
}
