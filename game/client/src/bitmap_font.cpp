#include "bitmap_font.hpp"

#include <array>
#include <cstddef>

namespace opencraft::client {

namespace {

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

} // namespace

// FontImage lives in bitmap_font.hpp (the caller uploads the texture).
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

} // namespace opencraft::client
