#pragma once

// Chunk geometry lifetime and the two chunk draw passes. Moved verbatim out of
// main.cpp by T-M1 (pure code motion): the upload paths, the draw order and the
// GL state each pass sets are unchanged; only the enclosing scope moved from
// main()'s anonymous namespace to the client namespace.

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "opencraft/render/mesher.hpp"
#include "opencraft/render/rhi.hpp"

namespace opencraft::client {

class WorldSource;

// GPU-side mesh of one chunk layer (opaque or translucent).
struct ChunkLayer {
    render::VertexArray vao;
    render::Buffer vbo;
    render::Buffer ebo;
    GLsizei index_count = 0;

    ChunkLayer(render::VertexArray &&vao_, render::Buffer &&vbo_, render::Buffer &&ebo_, GLsizei count)
        : vao(std::move(vao_)), vbo(std::move(vbo_)), ebo(std::move(ebo_)), index_count(count) {}
};

// Per-chunk renderable: nullopt when the bucket has no geometry.
struct ChunkRenderable {
    render::ChunkPos pos;
    std::optional<ChunkLayer> opaque;
    std::optional<ChunkLayer> translucent;
    // T-F1 water surfaces: fractional-height fluid quads, drawn in the same
    // pass as the translucent bucket (float positions, so its own layer).
    std::optional<ChunkLayer> fluid;
    glm::vec3 center; // chunk center in world space, for draw sorting
};

using ChunkRenderableMap = std::unordered_map<std::int64_t, ChunkRenderable>;

ChunkLayer upload_layer(const render::MeshBucket &bucket);
ChunkLayer upload_fluid_layer(const render::FluidBucket &bucket);

[[nodiscard]] std::int64_t chunk_key(int cx, int cz);

// Meshes one chunk on the CPU, uploads whichever buckets came back non-empty
// and stores the result in `renderables` (replacing any previous entry).
// `last_mesh_ms` receives the CPU-side meshing time for the frame log.
void mesh_chunk(ChunkRenderableMap &renderables, const WorldSource &world, int cx, int cz, double &last_mesh_ms);

// Opaque pass: near -> far (early-Z friendly), depth writes on. `order` is the
// caller-owned scratch buffer (reused between the two passes, as before).
void draw_chunk_opaque_pass(const ChunkRenderableMap &renderables, const render::Shader &shader, const glm::vec3 &eye_f,
                            std::vector<const ChunkRenderable *> &order);

// Translucent pass (water/leaves/glass): far -> near, no depth writes,
// double-sided (docs/research/03 §1.5; per-chunk sorting only). Water surfaces
// ride in the same pass with the same shader; only the vertex layout differs.
void draw_chunk_translucent_pass(const ChunkRenderableMap &renderables, const render::Shader &shader,
                                 const glm::vec3 &eye_f, std::vector<const ChunkRenderable *> &order);

} // namespace opencraft::client
