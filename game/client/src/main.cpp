#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "atlas.hpp"
#include "camera_spring.hpp"
#include "fov.hpp"
#include "opencraft/core/log.hpp"
#include "opencraft/core/tick_clock.hpp"
#include "opencraft/core/version.hpp"
#include "opencraft/game/mining.hpp"
#include "opencraft/game/placement.hpp"
#include "opencraft/game/raycast.hpp"
#include "opencraft/physics/auto_jump.hpp"
#include "opencraft/physics/input_state.hpp"
#include "opencraft/physics/player_physics.hpp"
#include "opencraft/physics/player_state.hpp"
#include "opencraft/render/mesher.hpp"
#include "opencraft/render/rhi.hpp"
#include "opencraft/storage/level_file.hpp"
#include "opencraft/storage/world_save.hpp"
#include "world.hpp"

namespace render = opencraft::render; // short alias used by the GPU glue below
namespace phy = opencraft::physics;
namespace gam = opencraft::game;
namespace client = opencraft::client;

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

// ── view / interaction constants ────────────────────────────────────────────
constexpr double kMouseSensitivity = 0.0025;
constexpr double kMaxPitch = 1.5533;   // ~89 degrees
constexpr double kReachDistance = 4.5; // ⚖ docs/01 §4: survival block reach
// Base FOV and sprint multiplier live in fov.hpp (T-D1, unit-tested).
constexpr int kViewRadius = 6;      // meshed chunk radius around the player
constexpr int kGenPerFrame = 2;     // sync-generation budget (docs: <= 2/frame)
constexpr int kNewMeshPerFrame = 4; // new-chunk meshing budget/frame

constexpr double kEyeStanding = 1.62;
constexpr double kEyeSneaking = 1.27;

// ── shaders ─────────────────────────────────────────────────────────────────
// Terrain shader: unchanged from T005 except that the shade byte now carries
// the sampled light (max(sky, block)/15 folded into the face shade).
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

// Flat-colored lines in clip space (selection wireframe, crosshair).
constexpr char kWireVertexShader[] = R"(
#version 410 core
layout(location=0) in vec3 a_pos;

uniform mat4 u_mvp;
uniform vec3 u_offset;
uniform float u_scale;

void main() {
    gl_Position = u_mvp * vec4(a_pos * u_scale + u_offset, 1.0);
}
)";

constexpr char kWireFragmentShader[] = R"(
#version 410 core
uniform vec4 u_color;
out vec4 frag_color;
void main() { frag_color = u_color; }
)";

// Crack overlay cube: unit cube with per-vertex uv; the 10 destruction stages
// share one buffer, the stage tile index comes in as a uniform.
constexpr char kCrackVertexShader[] = R"(
#version 410 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_uv;

uniform mat4 u_mvp;
uniform vec3 u_offset;
uniform float u_scale;
uniform float u_tile;
uniform float u_tiles_per_row;
uniform float u_texel;

out vec2 v_uv;

void main() {
    gl_Position = u_mvp * vec4(a_pos * u_scale + u_offset, 1.0);
    float tx = mod(u_tile, u_tiles_per_row);
    float ty = floor(u_tile / u_tiles_per_row);
    vec2 inset = vec2(0.5 * u_texel);
    v_uv = (vec2(tx, ty) + mix(inset, vec2(1.0) - inset, a_uv)) * (16.0 * u_texel);
}
)";

constexpr char kCrackFragmentShader[] = R"(
#version 410 core
uniform sampler2D u_atlas;
in vec2 v_uv;
out vec4 frag_color;
void main() {
    vec4 texel = texture(u_atlas, v_uv);
    if (texel.a < 0.5) { discard; }
    frag_color = vec4(texel.rgb, texel.a);
}
)";

// Break particles: world-space GL points colored per block (T009).
constexpr char kParticleVertexShader[] = R"(
#version 410 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec4 a_color;
uniform mat4 u_mvp;
uniform float u_point_px;
out vec4 v_color;
void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    gl_PointSize = u_point_px;
    v_color = a_color;
}
)";

constexpr char kParticleFragmentShader[] = R"(
#version 410 core
in vec4 v_color;
out vec4 frag_color;
void main() { frag_color = v_color; }
)";

// UI: flat quads (backdrop, buttons) and bitmap-font text.
constexpr char kUiFlatVertexShader[] = R"(
#version 410 core
layout(location=0) in vec2 a_pos;
uniform vec4 u_color;
out vec4 v_color;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_color = u_color;
}
)";

constexpr char kUiFlatFragmentShader[] = R"(
#version 410 core
in vec4 v_color;
out vec4 frag_color;
void main() { frag_color = v_color; }
)";

constexpr char kUiTextVertexShader[] = R"(
#version 410 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
uniform vec4 u_color;
out vec2 v_uv;
out vec4 v_color;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
    v_color = u_color;
}
)";

constexpr char kUiTextFragmentShader[] = R"(
#version 410 core
uniform sampler2D u_font;
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
void main() {
    if (texture(u_font, v_uv).a < 0.5) { discard; }
    frag_color = vec4(v_color.rgb, v_color.a);
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
    // T-F1 water surfaces: fractional-height fluid quads, drawn in the same
    // pass as the translucent bucket (float positions, so its own layer).
    std::optional<ChunkLayer> fluid;
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

[[nodiscard]] std::int64_t chunk_key(int cx, int cz) {
    const auto ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx));
    const auto uz = static_cast<std::uint32_t>(cz);
    return static_cast<std::int64_t>((ux << 32) | uz);
}

// Unit cube geometry for the overlays: 12 edges (wireframe) and 6 quads with
// uv corners (crack overlay), all in [0,1]^3.
struct CubeGeometry {
    std::array<glm::vec3, 24> edge_vertices;
    std::array<glm::vec3, 36> face_vertices;
    std::array<glm::vec2, 36> face_uv;
};

CubeGeometry build_cube_geometry() {
    CubeGeometry geo;
    const glm::vec3 c[8] = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}, {0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}};
    static constexpr int kEdges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                          {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int e = 0; e < 12; ++e) {
        geo.edge_vertices[e * 2] = c[kEdges[e][0]];
        geo.edge_vertices[e * 2 + 1] = c[kEdges[e][1]];
    }
    static constexpr int kFaces[6][4] = {{4, 5, 6, 7}, {0, 3, 2, 1}, {1, 5, 6, 2},
                                         {3, 7, 6, 2}, {0, 4, 7, 3}, {0, 1, 5, 4}};
    static constexpr glm::vec2 kQuadUv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    // Two triangles per face (non-indexed): 6 faces x 6 verts = 36, matching
    // the glDrawArrays(GL_TRIANGLES, 0, 36) crack overlay call.
    for (int f = 0; f < 6; ++f) {
        static constexpr int kTriOrder[6] = {0, 1, 2, 0, 2, 3};
        for (int v = 0; v < 6; ++v) {
            geo.face_vertices[f * 6 + v] = c[kFaces[f][kTriOrder[v]]];
            geo.face_uv[f * 6 + v] = kQuadUv[kTriOrder[v]];
        }
    }
    return geo;
}

// ── original 5x7 bitmap font (T009: full A-Z, digits, punctuation, hearts) ──
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
// \x01 full heart, \x02 half heart, \x03 empty heart (health bar glyphs).
constexpr const char *kFontChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-\x01\x02\x03";
constexpr std::array<std::uint8_t, kGlyphHeight> kGlyphs[] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // B
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // C
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // D
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, // G
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // H
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // I
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // N
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // O
    {0x0E, 0x11, 0x11, 0x0F, 0x10, 0x10, 0x10}, // P
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // Q
    {0x0E, 0x11, 0x11, 0x0E, 0x12, 0x12, 0x11}, // R
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // S
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, // W
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
    {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, // Y
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, // Z
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}, // 2
    {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
    {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}, // :
    {0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00}, // -
    {0x0A, 0x1F, 0x1F, 0x1F, 0x0E, 0x04, 0x00}, // \x01 full heart
    {0x08, 0x1C, 0x1C, 0x1C, 0x0C, 0x04, 0x00}, // \x02 half heart
    {0x0A, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00}, // \x03 empty heart
};

struct FontImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels; // RGBA8 like AtlasImage
};

FontImage build_font_texture() {
    FontImage font;
    const int count = static_cast<int>(std::size(kGlyphs));
    font.width = count * (kGlyphWidth + 1);
    font.height = kGlyphHeight;
    font.pixels.assign(static_cast<std::size_t>(font.width) * font.height, 0);
    for (int g = 0; g < count; ++g) {
        for (int row = 0; row < kGlyphHeight; ++row) {
            for (int col = 0; col < kGlyphWidth; ++col) {
                if ((kGlyphs[g][row] >> (kGlyphWidth - 1 - col)) & 1) {
                    font.pixels[static_cast<std::size_t>(row * font.width + g * (kGlyphWidth + 1) + col)] =
                        0xFF000000; // opaque (rgb unused; the shader colors via uniform)
                }
            }
        }
    }
    return font;
}

