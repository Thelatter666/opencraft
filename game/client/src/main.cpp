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
#include "opencraft/core/log.hpp"
#include "opencraft/core/tick_clock.hpp"
#include "opencraft/core/version.hpp"
#include "opencraft/game/mining.hpp"
#include "opencraft/game/placement.hpp"
#include "opencraft/game/raycast.hpp"
#include "opencraft/physics/input_state.hpp"
#include "opencraft/physics/player_physics.hpp"
#include "opencraft/physics/player_state.hpp"
#include "opencraft/render/mesher.hpp"
#include "opencraft/render/rhi.hpp"
#include "world.hpp"

namespace render = opencraft::render; // short alias used by the GPU glue below
namespace phy = opencraft::physics;
namespace gam = opencraft::game;

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

// ── view / interaction constants ────────────────────────────────────────────
constexpr double kMouseSensitivity = 0.0025;
constexpr double kMaxPitch = 1.5533;   // ~89 degrees
constexpr double kReachDistance = 4.5; // ⚖ docs/01 §4: survival block reach
constexpr float kBaseFov = 70.0f;
constexpr float kSprintFovBoost = 8.0f; // docs/01 §2 feel item: sprint FOV stretch
constexpr int kViewRadius = 6;          // meshed chunk radius around the player
constexpr int kGenPerFrame = 2;         // sync-generation budget (docs: <= 2/frame)
constexpr int kNewMeshPerFrame = 4;     // new-chunk meshing budget/frame

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

// ── minimal original 5x7 bitmap font for the pause menu ────────────────────
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr const char *kFontChars = "PAUSEDRMQIT";
constexpr std::array<std::uint8_t, kGlyphHeight> kGlyphs[] = {
    {0x0F, 0x11, 0x11, 0x0F, 0x10, 0x10, 0x10}, // P
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // S
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // D
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // R
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // Q
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, // I
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
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
        const glm::vec2 p0 = to_ndc(cursor, y_px);
        const glm::vec2 p1 = to_ndc(cursor + static_cast<float>(kGlyphWidth) * scale, y_px + px_height);
        const float u0 = static_cast<float>(glyph) * (kGlyphWidth + 1) * u_span;
        const float u1 = u0 + static_cast<float>(kGlyphWidth) * u_span;
        // Texture v=0 is the FIRST uploaded row = the glyph's top row.
        verts.insert(verts.end(), {p0, {p1.x, p0.y}, p1, p0, {p0.x, p1.y}, p1});
        uvs.insert(uvs.end(), {{u0, 0.0f}, {u1, 0.0f}, {u1, 1.0f}, {u0, 0.0f}, {u0, 1.0f}, {u1, 1.0f}});
        cursor += static_cast<float>(kGlyphWidth + 2) * scale;
    }
}

