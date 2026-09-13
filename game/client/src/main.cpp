#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "atlas.hpp"
#include "opencraft/core/log.hpp"
#include "opencraft/core/version.hpp"
#include "opencraft/render/mesher.hpp"
#include "opencraft/render/rhi.hpp"
#include "terrain.hpp"

namespace render = opencraft::render; // short alias used by the GPU glue below

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

// GLSL 4.10 core (macOS GL ceiling). The vertex format is the packed
// MeshVertex from opencraft/render/mesher.hpp: u16 x3 position, u16 tile,
// u8 uv (bit0 u, bit1 v), u8 shade.
constexpr char kVertexShader[] = R"(
#version 410 core
layout(location=0) in vec3 a_pos;
layout(location=1) in float a_tile;
layout(location=2) in float a_uv;
layout(location=3) in float a_shade;

uniform mat4 u_mvp;
uniform vec3 u_chunk_origin;
uniform float u_tiles_per_row;
uniform float u_texel; // 1 / atlas_width_px

out vec2 v_uv;
out float v_shade;

void main() {
    gl_Position = u_mvp * vec4(a_pos + u_chunk_origin, 1.0);
    float corner_u = mod(a_uv, 2.0);
    float corner_v = mod(floor(a_uv * 0.5), 2.0);
    float tx = mod(a_tile, u_tiles_per_row);
    float ty = floor(a_tile / u_tiles_per_row);
    // 0.5px inset per tile edge prevents atlas bleeding (docs/03 §4).
    vec2 inset = vec2(0.5 * u_texel);
    vec2 corner = vec2(corner_u, corner_v);
    v_uv = (vec2(tx, ty) + mix(inset, vec2(1.0) - inset, corner)) * (16.0 * u_texel);
    v_shade = a_shade / 255.0;
}
)";

constexpr char kFragmentShader[] = R"(
#version 410 core
uniform sampler2D u_atlas;

in vec2 v_uv;
in float v_shade;

out vec4 frag_color;

void main() {
    vec4 texel = texture(u_atlas, v_uv);
    frag_color = vec4(texel.rgb * v_shade, texel.a);
}
)";

void error_callback(int error_code, const char *description) {
    OC_LOG_ERROR("GLFW error {}: {}", error_code, description);
}

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
    glm::vec3 center; // chunk center in world space, for draw sorting
};

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

} // namespace