// Appends one textured string (screen pixels in, y = top, NDC quads out).
// Quads are emitted counter-clockwise in NDC (y up) so they are front-facing
// under the default GL_CCW/M_LESS state - the HUD draws text with back-face
// culling still enabled (T009 fix: CW quads lost their upper triangle there).
void draw_text(const std::string &text, float x_px, float y_px, float px_height, int fb_w, int fb_h,
               std::vector<glm::vec2> &verts, std::vector<glm::vec2> &uvs) {
    const auto to_ndc = [&](float x, float y) {
        return glm::vec2((x / static_cast<float>(fb_w)) * 2.0f - 1.0f, 1.0f - (y / static_cast<float>(fb_h)) * 2.0f);
    };
    const float scale = px_height / static_cast<float>(kGlyphHeight);
    const int count = static_cast<int>(std::size(kGlyphs));
    const float u_span = 1.0f / static_cast<float>(count * (kGlyphWidth + 1));
    float cursor = x_px;
    for (char ch : text) {
        int glyph = -1;
        for (int g = 0; g < count; ++g) {
            if (kFontChars[g] == ch) {
                glyph = g;
                break;
            }
        }
        if (glyph < 0) {
            cursor += 3.0f * scale;
            continue;
        }
        const glm::vec2 p0 = to_ndc(cursor, y_px);                                                       // top-left
        const glm::vec2 p1 = to_ndc(cursor + static_cast<float>(kGlyphWidth) * scale, y_px + px_height); // bottom-right
        const float u0 = static_cast<float>(glyph) * (kGlyphWidth + 1) * u_span;
        const float u1 = u0 + static_cast<float>(kGlyphWidth) * u_span;
        // Texture v=0 is the FIRST uploaded row = the glyph's top row.
        // CCW order: (tl, bl, br) then (tl, br, tr).
        verts.insert(verts.end(), {p0, {p0.x, p1.y}, p1, p0, p1, {p1.x, p0.y}});
        uvs.insert(uvs.end(), {{u0, 0.0f}, {u0, 1.0f}, {u1, 1.0f}, {u0, 0.0f}, {u1, 1.0f}, {u1, 0.0f}});
        cursor += static_cast<float>(kGlyphWidth + 2) * scale;
    }
}

// Appends one flat NDC quad from a screen-pixel rect. CCW winding, see
// draw_text.
void draw_rect(float x0, float y0, float x1, float y1, int fb_w, int fb_h, std::vector<glm::vec2> &verts) {
    const auto to_ndc = [&](float x, float y) {
        return glm::vec2((x / static_cast<float>(fb_w)) * 2.0f - 1.0f, 1.0f - (y / static_cast<float>(fb_h)) * 2.0f);
    };
    const glm::vec2 a = to_ndc(x0, y0); // top-left
    const glm::vec2 b = to_ndc(x1, y1); // bottom-right
    verts.insert(verts.end(), {a, {a.x, b.y}, b, a, b, {b.x, a.y}});
}

// Average color of each block's side tile (slot 1) - the "main color" used
// by break particles (T009). Returns one RGB triple per registry id.
std::vector<glm::vec3> block_main_colors(const opencraft::client::AtlasImage &atlas, std::size_t registry_size) {
    std::vector<glm::vec3> colors(registry_size, glm::vec3(0.7f));
    const int tile_px = 16;
    for (std::size_t id = 0; id < registry_size; ++id) {
        const int tile = static_cast<int>(id) * 3 + 1; // side tile (mesher convention)
        const int tx = (tile % atlas.tiles_per_row) * tile_px;
        const int ty = (tile / atlas.tiles_per_row) * tile_px;
        glm::vec3 sum(0.0f);
        int count = 0;
        for (int y = 0; y < tile_px; ++y) {
            for (int x = 0; x < tile_px; ++x) {
                const std::uint32_t pixel = atlas.pixels[static_cast<std::size_t>((ty + y) * atlas.width + tx + x)];
                const float alpha = static_cast<float>((pixel >> 24) & 0xFF);
                if (alpha < 128.0f) {
                    continue;
                }
                sum += glm::vec3(static_cast<float>(pixel & 0xFF), static_cast<float>((pixel >> 8) & 0xFF),
                                 static_cast<float>((pixel >> 16) & 0xFF)) /
                       255.0f;
                ++count;
            }
        }
        if (count > 0) {
            colors[id] = sum / static_cast<float>(count);
        }
    }
    return colors;
}

// ── break particles (T009) ───────────────────────────────────────────────────
struct Particle {
    glm::vec3 pos;
    glm::vec3 vel;
    float life;      // seconds until removal
    glm::vec4 color; // rgb + current alpha (fades with life)
};

constexpr int kParticlesPerBreak = 20;
constexpr std::size_t kMaxParticles = 256;

