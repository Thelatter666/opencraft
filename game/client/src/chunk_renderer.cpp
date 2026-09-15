#include "chunk_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>

#include "world.hpp"

namespace opencraft::client {

namespace {

// Squared distance from the eye to a chunk centre; orders both passes. Kept as
// a named lambda so the pre-split definition moves over unchanged (it used to
// be one lambda captured by both sorts).
const auto distance_sq = [](const ChunkRenderable &r, const glm::vec3 &eye_f) {
    return glm::dot(r.center - eye_f, r.center - eye_f);
};

} // namespace

ChunkLayer upload_layer(const render::MeshBucket &bucket) {
    auto vao = render::VertexArray();
    vao.bind();
    auto vbo = render::Buffer(render::Buffer::Target::Vertex, bucket.vertices.data(),
                              bucket.vertices.size() * sizeof(render::MeshVertex), render::Buffer::Usage::Static);
    vbo.bind();
    constexpr std::size_t kStride = sizeof(render::MeshVertex);
    vao.set_attribute(0, 3, GL_UNSIGNED_SHORT, kStride, offsetof(render::MeshVertex, x));
    vao.set_attribute(1, 1, GL_UNSIGNED_SHORT, kStride, offsetof(render::MeshVertex, tile));
    vao.set_attribute(2, 1, GL_UNSIGNED_BYTE, kStride, offsetof(render::MeshVertex, uv));
    vao.set_attribute(3, 1, GL_UNSIGNED_BYTE, kStride, offsetof(render::MeshVertex, shade));
    auto ebo = render::Buffer(render::Buffer::Target::Index, bucket.indices.data(),
                              bucket.indices.size() * sizeof(std::uint32_t), render::Buffer::Usage::Static);
    ebo.bind(); // index buffer is captured by the bound VAO
    return ChunkLayer(std::move(vao), std::move(vbo), std::move(ebo), static_cast<GLsizei>(bucket.indices.size()));
}

ChunkLayer upload_fluid_layer(const render::FluidBucket &bucket) {
    auto vao = render::VertexArray();
    vao.bind();
    auto vbo = render::Buffer(render::Buffer::Target::Vertex, bucket.vertices.data(),
                              bucket.vertices.size() * sizeof(render::FluidVertex), render::Buffer::Usage::Static);
    vbo.bind();
    constexpr std::size_t kStride = sizeof(render::FluidVertex);
    // Float positions: a water surface sits at a fractional height.
    vao.set_attribute(0, 3, GL_FLOAT, kStride, offsetof(render::FluidVertex, x));
    vao.set_attribute(1, 1, GL_UNSIGNED_SHORT, kStride, offsetof(render::FluidVertex, tile));
    vao.set_attribute(2, 1, GL_UNSIGNED_BYTE, kStride, offsetof(render::FluidVertex, uv));
    vao.set_attribute(3, 1, GL_UNSIGNED_BYTE, kStride, offsetof(render::FluidVertex, shade));
    auto ebo = render::Buffer(render::Buffer::Target::Index, bucket.indices.data(),
                              bucket.indices.size() * sizeof(std::uint32_t), render::Buffer::Usage::Static);
    ebo.bind();
    return ChunkLayer(std::move(vao), std::move(vbo), std::move(ebo), static_cast<GLsizei>(bucket.indices.size()));
}

std::int64_t chunk_key(int cx, int cz) {
    const auto ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx));
    const auto uz = static_cast<std::uint32_t>(cz);
    return static_cast<std::int64_t>((ux << 32) | uz);
}

void mesh_chunk(ChunkRenderableMap &renderables, const WorldSource &world, int cx, int cz, double &last_mesh_ms) {
    const auto t0 = std::chrono::steady_clock::now();
    render::MeshData mesh = render::build_chunk_mesh(world, render::ChunkPos{cx, cz}, &world);
    opencraft::client::shade_mesh_with_light(mesh, world, cx, cz);
    last_mesh_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    ChunkRenderable r;
    r.pos = render::ChunkPos{cx, cz};
    r.center = glm::vec3(static_cast<float>(cx) * 16.0f + 8.0f, 8.0f, static_cast<float>(cz) * 16.0f + 8.0f);
    if (!mesh.opaque.indices.empty()) {
        r.opaque.emplace(upload_layer(mesh.opaque));
    }
    if (!mesh.translucent.indices.empty()) {
        r.translucent.emplace(upload_layer(mesh.translucent));
    }
    if (!mesh.fluid.indices.empty()) {
        r.fluid.emplace(upload_fluid_layer(mesh.fluid));
    }
    renderables[chunk_key(cx, cz)] = std::move(r);
}

void draw_chunk_opaque_pass(const ChunkRenderableMap &renderables, const render::Shader &shader, const glm::vec3 &eye_f,
                            std::vector<const ChunkRenderable *> &order) {
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    order.reserve(renderables.size());
    for (const auto &entry : renderables) {
        if (entry.second.opaque) {
            order.push_back(&entry.second);
        }
    }
    std::sort(order.begin(), order.end(), [&](const ChunkRenderable *a, const ChunkRenderable *b) {
        return distance_sq(*a, eye_f) < distance_sq(*b, eye_f);
    });
    for (const ChunkRenderable *r : order) {
        const auto [cx, cz] = r->pos;
        glUniform3f(shader.uniform_location("u_chunk_origin"), static_cast<float>(cx) * 16.0f, 0.0f,
                    static_cast<float>(cz) * 16.0f);
        r->opaque->vao.bind();
        glDrawElements(GL_TRIANGLES, r->opaque->index_count, GL_UNSIGNED_INT, nullptr);
    }
}

void draw_chunk_translucent_pass(const ChunkRenderableMap &renderables, const render::Shader &shader,
                                 const glm::vec3 &eye_f, std::vector<const ChunkRenderable *> &order) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    order.clear();
    for (const auto &entry : renderables) {
        if (entry.second.translucent || entry.second.fluid) {
            order.push_back(&entry.second);
        }
    }
    std::sort(order.begin(), order.end(), [&](const ChunkRenderable *a, const ChunkRenderable *b) {
        return distance_sq(*a, eye_f) > distance_sq(*b, eye_f);
    });
    for (const ChunkRenderable *r : order) {
        if (!r->translucent) {
            continue;
        }
        const auto [cx, cz] = r->pos;
        glUniform3f(shader.uniform_location("u_chunk_origin"), static_cast<float>(cx) * 16.0f, 0.0f,
                    static_cast<float>(cz) * 16.0f);
        r->translucent->vao.bind();
        glDrawElements(GL_TRIANGLES, r->translucent->index_count, GL_UNSIGNED_INT, nullptr);
    }
    // Water surfaces ride in the same pass with the same shader; only the
    // vertex layout differs (float heights).
    for (const ChunkRenderable *r : order) {
        if (!r->fluid) {
            continue;
        }
        const auto [cx, cz] = r->pos;
        glUniform3f(shader.uniform_location("u_chunk_origin"), static_cast<float>(cx) * 16.0f, 0.0f,
                    static_cast<float>(cz) * 16.0f);
        r->fluid->vao.bind();
        glDrawElements(GL_TRIANGLES, r->fluid->index_count, GL_UNSIGNED_INT, nullptr);
    }
    glEnable(GL_CULL_FACE);
}

} // namespace opencraft::client