// Appends one flat NDC quad from a screen-pixel rect.
void draw_rect(float x0, float y0, float x1, float y1, int fb_w, int fb_h, std::vector<glm::vec2> &verts) {
    const auto to_ndc = [&](float x, float y) {
        return glm::vec2((x / static_cast<float>(fb_w)) * 2.0f - 1.0f, 1.0f - (y / static_cast<float>(fb_h)) * 2.0f);
    };
    const glm::vec2 a = to_ndc(x0, y0);
    const glm::vec2 b = to_ndc(x1, y1);
    verts.insert(verts.end(), {a, {b.x, a.y}, b, a, {a.x, b.y}, b});
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

    // ── world ───────────────────────────────────────────────────────────────
    opencraft::client::WorldSource world;

    // Spawn: generate the center chunk first, then take the surface top of
    // the spawn column (formal spawn scan is T009).
    static_cast<void>(world.ensure_chunk(0, 0));
    const int spawn_y = world.surface_height(8, 8);
    OC_LOG_INFO("spawn surface at storage y={}", spawn_y);

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
    glClearColor(0.52f, 0.62f, 0.72f, 1.0f);

    // ── player + simulation state ───────────────────────────────────────────
    phy::PlayerState prev_state;
    prev_state.position = glm::dvec3(8.5, static_cast<double>(spawn_y), 8.5);
    prev_state.on_ground = true;
    prev_state.fall_peak_y = prev_state.position.y;
    phy::PlayerState curr_state = prev_state;

    double view_yaw = 0.0;   // convention: forward = (-sin yaw, -cos yaw)
    double view_pitch = 0.0; // positive = looking down
    bool paused = false;
    bool prev_esc = false;
    bool prev_right = false;
    int place_cooldown = 0;
    std::uint16_t selected_block = world.registry().id_of("stone");
    glm::ivec3 crack_pos{0, 0, 0};
    int crack_stage = -1; // -1 = no overlay
    glm::ivec3 target_pos{0, 0, 0};
    bool has_target = false;
    gam::MiningTracker mining(world.registry());
    opencraft::core::TickClock tick_clock;
    std::vector<std::pair<int, int>> dirty_chunks;
    std::unordered_map<std::int64_t, ChunkRenderable> renderables;
    double last_mesh_ms = 0.0;

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
        render::MeshData mesh = render::build_chunk_mesh(world, render::ChunkPos{cx, cz});
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

    // One 20 TPS logic tick: physics -> targeting -> mining -> placement.
    auto run_tick = [&] {
        // ── input mapping (WASD + space + shift + ctrl) ──────────────────────
        // InputState (T007) has no backward flag; backing up is simulated
        // exactly client-side: a backward+strafe move vector at view yaw θ
        // equals a forward move vector at θ+π with A/D swapped (180° rotation
        // of the move vector), so no physics change is needed.
        const bool w = key_pressed(GLFW_KEY_W);
        const bool s = key_pressed(GLFW_KEY_S);
        const bool a = key_pressed(GLFW_KEY_A);
        const bool d = key_pressed(GLFW_KEY_D);
        phy::InputState in;
        in.yaw = view_yaw;
        in.pitch = view_pitch;
        if (s && !w) {
            in.yaw = view_yaw + 3.14159265358979323846;
            in.forward = true;
            in.right = a;
            in.left = d;
        } else {
            in.forward = w;
            in.left = a;
            in.right = d;
        }
        in.jump = key_pressed(GLFW_KEY_SPACE) != 0;
        in.sneak = key_pressed(GLFW_KEY_LEFT_SHIFT) != 0;
        in.sprint = key_pressed(GLFW_KEY_LEFT_CONTROL) != 0;

        prev_state = curr_state;
        phy::step_player(curr_state, in, world);

        // ── targeting ────────────────────────────────────────────────────────
        const double eye_height = curr_state.pose == phy::Pose::Sneaking ? kEyeSneaking : kEyeStanding;
        const glm::dvec3 eye = curr_state.position + glm::dvec3(0.0, eye_height, 0.0);
        const auto filter = [&](std::uint16_t id) {
            return id != 0 && !world.registry().def_of(id).liquid; // liquids are not targetable
        };
        const gam::VoxelRayHit hit = gam::raycast_voxel(eye, view_dir(), kReachDistance, world, filter);
        has_target = hit.hit;
        target_pos = hit.block_pos;

        // ── mining ───────────────────────────────────────────────────────────
        const bool left_held = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const std::uint16_t target_id = hit.hit ? world.block_at(hit.block_pos.x, hit.block_pos.y, hit.block_pos.z) : 0;
        const auto mining_tick = mining.tick(hit.block_pos, target_id, hit.hit, left_held);
        if (mining_tick.broke) {
            world.set_block(hit.block_pos.x, hit.block_pos.y, hit.block_pos.z, 0, dirty_chunks);
            crack_stage = -1;
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
        if (right_held && (place_cooldown == 0 || !prev_right)) {
            if (hit.hit) {
                const glm::ivec3 cell = gam::placement_cell(hit);
                const auto status = gam::check_placement(world.registry(), world, cell, curr_state.position,
                                                         curr_state.height(), phy::PlayerState::kHalfWidth);
                if (status == gam::PlacementStatus::Ok) {
                    world.set_block(cell.x, cell.y, cell.z, selected_block, dirty_chunks);
                    OC_LOG_INFO("placed {} at ({}, {}, {})", world.registry().string_of(selected_block), cell.x, cell.y,
                                cell.z);
                }
            }
            place_cooldown = 4; // ⚖ retry rhythm whether or not the attempt succeeded
        }
        prev_right = right_held;

        // ── creative palette (infinite blocks via number keys; the real
        //    inventory is M2) ─────────────────────────────────────────────────
        static constexpr std::array<std::pair<int, const char *>, 8> kPalette = {{
            {GLFW_KEY_1, "stone"},
            {GLFW_KEY_2, "cobblestone"},
            {GLFW_KEY_3, "dirt"},
            {GLFW_KEY_4, "planks"},
            {GLFW_KEY_5, "log"},
            {GLFW_KEY_6, "leaves"},
            {GLFW_KEY_7, "glass"},
            {GLFW_KEY_8, "sand"},
        }};
        for (const auto &[key, name] : kPalette) {
            if (key_pressed(key)) {
                selected_block = world.registry().id_of(name);
            }
        }
    };

    // ── main loop ───────────────────────────────────────────────────────────
    double last_frame = glfwGetTime();
    double fps_timer = last_frame;
    int fps_frames = 0;
    double last_cursor_x = 0.0;
    double last_cursor_y = 0.0;
    bool cursor_anchored = false;
    float fov = kBaseFov;
    int stream_meshed = 0;

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
            const int ticks = tick_clock.advance((now - last_frame) * 1000.0);
            last_frame = now;
            for (int i = 0; i < ticks; ++i) {
                run_tick();
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
        const double alpha = tick_clock.alpha();
        const glm::dvec3 cam_pos = prev_state.position + (curr_state.position - prev_state.position) * alpha;
        const double eye_height = curr_state.pose == phy::Pose::Sneaking ? kEyeSneaking : kEyeStanding;
        const glm::dvec3 eye = cam_pos + glm::dvec3(0.0, eye_height, 0.0);

        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        glViewport(0, 0, fb_width, fb_height);

        // Sprint FOV stretch (docs/01 §2 feel item), smoothed.
        const bool sprinting = key_pressed(GLFW_KEY_LEFT_CONTROL) && key_pressed(GLFW_KEY_W);
        const float fov_target = sprinting ? kBaseFov + kSprintFovBoost : kBaseFov;
        fov += (fov_target - fov) * 0.18f;

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
            if (entry.second.translucent) {
                order.push_back(&entry.second);
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
        glEnable(GL_CULL_FACE);

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

        // ── pause menu (drawn over the live scene; no ticks while paused) ───
        if (paused) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);

            double mx = 0.0;
            double my = 0.0;
            glfwGetCursorPos(window, &mx, &my);
            const bool clicked = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            const float bx0 = static_cast<float>(fb_width) * 0.5f - 90.0f;
            const float bx1 = static_cast<float>(fb_width) * 0.5f + 90.0f;
            const float cy = static_cast<float>(fb_height) * 0.5f;
            const bool resume_hover = mx >= bx0 && mx <= bx1 && my >= cy - 64.0 && my <= cy - 28.0;
            const bool quit_hover = mx >= bx0 && mx <= bx1 && my >= cy + 8.0 && my <= cy + 44.0;

            std::vector<glm::vec2> flat;
            draw_rect(0.0f, 0.0f, static_cast<float>(fb_width), static_cast<float>(fb_height), fb_width, fb_height,
                      flat);
            draw_rect(bx0, cy - 64.0f, bx1, cy - 28.0f, fb_width, fb_height, flat);
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
                glUniform4f(ui_flat_shader.uniform_location("u_color"), 0.22f, 0.24f, 0.24f, 0.9f);
                glDrawArrays(GL_TRIANGLES, 0, 6); // quit button
            }

            std::vector<glm::vec2> tverts;
            std::vector<glm::vec2> tuvs;
            draw_text("PAUSED", static_cast<float>(fb_width) / 2.0f - 60.0f, cy - 110.0f, 22.0f, fb_width, fb_height,
                      tverts, tuvs);
            draw_text("RESUME", bx0 + 52.0f, cy - 54.0f, 16.0f, fb_width, fb_height, tverts, tuvs);
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
    glfwTerminate();
    OC_LOG_INFO("clean shutdown");
    return 0;
}
