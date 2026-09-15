#pragma once

#include <cstdint>
#include <vector>

namespace opencraft::render {

// Chunk geometry constants used by the mesher. Kept local to the render
// namespace (with a consistency assert in mesher.cpp) so the meshing pure
// function stays decoupled from the storage types in opencraft::voxel.
inline constexpr int kChunkSizeX = 16;
inline constexpr int kChunkSizeY = 384;
inline constexpr int kChunkSizeZ = 16;
// Side of one cubic storage section (mirrors voxel::Chunk::kSectionSize); the
// fluid span below is expressed in these bands.
inline constexpr int kSectionSize = 16;

// Position of the chunk being meshed, in chunk coordinates.
struct ChunkPos {
    int cx = 0;
    int cz = 0;
};

// Minimal read-only block accessor the mesher is fed with (task T005
// contract). Coordinates are *world* block coordinates so the implementation
// can transparently serve cross-chunk neighbor queries.
//
// Contract for implementations:
//   * Must be safe to call for any (wx, wy, wz), including coordinates far
//     outside the loaded world.
//   * Must return 0 (air) for unloaded chunks and for y outside [0, 384).
//     Returning air keeps the world edge visible; this is the documented
//     choice (out of the "solid vs air" alternatives, air was preferred so
//     borders are not silently swallowed).
//   * Must be stable for the duration of one build_chunk_mesh() call
//     (meshing snapshots the world through this interface).
class IBlockSource {
public:
    virtual ~IBlockSource() = default;

    [[nodiscard]] virtual std::uint16_t block_at(int wx, int wy, int wz) const = 0;
};

// Half-open vertical span of a chunk, counted in 16-block sections. An empty
// span (first == last) means "no fluid anywhere in this chunk".
struct FluidSpan {
    int first = 0;
    int last = 0;

    [[nodiscard]] bool empty() const { return first >= last; }
};

// Fluid surface provider (task T-F1, docs/research/10 §1.3). Returns the
// surface height of the fluid sitting in a cell: 0 means "no fluid here",
// otherwise 0 < h <= 1 gives the surface height above the cell floor. Same
// coordinate contract as IBlockSource: safe for any position, 0 for unloaded
// chunks and out-of-world y.
//
// A cell whose block layer holds water while this answers 0 is legacy static
// water (worldgen oceans predate the fluid layer); it still meshes as a full
// cube, so both representations coexist in one world.
class IFluidSource {
public:
    virtual ~IFluidSource() = default;

    [[nodiscard]] virtual float fluid_height_at(int wx, int wy, int wz) const = 0;

    // Sections of the chunk that can hold fluid, so the mesher can skip the
    // per-voxel fluid query outside them. Every cell of a fluid-free chunk
    // would otherwise cost a world-position lookup (measured 0.74 -> 6.25 ms
    // per chunk before this hook existed, against the docs/03 §10 5 ms
    // budget).
    [[nodiscard]] virtual FluidSpan fluid_span(int cx, int cz) const = 0;
};

// Packed mesh vertex, 10 bytes (task T005 contract: compact layout with a
// reserved light byte).
//
//   offset 0  u16 x    chunk-local position, 0..16 (face on the +side sits at 16)
//   offset 2  u16 y    chunk-local position, 0..384
//   offset 4  u16 z    chunk-local position, 0..16
//   offset 6  u16 tile atlas tile index, see tile_index() below
//   offset 8  u8  uv   bit 0: corner u (0/1), bit 1: corner v (0/1)
//   offset 9  u8  shade per-face brightness 0..255; reserved lighting byte,
//                      currently holding a simple directional face shade so
//                      terrain reads as 3D until T006 plugs in real lighting
struct MeshVertex {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t z = 0;
    std::uint16_t tile = 0;
    std::uint8_t uv = 0;
    std::uint8_t shade = 255;