int main() {
    opencraft::core::log::init();
    OC_LOG_INFO("{} starting", opencraft::core::version_string());

    glfwSetErrorCallback(error_callback);
    if (glfwInit() != GLFW_TRUE) {
        OC_LOG_CRITICAL("glfwInit failed");
        return 1;
    }

    // 4.1 core is the highest portable floor (macOS tops out at 4.1).
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(kWindowWidth, kWindowHeight, "OpenCraft", nullptr, nullptr);
    if (window == nullptr) {
        OC_LOG_CRITICAL("glfwCreateWindow failed");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    const int gl_version = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
    if (gl_version < GLAD_MAKE_VERSION(4, 1)) {
        OC_LOG_CRITICAL("OpenGL 4.1 not available (got {:x})", gl_version);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    OC_LOG_INFO("GL {} / glad loaded", reinterpret_cast<const char *>(glGetString(GL_VERSION)));

    // --- world -----------------------------------------------------------------
    opencraft::client::TestTerrain terrain;
    opencraft::client::ChunkSource source(terrain.chunks());

    // --- atlas -------------------------------------------------------------------
    const opencraft::client::AtlasImage atlas_image = opencraft::client::generate_atlas(terrain.registry());
    const render::Texture2D atlas(atlas_image.width, atlas_image.height, atlas_image.pixels.data());
    const float tiles_per_row = static_cast<float>(atlas_image.tiles_per_row);
    const float texel = 1.0f / static_cast<float>(atlas_image.width);
    OC_LOG_INFO("atlas: {}x{} px, {} tiles/row", atlas_image.width, atlas_image.height, atlas_image.tiles_per_row);

    // --- meshing -----------------------------------------------------------------
    std::vector<ChunkRenderable> renderables;
    double total_mesh_ms = 0.0;
    double max_mesh_ms = 0.0;
    std::size_t total_quads = 0;
    for (int cx = -opencraft::client::TestTerrain::kRadius; cx <= opencraft::client::TestTerrain::kRadius; ++cx) {
        for (int cz = -opencraft::client::TestTerrain::kRadius; cz <= opencraft::client::TestTerrain::kRadius; ++cz) {
            const render::ChunkPos pos{cx, cz};
            const auto t0 = std::chrono::steady_clock::now();
            const render::MeshData mesh = render::build_chunk_mesh(source, pos);
            const double mesh_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            total_mesh_ms += mesh_ms;
            max_mesh_ms = std::max(max_mesh_ms, mesh_ms);

            ChunkRenderable r;
            r.pos = pos;
            r.center = glm::vec3(static_cast<float>(cx) * 16.0f + 8.0f, 8.0f, static_cast<float>(cz) * 16.0f + 8.0f);
            if (!mesh.opaque.indices.empty()) {
                total_quads += mesh.opaque.indices.size() / 6;
                r.opaque.emplace(upload_layer(mesh.opaque));
            }
            if (!mesh.translucent.indices.empty()) {
                total_quads += mesh.translucent.indices.size() / 6;
                r.translucent.emplace(upload_layer(mesh.translucent));
            }
            renderables.push_back(std::move(r));
        }
    }
    OC_LOG_INFO("meshed {} chunks: total {:.1f} ms, avg {:.2f} ms/chunk, max {:.2f} ms/chunk, {} quads",
                renderables.size(), total_mesh_ms, total_mesh_ms / static_cast<double>(renderables.size()), max_mesh_ms,
                total_quads);

    // --- GL state ----------------------------------------------------------------
    int fb_width = 0;
    int fb_height = 0;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    glViewport(0, 0, fb_width, fb_height);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.52f, 0.62f, 0.72f, 1.0f);

    const render::Shader shader(kVertexShader, kFragmentShader);
    shader.use();
    glUniform1i(shader.uniform_location("u_atlas"), 0);
    glUniform1f(shader.uniform_location("u_tiles_per_row"), tiles_per_row);
    glUniform1f(shader.uniform_location("u_texel"), texel);
    atlas.bind(0);

    // --- camera ------------------------------------------------------------------
    // Temporary demo orbit camera: auto-rotates around the terrain center;
    // dragging with the left mouse button takes over until released (the
    // real first-person controller arrives with T008).
    const glm::vec3 target(0.0f, 10.0f, 0.0f);
    float azimuth = 0.6f;
    float elevation = 0.55f;
    const float radius = 62.0f;
    bool dragging = false;
    double last_cursor_x = 0.0;
    double last_cursor_y = 0.0;
    glfwSetMouseButtonCallback(window, [](GLFWwindow *w, int button, int action, int) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            glfwSetWindowUserPointer(w, reinterpret_cast<void *>(static_cast<intptr_t>(action == GLFW_PRESS)));
        }
    });

    double last_frame = glfwGetTime();
    double fps_timer = last_frame;
    int fps_frames = 0;

    while (glfwWindowShouldClose(window) != GLFW_TRUE) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        const double now = glfwGetTime();
        const double dt = now - last_frame;
        last_frame = now;

        // Drag-to-orbit: read cursor delta while the left button is held.
        const bool pressed = glfwGetWindowUserPointer(window) != nullptr;
        if (pressed && !dragging) {
            dragging = true;
            glfwGetCursorPos(window, &last_cursor_x, &last_cursor_y);
        } else if (!pressed && dragging) {
            dragging = false;
        } else if (dragging) {
            double x = 0.0;
            double y = 0.0;
            glfwGetCursorPos(window, &x, &y);
            azimuth += static_cast<float>((x - last_cursor_x) * 0.005);
            elevation = std::clamp(elevation + static_cast<float>((y - last_cursor_y) * 0.005), 0.08f, 1.45f);
            last_cursor_x = x;
            last_cursor_y = y;
        }
        if (!dragging) {
            azimuth += static_cast<float>(dt * 0.15); // slow auto orbit
        }

        const float cos_e = std::cos(elevation);
        const glm::vec3 eye =
            target + radius * glm::vec3(cos_e * std::sin(azimuth), std::sin(elevation), cos_e * std::cos(azimuth));
        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(
            glm::radians(50.0f), static_cast<float>(fb_width) / static_cast<float>(fb_height), 0.5f, 500.0f);
        const glm::mat4 mvp = projection * view;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shader.use();
        glUniformMatrix4fv(shader.uniform_location("u_mvp"), 1, GL_FALSE, &mvp[0][0]);

        auto distance_sq = [&](const ChunkRenderable &r) { return glm::dot(r.center - eye, r.center - eye); };

        // Opaque pass: near -> far (early-Z friendly), depth writes on.
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        std::vector<const ChunkRenderable *> order;
        order.reserve(renderables.size());
        for (const auto &r : renderables) {
            if (r.opaque) {
                order.push_back(&r);
            }
        }
        std::sort(order.begin(), order.end(), [&](const ChunkRenderable *a, const ChunkRenderable *b) {
            return distance_sq(*a) < distance_sq(*b);
        });
        for (const ChunkRenderable *r : order) {
            const auto [cx, cz] = r->pos;
            glUniform3f(shader.uniform_location("u_chunk_origin"), static_cast<float>(cx) * 16.0f, 0.0f,
                        static_cast<float>(cz) * 16.0f);
            r->opaque->vao.bind();
            glDrawElements(GL_TRIANGLES, r->opaque->index_count, GL_UNSIGNED_INT, nullptr);
        }

        // Translucent pass (water/leaves/glass): far -> near, no depth writes,
        // double-sided (docs/research/03 §1.5; per-chunk sorting only).
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        order.clear();
        for (const auto &r : renderables) {
            if (r.translucent) {
                order.push_back(&r);
            }
        }
        std::sort(order.begin(), order.end(), [&](const ChunkRenderable *a, const ChunkRenderable *b) {
            return distance_sq(*a) > distance_sq(*b);
        });
        for (const ChunkRenderable *r : order) {
            const auto [cx, cz] = r->pos;
            glUniform3f(shader.uniform_location("u_chunk_origin"), static_cast<float>(cx) * 16.0f, 0.0f,
                        static_cast<float>(cz) * 16.0f);
            r->translucent->vao.bind();
            glDrawElements(GL_TRIANGLES, r->translucent->index_count, GL_UNSIGNED_INT, nullptr);
        }
        glDepthMask(GL_TRUE);

        glfwSwapBuffers(window);

        ++fps_frames;
        if (now - fps_timer >= 2.0) {
            OC_LOG_INFO("fps {:.1f} | eye ({:.0f}, {:.0f}, {:.0f})",
                        static_cast<double>(fps_frames) / (now - fps_timer), eye.x, eye.y, eye.z);
            fps_frames = 0;
            fps_timer = now;
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    OC_LOG_INFO("clean shutdown");
    return 0;
}
