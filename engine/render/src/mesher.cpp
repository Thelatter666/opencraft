#include "opencraft/render/mesher.hpp"

#include "opencraft/voxel/chunk.hpp"

namespace opencraft::render {

static_assert(voxel::Chunk::kSizeX == kChunkSizeX && voxel::Chunk::kSizeY == kChunkSizeY &&
                  voxel::Chunk::kSizeZ == kChunkSizeZ,
              "mesher chunk dimensions must match the storage chunk");

bool is_translucent_block(std::uint16_t block_id) {
    // Numeric ids of the blocks registered with transparent = true by
    // BlockRegistry::create_default() (air is handled separately by callers):
    // leaves (9), glass (11), water (12). This hard-codes the launch registry
    // ordering on purpose - the task contract keeps build_chunk_mesh() free of
    // extra parameters; when the registry turns data-driven (later milestone)
    // this table moves into a per-registry style provider fed by the caller.
    switch (block_id) {
    case 9:
    case 11:
    case 12:
        return true;
    default:
        return false;
    }
}

namespace {

// Face shade factors (0..255): bright top, dark bottom, mid-level sides with
// the two side axes distinguishable. Temporary stand-in for real lighting.
constexpr std::uint8_t kShadeTop = 255;
constexpr std::uint8_t kShadeBottom = 128;
constexpr std::uint8_t kShadeSideX = 210;
constexpr std::uint8_t kShadeSideZ = 170;

struct FaceDef {
    // Neighbor offset in world coordinates.
    int dx;
    int dy;
    int dz;
    int slot; // 0 = top, 1 = side, 2 = bottom (see tile_index())
    std::uint8_t shade;
    // 4 corners, each as (offset xyz, uv corner index): CCW winding seen from
    // outside the block so GL back-face culling works with the default front
    // face. uv is already encoded as MeshVertex::uv (bit0 = u, bit1 = v).
    std::int8_t corners[4][3];
    std::uint8_t corner_uv[4];
};

// Corner geometry verified via outward cross products; u/v keep textures
// upright on side faces (v follows y) and axis-aligned on top/bottom.
constexpr FaceDef kFaces[6] = {
    // +Y top
    {0, 1, 0, 0, kShadeTop, {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, {0, 2, 3, 1}},
    // -Y bottom
    {0, -1, 0, 2, kShadeBottom, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {0, 1, 3, 2}},
    // +X
    {1, 0, 0, 1, kShadeSideX, {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {1, 0, 2, 3}},
    // -X
    {-1, 0, 0, 1, kShadeSideX, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {0, 1, 3, 2}},
    // +Z
    {0, 0, 1, 1, kShadeSideZ, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {0, 1, 3, 2}},
    // -Z
    {0, 0, -1, 1, kShadeSideZ, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {1, 0, 2, 3}},
};

bool face_visible(std::uint16_t self, std::uint16_t neighbor) {
    if (neighbor == 0) { // air: always exposed
        return true;
    }
    // Against a *different* translucent block the face stays visible (ground
    // under water, terrain behind glass); identical translucent neighbors
    // (water-water, glass-glass, leaves-leaves) cull their shared faces.
    return is_translucent_block(neighbor) && neighbor != self;
}

} // namespace

MeshData build_chunk_mesh(const IBlockSource &blocks, ChunkPos pos) {
    MeshData mesh;
    const int base_x = pos.cx * kChunkSizeX;
    const int base_z = pos.cz * kChunkSizeZ;

    for (int y = 0; y < kChunkSizeY; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                const std::uint16_t id = blocks.block_at(base_x + x, y, base_z + z);
                if (id == 0) {
                    continue;
                }
                const bool translucent = is_translucent_block(id);
                MeshBucket &bucket = translucent ? mesh.translucent : mesh.opaque;

                for (const FaceDef &face : kFaces) {
                    if (translucent && face.dy < 0) {
                        continue; // translucent blocks skip their bottom face
                    }
                    const std::uint16_t neighbor =
                        blocks.block_at(base_x + x + face.dx, y + face.dy, base_z + z + face.dz);
                    if (!face_visible(id, neighbor)) {
                        continue;
                    }

                    const auto base = static_cast<std::uint32_t>(bucket.vertices.size());
                    for (int c = 0; c < 4; ++c) {
                        MeshVertex v;
                        v.x = static_cast<std::uint16_t>(x + face.corners[c][0]);
                        v.y = static_cast<std::uint16_t>(y + face.corners[c][1]);
                        v.z = static_cast<std::uint16_t>(z + face.corners[c][2]);
                        v.tile = tile_index(id, face.slot);
                        v.uv = face.corner_uv[c];
                        v.shade = face.shade;
                        bucket.vertices.push_back(v);
                    }
                    bucket.indices.insert(bucket.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
                }
            }
        }
    }
    return mesh;
}

} // namespace opencraft::render
