#include "hud.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "bitmap_font.hpp"
#include "client_config.hpp"
#include "opencraft/render/mesher.hpp"
#include "world.hpp"

namespace opencraft::client {

void draw_hud(const HudResources &res, const HudState &state) {
    glDisable(GL_DEPTH_TEST);

    constexpr float kSlotPx = 24.0f;
    constexpr float kSlotGap = 2.0f;
    const float bar_w = kHotbarSlots * kSlotPx + (kHotbarSlots - 1) * kSlotGap;
    const float bar_x0 = static_cast<float>(state.fb_width) / 2.0f - bar_w / 2.0f;
    const float bar_y0 = static_cast<float>(state.fb_height) - kSlotPx - 10.0f;

    // Slot backdrops (one flat-color draw call for all of them).
    {
        std::vector<glm::vec2> flat;
        for (int i = 0; i < kHotbarSlots; ++i) {
            const float x0 = bar_x0 + static_cast<float>(i) * (kSlotPx + kSlotGap);
            draw_rect(x0, bar_y0, x0 + kSlotPx, bar_y0 + kSlotPx, state.fb_width, state.fb_height, flat);
        }
        res.flat_shader.use();
        render::VertexArray vao;
        vao.bind();
        render::Buffer vbo(render::Buffer::Target::Vertex, flat.data(),
                           static_cast<std::size_t>(flat.size()) * sizeof(glm::vec2), render::Buffer::Usage::Static);
        vbo.bind();
        vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
        glUniform4f(res.flat_shader.uniform_location("u_color"), 0.0f, 0.0f, 0.0f, 0.55f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(flat.size()));
    }
    // Selected slot highlight: 2 px white frame (4 thin rects).
    {
        std::vector<glm::vec2> flat;
        const float fx0 = bar_x0 + static_cast<float>(state.selected_slot) * (kSlotPx + kSlotGap);
        const float fx1 = fx0 + kSlotPx;
        const float fy0 = bar_y0;
        const float fy1 = bar_y0 + kSlotPx;
        draw_rect(fx0 - 2.0f, fy0 - 2.0f, fx1 + 2.0f, fy0, state.fb_width, state.fb_height, flat);
        draw_rect(fx0 - 2.0f, fy1, fx1 + 2.0f, fy1 + 2.0f, state.fb_width, state.fb_height, flat);
        draw_rect(fx0 - 2.0f, fy0, fx0, fy1, state.fb_width, state.fb_height, flat);
        draw_rect(fx1, fy0, fx1 + 2.0f, fy1, state.fb_width, state.fb_height, flat);
        res.flat_shader.use();
        render::VertexArray vao;
        vao.bind();
        render::Buffer vbo(render::Buffer::Target::Vertex, flat.data(),
                           static_cast<std::size_t>(flat.size()) * sizeof(glm::vec2), render::Buffer::Usage::Static);
        vbo.bind();
        vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
        glUniform4f(res.flat_shader.uniform_location("u_color"), 0.95f, 0.95f, 0.95f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(flat.size()));
    }
    // Slot icons: reuse the terrain shader's tile math (proven path)
    // with a pixel-space ortho projection; one quad per non-empty cell.
    // Cells whose item has no generated texture (food, tools, armour) are flat
    // tinted quads instead, batched per colour and drawn after the tiles.
    {
        const glm::mat4 hud_ortho =
            glm::ortho(0.0f, static_cast<float>(state.fb_width), static_cast<float>(state.fb_height), 0.0f);
        std::vector<render::MeshVertex> icon_verts;
        icon_verts.reserve(kHotbarSlots * 4);
        std::vector<std::uint32_t> icon_indices;
        icon_indices.reserve(kHotbarSlots * 6);

        struct TintGroup {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            std::vector<glm::vec2> rects;
        };

        std::vector<TintGroup> tint_groups;
        const float pad = 3.0f;
        const std::uint16_t water = res.world.water_block_id();
        for (int i = 0; i < kHotbarSlots; ++i) {
            const game::ItemStack &cell = state.hotbar[static_cast<std::size_t>(i)];
            if (cell.empty()) {
                continue; // an empty cell keeps the backdrop alone
            }
            const float x0 = bar_x0 + static_cast<float>(i) * (kSlotPx + kSlotGap) + pad;
            const float y0 = bar_y0 + pad;
            const float x1 = x0 + kSlotPx - pad * 2.0f;
            const float y1 = y0 + kSlotPx - pad * 2.0f;
            // A block item shows its own block; a vessel shows the water it
            // carries (dimmed while empty) -- the T-F1 hand art, now driven by
            // the held item instead of a boolean.
            const StandInVisual visual = stand_in_visual_of(*state.items, cell, water, state.vessels);
            if (visual.block == game::kNoBlock) {
                const glm::vec3 tint = item_tint(state.items->string_of(cell.item));
                std::vector<glm::vec2> *rects = nullptr;
                for (TintGroup &group : tint_groups) {
                    if (group.r == tint.r && group.g == tint.g && group.b == tint.b) {
                        rects = &group.rects;
                        break;
                    }
                }
                if (rects == nullptr) {
                    tint_groups.push_back({tint.r, tint.g, tint.b, {}});
                    rects = &tint_groups.back().rects;
                }
                draw_rect(x0, y0, x1, y1, state.fb_width, state.fb_height, *rects);
                continue;
            }
            const std::uint16_t tile = static_cast<std::uint16_t>(visual.block * 3 + 1); // side tile
            const std::uint16_t base = static_cast<std::uint16_t>(icon_verts.size());
            // uv corner codes: 0=(0,0) tl, 1=(1,0) tr, 2=(0,1) bl, 3=(1,1) br.
            icon_verts.push_back(
                {static_cast<std::uint16_t>(x0), static_cast<std::uint16_t>(y0), 0, tile, 0, visual.shade});
            icon_verts.push_back(
                {static_cast<std::uint16_t>(x1), static_cast<std::uint16_t>(y0), 0, tile, 1, visual.shade});
            icon_verts.push_back(
                {static_cast<std::uint16_t>(x1), static_cast<std::uint16_t>(y1), 0, tile, 3, visual.shade});
            icon_verts.push_back(
                {static_cast<std::uint16_t>(x0), static_cast<std::uint16_t>(y1), 0, tile, 2, visual.shade});
            icon_indices.insert(icon_indices.end(),
                                {static_cast<std::uint32_t>(base), static_cast<std::uint32_t>(base + 1),
                                 static_cast<std::uint32_t>(base + 2), static_cast<std::uint32_t>(base),
                                 static_cast<std::uint32_t>(base + 2), static_cast<std::uint32_t>(base + 3)});
        }
        if (!icon_verts.empty()) {
            res.terrain_shader.use();
            glUniformMatrix4fv(res.terrain_shader.uniform_location("u_mvp"), 1, GL_FALSE, &hud_ortho[0][0]);
            glUniform3f(res.terrain_shader.uniform_location("u_chunk_origin"), 0.0f, 0.0f, 0.0f);
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
        for (const TintGroup &group : tint_groups) {
            res.flat_shader.use();
            render::VertexArray vao;
            vao.bind();
            render::Buffer vbo(render::Buffer::Target::Vertex, group.rects.data(),
                               group.rects.size() * sizeof(glm::vec2), render::Buffer::Usage::Static);
            vbo.bind();
            vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
            glUniform4f(res.flat_shader.uniform_location("u_color"), group.r, group.g, group.b, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(group.rects.size()));
        }
    }
    // Selected item name (uppercase) above the hotbar. An empty hand has no
    // name to show.
    if (!state.hotbar[static_cast<std::size_t>(state.selected_slot)].empty()) {
        std::string name =
            state.items->def_of(state.hotbar[static_cast<std::size_t>(state.selected_slot)].item).display_name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        std::vector<glm::vec2> tverts;
        std::vector<glm::vec2> tuvs;
        // Own line above the hearts row (bar_y0-24); drawing both at
        // the same y made the name collide with the hearts.
        draw_text(name, static_cast<float>(state.fb_width) / 2.0f - static_cast<float>(name.size()) * 7.0f,
                  bar_y0 - 46.0f, 16.0f, state.fb_width, state.fb_height, tverts, tuvs);
        res.text_shader.use();
        res.font.bind(1);
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
    // Stack counts: a cell holding more than one unit prints its number in the
    // cell's bottom-right corner (MC convention). Empty cells and single units
    // print nothing. All counts are one text batch / one draw call.
    {
        std::vector<glm::vec2> tverts;
        std::vector<glm::vec2> tuvs;
        constexpr float kCountPx = 10.0f;
        bool any = false;
        for (int i = 0; i < kHotbarSlots; ++i) {
            const game::ItemStack &cell = state.hotbar[static_cast<std::size_t>(i)];
            if (cell.count <= 1) {
                continue;
            }
            const std::string text = std::to_string(cell.count);
            // draw_text advances one glyph per kCountPx, so the string's width
            // is text.size() * kCountPx; right-align it inside the cell.
            const float x1 = bar_x0 + static_cast<float>(i + 1) * (kSlotPx + kSlotGap) - kSlotGap - 1.0f;
            const float x0 = x1 - static_cast<float>(text.size()) * kCountPx;
            draw_text(text, x0, bar_y0 + kSlotPx - kCountPx - 1.0f, kCountPx, state.fb_width, state.fb_height, tverts,
                      tuvs);
            any = true;
        }
        if (any) {
            res.text_shader.use();
            res.font.bind(1);
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
            glUniform4f(res.text_shader.uniform_location("u_color"), 1.0f, 1.0f, 1.0f, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tverts.size()));
        }
    }
    // Health: 10 hearts driven by PlayerState::health (T007), 2 hp per
    // heart. Red pass (full + half), then a dim pass for empty ones.
    {
        const int full = static_cast<int>(state.health) / 2;
        const bool half = static_cast<int>(state.health) % 2 != 0;
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
            draw_text(text, bar_x0, hearts_y, heart_px, state.fb_width, state.fb_height, tverts, tuvs);
            res.text_shader.use();
            res.font.bind(1);
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
            glUniform4f(res.text_shader.uniform_location("u_color"), r, g, b, a);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tverts.size()));
        };
        draw_hearts(hearts_empty, 0.25f, 0.25f, 0.25f, 0.9f);
        draw_hearts(hearts_red, 0.85f, 0.15f, 0.15f, 1.0f);
    }

    glEnable(GL_DEPTH_TEST);
}

} // namespace opencraft::client
