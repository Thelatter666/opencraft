#pragma once

// Original 5x7 bitmap font (T009): the glyph table plus the CPU-side string
// and rectangle builders for the HUD. Moved verbatim out of main.cpp by T-M1
// (pure code motion); the kGlyphs table is byte-identical to the pre-split
// file. The GPU font texture is uploaded by the caller (main.cpp).

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace opencraft::client {

struct FontImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels; // RGBA8 like AtlasImage
};

// Rasterises every glyph in kGlyphs into one horizontal strip.
[[nodiscard]] FontImage build_font_texture();

// Appends one textured string (screen pixels in, y = top, NDC quads out).
// Quads are emitted counter-clockwise in NDC (y up) so they are front-facing
// under the default GL_CCW/M_LESS state.
void draw_text(const std::string &text, float x_px, float y_px, float px_height, int fb_w, int fb_h,
               std::vector<glm::vec2> &verts, std::vector<glm::vec2> &uvs);

// Appends one flat NDC quad from a screen-pixel rect. CCW winding, see
// draw_text.
void draw_rect(float x0, float y0, float x1, float y1, int fb_w, int fb_h, std::vector<glm::vec2> &verts);

} // namespace opencraft::client
