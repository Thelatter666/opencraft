#include "hud.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "bitmap_font.hpp"
#include "client_config.hpp"
#include "opencraft/render/mesher.hpp"
#include "world.hpp"

namespace opencraft::client {

namespace {

// ── one item's icon (T-I2's stand-in art, factored out by T-D60) ────────────
// A cell's icon: a block item shows its own block tile through the terrain
// shader with a pixel-space ortho projection, and an item with no generated
// texture (food, tools, armour, the stick) is a flat tint from item_tint(). The
// tiles batch into one draw call and the tints into one per distinct colour.
//
// This used to live inline in draw_hud's hotbar loop; the crafting panel draws
// the same thing in 51 cells, so the code moved here rather than being copied -
// two copies would eventually disagree about what a stack looks like. The
// hotbar's own rects, order and batching are unchanged.
struct IconCell {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    game::ItemStack stack;
};

struct TintGroup {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    std::vector<glm::vec2> rects;
};

void draw_icons(const HudResources &res, const game::ItemRegistry &items, const VesselIds &vessels,
                const std::uint16_t water_block, const std::vector<IconCell> &cells, const int fb_width,
                const int fb_height) {
    const glm::mat4 hud_ortho = glm::ortho(0.0f, static_cast<float>(fb_width), static_cast<float>(fb_height), 0.0f);
    std::vector<render::MeshVertex> icon_verts;
    icon_verts.reserve(cells.size() * 4);
    std::vector<std::uint32_t> icon_indices;
    icon_indices.reserve(cells.size() * 6);
    std::vector<TintGroup> tint_groups;

    for (const IconCell &cell : cells) {
        if (cell.stack.empty()) {
            continue; // an empty cell keeps the backdrop alone
        }
        // A block item shows its own block; a vessel shows the water it carries
        // (dimmed while empty) -- the T-F1 hand art, now driven by the held item
        // instead of a boolean.
        const StandInVisual visual = stand_in_visual_of(items, cell.stack, water_block, vessels);
        if (visual.block == game::kNoBlock) {
            const glm::vec3 tint = item_tint(items.string_of(cell.stack.item));
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
            draw_rect(cell.x0, cell.y0, cell.x1, cell.y1, fb_width, fb_height, *rects);
            continue;
        }
        const std::uint16_t tile = static_cast<std::uint16_t>(visual.block * 3 + 1); // side tile
        const auto base = static_cast<std::uint32_t>(icon_verts.size());
        // uv corner codes: 0=(0,0) tl, 1=(1,0) tr, 2=(0,1) bl, 3=(1,1) br.
        icon_verts.push_back(
            {static_cast<std::uint16_t>(cell.x0), static_cast<std::uint16_t>(cell.y0), 0, tile, 0, visual.shade});
        icon_verts.push_back(
            {static_cast<std::uint16_t>(cell.x1), static_cast<std::uint16_t>(cell.y0), 0, tile, 1, visual.shade});
        icon_verts.push_back(
            {static_cast<std::uint16_t>(cell.x1), static_cast<std::uint16_t>(cell.y1), 0, tile, 3, visual.shade});
        icon_verts.push_back(
            {static_cast<std::uint16_t>(cell.x0), static_cast<std::uint16_t>(cell.y1), 0, tile, 2, visual.shade});
        icon_indices.insert(icon_indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
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
        render::Buffer vbo(render::Buffer::Target::Vertex, group.rects.data(), group.rects.size() * sizeof(glm::vec2),
                           render::Buffer::Usage::Static);
        vbo.bind();
        vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
        glUniform4f(res.flat_shader.uniform_location("u_color"), group.r, group.g, group.b, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(group.rects.size()));
    }
}

// One flat-colour rect batch through the flat shader. Every panel in this file
// draws its backdrops this way.
void draw_flat(const HudResources &res, const std::vector<glm::vec2> &verts, const float r, const float g,
               const float b, const float a) {
    if (verts.empty()) {
        return;
    }
    res.flat_shader.use();
    render::VertexArray vao;
    vao.bind();
    render::Buffer vbo(render::Buffer::Target::Vertex, verts.data(), verts.size() * sizeof(glm::vec2),
                       render::Buffer::Usage::Static);
    vbo.bind();
    vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
    glUniform4f(res.flat_shader.uniform_location("u_color"), r, g, b, a);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
}

// One text batch through the font shader, in one colour.
void draw_text_batch(const HudResources &res, const std::vector<glm::vec2> &verts, const std::vector<glm::vec2> &uvs,
                     const float r, const float g, const float b, const float a) {
    if (verts.empty()) {
        return;
    }
    res.text_shader.use();
    res.font.bind(1);
    render::VertexArray vao;
    vao.bind();
    render::Buffer vbo(render::Buffer::Target::Vertex, verts.data(), verts.size() * sizeof(glm::vec2),
                       render::Buffer::Usage::Static);
    vbo.bind();
    vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
    render::Buffer uvbo(render::Buffer::Target::Vertex, uvs.data(), uvs.size() * sizeof(glm::vec2),
                        render::Buffer::Usage::Static);
    uvbo.bind();
    vao.set_attribute(1, 2, GL_FLOAT, sizeof(glm::vec2), 0);
    glUniform4f(res.text_shader.uniform_location("u_color"), r, g, b, a);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
}

// The count readout in a cell's bottom-right corner (MC convention): nothing for
// an empty cell or a single unit, the number otherwise. One batch for the lot.
void draw_counts(const HudResources &res, const std::vector<IconCell> &cells, const int fb_width, const int fb_height) {
    constexpr float kCountPx = 10.0f;
    std::vector<glm::vec2> tverts;
    std::vector<glm::vec2> tuvs;
    for (const IconCell &cell : cells) {
        if (cell.stack.count <= 1) {
            continue;
        }
        const std::string text = std::to_string(cell.stack.count);
        // draw_text advances one glyph per kCountPx, so the string's width is
        // text.size() * kCountPx; right-align it inside the cell.
        const float x1 = cell.x1 - 1.0f;
        const float x0 = x1 - static_cast<float>(text.size()) * kCountPx;
        draw_text(text, x0, cell.y1 - kCountPx - 1.0f, kCountPx, fb_width, fb_height, tverts, tuvs);
    }
    draw_text_batch(res, tverts, tuvs, 1.0f, 1.0f, 1.0f, 1.0f);
}

} // namespace

void draw_hud(const HudResources &res, const HudState &state) {
    if (state.dead) {
        // T-D45: nothing of the HUD survives a death. Returning before the
        // depth-test toggle below leaves the GL state exactly as the caller
        // left it, which is what the contract on the declaration asks for.
        return;
    }
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
    // Slot icons: the shared item-icon pass (see draw_icons above), whose rects
    // and batching are exactly what this block used to do inline.
    {
        std::vector<IconCell> cells;
        cells.reserve(kHotbarSlots);
        const float pad = 3.0f;
        for (int i = 0; i < kHotbarSlots; ++i) {
            const game::ItemStack &cell = state.hotbar[static_cast<std::size_t>(i)];
            if (cell.empty()) {
                continue; // an empty cell keeps the backdrop alone
            }
            const float x0 = bar_x0 + static_cast<float>(i) * (kSlotPx + kSlotGap) + pad;
            const float y0 = bar_y0 + pad;
            cells.push_back({x0, y0, x0 + kSlotPx - pad * 2.0f, y0 + kSlotPx - pad * 2.0f, cell});
        }
        draw_icons(res, *state.items, state.vessels, res.world.water_block_id(), cells, state.fb_width,
                   state.fb_height);
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
    // ── attack charge (T-D59) ────────────────────────────────────────────
    // One flat bar, drawn through the same draw_rect path the backdrops and the
    // selection frame use. Layout, and the reason for it:
    //
    //   name row      y = bar_y0 - 46, 16 px tall
    //   hearts row    y = bar_y0 - 22, 14 px tall  → ends at bar_y0 - 8
    //   ATTACK BAR    y = bar_y0 -  6 … bar_y0 - 2 (4 px, 2 px clear above)
    //   hotbar        y = bar_y0 … bar_y0 + 24
    //
    // The stack above the hotbar is full - T009's comment two screens up records
    // the name/hearts collision that came of doubling up a row - so this bar gets
    // its own line, in the only band left. It is full hotbar width and starts at
    // bar_x0, so it reads as part of the hotbar rather than as a stray widget.
    //
    // The 84.8% gate is marked twice, deliberately: a dark tick that is always
    // visible (drawn last, so neither the track nor the fill can hide it) and the
    // fill's own colour change. One of them is a landmark you can find without
    // knowing the colour, the other is what you actually notice while fighting.
    {
        auto fill_rects = [&](const std::vector<glm::vec2> &verts, const float r, const float g, const float b,
                              const float a) {
            if (verts.empty()) {
                return;
            }
            res.flat_shader.use();
            render::VertexArray vao;
            vao.bind();
            render::Buffer vbo(render::Buffer::Target::Vertex, verts.data(), verts.size() * sizeof(glm::vec2),
                               render::Buffer::Usage::Static);
            vbo.bind();
            vao.set_attribute(0, 2, GL_FLOAT, sizeof(glm::vec2), 0);
            glUniform4f(res.flat_shader.uniform_location("u_color"), r, g, b, a);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
        };

        constexpr float kChargeBarPx = 4.0f;
        constexpr float kGateTickHalfPx = 1.0f;
        const float charge = static_cast<float>(std::clamp(state.attack_charge, 0.0, 1.0));
        const float cy0 = bar_y0 - 6.0f;
        const float cy1 = cy0 + kChargeBarPx;
        const float gate_x = bar_x0 + bar_w * static_cast<float>(game::kAttackChargeThreshold);

        std::vector<glm::vec2> track;
        draw_rect(bar_x0, cy0, bar_x0 + bar_w, cy1, state.fb_width, state.fb_height, track);
        fill_rects(track, 0.22f, 0.22f, 0.25f, 0.85f);

        // The filled part. `>=` and not `>`: the gate is inclusive, and the two
        // sides of it are exactly the case the authority's own `charged` flag
        // splits on, so the bar turns at the same instant the crit unlocks.
        std::vector<glm::vec2> filled;
        draw_rect(bar_x0, cy0, bar_x0 + bar_w * charge, cy1, state.fb_width, state.fb_height, filled);
        if (charge >= static_cast<float>(game::kAttackChargeThreshold)) {
            fill_rects(filled, 0.98f, 0.92f, 0.32f, 1.0f); // charged: crit and sprint shove are live
        } else {
            fill_rects(filled, 0.90f, 0.54f, 0.12f, 1.0f); // charging: a hit, but not a full one
        }

        // The gate landmark. One pixel proud of the bar on each side so it is
        // still findable when the fill has swallowed it.
        std::vector<glm::vec2> gate;
        draw_rect(gate_x - kGateTickHalfPx, cy0 - 1.0f, gate_x + kGateTickHalfPx, cy1 + 1.0f, state.fb_width,
                  state.fb_height, gate);
        fill_rects(gate, 0.05f, 0.05f, 0.06f, 0.95f);
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

// ── T-D60: the crafting screen ─────────────────────────────────────────────
// Drawn over the live scene while a surface is open. Every position comes from
// CraftingDrawState::layout, which is also what the click was hit-tested
// against, so the panel cannot show one thing and accept clicks on another.
//
// The visual language is the HUD's own: the same flat rects, the same item icons
// (a block's side tile or the flat tint), the same corner count readout. No new
// font, no new atlas, no new shader - the card's scope is behaviour, and the look
// is deliberately the hotbar's.
void draw_crafting_screen(const HudResources &res, const CraftingDrawState &state) {
    if (state.crafting == nullptr || state.inventory == nullptr || state.items == nullptr || !state.crafting->open()) {
        return;
    }
    const CraftingLayout &layout = state.layout;
    const int fbw = static_cast<int>(layout.fb_width);
    const int fbh = static_cast<int>(layout.fb_height);
    const int grid_width = state.crafting->width();
    const game::ItemRegistry &items = *state.items;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // The dim, then the window's own backdrop.
    {
        std::vector<glm::vec2> flat;
        draw_rect(0.0f, 0.0f, static_cast<float>(fbw), static_cast<float>(fbh), fbw, fbh, flat);
        draw_flat(res, flat, 0.04f, 0.04f, 0.06f, 0.6f);
        flat.clear();
        draw_rect(layout.panel_x0, layout.panel_y0, layout.panel_x1, layout.panel_y1, fbw, fbh, flat);
        draw_flat(res, flat, 0.15f, 0.15f, 0.17f, 0.95f);
    }

    // Every cell's backdrop in ONE batch: the craft grid, the result cell, and
    // the player's 41. The icon rect is inset by 3 px inside the backdrop, the
    // same inset the hotbar's cells use.
    constexpr float kIconInset = 3.0f;
    std::vector<glm::vec2> backdrops;
    std::vector<IconCell> icons;
    std::vector<IconCell> counts;
    const auto add_cell = [&](const CraftSlotRef &ref, const game::ItemStack &stack) {
        const CraftingLayout::Rect rect = layout.rect(ref, grid_width);
        if (rect.x1 <= rect.x0) {
            return;
        }
        draw_rect(rect.x0, rect.y0, rect.x1, rect.y1, fbw, fbh, backdrops);
        icons.push_back(
            {rect.x0 + kIconInset, rect.y0 + kIconInset, rect.x1 - kIconInset, rect.y1 - kIconInset, stack});
        counts.push_back({rect.x0, rect.y0, rect.x1, rect.y1, stack});
    };
    for (int i = 0; i < grid_width * grid_width; ++i) {
        add_cell(CraftSlotRef{CraftSlotKind::Grid, i}, state.crafting->grid[i]);
    }
    for (int slot = 0; slot < game::kInventorySlots; ++slot) {
        add_cell(CraftSlotRef{CraftSlotKind::Inventory, slot}, state.inventory->slot(slot));
    }
    // The result cell shows what the grid makes, not a stored stack: it is the
    // recipe's output, so an empty grid leaves it empty.
    add_cell(CraftSlotRef{CraftSlotKind::Result, 0}, state.result.has_value()
                                                         ? game::ItemStack::of(state.result->item, state.result->count)
                                                         : game::ItemStack{});
    draw_flat(res, backdrops, 0.27f, 0.27f, 0.30f, 0.95f);
    draw_icons(res, items, state.vessels, res.world.water_block_id(), icons, fbw, fbh);
    draw_counts(res, counts, fbw, fbh);

    // The hover frame: the same 2 px white outline the hotbar's selection uses,
    // drawn around the cell the pointer is on.
    if (state.hover.kind != CraftSlotKind::None) {
        const CraftingLayout::Rect rect = layout.rect(state.hover, grid_width);
        std::vector<glm::vec2> frame;
        draw_rect(rect.x0 - 2.0f, rect.y0 - 2.0f, rect.x1 + 2.0f, rect.y0, fbw, fbh, frame);
        draw_rect(rect.x0 - 2.0f, rect.y1, rect.x1 + 2.0f, rect.y1 + 2.0f, fbw, fbh, frame);
        draw_rect(rect.x0 - 2.0f, rect.y0, rect.x0, rect.y1, fbw, fbh, frame);
        draw_rect(rect.x1, rect.y0, rect.x1 + 2.0f, rect.y1, fbw, fbh, frame);
        draw_flat(res, frame, 0.95f, 0.95f, 0.95f, 1.0f);
    }

    // The title. Two lines' worth of information: which surface this is (a 2x2
    // pocket grid or the bench's 3x3), and - only when it matters - how many
    // cells the grid has, so "why does nothing match" has a visible cause.
    {
        const char *const title = grid_width == 3 ? "ASSEMBLY BENCH" : "CRAFTING";
        constexpr float kTitlePx = 16.0f;
        std::size_t length = 0;
        for (const char *c = title; *c != '\0'; ++c) {
            ++length;
        }
        std::vector<glm::vec2> tverts;
        std::vector<glm::vec2> tuvs;
        draw_text(title, (layout.panel_x0 + layout.panel_x1) * 0.5f - static_cast<float>(length) * kTitlePx * 0.5f,
                  layout.panel_y0 + CraftingLayout::kPadPx, kTitlePx, fbw, fbh, tverts, tuvs);
        draw_text_batch(res, tverts, tuvs, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    // The carried stack, on top of everything and at the pointer - the one piece
    // of this screen that has no cell: it is what the click will put down.
    if (!state.crafting->cursor.empty()) {
        const CraftingLayout::Rect rect{state.cursor_x, state.cursor_y, state.cursor_x + CraftingLayout::kCellPx,
                                        state.cursor_y + CraftingLayout::kCellPx};
        std::vector<glm::vec2> flat;
        draw_rect(rect.x0, rect.y0, rect.x1, rect.y1, fbw, fbh, flat);
        draw_flat(res, flat, 0.42f, 0.42f, 0.46f, 0.95f);
        const IconCell carried{rect.x0 + kIconInset, rect.y0 + kIconInset, rect.x1 - kIconInset, rect.y1 - kIconInset,
                               state.crafting->cursor};
        draw_icons(res, items, state.vessels, res.world.water_block_id(), {carried}, fbw, fbh);
        draw_counts(res, {{rect.x0, rect.y0, rect.x1, rect.y1, state.crafting->cursor}}, fbw, fbh);
    }
}

} // namespace opencraft::client
