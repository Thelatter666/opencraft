#include "pause_menu.hpp"

#include <cstddef>
#include <vector>

#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <glm/glm.hpp>

#include "bitmap_font.hpp"

namespace opencraft::client {

PauseMenuOut draw_pause_menu(const PauseMenuResources &res, bool auto_jump_enabled, bool prev_menu_clicked,
                             int fb_width, int fb_height) {

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    double mx = 0.0;
    double my = 0.0;
    glfwGetCursorPos(res.window, &mx, &my);
    // glfwGetCursorPos reports CONTENT pixels; the UI ortho projection
    // below spans the FRAMEBUFFER size. On a HiDPI display the two
    // differ by the backing-store scale, so convert once for every
    // hit-test in this menu.
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
    const bool resume_hover = mx >= bx0 && mx <= bx1 && my >= cy - 64.0 && my <= cy - 28.0;
    // T-D14: AUTO-JUMP sits in the 44 px gap between RESUME and QUIT
    // (cy-28..cy+8): 30 px band [cy-24, cy+4], 4 px breathing room on
    // each side, same 18 px pitch as the existing rows.
    const bool autojump_hover = mx >= bx0 && mx <= bx1 && my >= cy - 24.0 && my <= cy + 4.0;
    const bool quit_hover = mx >= bx0 && mx <= bx1 && my >= cy + 8.0 && my <= cy + 44.0;

    std::vector<glm::vec2> flat;
    draw_rect(0.0f, 0.0f, static_cast<float>(fb_width), static_cast<float>(fb_height), fb_width, fb_height, flat);
    draw_rect(bx0, cy - 64.0f, bx1, cy - 28.0f, fb_width, fb_height, flat);
    draw_rect(bx0, cy - 24.0f, bx1, cy + 4.0f, fb_width, fb_height, flat);
    draw_rect(bx0, cy + 8.0f, bx1, cy + 44.0f, fb_width, fb_height, flat);
    res.flat_shader.use();
    // One color per draw call: backdrop first, then each button with a
    // hover-dependent color.
    {
        render::VertexArray vao;
        vao.bind();
        render::Buffer vbo(render::Buffer::Target::Vertex, flat.data(),
                           static_cast<std::size_t>(flat.size()) * sizeof(glm::vec2), render::Buffer::Usage::Static);
        vbo.bind();
        vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
        glUniform4f(res.flat_shader.uniform_location("u_color"), 0.0f, 0.0f, 0.0f, 0.55f);
        glDrawArrays(GL_TRIANGLES, 0, 6); // backdrop only
        flat.erase(flat.begin(), flat.begin() + 6);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(flat.size() * sizeof(glm::vec2)), flat.data(),
                     GL_STATIC_DRAW);
        glUniform4f(res.flat_shader.uniform_location("u_color"), 0.22f, 0.24f, 0.22f, 0.9f);
        glDrawArrays(GL_TRIANGLES, 0, 6); // resume button
        flat.erase(flat.begin(), flat.begin() + 6);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(flat.size() * sizeof(glm::vec2)), flat.data(),
                     GL_STATIC_DRAW);
        glUniform4f(res.flat_shader.uniform_location("u_color"), autojump_hover ? 0.34f : 0.22f,
                    autojump_hover ? 0.36f : 0.24f, 0.24f, 0.9f);
        glDrawArrays(GL_TRIANGLES, 0, 6); // auto-jump button
        flat.erase(flat.begin(), flat.begin() + 6);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(flat.size() * sizeof(glm::vec2)), flat.data(),
                     GL_STATIC_DRAW);
        glUniform4f(res.flat_shader.uniform_location("u_color"), 0.22f, 0.24f, 0.24f, 0.9f);
        glDrawArrays(GL_TRIANGLES, 0, 6); // quit button
    }

    std::vector<glm::vec2> tverts;
    std::vector<glm::vec2> tuvs;
    draw_text("PAUSED", static_cast<float>(fb_width) / 2.0f - 60.0f, cy - 110.0f, 22.0f, fb_width, fb_height, tverts,
              tuvs);
    draw_text("RESUME", bx0 + 52.0f, cy - 54.0f, 16.0f, fb_width, fb_height, tverts, tuvs);
    // Label carries the live state so the toggle is observable
    // (MC shows ON/OFF on its accessibility options, not a bare name).
    draw_text(auto_jump_enabled ? "AUTO-JUMP ON" : "AUTO-JUMP OFF", bx0 + 19.0f, cy - 14.0f, 16.0f, fb_width, fb_height,
              tverts, tuvs);
    draw_text("QUIT", bx0 + 62.0f, cy + 18.0f, 16.0f, fb_width, fb_height, tverts, tuvs);
    res.text_shader.use();
    glUniform4f(res.text_shader.uniform_location("u_color"), 1.0f, 1.0f, 1.0f, 1.0f);
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
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tverts.size()));
    }
    const bool resume_clicked = clicked && resume_hover;
    // Auto-jump toggle is the only button whose action is NOT idempotent
    // (RESUME/QUIT are state-setters), so it needs a click EDGE: holding the
    // button must flip the option once, not every frame it is pressed.
    const bool autojump_clicked = clicked && !prev_menu_clicked && autojump_hover;
    const bool quit_clicked = clicked && quit_hover;

    return PauseMenuOut{resume_clicked, quit_clicked, autojump_clicked ? !auto_jump_enabled : auto_jump_enabled,
                        clicked};
}

} // namespace opencraft::client
