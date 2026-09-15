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

// ── fluid surfaces (T-F1) ───────────────────────────────────────────────────
//
// The block id the fluid layer uses as its placeholder: the launch registry's
// "water" (id 12, the same table is_translucent_block() hard-codes). A voxel
// with a fluid surface renders from the fluid bucket instead of the block cube.
constexpr std::uint16_t kFluidWaterBlockId = 12;
// Water is not fed through the T008 light sampling (that path walks the block
// buckets); a fixed brightness keeps it readable until water lighting lands.
constexpr std::uint8_t kFluidShade = 220;

// Four corners of one quad, flattened to keep the aggregate initialisers
// readable: corner[c * 3 + 0/1/2] is the (x, y, z) of corner c.
struct FluidQuad {
    float corner[12]; // chunk-local, y already resolved to the water surface
    std::uint8_t corner_uv[4];
};

// Top surface at y = h. Corner order and uv codes follow the block mesher's
// kFaces convention; culling is off in the translucent pass, so winding only
// has to stay consistent for future passes.
[[nodiscard]] FluidQuad make_top_quad(float x, float y, float z, float h) {
    const float top = y + h;
    return FluidQuad{{x, top, z, x, top, z + 1.0f, x + 1.0f, top, z + 1.0f, x + 1.0f, top, z}, {0, 2, 3, 1}};
}

// One side wall spanning the visible band [h_low, h_high]: for an air
// neighbour h_low is 0, for a shallower water neighbour it is that surface, so
// a level step shows as a step instead of a gap.
[[nodiscard]] FluidQuad make_side_quad(int direction, float x, float y, float z, float h_low, float h_high) {
    const float low = y + h_low;
    const float high = y + h_high;
    switch (direction) {
    case 0: // +X
        return FluidQuad{{x + 1.0f, low, z + 1.0f, x + 1.0f, low, z, x + 1.0f, high, z, x + 1.0f, high, z + 1.0f},
                         {1, 0, 2, 3}};
    case 1: // -X
        return FluidQuad{{x, low, z, x, low, z + 1.0f, x, high, z + 1.0f, x, high, z}, {0, 1, 3, 2}};
    case 2: // +Z
        return FluidQuad{{x, low, z + 1.0f, x + 1.0f, low, z + 1.0f, x + 1.0f, high, z + 1.0f, x, high, z + 1.0f},
                         {0, 1, 3, 2}};
    default: // -Z
        return FluidQuad{{x + 1.0f, low, z, x, low, z, x, high, z, x + 1.0f, high, z}, {1, 0, 2, 3}};
    }
}

void append_fluid_quad(FluidBucket &bucket, const FluidQuad &quad, std::uint16_t tile) {
    const auto base = static_cast<std::uint32_t>(bucket.vertices.size());
    for (int c = 0; c < 4; ++c) {
        FluidVertex v;
        v.x = quad.corner[c * 3 + 0];
        v.y = quad.corner[c * 3 + 1];
        v.z = quad.corner[c * 3 + 2];
        v.tile = tile;
        v.uv = quad.corner_uv[c];
        v.shade = kFluidShade;
        bucket.vertices.push_back(v);
    }
    bucket.indices.insert(bucket.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

} // namespace

MeshData build_chunk_mesh(const IBlockSource &blocks, ChunkPos pos, const IFluidSource *fluid) {
    MeshData mesh;
    const int base_x = pos.cx * kChunkSizeX;
    const int base_z = pos.cz * kChunkSizeZ;

    constexpr int kSideOffsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    // Fluid-free chunks (the vast majority) skip the whole fluid scan: without
    // this every cell of every chunk paid a world-position fluid lookup.
    int fluid_y0 = 0;
    int fluid_y1 = 0;
    if (fluid != nullptr) {
        const FluidSpan span = fluid->fluid_span(pos.cx, pos.cz);
        fluid_y0 = span.empty() ? 0 : span.first * kSectionSize;
        fluid_y1 = span.empty() ? 0 : span.last * kSectionSize;
    }

    for (int y = 0; y < kChunkSizeY; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                const int world_x = base_x + x;
                const int world_z = base_z + z;
                const std::uint16_t id = blocks.block_at(world_x, y, world_z);

                if (y >= fluid_y0 && y < fluid_y1) {
                    const float height = fluid->fluid_height_at(world_x, y, world_z);
                    if (height > 0.0f) {
                        const float fx = static_cast<float>(x);
                        const float fy = static_cast<float>(y);
                        const float fz = static_cast<float>(z);
                        // The surface is exposed only when no fluid of the
                        // same body sits directly above.
                        if (fluid->fluid_height_at(world_x, y + 1, world_z) <= 0.0f) {
                            append_fluid_quad(mesh.fluid, make_top_quad(fx, fy, fz, height),
                                              tile_index(kFluidWaterBlockId, 0));
                        }
                        for (int direction = 0; direction < 4; ++direction) {
                            const float neighbour = fluid->fluid_height_at(world_x + kSideOffsets[direction][0], y,
                                                                           world_z + kSideOffsets[direction][1]);
                            if (neighbour >= height) {
                                continue; // level neighbour (or taller): no wall of ours
                            }
                            append_fluid_quad(mesh.fluid, make_side_quad(direction, fx, fy, fz, neighbour, height),
                                              tile_index(kFluidWaterBlockId, 1));
                        }
                        if (id == kFluidWaterBlockId) {
                            continue; // the fluid bucket already drew this voxel
                        }
                    }
                }

                if (id == 0) {
                    continue;
                }
                const bool translucent = is_translucent_block(id);
                MeshBucket &bucket = translucent ? mesh.translucent : mesh.opaque;

                for (const FaceDef &face : kFaces) {
                    if (translucent && face.dy < 0) {
                        continue; // translucent blocks skip their bottom face
                    }
                    const std::uint16_t neighbor = blocks.block_at(world_x + face.dx, y + face.dy, world_z + face.dz);
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
