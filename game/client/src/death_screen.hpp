#pragma once

// T-D45: the death screen - a full-screen dim, "YOU DIED" and one RESPAWN
// button. A new module rather than a fourth button in the pause menu: every
// responsive click coordinate in the repository is bound to that menu's layout
// (3 bands, 18 px pitch), and T-D19 is the recorded cost of moving them.
//
// Header-only on purpose: the client's CMake source list is not in this card's
// 白名单, so the death screen ships as a header instead of adding a .cpp the
// build would not see. It reuses only the existing 5x7 font (draw_text /
// draw_rect in bitmap_font.hpp) - no new font, no new atlas.

#include <cstddef>
#include <vector>

// GLFW must not pull in a GL header of its own post-split (main.cpp does the
// same): glad is loaded separately.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "bitmap_font.hpp"
#include "opencraft/render/rhi.hpp"
#include "pause_menu.hpp" // PauseMenuResources: the same four GL handles

namespace opencraft::client {

// The GL handles the screen draws through - exactly the pause menu's, aliased
// rather than duplicated (window + flat shader + text shader + font).
using DeathScreenResources = PauseMenuResources;

struct DeathScreenOut {
    bool respawn_clicked = false;
    // This frame's primary-button state, so the caller can derive the click
    // EDGE: the respawn button is a state-changing action and holding the
    // button must not re-trigger it every frame (the pause menu's auto-jump
    // toggle has the same shape).
    bool clicked = false;
};

// Draws the screen over the live scene and reports whether RESPAWN was clicked.
//
// The button's band is 36 px tall in the middle of the screen, matching the
// pause menu's rows so the two read as the same UI. The cursor is read from
// GLFW in CONTENT pixels and converted to the framebuffer space the UI
// projection uses (HiDPI: the two differ by the backing-store scale) - the same
// conversion, for the same reason, as draw_pause_menu.
[[nodiscard]] inline DeathScreenOut draw_death_screen(const DeathScreenResources &res, const bool prev_clicked,
                                                      const int fb_width, const int fb_height) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    double mx = 0.0;
    double my = 0.0;
    glfwGetCursorPos(res.window, &mx, &my);
    int win_w = 0;
    int win_h = 0;
    glfwGetWindowSize(res.window, &win_w, &win_h);
    if (win_w > 0 && win_h > 0 && (win_w != fb_width || win_h != fb_height)) {
        mx *= static_cast<double>(fb_width) / static_cast<double>(win_w);
        my *= static_cast<double>(fb_height) / static_cast<double>(win_h);
    }
    const bool clicked = glfwGetMouseButton(res.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    const float bx0 = static_cast<float>(fb_width) * 0.5f - 90.0f;
    const float bx1 = static_cast<float>(fb_width) * 0.5f + 90.0f;
    const float cy = static_cast<float>(fb_height) * 0.5f;
    const float by0 = cy + 12.0f;
    const float by1 = by0 + 36.0f;
    const bool hover = mx >= bx0 && mx <= bx1 && my >= by0 && my <= by1;

    // Backdrop, then the button: one flat shader, one color per draw call.
    {
        std::vector<glm::vec2> flat;
        draw_rect(0.0f, 0.0f, static_cast<float>(fb_width), static_cast<float>(fb_height), fb_width, fb_height, flat);
        res.flat_shader.use();
        render::VertexArray vao;
        vao.bind();
        render::Buffer vbo(render::Buffer::Target::Vertex, flat.data(),
                           static_cast<std::size_t>(flat.size()) * sizeof(glm::vec2), render::Buffer::Usage::Static);
        vbo.bind();
        vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
        glUniform4f(res.flat_shader.uniform_location("u_color"), 0.35f, 0.0f, 0.0f, 0.55f);
        glDrawArrays(GL_TRIANGLES, 0, 6); // the red dim of a death screen

        flat.clear();
        draw_rect(bx0, by0, bx1, by1, fb_width, fb_height, flat);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(flat.size() * sizeof(glm::vec2)), flat.data(),
                     GL_STREAM_DRAW);
        glUniform4f(res.flat_shader.uniform_location("u_color"), hover ? 0.34f : 0.22f, hover ? 0.26f : 0.24f,
                    hover ? 0.26f : 0.24f, 0.9f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // ⚠ The 5x7 font has no space glyph (bitmap_font.cpp's kFontChars), so the
    // gap in "YOU DIED" is the unknown-glyph advance - narrower than a real
    // word space. Cosmetic; a wider gap would mean inventing a glyph.
    //
    // Centring: draw_text advances (kGlyphWidth + 2) * (px / kGlyphHeight) per
    // glyph, which is exactly px_height, so an n-character string is n of them
    // wide (last glyph's advance slightly overshoots its 5 px of ink).
    constexpr float kTitlePx = 26.0f;
    const char *const kTitle = "YOU DIED";
    const char *const kButton = "RESPAWN";
    const auto text_width = [](const char *text, const float px) {
        std::size_t n = 0;
        for (const char *c = text; *c != '\0'; ++c) {
            ++n;
        }
        return static_cast<float>(n) * px;
    };
    std::vector<glm::vec2> tverts;
    std::vector<glm::vec2> tuvs;
    draw_text(kTitle, static_cast<float>(fb_width) * 0.5f - text_width(kTitle, kTitlePx) * 0.5f, cy - 60.0f, kTitlePx,
              fb_width, fb_height, tverts, tuvs);
    constexpr float kButtonPx = 16.0f;
    draw_text(kButton, (bx0 + bx1) * 0.5f - text_width(kButton, kButtonPx) * 0.5f, by0 + 10.0f, kButtonPx, fb_width,
              fb_height, tverts, tuvs);
    res.text_shader.use();
    res.font.bind(1);
    {
        render::VertexArray vao;
        vao.bind();
        render::Buffer vbo(render::Buffer::Target::Vertex, tverts.data(),
                           static_cast<std::size_t>(tverts.size()) * sizeof(glm::vec2), render::Buffer::Usage::Static);
        vbo.bind();
        vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
        render::Buffer uvbo(render::Buffer::Target::Vertex, tuvs.data(),
                            static_cast<std::size_t>(tuvs.size()) * sizeof(glm::vec2), render::Buffer::Usage::Static);
        uvbo.bind();
        vao.set_attribute(1, 2, GL_FLOAT, sizeof(glm::vec2), 0);
        glUniform4f(res.text_shader.uniform_location("u_color"), 1.0f, 1.0f, 1.0f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tverts.size()));
    }

    return DeathScreenOut{clicked && !prev_clicked && hover, clicked};
}

} // namespace opencraft::client