void update_particles(std::vector<Particle> &particles, float dt) {
    constexpr float kGravity = 13.0f; // blocks/s^2, snappier than real g for feel
    for (Particle &p : particles) {
        p.vel.y -= kGravity * dt;
        p.pos += p.vel * dt;
        p.life -= dt;
        p.color.a = std::clamp(p.life / 0.5f, 0.0f, 1.0f);
    }
    particles.erase(
        std::remove_if(particles.begin(), particles.end(), [](const Particle &p) { return p.life <= 0.0f; }),
        particles.end());
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

    // ── save + level ────────────────────────────────────────────────────────
    // World directory fixed to "world" (T009 contract); saves/ is relative to
    // the working directory (./build/opencraft -> build/saves/world).
    opencraft::storage::WorldSave save("saves", "world");
    const std::optional<opencraft::storage::LevelData> stored_level = save.try_read_level();
    const std::uint64_t world_seed =
        stored_level.has_value() ? stored_level->seed : opencraft::client::WorldSource::kSeed;
    std::uint64_t game_ticks = stored_level.has_value() ? stored_level->tick_count : 0;
    if (stored_level.has_value()) {
        OC_LOG_INFO("save: loaded level.ocd (seed={:#x}, ticks={}, player=({:.2f}, {:.2f}, {:.2f}), hp={:.1f})",
                    stored_level->seed, stored_level->tick_count, stored_level->player_x, stored_level->player_y,
                    stored_level->player_z, stored_level->health);
    } else {
        OC_LOG_INFO("save: no level.ocd, new world with seed {:#x}", world_seed);
    }

    // ── world ───────────────────────────────────────────────────────────────
    opencraft::client::WorldSource world(world_seed);
    world.attach_save(&save);

    // Spawn: generate the center chunk first, then scan 5x5 surface columns
    // (T009 card item) - or reuse the persisted player position.
    static_cast<void>(world.ensure_chunk(0, 0));
    const glm::dvec3 spawn_pos = [&] {
        if (stored_level.has_value() && stored_level->has_player) {
            return glm::dvec3(stored_level->player_x, stored_level->player_y, stored_level->player_z);
        }
        const glm::dvec3 scanned = world.find_spawn();
        OC_LOG_INFO("spawn scan: surface at ({:.1f}, {:.1f}, {:.1f})", scanned.x, scanned.y, scanned.z);
        return scanned;
    }();

    // Startup burst: 5x5 generated synchronously so the 3x3 spawn meshes have
    // lit neighbors; everything farther streams in within the frame budget.
    double total_gen_ms = 0.0;
    double max_gen_ms = 0.0;
    int gen_count = 0;
    for (int cx = -2; cx <= 2; ++cx) {
        for (int cz = -2; cz <= 2; ++cz) {
            const auto t0 = std::chrono::steady_clock::now();
            if (world.ensure_chunk(cx, cz)) {
                const double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                total_gen_ms += ms;
                max_gen_ms = std::max(max_gen_ms, ms);
                ++gen_count;
            }
        }
    }
    OC_LOG_INFO("startup gen: {} chunks, total {:.1f} ms, avg {:.2f} ms/chunk, max {:.2f} ms", gen_count, total_gen_ms,
                gen_count > 0 ? total_gen_ms / gen_count : 0.0, max_gen_ms);

    // ── atlas + font ────────────────────────────────────────────────────────
    const opencraft::client::AtlasImage atlas_image = opencraft::client::generate_atlas(world.registry());
    const render::Texture2D atlas(atlas_image.width, atlas_image.height, atlas_image.pixels.data());
    const float tiles_per_row = static_cast<float>(atlas_image.tiles_per_row);
    const float texel = 1.0f / static_cast<float>(atlas_image.width);
    const std::uint16_t crack_base = opencraft::client::crack_tile_base(world.registry().size());
    OC_LOG_INFO("atlas: {}x{} px, {} tiles/row, crack tiles at {}", atlas_image.width, atlas_image.height,
                atlas_image.tiles_per_row, crack_base);

    const FontImage font_image = build_font_texture();
    const render::Texture2D font(font_image.width, font_image.height, font_image.pixels.data());

    // Block main colors for break particles (needs the generated atlas).
    std::vector<glm::vec3> block_colors = block_main_colors(atlas_image, world.registry().size());

    // ── overlay geometry ─────────────────────────────────────────────────────
    const CubeGeometry cube = build_cube_geometry();

    render::VertexArray wire_vao;
    wire_vao.bind();
    render::Buffer wire_vbo(render::Buffer::Target::Vertex, cube.edge_vertices.data(),
                            cube.edge_vertices.size() * sizeof(glm::vec3), render::Buffer::Usage::Static);
    wire_vbo.bind();
    wire_vao.set_attribute(0, 3, GL_FLOAT, sizeof(glm::vec3), 0);

    render::VertexArray crack_vao;
    crack_vao.bind();
    render::Buffer crack_pos_vbo(render::Buffer::Target::Vertex, cube.face_vertices.data(),
                                 cube.face_vertices.size() * sizeof(glm::vec3), render::Buffer::Usage::Static);
    crack_pos_vbo.bind();
    crack_vao.set_attribute(0, 3, GL_FLOAT, sizeof(glm::vec3), 0);
    render::Buffer crack_uv_vbo(render::Buffer::Target::Vertex, cube.face_uv.data(),
                                cube.face_uv.size() * sizeof(glm::vec2), render::Buffer::Usage::Static);
    crack_uv_vbo.bind();
    crack_vao.set_attribute(1, 2, GL_FLOAT, sizeof(glm::vec2), 0);

    render::VertexArray crosshair_vao;
    crosshair_vao.bind();
    const std::array<glm::vec3, 4> crosshair_verts = {glm::vec3(-0.018f, 0.0f, 0.0f), glm::vec3(0.018f, 0.0f, 0.0f),
                                                      glm::vec3(0.0f, -0.03f, 0.0f), glm::vec3(0.0f, 0.03f, 0.0f)};
    render::Buffer crosshair_vbo(render::Buffer::Target::Vertex, crosshair_verts.data(),
                                 crosshair_verts.size() * sizeof(glm::vec3), render::Buffer::Usage::Static);
    crosshair_vbo.bind();
    crosshair_vao.set_attribute(0, 3, GL_FLOAT, sizeof(glm::vec3), 0);

    // Break particles: dynamic point cloud (pos + rgba per point, T009).
    render::VertexArray particle_vao;
    particle_vao.bind();
    render::Buffer particle_vbo(render::Buffer::Target::Vertex, nullptr, 0, render::Buffer::Usage::Dynamic);
    particle_vbo.bind();
    particle_vao.set_attribute(0, 3, GL_FLOAT, sizeof(glm::vec4) + sizeof(glm::vec3), 0);
    particle_vao.set_attribute(1, 4, GL_FLOAT, sizeof(glm::vec4) + sizeof(glm::vec3), sizeof(glm::vec3));

    // ── shaders ─────────────────────────────────────────────────────────────
    const render::Shader shader(kVertexShader, kFragmentShader);
    shader.use();
    glUniform1i(shader.uniform_location("u_atlas"), 0);
    glUniform1f(shader.uniform_location("u_tiles_per_row"), tiles_per_row);
    glUniform1f(shader.uniform_location("u_texel"), texel);
    atlas.bind(0);

    const render::Shader wire_shader(kWireVertexShader, kWireFragmentShader);

    const render::Shader crack_shader(kCrackVertexShader, kCrackFragmentShader);
    crack_shader.use();
    glUniform1i(crack_shader.uniform_location("u_atlas"), 0);
    glUniform1f(crack_shader.uniform_location("u_tiles_per_row"), tiles_per_row);
    glUniform1f(crack_shader.uniform_location("u_texel"), texel);

    const render::Shader particle_shader(kParticleVertexShader, kParticleFragmentShader);

    const render::Shader ui_flat_shader(kUiFlatVertexShader, kUiFlatFragmentShader);

    const render::Shader ui_text_shader(kUiTextVertexShader, kUiTextFragmentShader);
    ui_text_shader.use();
    glUniform1i(ui_text_shader.uniform_location("u_font"), 1);

    // ── GL state ────────────────────────────────────────────────────────────
    int fb_width = kWindowWidth;
    int fb_height = kWindowHeight;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    glViewport(0, 0, fb_width, fb_height);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    // gl_PointSize only takes effect in a core profile when this is enabled;
    // without it the break particles collapse to single pixels (T009).
    glEnable(GL_PROGRAM_POINT_SIZE);
    glClearColor(0.52f, 0.62f, 0.72f, 1.0f);

    // ── player + simulation state ───────────────────────────────────────────
    phy::PlayerState prev_state;
    prev_state.position = spawn_pos;
    prev_state.on_ground = true;
    prev_state.fall_peak_y = prev_state.position.y;
    phy::PlayerState curr_state = prev_state;
    if (stored_level.has_value() && stored_level->has_player) {
        curr_state.position = glm::dvec3(stored_level->player_x, stored_level->player_y, stored_level->player_z);
        curr_state.velocity = glm::dvec3(stored_level->player_vx, stored_level->player_vy, stored_level->player_vz);
        curr_state.health = stored_level->health;
        curr_state.fall_peak_y = stored_level->fall_peak_y;
        curr_state.fall_distance = stored_level->fall_distance;
        curr_state.pose = static_cast<phy::Pose>(stored_level->pose & 1U);
        curr_state.on_ground = stored_level->on_ground;
    }
    prev_state = curr_state;

    double view_yaw = stored_level.has_value() && stored_level->has_player ? stored_level->yaw : 0.0;
    double view_pitch = stored_level.has_value() && stored_level->has_player ? stored_level->pitch : 0.0;
    bool paused = false;
    // T-D14: Auto-Jump master switch. In-memory only (card §4: 不要求落盘);
    // default ON, matching MC's client option. Toggled from the pause menu.
    bool auto_jump_enabled = true;
    // Last frame's primary-button state, for click-EDGE detection in the
    // pause menu (the non-idempotent AUTO-JUMP toggle button).
    bool prev_menu_clicked = false;
    bool prev_esc = false;
    bool prev_right = false;
    bool prev_w = false;
    int place_cooldown = 0;

    // T-D1 QA evidence (acceptance 6c): measure each sprint-jump arc so the
    // on-machine distance can be compared against the headless ⚖ assertions
    // (arc average 7.127 ±1%, gap clearance ≈4 blocks). An arc opens on the
    // tick a grounded sprint jump leaves the ground and closes on landing.
    bool jump_arc_open = false;
    glm::dvec3 jump_arc_start{0.0, 0.0, 0.0};
    int jump_arc_ticks = 0;

    // ── hotbar (10 slots: keys 1..9 pick blocks, 0 picks the bucket; creative
    //    palette, real inventory is M2) ───────────────────────────────────────
    static constexpr std::array<const char *, 9> kHotbarNames = {"stone",  "cobblestone", "dirt", "planks", "log",
                                                                 "leaves", "glass",       "sand", "gravel"};
    // T-F1: the bucket is the minimal item form the card allows - one extra
    // hotbar slot plus a has-water flag, no item registry.
    static constexpr int kBucketSlot = 9;
    static constexpr int kHotbarSlots = 10;
    std::array<std::uint16_t, 9> hotbar{};
    for (int slot = 0; slot < 9; ++slot) {
        hotbar[slot] = world.registry().id_of(kHotbarNames[slot]);
    }
    bool bucket_has_water = false;
    // Mirrors selected_slot for the HUD and the render pass (the tick's
    // targeting needs it before the hotbar keys are polled).
    bool bucket_selected = false;
    int selected_slot = 0;
    if (stored_level.has_value() && stored_level->has_player &&
        stored_level->selected_block < world.registry().size()) {
        // Restore the persisted selection to its slot when it is on the bar.
        // The bucket is not a block, so it persists as the water id it mashes
        // to; nothing else on the bar has that id.
        if (stored_level->selected_block == world.water_block_id()) {
            selected_slot = kBucketSlot;
        }
        for (int slot = 0; slot < 9; ++slot) {
            if (hotbar[slot] == stored_level->selected_block) {
                selected_slot = slot;
                break;
            }
        }
    }
    // The id the held-item overlay and the placed block use; for the bucket it
    // is the water placeholder (the bucket itself has no block form yet).
    const auto slot_block = [&](int slot) { return slot == kBucketSlot ? world.water_block_id() : hotbar[slot]; };
    std::uint16_t selected_block = slot_block(selected_slot);

    glm::ivec3 crack_pos{0, 0, 0};
    int crack_stage = -1; // -1 = no overlay
    glm::ivec3 target_pos{0, 0, 0};
    bool has_target = false;
    gam::MiningTracker mining(world.registry());
    opencraft::core::TickClock tick_clock;
    std::vector<std::pair<int, int>> dirty_chunks;
    std::unordered_map<std::int64_t, ChunkRenderable> renderables;
    double last_mesh_ms = 0.0;

    // Hand swing + break particles (T009 mining feedback).
    double swing_start = -10.0; // glfwGetTime() of the last swing start
    bool swinging = false;
    std::vector<Particle> particles;

    // Streaming offsets sorted by distance; generation radius = view + 1 so
    // chunks at the view edge mesh against lit neighbors.
    std::vector<std::pair<int, int>> gen_offsets;
    for (int dx = -(kViewRadius + 1); dx <= kViewRadius + 1; ++dx) {
        for (int dz = -(kViewRadius + 1); dz <= kViewRadius + 1; ++dz) {
            gen_offsets.emplace_back(dx, dz);
        }
    }
    std::sort(gen_offsets.begin(), gen_offsets.end(), [](const auto &a, const auto &b) {
        return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
    });

    // Pointer lock.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_RAW_MOUSE_MOTION);
    }

    auto mesh_chunk = [&](int cx, int cz) {
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
    };

    // Initial 3x3 meshes (5x5 generated above, so neighbors are lit).
    for (int cx = -1; cx <= 1; ++cx) {
        for (int cz = -1; cz <= 1; ++cz) {
            mesh_chunk(cx, cz);
        }
    }

    const auto key_pressed = [&](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };
    const auto view_dir = [&] {
        const double cp = std::cos(view_pitch);
        return glm::dvec3(-std::sin(view_yaw) * cp, -std::sin(view_pitch), -std::cos(view_yaw) * cp);
    };

    // Level snapshot for periodic + exit saves (T009). Field-for-field the
    // inverse of storage::serialize_level - keep both in sync.
    const auto make_level_data = [&] {
        opencraft::storage::LevelData level;
        level.seed = world_seed;
        level.tick_count = game_ticks;
        level.has_player = true;
        level.spawn_x = spawn_pos.x;
        level.spawn_y = spawn_pos.y;
        level.spawn_z = spawn_pos.z;
        level.player_x = curr_state.position.x;
        level.player_y = curr_state.position.y;
        level.player_z = curr_state.position.z;
        level.player_vx = curr_state.velocity.x;
        level.player_vy = curr_state.velocity.y;
        level.player_vz = curr_state.velocity.z;
        level.yaw = view_yaw;
        level.pitch = view_pitch;
        level.health = curr_state.health;
        level.fall_peak_y = curr_state.fall_peak_y;
        level.fall_distance = curr_state.fall_distance;
        level.pose = static_cast<std::uint8_t>(curr_state.pose);
        level.on_ground = curr_state.on_ground;
        level.selected_block = selected_block;
        return level;
    };

    // One 20 TPS logic tick: physics -> targeting -> mining -> placement.
    auto run_tick = [&] {
        // ── input mapping (WASD + space + shift + ctrl) ──────────────────────
        // T-D1: S maps to the explicit InputState::backward field (T007 had
        // no backward flag and simulated it as a 180° yaw flip; the physics
        // input direction now handles backward directly, and sprint requires
        // forward + not-backward per MC). forward_press is the one-tick W
        // keydown edge that drives the physics double-tap sprint window.
        const bool w = key_pressed(GLFW_KEY_W);
        const bool s = key_pressed(GLFW_KEY_S);
        const bool a = key_pressed(GLFW_KEY_A);
        const bool d = key_pressed(GLFW_KEY_D);
        phy::InputState in;
        in.yaw = view_yaw;
        in.pitch = view_pitch;
        in.forward = w && !s;
        in.backward = s && !w;
        in.left = a;
        in.right = d;
        in.forward_press = w && !prev_w;
        prev_w = w;
        in.jump = key_pressed(GLFW_KEY_SPACE) != 0;
        in.sneak = key_pressed(GLFW_KEY_LEFT_SHIFT) != 0;
        in.sprint = key_pressed(GLFW_KEY_LEFT_CONTROL) != 0;

        // ── T-D14 Auto-Jump (card §4): input-stage injection ────────────────
        // Decide BEFORE the physics step (docs/research/08 §2: the mechanism
        // lives in the input stage of the tick). When the pure predicate says
        // the forward move ends against a 0.6–1.25-block obstacle with
        // headroom, set in.jump so the EXISTING jump branch in step_player
        // runs — manual-jump semantics (incl. the sprint +0.2 boost) come
        // free, and the golden numbers stay shared. The player's own jump
        // input short-circuits the call (nothing to inject).
        if (!in.jump && auto_jump_enabled) {
            phy::AutoJumpConfig aj_cfg; // defaults = card §1: ON, 1.0 scan, 1.8 clearance
            if (phy::should_auto_jump(curr_state, in, world, phy::PhysicsConfig{}, aj_cfg)) {
                in.jump = true;
            }
        }

        prev_state = curr_state;
        phy::step_player(curr_state, in, world);

        // Sprint transitions come from the physics state machine (explicit
        // state per the T-D1 contract) — log them for QA evidence.
        if (curr_state.sprinting != prev_state.sprinting) {
            OC_LOG_INFO("sprint {} at tick {} (pos {:.2f}, {:.2f}, {:.2f})", curr_state.sprinting ? "start" : "stop",
                        game_ticks, curr_state.position.x, curr_state.position.y, curr_state.position.z);
        }

        // Sprint-jump arc measurement (acceptance 6c). A sprint jump takes off
        // from the ground while sprinting; the arc closes when the player is
        // grounded again, and the horizontal centre-to-centre distance and the
        // tick count give an on-machine average speed comparable to the
        // headless ⚖ figure (12-move arc, see test_sprint_feel.cpp).
        // NOTE: take-off is detected from prev_state.on_ground — step_player
        // applies the jump, so curr_state is already airborne on this tick.
        if (!jump_arc_open && curr_state.sprinting && prev_state.on_ground && in.jump) {
            jump_arc_open = true;
            jump_arc_start = curr_state.position;
            jump_arc_ticks = 0;
        } else if (jump_arc_open) {
            ++jump_arc_ticks;
            if (curr_state.on_ground) {
                const double dx = curr_state.position.x - jump_arc_start.x;
                const double dz = curr_state.position.z - jump_arc_start.z;
                const double dist = std::sqrt(dx * dx + dz * dz);
                // Move count includes the take-off tick itself, matching the
                // headless 12-move arc convention in test_sprint_feel.cpp.
                const int moves = jump_arc_ticks + 1;
                OC_LOG_INFO("sprint-jump arc: {} moves, horizontal {:.3f} blocks, clearance {:.3f}, "
                            "avg {:.3f} m/s (from ({:.2f}, {:.2f}, {:.2f}))",
                            moves, dist, dist - 0.6, dist * 20.0 / moves, jump_arc_start.x, jump_arc_start.y,
                            jump_arc_start.z);
                jump_arc_open = false;
            } else if (jump_arc_ticks > 60) {
                jump_arc_open = false; // safety: never let a stuck arc log forever
            }
        }

        // ── targeting ────────────────────────────────────────────────────────
        bucket_selected = selected_slot == kBucketSlot;
        // An empty bucket is aimed at water, so liquids become targetable for
        // it; everything else keeps the T008 filter (aim through water).
        const bool bucket_filling = bucket_selected && !bucket_has_water;
        const double eye_height = curr_state.pose == phy::Pose::Sneaking ? kEyeSneaking : kEyeStanding;
        const glm::dvec3 eye = curr_state.position + glm::dvec3(0.0, eye_height, 0.0);
        const auto filter = [&](std::uint16_t id) {
            const bool liquid = id != 0 && world.registry().def_of(id).liquid;
            if (bucket_filling) {
                return id != 0; // water surfaces are targetable
            }
            return id != 0 && !liquid; // liquids are not targetable
        };
        const gam::VoxelRayHit hit = gam::raycast_voxel(eye, view_dir(), kReachDistance, world, filter);
        has_target = hit.hit;
        target_pos = hit.block_pos;

        // ── mining ───────────────────────────────────────────────────────────
        const bool left_held = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const std::uint16_t target_id = hit.hit ? world.block_at(hit.block_pos.x, hit.block_pos.y, hit.block_pos.z) : 0;
        const auto mining_tick = mining.tick(hit.block_pos, target_id, hit.hit, left_held);
        if (left_held && hit.hit && !swinging) {
            swinging = true;
            swing_start = glfwGetTime();
        }
        if (mining_tick.broke) {
            world.set_block(hit.block_pos.x, hit.block_pos.y, hit.block_pos.z, 0, dirty_chunks);
            crack_stage = -1;
            // Break particles: block main color with brightness jitter (T009).
            const std::uint16_t broken_id = target_id;
            if (broken_id < block_colors.size()) {
                const glm::dvec3 center = glm::dvec3(hit.block_pos) + glm::dvec3(0.5, 0.5, 0.5);
                std::uint32_t rng = static_cast<std::uint32_t>(hit.block_pos.x * 73856093) ^
                                    static_cast<std::uint32_t>(hit.block_pos.y * 19349663) ^
                                    static_cast<std::uint32_t>(hit.block_pos.z * 83492791) ^
                                    static_cast<std::uint32_t>(game_ticks * 2654435761u);
                const auto next_rand = [&rng]() {
                    rng = rng * 1664525u + 1013904223u;
                    return static_cast<float>(rng >> 8) / static_cast<float>(1 << 24);
                };
                for (int i = 0; i < kParticlesPerBreak && particles.size() < kMaxParticles; ++i) {
                    Particle p;
                    p.pos = center + glm::dvec3(next_rand() - 0.5, next_rand() - 0.5, next_rand() - 0.5) * 0.6;
                    p.vel = glm::vec3(next_rand() - 0.5, next_rand(), next_rand() - 0.5) * 3.5f;
                    p.life = 0.4f + next_rand() * 0.25f;
                    const float jitter = 0.75f + next_rand() * 0.5f;
                    p.color = glm::vec4(glm::clamp(block_colors[broken_id] * jitter, 0.0f, 1.0f), 1.0f);
                    particles.push_back(p);
                }
            }
            swinging = true;
            swing_start = glfwGetTime();
        } else if (hit.hit && mining_tick.progress > 0.0f) {
            crack_pos = hit.block_pos;
            crack_stage = mining_tick.crack_stage;
        } else {
            crack_stage = -1;
        }

        // ── placement (hold right: first attempt immediate, then every 4 ticks
        //    ⚖ docs/01 §4; interactive blocks would take priority here - none
        //    exist in this milestone, the hook stays at the call site) ────────
        const bool right_held = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (place_cooldown > 0) {
            --place_cooldown;
        }
        // The block path repeats every 4 ticks while the button is held
        // (docs/01 §4). The bucket must NOT: a placed source is immediately
        // pickable again, so a held button alternates pour -> scoop (observed
        // on-machine: pour then scoop 4 ticks later), which reads as a broken
        // item. Item use is therefore edge-triggered.
        const bool use_edge = !prev_right;
        if (right_held && (place_cooldown == 0 || !prev_right)) {
            if (hit.hit && (!bucket_selected || use_edge)) {
                if (bucket_selected) {
                    // ── bucket (T-F1) ────────────────────────────────────────
                    // Filled: pour a source into the cell the hit face opens
                    // onto. Empty: scoop a source block out of the world.
                    // Placement skips check_placement's player-overlap test on
                    // purpose: fluids are non-solid, so pouring water at your
                    // own feet is legal (MC does the same); the replaceable
                    // test is the same one the block path uses.
                    if (bucket_has_water) {
                        const glm::ivec3 cell = gam::placement_cell(hit);
                        const std::uint16_t occupant = world.block_at(cell.x, cell.y, cell.z);
                        if (gam::is_replaceable(world.registry(), occupant) &&
                            world.place_water_source(cell.x, cell.y, cell.z)) {
                            bucket_has_water = false;
                            OC_LOG_INFO("bucket: poured water source at ({}, {}, {})", cell.x, cell.y, cell.z);
                        }
                    } else if (world.remove_water_source(hit.block_pos.x, hit.block_pos.y, hit.block_pos.z,
                                                         dirty_chunks)) {
                        bucket_has_water = true;
                        OC_LOG_INFO("bucket: filled from ({}, {}, {})", hit.block_pos.x, hit.block_pos.y,
                                    hit.block_pos.z);
                    }
                } else {
                    const glm::ivec3 cell = gam::placement_cell(hit);
                    const auto status = gam::check_placement(world.registry(), world, cell, curr_state.position,
                                                             curr_state.height(), phy::PlayerState::kHalfWidth);
                    if (status == gam::PlacementStatus::Ok) {
                        world.set_block(cell.x, cell.y, cell.z, selected_block, dirty_chunks);
                        OC_LOG_INFO("placed {} at ({}, {}, {})", world.registry().string_of(selected_block), cell.x,
                                    cell.y, cell.z);
                    }
                }
            }
            place_cooldown = 4; // ⚖ retry rhythm whether or not the attempt succeeded
            swinging = true;
            swing_start = glfwGetTime();
        }
        prev_right = right_held;

        // ── hotbar selection (blocks on 1..9, the bucket on 0) ───────────────
        for (int slot = 0; slot < 9; ++slot) {
            if (key_pressed(GLFW_KEY_1 + slot)) {
                selected_slot = slot;
                selected_block = hotbar[slot];
            }
        }
        if (key_pressed(GLFW_KEY_0)) {
            selected_slot = kBucketSlot;
            selected_block = world.water_block_id();
        }

        // ── fluid scheduled ticks (T-F1): one step per game tick, exactly like
        //    the rest of the simulation. Changed chunks go to the remesh list.
        world.fluid_step(dirty_chunks);

        // ── autosave cadence: 200 ticks = ~10 s of game time (T009) ──────────
        if (save.maybe_autosave_tick()) {
            const std::size_t chunks = world.autosave_pass();
            save.write_level_now(make_level_data());
            OC_LOG_INFO("autosave: {} chunk(s) queued for async write, level written (ticks={})", chunks, game_ticks);
        }
    };

    // ── main loop ───────────────────────────────────────────────────────────
    double last_frame = glfwGetTime();
    double fps_timer = last_frame;
    double last_particle_time = last_frame;
    int fps_frames = 0;
    double last_cursor_x = 0.0;
    double last_cursor_y = 0.0;
    bool cursor_anchored = false;
    float fov = client::kBaseFov;
    int stream_meshed = 0;

    // T-D13: render-camera vertical spring. The camera's Y follows the eye
    // target through a critically damped filter while X/Z pass through exactly,
    // so the single-tick step-assist lift stops reading as a teleport. Seeded
    // from the same position the physics starts at, so frame 1 has no transient.
    client::CameraFilter camera;
    camera.reset(curr_state.position.x, curr_state.position.y, curr_state.position.z,
                 curr_state.pose == phy::Pose::Sneaking ? kEyeSneaking : kEyeStanding);
    double camera_last_time = last_frame;
    // Physics ticks executed by the frame currently being rendered. The spring
    // uses these to bend its per-frame ramp where the physics tick landed.
    int ticks_this_frame = 0;
    double tick_kink_dt = 0.0;
    double tick_kink_eye_y = 0.0;

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();

        // ESC edge: toggle pause in both directions.
        const bool esc_down = key_pressed(GLFW_KEY_ESCAPE) != 0;
        if (esc_down && !prev_esc) {
            paused = !paused;
            if (paused) {
                mining.reset();
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                tick_clock.reset();
                cursor_anchored = false;
            }
        }
        prev_esc = esc_down;

        // ── mouse look ──────────────────────────────────────────────────────
        if (!paused) {
            if (!cursor_anchored) {
                glfwGetCursorPos(window, &last_cursor_x, &last_cursor_y);
                cursor_anchored = true;
            } else {
                double x = 0.0;
                double y = 0.0;
                glfwGetCursorPos(window, &x, &y);
                view_yaw -= (x - last_cursor_x) * kMouseSensitivity;
                view_pitch += (y - last_cursor_y) * kMouseSensitivity;
                view_pitch = std::clamp(view_pitch, -kMaxPitch, kMaxPitch);
                last_cursor_x = x;
                last_cursor_y = y;
            }

            // ── fixed-step simulation ───────────────────────────────────────────
            const double now = glfwGetTime();
            // T-D13: the partial-tick LERP's ramp bends where this frame's tick
            // lands. alpha_before is how far into the pending tick we already
            // are, and the eye target at the bend is the one the player state
            // holds *before* step_player runs; both are read here, before
            // advance()/run_tick() consume them.
            const double alpha_before = tick_clock.alpha();
            const int ticks = tick_clock.advance((now - last_frame) * 1000.0);
            last_frame = now;
            ticks_this_frame = ticks;
            if (ticks > 0) {
                tick_kink_dt = (1.0 - alpha_before) * (opencraft::core::TickClock::kTickDurationMs / 1000.0);
                tick_kink_eye_y =
                    curr_state.position.y + (curr_state.pose == phy::Pose::Sneaking ? kEyeSneaking : kEyeStanding);
            } else {
                tick_kink_dt = 0.0;
            }
            for (int i = 0; i < ticks; ++i) {
                run_tick();
                ++game_ticks;
            }

            // ── streaming ───────────────────────────────────────────────────────
            const auto [pcx, pcz] =
                opencraft::voxel::Chunk::chunk_coords(static_cast<int>(std::floor(curr_state.position.x)),
                                                      static_cast<int>(std::floor(curr_state.position.z)));
            int gen_left = kGenPerFrame;
            for (const auto &[dx, dz] : gen_offsets) {
                if (gen_left == 0) {
                    break;
                }
                if (world.ensure_chunk(pcx + dx, pcz + dz)) {
                    --gen_left;
                }
            }

            if (!dirty_chunks.empty()) {
                // The fluid simulation can name the same chunk hundreds of
                // times in one frame; remesh each chunk once.
                std::sort(dirty_chunks.begin(), dirty_chunks.end());
                dirty_chunks.erase(std::unique(dirty_chunks.begin(), dirty_chunks.end()), dirty_chunks.end());
                const auto t0 = std::chrono::steady_clock::now();
                for (const auto &[cx, cz] : dirty_chunks) {
                    if (world.chunk_ready(cx, cz)) {
                        mesh_chunk(cx, cz);
                    }
                }
                OC_LOG_INFO("remeshed {} chunk(s) in {:.2f} ms (last mesh {:.2f} ms)", dirty_chunks.size(),
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                            last_mesh_ms);
                dirty_chunks.clear();
            }
            int mesh_left = kNewMeshPerFrame;
            for (const auto &[dx, dz] : gen_offsets) {
                if (mesh_left == 0) {
                    break;
                }
                if (dx * dx + dz * dz > kViewRadius * kViewRadius) {
                    continue; // mesh only within the view radius
                }
                const int cx = pcx + dx;
                const int cz = pcz + dz;
                if (world.neighbors_ready(cx, cz) && renderables.find(chunk_key(cx, cz)) == renderables.end()) {
                    mesh_chunk(cx, cz);
                    ++stream_meshed;
                    --mesh_left;
                }
            }
        } // !paused
        const double now = glfwGetTime();
        last_frame = now;

        // ── camera (partial-tick interpolation) ─────────────────────────────
        // X/Z are the interpolated physics position, unfiltered. Only the Y
        // component runs through the T-D13 vertical spring, which turns the
        // step-assist's single-tick 0.6 lift into a ~0.2 s S-curve instead of a
        // one-frame jump. `now - camera_last_time` is the real frame duration;
        // the spring clamps it internally.
        const double alpha = tick_clock.alpha();
        const glm::dvec3 cam_pos = prev_state.position + (curr_state.position - prev_state.position) * alpha;
        const double eye_height = curr_state.pose == phy::Pose::Sneaking ? kEyeSneaking : kEyeStanding;
        const double frame_dt = std::max(0.0, now - camera_last_time);
        camera_last_time = now;
        // The LERP ramp bends where this frame's tick landed (see the kink
        // capture above). Only inside a simulated frame is it a real bend;
        // otherwise the single-piece path applies. A paused frame runs no ticks,
        // so any captured kink is stale and must not be used.
        if (!paused && ticks_this_frame > 0 && tick_kink_dt > 0.0 && tick_kink_dt < frame_dt) {
            camera.update(cam_pos.x, cam_pos.y, cam_pos.z, eye_height, frame_dt, tick_kink_eye_y, tick_kink_dt);
        } else {
            camera.update(cam_pos.x, cam_pos.y, cam_pos.z, eye_height, frame_dt);
        }
        const glm::dvec3 eye(camera.x(), camera.y(), camera.z());

        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        glViewport(0, 0, fb_width, fb_height);

        // Sprint FOV (T-D1): driven by the physics sprint state — double-tap
        // and Ctrl both stretch the view. Constants and easing live in
        // fov.hpp so the transition is unit-testable.
        fov = client::fov_step(fov, curr_state.sprinting);

        const glm::mat4 projection = glm::perspective(
            glm::radians(fov), static_cast<float>(fb_width) / static_cast<float>(fb_height), 0.05f, 600.0f);
        const glm::vec3 eye_f(eye);
        const glm::mat4 view = glm::lookAt(eye_f, eye_f + glm::vec3(view_dir()), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 mvp = projection * view;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shader.use();
        glUniformMatrix4fv(shader.uniform_location("u_mvp"), 1, GL_FALSE, &mvp[0][0]);

        const auto distance_sq = [&](const ChunkRenderable &r) { return glm::dot(r.center - eye_f, r.center - eye_f); };

        // Opaque pass: near -> far (early-Z friendly), depth writes on.
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        std::vector<const ChunkRenderable *> order;
        order.reserve(renderables.size());
        for (const auto &entry : renderables) {
            if (entry.second.opaque) {
                order.push_back(&entry.second);
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
        for (const auto &entry : renderables) {
            if (entry.second.translucent || entry.second.fluid) {
                order.push_back(&entry.second);
            }
        }
        std::sort(order.begin(), order.end(), [&](const ChunkRenderable *a, const ChunkRenderable *b) {
            return distance_sq(*a) > distance_sq(*b);
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

        // ── break particles (T009): depth-tested points, no depth writes ────
        {
            const double particles_now = glfwGetTime();
            const float dt = static_cast<float>(std::min(particles_now - last_particle_time, 0.1));
            last_particle_time = particles_now;
            update_particles(particles, dt);
            if (!particles.empty()) {
                std::vector<float> point_data;
                point_data.reserve(particles.size() * 7);
                for (const Particle &p : particles) {
                    point_data.insert(point_data.end(),
                                      {p.pos.x, p.pos.y, p.pos.z, p.color.r, p.color.g, p.color.b, p.color.a});
                }
                glDepthMask(GL_FALSE);
                particle_shader.use();
                glUniformMatrix4fv(particle_shader.uniform_location("u_mvp"), 1, GL_FALSE, &mvp[0][0]);
                glUniform1f(particle_shader.uniform_location("u_point_px"),
                            7.0f * static_cast<float>(fb_height) / 720.0f);
                particle_vao.bind();
                particle_vbo.bind();
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(point_data.size() * sizeof(float)),
                             point_data.data(), GL_STREAM_DRAW);
                glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(particles.size()));
                glDepthMask(GL_TRUE);
            }
        }

        // ── selection wireframe + crack overlay ─────────────────────────────
        if (has_target) {
            wire_shader.use();
            glUniformMatrix4fv(wire_shader.uniform_location("u_mvp"), 1, GL_FALSE, &mvp[0][0]);
            glUniform3f(wire_shader.uniform_location("u_offset"), static_cast<float>(target_pos.x),
                        static_cast<float>(target_pos.y), static_cast<float>(target_pos.z));
            glUniform1f(wire_shader.uniform_location("u_scale"), 1.002f);
            glUniform4f(wire_shader.uniform_location("u_color"), 0.05f, 0.05f, 0.05f, 1.0f);
            wire_vao.bind();
            glDrawArrays(GL_LINES, 0, 24);
        }
        if (crack_stage >= 0) {
            crack_shader.use();
            glUniformMatrix4fv(crack_shader.uniform_location("u_mvp"), 1, GL_FALSE, &mvp[0][0]);
            glUniform3f(crack_shader.uniform_location("u_offset"), static_cast<float>(crack_pos.x) - 0.001f,
                        static_cast<float>(crack_pos.y) - 0.001f, static_cast<float>(crack_pos.z) - 0.001f);
            glUniform1f(crack_shader.uniform_location("u_scale"), 1.002f);
            glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(crack_base + crack_stage));
            // Overlay cube winding is mirrored vs the mesher's CCW convention;
            // draw double-sided so culling cannot swallow the overlay.
            glDisable(GL_CULL_FACE);
            crack_vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glEnable(GL_CULL_FACE);
        }
        glDepthMask(GL_TRUE);

        // ── held block with swing animation (T009) ──────────────────────────
        // Drawn last against cleared depth so it always sits over the world.
        // The crack shader does the textured-cube job: per-face tile uniform,
        // so the top/bottom/side faces get three draw calls.
        {
            double swing_t = (glfwGetTime() - swing_start) / 0.25;
            if (swing_t >= 1.0) {
                swing_t = 0.0;
                swinging = false;
            }
            const float s = swinging ? std::sin(static_cast<float>(swing_t) * 3.14159265f) : 0.0f;
            glClear(GL_DEPTH_BUFFER_BIT);
            glDisable(GL_CULL_FACE);
            crack_shader.use();
            glUniformMatrix4fv(crack_shader.uniform_location("u_mvp"), 1, GL_FALSE, &projection[0][0]);
            glUniform3f(crack_shader.uniform_location("u_offset"), 0.42f - s * 0.16f, -0.42f - s * 0.14f,
                        -0.80f - s * 0.12f);
            glUniform1f(crack_shader.uniform_location("u_scale"), 0.32f);
            crack_vao.bind();
            // build_cube_geometry face order: f0 top, f1 bottom, f2..f5 sides.
            glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(selected_block * 3 + 1));
            glDrawArrays(GL_TRIANGLES, 12, 24); // 4 side faces
            glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(selected_block * 3 + 0));
            glDrawArrays(GL_TRIANGLES, 0, 6); // top
            glUniform1f(crack_shader.uniform_location("u_tile"), static_cast<float>(selected_block * 3 + 2));
            glDrawArrays(GL_TRIANGLES, 6, 6); // bottom
            glEnable(GL_CULL_FACE);
        }

        // ── crosshair ───────────────────────────────────────────────────────
        glDisable(GL_DEPTH_TEST);
        wire_shader.use();
        static const glm::mat4 kIdentity(1.0f);
        glUniformMatrix4fv(wire_shader.uniform_location("u_mvp"), 1, GL_FALSE, &kIdentity[0][0]);
        glUniform3f(wire_shader.uniform_location("u_offset"), 0.0f, 0.0f, 0.0f);
        glUniform1f(wire_shader.uniform_location("u_scale"), 1.0f);
        glUniform4f(wire_shader.uniform_location("u_color"), 0.92f, 0.92f, 0.92f, 0.85f);
        crosshair_vao.bind();
        glDrawArrays(GL_LINES, 0, 4);
        glEnable(GL_DEPTH_TEST);

        // ── HUD: hotbar (10 slots + item name) and health hearts (T009) ─────
        {
            glDisable(GL_DEPTH_TEST);

            constexpr float kSlotPx = 24.0f;
            constexpr float kSlotGap = 2.0f;
            const float bar_w = kHotbarSlots * kSlotPx + (kHotbarSlots - 1) * kSlotGap;
            const float bar_x0 = static_cast<float>(fb_width) / 2.0f - bar_w / 2.0f;
            const float bar_y0 = static_cast<float>(fb_height) - kSlotPx - 10.0f;

            // Slot backdrops (one flat-color draw call for all of them).
            {
                std::vector<glm::vec2> flat;
                for (int i = 0; i < kHotbarSlots; ++i) {
                    const float x0 = bar_x0 + static_cast<float>(i) * (kSlotPx + kSlotGap);
                    draw_rect(x0, bar_y0, x0 + kSlotPx, bar_y0 + kSlotPx, fb_width, fb_height, flat);
                }
                ui_flat_shader.use();
                render::VertexArray vao;
                vao.bind();
                render::Buffer vbo(render::Buffer::Target::Vertex, flat.data(),
                                   static_cast<std::size_t>(flat.size()) * sizeof(glm::vec2),
                                   render::Buffer::Usage::Static);
                vbo.bind();
                vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
                glUniform4f(ui_flat_shader.uniform_location("u_color"), 0.0f, 0.0f, 0.0f, 0.55f);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(flat.size()));
            }
            // Selected slot highlight: 2 px white frame (4 thin rects).
            {
                std::vector<glm::vec2> flat;
                const float fx0 = bar_x0 + static_cast<float>(selected_slot) * (kSlotPx + kSlotGap);
                const float fx1 = fx0 + kSlotPx;
                const float fy0 = bar_y0;
                const float fy1 = bar_y0 + kSlotPx;
                draw_rect(fx0 - 2.0f, fy0 - 2.0f, fx1 + 2.0f, fy0, fb_width, fb_height, flat);
                draw_rect(fx0 - 2.0f, fy1, fx1 + 2.0f, fy1 + 2.0f, fb_width, fb_height, flat);
                draw_rect(fx0 - 2.0f, fy0, fx0, fy1, fb_width, fb_height, flat);
                draw_rect(fx1, fy0, fx1 + 2.0f, fy1, fb_width, fb_height, flat);
                ui_flat_shader.use();
                render::VertexArray vao;
                vao.bind();
                render::Buffer vbo(render::Buffer::Target::Vertex, flat.data(),
                                   static_cast<std::size_t>(flat.size()) * sizeof(glm::vec2),
                                   render::Buffer::Usage::Static);
                vbo.bind();
                vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
                glUniform4f(ui_flat_shader.uniform_location("u_color"), 0.95f, 0.95f, 0.95f, 1.0f);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(flat.size()));
            }
            // Slot icons: reuse the terrain shader's tile math (proven path)
            // with a pixel-space ortho projection; one quad per slot.
            {
                const glm::mat4 hud_ortho =
                    glm::ortho(0.0f, static_cast<float>(fb_width), static_cast<float>(fb_height), 0.0f);
                std::vector<render::MeshVertex> icon_verts;
                icon_verts.reserve(kHotbarSlots * 4);
                std::vector<std::uint32_t> icon_indices;
                icon_indices.reserve(kHotbarSlots * 6);
                const float pad = 3.0f;
                for (int i = 0; i < kHotbarSlots; ++i) {
                    const float x0 = bar_x0 + static_cast<float>(i) * (kSlotPx + kSlotGap) + pad;
                    const float y0 = bar_y0 + pad;
                    const float x1 = x0 + kSlotPx - pad * 2.0f;
                    const float y1 = y0 + kSlotPx - pad * 2.0f;
                    // The bucket borrows the water tile: bright while it holds
                    // water, dimmed when empty (placeholder art; a real icon
                    // belongs to the M2 item layer).
                    const bool bucket = i == kBucketSlot;
                    const std::uint16_t icon_block = bucket ? world.water_block_id() : hotbar[i];
                    const std::uint8_t icon_shade = bucket && !bucket_has_water ? 70 : 235;
                    const std::uint16_t tile = static_cast<std::uint16_t>(icon_block * 3 + 1); // side tile
                    const std::uint16_t base = static_cast<std::uint16_t>(icon_verts.size());
                    // uv corner codes: 0=(0,0) tl, 1=(1,0) tr, 2=(0,1) bl, 3=(1,1) br.
                    icon_verts.push_back(
                        {static_cast<std::uint16_t>(x0), static_cast<std::uint16_t>(y0), 0, tile, 0, icon_shade});
                    icon_verts.push_back(
                        {static_cast<std::uint16_t>(x1), static_cast<std::uint16_t>(y0), 0, tile, 1, icon_shade});
                    icon_verts.push_back(
                        {static_cast<std::uint16_t>(x1), static_cast<std::uint16_t>(y1), 0, tile, 3, icon_shade});
                    icon_verts.push_back(
                        {static_cast<std::uint16_t>(x0), static_cast<std::uint16_t>(y1), 0, tile, 2, icon_shade});
                    icon_indices.insert(icon_indices.end(),
                                        {static_cast<std::uint32_t>(base), static_cast<std::uint32_t>(base + 1),
                                         static_cast<std::uint32_t>(base + 2), static_cast<std::uint32_t>(base),
                                         static_cast<std::uint32_t>(base + 2), static_cast<std::uint32_t>(base + 3)});
                }
                shader.use();
                glUniformMatrix4fv(shader.uniform_location("u_mvp"), 1, GL_FALSE, &hud_ortho[0][0]);
                glUniform3f(shader.uniform_location("u_chunk_origin"), 0.0f, 0.0f, 0.0f);
                render::VertexArray vao;
                vao.bind();
                render::Buffer vbo(render::Buffer::Target::Vertex, icon_verts.data(),
                                   icon_verts.size() * sizeof(render::MeshVertex), render::Buffer::Usage::Static);
                vbo.bind();
                constexpr std::size_t kIconStride = sizeof(render::MeshVertex);
                vao.set_attribute(0, 3, GL_UNSIGNED_SHORT, kIconStride, offsetof(render::MeshVertex, x));
                vao.set_attribute(1, 1, GL_UNSIGNED_SHORT, kIconStride, offsetof(render::MeshVertex, tile));
                vao.set_attribute(2, 1, GL_UNSIGNED_BYTE, kIconStride, offsetof(render::MeshVertex, uv));
                vao.set_attribute(3, 1, GL_UNSIGNED_BYTE, kIconStride, offsetof(render::MeshVertex, shade));
                render::Buffer ebo(render::Buffer::Target::Index, icon_indices.data(),
                                   icon_indices.size() * sizeof(std::uint32_t), render::Buffer::Usage::Static);
                ebo.bind();
                glDisable(GL_CULL_FACE);
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(icon_indices.size()), GL_UNSIGNED_INT, nullptr);
                glEnable(GL_CULL_FACE);
            }
            // Selected item name (uppercase) above the hotbar.
            {
                std::string name = bucket_selected ? (bucket_has_water ? "water bucket" : "bucket")
                                                   : world.registry().string_of(selected_block);
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                std::vector<glm::vec2> tverts;
                std::vector<glm::vec2> tuvs;
                // Own line above the hearts row (bar_y0-24); drawing both at
                // the same y made the name collide with the hearts.
                draw_text(name, static_cast<float>(fb_width) / 2.0f - static_cast<float>(name.size()) * 7.0f,
                          bar_y0 - 46.0f, 16.0f, fb_width, fb_height, tverts, tuvs);
                ui_text_shader.use();
                font.bind(1);
                render::VertexArray vao;
                vao.bind();
                render::Buffer vbo(render::Buffer::Target::Vertex, tverts.data(),
                                   static_cast<std::size_t>(tverts.size()) * sizeof(glm::vec2),
                                   render::Buffer::Usage::Static);
                vbo.bind();
                vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
                render::Buffer uvbo(render::Buffer::Target::Vertex, tuvs.data(),
                                    static_cast<std::size_t>(tuvs.size()) * sizeof(glm::vec2),
                                    render::Buffer::Usage::Static);
                uvbo.bind();
                vao.set_attribute(1, 2, GL_FLOAT, sizeof(glm::vec2), 0);
                glUniform4f(ui_text_shader.uniform_location("u_color"), 1.0f, 1.0f, 1.0f, 1.0f);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tverts.size()));
            }
            // Health: 10 hearts driven by PlayerState::health (T007), 2 hp per
            // heart. Red pass (full + half), then a dim pass for empty ones.
            {
                const int full = static_cast<int>(curr_state.health) / 2;
                const bool half = static_cast<int>(curr_state.health) % 2 != 0;
                std::string hearts_red;
                hearts_red.reserve(10);
                for (int i = 0; i < full; ++i) {
                    hearts_red += '\x01';
                }
                if (half && full < 10) {
                    hearts_red += '\x02';
                }
                const int empties = 10 - full - (half && full < 10 ? 1 : 0);
                std::string hearts_empty(static_cast<std::size_t>(std::max(empties, 0)), '\x03');
                const float heart_px = 14.0f;
                const float hearts_y = bar_y0 - 22.0f;
                auto draw_hearts = [&](const std::string &text, float r, float g, float b, float a) {
                    if (text.empty()) {
                        return;
                    }
                    std::vector<glm::vec2> tverts;
                    std::vector<glm::vec2> tuvs;
                    draw_text(text, bar_x0, hearts_y, heart_px, fb_width, fb_height, tverts, tuvs);
                    ui_text_shader.use();
                    font.bind(1);
                    render::VertexArray vao;
                    vao.bind();
                    render::Buffer vbo(render::Buffer::Target::Vertex, tverts.data(),
                                       static_cast<std::size_t>(tverts.size()) * sizeof(glm::vec2),
                                       render::Buffer::Usage::Static);
                    vbo.bind();
                    vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
                    render::Buffer uvbo(render::Buffer::Target::Vertex, tuvs.data(),
                                        static_cast<std::size_t>(tuvs.size()) * sizeof(glm::vec2),
                                        render::Buffer::Usage::Static);
                    uvbo.bind();
                    vao.set_attribute(1, 2, GL_FLOAT, sizeof(glm::vec2), 0);
                    glUniform4f(ui_text_shader.uniform_location("u_color"), r, g, b, a);
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tverts.size()));
                };
                draw_hearts(hearts_empty, 0.25f, 0.25f, 0.25f, 0.9f);
                draw_hearts(hearts_red, 0.85f, 0.15f, 0.15f, 1.0f);
            }

            glEnable(GL_DEPTH_TEST);
        }

        // ── pause menu (drawn over the live scene; no ticks while paused) ───
        if (paused) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);

            double mx = 0.0;
            double my = 0.0;
            glfwGetCursorPos(window, &mx, &my);
            // glfwGetCursorPos reports CONTENT pixels; the UI ortho projection
            // below spans the FRAMEBUFFER size. On a HiDPI display the two
            // differ by the backing-store scale, so convert once for every
            // hit-test in this menu.
            int win_w = 0;
            int win_h = 0;
            glfwGetWindowSize(window, &win_w, &win_h);
            if (win_w > 0 && win_h > 0 && (win_w != fb_width || win_h != fb_height)) {
                mx *= static_cast<double>(fb_width) / static_cast<double>(win_w);
                my *= static_cast<double>(fb_height) / static_cast<double>(win_h);
            }
            const bool clicked = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            const float bx0 = static_cast<float>(fb_width) * 0.5f - 90.0f;
            const float bx1 = static_cast<float>(fb_width) * 0.5f + 90.0f;
            const float cy = static_cast<float>(fb_height) * 0.5f;
            const bool resume_hover = mx >= bx0 && mx <= bx1 && my >= cy - 64.0 && my <= cy - 28.0;
            // T-D14: AUTO-JUMP sits in the 44 px gap between RESUME and QUIT
            // (cy-28..cy+8): 30 px band [cy-24, cy+4], 4 px breathing room on
            // each side, same 18 px pitch as the existing rows.
            const bool autojump_hover = mx >= bx0 && mx <= bx1 && my >= cy - 24.0 && my <= cy + 4.0;
            const bool quit_hover = mx >= bx0 && mx <= bx1 && my >= cy + 8.0 && my <= cy + 44.0;

            std::vector<glm::vec2> flat;
            draw_rect(0.0f, 0.0f, static_cast<float>(fb_width), static_cast<float>(fb_height), fb_width, fb_height,
                      flat);
            draw_rect(bx0, cy - 64.0f, bx1, cy - 28.0f, fb_width, fb_height, flat);
            draw_rect(bx0, cy - 24.0f, bx1, cy + 4.0f, fb_width, fb_height, flat);
            draw_rect(bx0, cy + 8.0f, bx1, cy + 44.0f, fb_width, fb_height, flat);
            ui_flat_shader.use();
            // One color per draw call: backdrop first, then each button with a
            // hover-dependent color.
            {
                render::VertexArray vao;
                vao.bind();
                render::Buffer vbo(render::Buffer::Target::Vertex, flat.data(),
                                   static_cast<std::size_t>(flat.size()) * sizeof(glm::vec2),
                                   render::Buffer::Usage::Static);
                vbo.bind();
                vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
                glUniform4f(ui_flat_shader.uniform_location("u_color"), 0.0f, 0.0f, 0.0f, 0.55f);
                glDrawArrays(GL_TRIANGLES, 0, 6); // backdrop only
                flat.erase(flat.begin(), flat.begin() + 6);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(flat.size() * sizeof(glm::vec2)), flat.data(),
                             GL_STATIC_DRAW);
                glUniform4f(ui_flat_shader.uniform_location("u_color"), 0.22f, 0.24f, 0.22f, 0.9f);
                glDrawArrays(GL_TRIANGLES, 0, 6); // resume button
                flat.erase(flat.begin(), flat.begin() + 6);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(flat.size() * sizeof(glm::vec2)), flat.data(),
                             GL_STATIC_DRAW);
                glUniform4f(ui_flat_shader.uniform_location("u_color"), autojump_hover ? 0.34f : 0.22f,
                            autojump_hover ? 0.36f : 0.24f, 0.24f, 0.9f);
                glDrawArrays(GL_TRIANGLES, 0, 6); // auto-jump button
                flat.erase(flat.begin(), flat.begin() + 6);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(flat.size() * sizeof(glm::vec2)), flat.data(),
                             GL_STATIC_DRAW);
                glUniform4f(ui_flat_shader.uniform_location("u_color"), 0.22f, 0.24f, 0.24f, 0.9f);
                glDrawArrays(GL_TRIANGLES, 0, 6); // quit button
            }

            std::vector<glm::vec2> tverts;
            std::vector<glm::vec2> tuvs;
            draw_text("PAUSED", static_cast<float>(fb_width) / 2.0f - 60.0f, cy - 110.0f, 22.0f, fb_width, fb_height,
                      tverts, tuvs);
            draw_text("RESUME", bx0 + 52.0f, cy - 54.0f, 16.0f, fb_width, fb_height, tverts, tuvs);
            // Label carries the live state so the toggle is observable
            // (MC shows ON/OFF on its accessibility options, not a bare name).
            draw_text(auto_jump_enabled ? "AUTO-JUMP ON" : "AUTO-JUMP OFF", bx0 + 19.0f, cy - 14.0f, 16.0f, fb_width,
                      fb_height, tverts, tuvs);
            draw_text("QUIT", bx0 + 62.0f, cy + 18.0f, 16.0f, fb_width, fb_height, tverts, tuvs);
            ui_text_shader.use();
            glUniform4f(ui_text_shader.uniform_location("u_color"), 1.0f, 1.0f, 1.0f, 1.0f);
            font.bind(1);
            {
                render::VertexArray vao;
                vao.bind();
                render::Buffer vbo(render::Buffer::Target::Vertex, tverts.data(),
                                   static_cast<std::size_t>(tverts.size()) * sizeof(glm::vec2),
                                   render::Buffer::Usage::Static);
                vbo.bind();
                vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
                render::Buffer uvbo(render::Buffer::Target::Vertex, tuvs.data(),
                                    static_cast<std::size_t>(tuvs.size()) * sizeof(glm::vec2),
                                    render::Buffer::Usage::Static);
                uvbo.bind();
                vao.set_attribute(1, 2, GL_FLOAT, sizeof(glm::vec2), 0);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tverts.size()));
            }

            if (clicked && resume_hover) {
                paused = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                tick_clock.reset();
                cursor_anchored = false;
            }
            // Auto-jump toggle is the only button whose action is NOT
            // idempotent (RESUME/QUIT are state-setters), so it needs a click
            // EDGE: holding the button must flip the option once, not every
            // frame it is pressed.
            if (clicked && !prev_menu_clicked && autojump_hover) {
                auto_jump_enabled = !auto_jump_enabled;
            }
            prev_menu_clicked = clicked;
            if (clicked && quit_hover) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
        }
        glDisable(GL_BLEND);

        glfwSwapBuffers(window);

        ++fps_frames;
        if (now - fps_timer >= 2.0) {
            OC_LOG_INFO("fps {:.1f} | pos ({:.2f}, {:.2f}, {:.2f}) | chunks {} | stream-meshed {}",
                        fps_frames / (now - fps_timer), curr_state.position.x, curr_state.position.y,
                        curr_state.position.z, renderables.size(), stream_meshed);
            fps_frames = 0;
            fps_timer = now;
        }
    }

    glfwDestroyWindow(window);

    // ── exit: force flush (T009: 退出时强制 flush，QUIT 与窗口关闭共用此路径) ──
    world.autosave_pass();
    save.write_level_now(make_level_data());
    save.flush();
    OC_LOG_INFO("save: flushed on exit (ticks={}, chunks cached={})", game_ticks, save.cached_region_count());

    glfwTerminate();
    OC_LOG_INFO("clean shutdown");
    return 0;
}