    friend bool operator==(const MeshVertex &a, const MeshVertex &b) {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.tile == b.tile && a.uv == b.uv && a.shade == b.shade;
    }
};

static_assert(sizeof(MeshVertex) == 10, "MeshVertex must stay packed at 10 bytes");

// Fluid surface vertex (task T-F1). Separate from MeshVertex because a water
// surface sits at a fractional height while MeshVertex stores chunk-local
// positions as integers (the T005 contract); keeping them apart means the
// block geometry stays byte-identical. 16 bytes, same tile/uv/shade tail.
struct FluidVertex {
    float x = 0.0f; // chunk-local, may be fractional
    float y = 0.0f;
    float z = 0.0f;
    std::uint16_t tile = 0;
    std::uint8_t uv = 0;
    std::uint8_t shade = 255;

    friend bool operator==(const FluidVertex &a, const FluidVertex &b) {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.tile == b.tile && a.uv == b.uv && a.shade == b.shade;
    }
};

// One drawable layer of a chunk mesh: vertices + triangle indices.
struct MeshBucket {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;

    friend bool operator==(const MeshBucket &a, const MeshBucket &b) {
        return a.vertices == b.vertices && a.indices == b.indices;
    }
};

// Fluid surfaces, drawn in the translucent pass with the block translucent
// bucket (docs/research/03 §1.5).
struct FluidBucket {
    std::vector<FluidVertex> vertices;
    std::vector<std::uint32_t> indices;

    friend bool operator==(const FluidBucket &a, const FluidBucket &b) {
        return a.vertices == b.vertices && a.indices == b.indices;
    }
};

// Mesh of one chunk, split into an opaque and a translucent bucket
// (docs/research/03 §1.5: translucent water/glass is drawn in a separate
// pass; sorting happens per chunk at draw time, never per face), plus the
// fractional-height fluid surfaces of T-F1.
struct MeshData {
    MeshBucket opaque;
    MeshBucket translucent;
    FluidBucket fluid;

    friend bool operator==(const MeshData &a, const MeshData &b) {
        return a.opaque == b.opaque && a.translucent == b.translucent && a.fluid == b.fluid;
    }
};

// Atlas layout convention shared with the texture generator: every block owns
// three consecutive tiles in registry-id order.
//   tile index = block_id * 3 + slot,  slot: 0 = top face, 1 = side face, 2 = bottom face
[[nodiscard]] constexpr std::uint16_t tile_index(std::uint16_t block_id, int slot) {
    return static_cast<std::uint16_t>(block_id * 3 + slot);
}

// Classification used to route blocks into the translucent bucket and to
// decide face visibility. It mirrors the `transparent` flag of
// BlockRegistry::create_default() (air excepted); test_mesher.cpp guards the
// two stay in sync. Water and glass therefore mesh into the translucent
// bucket, everything else into the opaque one.
[[nodiscard]] bool is_translucent_block(std::uint16_t block_id);

// Culled mesher (docs/03 §4, docs/research/03 §1.1-1.2): emits only the faces
// of blocks that touch air or a translucent block of a different type. A face
// against an identical translucent block (water-water, glass-glass) is culled;
// a face of any block against water/glass is kept so the ground under water
// is rendered.
//
// Pure function: the result depends only on the block data served by
// `blocks`, no global state is read or written, safe to call from any thread.
//
// Per task T005 scope this is culled meshing only - no greedy merging, no AO,
// no light sampling. Water emits its top and side faces; the bottom face of
// translucent blocks is skipped (docs/research/03 §1.5) since water always
// rests on solid ground in practice.
//
// T-F1: when `fluid` is supplied, cells whose fluid surface is above the floor
// are meshed as fractional-height water instead of full cubes (the fluid
// layer takes precedence over the block placeholder), and neighbouring blocks
// keep their faces against such a cell as before. Cells without fluid - every
// chunk in the T005 tests, and legacy worldgen oceans - mesh exactly as they
// did before.
[[nodiscard]] MeshData build_chunk_mesh(const IBlockSource &blocks, ChunkPos pos, const IFluidSource *fluid = nullptr);

} // namespace opencraft::render
