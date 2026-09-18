#include "shaders.hpp"

namespace opencraft::client {

const char kVertexShader[] = R"(
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

const char kFragmentShader[] = R"(
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
const char kWireVertexShader[] = R"(
#version 410 core
layout(location=0) in vec3 a_pos;

uniform mat4 u_mvp;
uniform vec3 u_offset;
uniform float u_scale;

void main() {
    gl_Position = u_mvp * vec4(a_pos * u_scale + u_offset, 1.0);
}
)";

const char kWireFragmentShader[] = R"(
#version 410 core
uniform vec4 u_color;
out vec4 frag_color;
void main() { frag_color = u_color; }
)";

// Crack overlay cube: unit cube with per-vertex uv; the 10 destruction stages
// share one buffer, the stage tile index comes in as a uniform.
const char kCrackVertexShader[] = R"(
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

const char kCrackFragmentShader[] = R"(
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
const char kParticleVertexShader[] = R"(
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

const char kParticleFragmentShader[] = R"(
#version 410 core
in vec4 v_color;
out vec4 frag_color;
void main() { frag_color = v_color; }
)";

// Mob voxel models (T-B1): the same "sample a texture, discard transparent
// texels" fragment work the crack shader does, with two differences that are
// the whole reason this needs its own program (research/12 §6.2):
//   * the UV is PER VERTEX (each face can be a different palette cell) instead
//     of a uniform tile index;
//   * the transform is PER JOINT, so the vertex shader gets the already
//     composed joint matrix in u_mvp rather than a whole-model transform.
// u_tint is the presentation-only multiply: 受击闪红 and the fuse's warning glow.
const char kMobVertexShader[] = R"(
#version 410 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_uv;

uniform mat4 u_mvp;

out vec2 v_uv;

void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_uv = a_uv;
}
)";

const char kMobFragmentShader[] = R"(
#version 410 core
uniform sampler2D u_palette;
uniform vec4 u_tint;

in vec2 v_uv;
out vec4 frag_color;

void main() {
    vec4 texel = texture(u_palette, v_uv);
    if (texel.a < 0.5) { discard; }
    frag_color = vec4(texel.rgb * u_tint.rgb, texel.a * u_tint.a);
}
)";

// UI: flat quads (backdrop, buttons) and bitmap-font text.
const char kUiFlatVertexShader[] = R"(
#version 410 core
layout(location=0) in vec2 a_pos;
uniform vec4 u_color;
out vec4 v_color;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_color = u_color;
}
)";

const char kUiFlatFragmentShader[] = R"(
#version 410 core
in vec4 v_color;
out vec4 frag_color;
void main() { frag_color = v_color; }
)";

const char kUiTextVertexShader[] = R"(
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

const char kUiTextFragmentShader[] = R"(
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

} // namespace opencraft::client
