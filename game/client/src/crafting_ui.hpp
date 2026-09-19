#pragma once

// T-D60: the crafting screen - its MODEL and the geometry its drawing and its
// hit-testing share. Header-only and free of GL/GLFW on purpose, exactly like
// inventory_wiring.hpp: tests/ already puts this directory on its include path,
// so the cursor stack, the recipe application and "a click lands on this cell"
// are all unit-testable without opening a window. The pixels are hud.cpp's
// (draw_crafting_screen); everything here is arithmetic over the model.
//
// Three rules come straight from the card and are spelled out where they are
// implemented rather than left to the reader:
//
//   * C-1 - the item-moving model IS the existing one (ItemStack::merge_from /
//     split / shrink, Inventory::set_slot, Inventory::accepts). The only new
//     concept is the cursor stack: the one stack the pointer carries, which
//     lives exactly as long as a screen is open and is handed back when it
//     closes.
//   * §2.3 - left click = whole stack / merge / exchange; right click = take
//     half or put one. ⚠ This exact gesture set has NO research source (the card
//     marks it 自定 §2.3 and asks for that to be written down): it is the PM's
//     ruling, chosen to match the familiar inventory feel.
//   * §2.3 - clicking the result takes ONE craft into the cursor (merging if the
//     cursor already holds that item) and consumes one unit from every non-empty
//     grid cell. No shift-crafting: the card lists 连合 as an optional extra for
//     this card, not a requirement.

#include <cstdint>
#include <optional>
#include <utility>

#include "opencraft/game/inventory.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"
#include "opencraft/game/recipe_registry.hpp"
#include "opencraft/voxel/block_registry.hpp"

namespace opencraft::client {

// Which surface is open. `None` is "the player is playing"; the two others are
// the two grid sizes the base game offers, and they differ only in size.
enum class CraftingMode : std::uint8_t {
    None = 0,
    Pocket, // E: the 2x2 grid, available anywhere
    Bench,  // right-click on an assembly_bench block: the 3x3 grid
};

[[nodiscard]] constexpr bool crafting_open(CraftingMode mode) {
    return mode != CraftingMode::None;
}

// The live grid's edge length: 3 for the bench, 2 for the pocket - and 0 when
// nothing is open, so a caller cannot accidentally treat a closed screen as a
// 2x2 one.
[[nodiscard]] constexpr int craft_grid_width(CraftingMode mode) {
    switch (mode) {
    case CraftingMode::Bench:
        return 3;
    case CraftingMode::Pocket:
        return 2;
    case CraftingMode::None:
        break;
    }
    return 0;
}

// ── where a click landed ───────────────────────────────────────────────────
enum class CraftSlotKind : std::uint8_t {
    None = 0,  // outside every cell
    Inventory, // one of the inventory's 41 cells (index == slot number)
    Grid,      // one of the craft grid's cells (row-major, index < width*width)
    Result,    // the output cell: read-only, clicking it performs the craft
};

struct CraftSlotRef {
    CraftSlotKind kind = CraftSlotKind::None;
    int index = 0;

    [[nodiscard]] friend constexpr bool operator==(const CraftSlotRef &, const CraftSlotRef &) = default;
};

// ── the screen's state ─────────────────────────────────────────────────────
struct CraftingState {
    CraftingMode mode = CraftingMode::None;
    // The grid, row-major, `craft_grid_width(mode)` cells per row. Cells outside
    // that square are never read and never drawn.
    game::ItemStack grid[game::kMaxCraftGrid * game::kMaxCraftGrid];
    // The cursor stack (C-1): what the pointer carries. It exists exactly while
    // a screen is open - crafting_close() hands it back.
    game::ItemStack cursor;

    [[nodiscard]] int width() const { return craft_grid_width(mode); }

    [[nodiscard]] bool open() const { return crafting_open(mode); }

    void open_mode(CraftingMode next) { mode = next; }
};

// ── reading and writing cells ──────────────────────────────────────────────

// Reads the cell a ref names. Grid cells come from the screen's state, inventory
// cells from the inventory; a Result / None ref reads empty.
[[nodiscard]] inline game::ItemStack read_cell(const game::Inventory &inventory, const CraftingState &state,
                                               const CraftSlotRef &ref) {
    switch (ref.kind) {
    case CraftSlotKind::Inventory:
        return game::Inventory::is_valid_slot(ref.index) ? inventory.slot(ref.index) : game::ItemStack{};
    case CraftSlotKind::Grid:
        return ref.index >= 0 && ref.index < state.width() * state.width() ? state.grid[ref.index] : game::ItemStack{};
    case CraftSlotKind::Result:
    case CraftSlotKind::None:
        break;
    }
    return {};
}

// Whether the cell a ref names would take `stack`. Inventory cells answer with
// Inventory::accepts, which is where the slot-type rule lives (an armour cell
// only takes its own piece); grid cells take anything that fits one.
[[nodiscard]] inline bool cell_accepts(const game::Inventory &inventory, const CraftingState &state,
                                       const CraftSlotRef &ref, const game::ItemStack &stack) {
    if (stack.empty()) {
        return true; // clearing a cell is never blocked (Inventory::accepts' own rule)
    }
    switch (ref.kind) {
    case CraftSlotKind::Inventory:
        return game::Inventory::is_valid_slot(ref.index) && inventory.accepts(ref.index, stack);
    case CraftSlotKind::Grid:
        return ref.index >= 0 && ref.index < state.width() * state.width() &&
               stack.count <= inventory.registry().def_of(stack.item).max_stack;
    case CraftSlotKind::Result:
    case CraftSlotKind::None:
        break;
    }
    return false;
}

// Writes the cell a ref names. Inventory cells go through set_slot(), which runs
// the slot-type rule again - the screen obeys the inventory's rule instead of
// re-implementing it. Returns whether the write happened.
inline bool write_cell(game::Inventory &inventory, CraftingState &state, const CraftSlotRef &ref,
                       const game::ItemStack &stack) {
    switch (ref.kind) {
    case CraftSlotKind::Inventory:
        return game::Inventory::is_valid_slot(ref.index) && inventory.set_slot(ref.index, stack);
    case CraftSlotKind::Grid:
        if (ref.index < 0 || ref.index >= state.width() * state.width() ||
            !cell_accepts(inventory, state, ref, stack)) {
            return false;
        }
        state.grid[ref.index] = stack.empty() ? game::ItemStack{} : stack;
        return true;
    case CraftSlotKind::Result:
    case CraftSlotKind::None:
        break;
    }
    return false;
}

// ── the recipe side ────────────────────────────────────────────────────────

// What the grid currently makes, if anything. Pure: it answers the same question
// the result cell draws and the click applies, so the two cannot disagree.
[[nodiscard]] inline std::optional<game::RecipeResult> craft_result_of(const CraftingState &state,
                                                                       const game::RecipeRegistry &recipes) {
    if (!state.open()) {
        return std::nullopt;
    }
    return recipes.match(state.grid, state.width());
}

// Can `stack` go onto the cursor? True when the cursor is empty, holds the same
// item, or would hold it without passing max_stack.
[[nodiscard]] inline bool cursor_accepts(const game::ItemRegistry &items, const game::ItemStack &cursor,
                                         const game::ItemStack &stack) {
    if (stack.empty() || cursor.empty()) {
        return true;
    }
    if (cursor.item != stack.item) {
        return false;
    }
    return cursor.count + stack.count <= items.def_of(cursor.item).max_stack;
}

// ── the clicks (C-1) ───────────────────────────────────────────────────────

// What a click did, for the caller's log line and for the tests.
struct CraftingClickOut {
    bool changed = false;  // the model moved something
    bool crafted = false;  // a craft happened (result -> cursor)
    bool refused = false;  // the click was understood, the slot type said no
    int crafted_count = 0; // units the recipe produced
};

// Applies ONE click of `right_button` on `ref`.
//
// LEFT (C-1): cursor empty -> take the whole stack; on an empty cell -> put the
// whole stack down; same item -> merge as much as fits (the rest stays on the
// cursor - Inventory::move_slot's semantics); different item -> exchange, and an
// exchange is all-or-nothing (both directions must be accepted, the rule
// Inventory::swap_slots applies between two cells).
//
// RIGHT (§2.3): cursor empty -> take half, rounded UP (a lone unit is not halved
// to nothing); cursor non-empty -> put ONE unit, which is what makes it safe to
// repeat. A right click on a cell holding a different item does nothing.
inline CraftingClickOut crafting_click(CraftingState &state, game::Inventory &inventory,
                                       const game::RecipeRegistry &recipes, const CraftSlotRef &ref,
                                       const bool right_button) {
    CraftingClickOut out;
    if (!state.open() || ref.kind == CraftSlotKind::None) {
        return out;
    }
    const game::ItemRegistry &items = inventory.registry();

    // ── the result cell: one craft into the cursor ──────────────────────────
    if (ref.kind == CraftSlotKind::Result) {
        const std::optional<game::RecipeResult> result = craft_result_of(state, recipes);
        if (!result.has_value()) {
            return out;
        }
        game::ItemStack produced = game::ItemStack::of(result->item, result->count);
        if (!cursor_accepts(items, state.cursor, produced)) {
            return out; // the cursor holds something else: refuse, consume nothing
        }
        if (state.cursor.empty()) {
            state.cursor = produced;
        } else {
            state.cursor.merge_from(produced, items.def_of(state.cursor.item).max_stack);
        }
        game::RecipeRegistry::consume_one(state.grid, state.width());
        out.changed = true;
        out.crafted = true;
        out.crafted_count = result->count;
        return out;
    }

    const game::ItemStack cell = read_cell(inventory, state, ref);

    // ── right: half out, or one in ─────────────────────────────────────────
    if (right_button) {
        if (state.cursor.empty()) {
            if (cell.empty()) {
                return out;
            }
            // ⚖ C-1: half the stack, rounded UP.
            game::ItemStack rest = cell;
            const game::ItemStack half = rest.split((cell.count + 1) / 2);
            if (write_cell(inventory, state, ref, rest)) {
                state.cursor = half;
                out.changed = true;
            }
            return out;
        }
        if (cell.empty() || cell.item == state.cursor.item) {
            const int limit = items.def_of(state.cursor.item).max_stack;
            if (!cell.empty() && cell.count >= limit) {
                out.refused = true; // the cell is full: nothing moves, nothing is lost
                return out;
            }
            game::ItemStack into = cell;
            game::ItemStack from = state.cursor;
            // ONE unit, not "as much as fits": the limit is raised to exactly one
            // past what the cell holds, which is what makes this gesture
            // repeatable one item at a time (and makes `merge_from` adopt the
            // item when the cell is empty, so a fresh stack of one opens).
            if (into.merge_from(from, into.count + 1) != 1) {
                return out;
            }
            if (write_cell(inventory, state, ref, into)) {
                state.cursor = from;
                out.changed = true;
            } else {
                out.refused = true;
            }
        }
        return out;
    }

    // ── left ───────────────────────────────────────────────────────────────
    if (state.cursor.empty()) {
        if (cell.empty()) {
            return out;
        }
        if (write_cell(inventory, state, ref, game::ItemStack{})) {
            state.cursor = cell; // the whole stack, however big it is
            out.changed = true;
        }
        return out;
    }

    if (cell.empty()) {
        // The whole stack goes down. For an armour cell this is also the "wear
        // it" path: set_slot() runs the EquipSlot rule, and a piece that does not
        // belong in that cell is refused with the cursor left alone.
        if (write_cell(inventory, state, ref, state.cursor)) {
            state.cursor = game::ItemStack{};
            out.changed = true;
        } else {
            out.refused = true;
        }
        return out;
    }

    if (cell.item == state.cursor.item) {
        game::ItemStack into = cell;
        game::ItemStack from = state.cursor;
        into.merge_from(from, items.def_of(cell.item).max_stack);
        if (write_cell(inventory, state, ref, into)) {
            state.cursor = from;
            out.changed = true;
        } else {
            out.refused = true;
        }
        return out;
    }

    // Different items: exchange, all-or-nothing. A cell that refuses the
    // cursor's stack (an armour cell and a pickaxe) leaves BOTH sides alone.
    if (!cell_accepts(inventory, state, ref, state.cursor)) {
        out.refused = true;
        return out;
    }
    const game::ItemStack swap_out = cell;
    if (write_cell(inventory, state, ref, state.cursor)) {
        state.cursor = swap_out;
        out.changed = true;
    } else {
        out.refused = true;
    }
    return out;
}

// ── closing (C-1) ──────────────────────────────────────────────────────────

// Hands the cursor stack AND whatever the grid still holds back to the inventory
// (the storage sections, hotbar first - the order a pickup uses) and closes the
// screen. Returns false, changing NOTHING, when something does not fit: the
// card's 取简 ruling is "先 add_item，失败则保持界面打开".
//
// The grid is included even though the card names only the cursor: a screen that
// is closed but still holds items would hide them from the player without losing
// them, and the next open would show them again - a limbo that costs nothing to
// avoid. The whole hand-back runs on a COPY of the inventory first, so a refusal
// cannot half-move the contents.
[[nodiscard]] inline bool crafting_close(CraftingState &state, game::Inventory &inventory) {
    if (!state.open()) {
        return true; // nothing to close
    }
    game::Inventory next = inventory;
    if (!state.cursor.empty()) {
        game::ItemStack cursor = state.cursor;
        if (next.add_item(cursor).remaining != 0) {
            return false;
        }
    }
    for (int i = 0; i < state.width() * state.width(); ++i) {
        if (state.grid[i].empty()) {
            continue;
        }
        game::ItemStack cell = state.grid[i];
        if (next.add_item(cell).remaining != 0) {
            return false;
        }
    }
    inventory = std::move(next);
    state.cursor = game::ItemStack{};
    for (game::ItemStack &cell : state.grid) {
        cell = game::ItemStack{};
    }
    state.mode = CraftingMode::None;
    return true;
}

// ── the station's own gesture (C-2/§2.3) ──────────────────────────────────
// Which screen a right-click on `block_id` opens. The block is named by its
// registry id, so a card that adds a second station extends this one switch
// instead of hunting for the place that knows about assembly benches.
[[nodiscard]] inline CraftingMode station_screen_for(const opencraft::voxel::BlockRegistry &blocks,
                                                     const std::uint16_t block_id) {
    if (!blocks.has_numeric(block_id)) {
        return CraftingMode::None;
    }
    return blocks.string_of(block_id) == "assembly_bench" ? CraftingMode::Bench : CraftingMode::None;
}

// ── the layout ────────────────────────────────────────────────────────────
// The panel's geometry in framebuffer pixels, computed once per frame and used
// by BOTH the drawing and the hit-test - one function, so a click cannot land
// somewhere other than what it looks like. (The HiDPI trap pause_menu.cpp
// records is that cursor and framebuffer pixels differ by the backing-store
// scale; that conversion happens in main.cpp, before this struct sees a number.)
//
// The sections, top to bottom: the craft area (always three rows tall, so the
// panel does not resize between a 2x2 and a 3x3 screen - a 2x2 grid is drawn in
// its top-left corner), the armour row with the offhand cell at the far right,
// the 27 main cells and the hotbar. The result cell sits one column clear of the
// grid, on the craft area's middle row.
struct CraftingLayout {
    static constexpr float kCellPx = 24.0f;
    static constexpr float kGapPx = 2.0f;
    static constexpr float kPadPx = 10.0f;
    static constexpr float kTitlePx = 16.0f;

    [[nodiscard]] static constexpr float pitch() { return kCellPx + kGapPx; }

    // 9 cells wide plus padding, matching the widest section (main / hotbar).
    [[nodiscard]] static constexpr float panel_width() { return kPadPx * 2.0f + 9.0f * kCellPx + 8.0f * kGapPx; }

    [[nodiscard]] static constexpr float panel_height() {
        return kPadPx * 2.0f + kTitlePx + 8.0f         // title band
               + 3.0f * kCellPx + 3.0f * kGapPx + 8.0f // craft area
               + kCellPx + 6.0f                        // armour row
               + 3.0f * kCellPx + 3.0f * kGapPx + 6.0f // main
               + kCellPx;                              // hotbar
    }

    float fb_width = 0.0f;
    float fb_height = 0.0f;
    float panel_x0 = 0.0f;
    float panel_y0 = 0.0f;
    float panel_x1 = 0.0f;
    float panel_y1 = 0.0f;

    [[nodiscard]] static CraftingLayout make(const float fb_width, const float fb_height) {
        CraftingLayout layout;
        layout.fb_width = fb_width;
        layout.fb_height = fb_height;
        layout.panel_x0 = (fb_width - panel_width()) * 0.5f;
        layout.panel_y0 = (fb_height - panel_height()) * 0.5f;
        layout.panel_x1 = layout.panel_x0 + panel_width();
        layout.panel_y1 = layout.panel_y0 + panel_height();
        return layout;
    }

    [[nodiscard]] float column_x(const int column) const {
        return panel_x0 + kPadPx + static_cast<float>(column) * pitch();
    }

    [[nodiscard]] float craft_y() const { return panel_y0 + kPadPx + kTitlePx + 8.0f; }

    [[nodiscard]] float armour_y() const { return craft_y() + 3.0f * kCellPx + 3.0f * kGapPx + 8.0f; }

    [[nodiscard]] float main_y() const { return armour_y() + kCellPx + 6.0f; }

    [[nodiscard]] float hotbar_y() const { return main_y() + 3.0f * kCellPx + 3.0f * kGapPx + 6.0f; }

    [[nodiscard]] float result_x() const { return column_x(game::kMaxCraftGrid + 1); }

    [[nodiscard]] float result_y() const { return craft_y() + kCellPx + kGapPx; }

    struct Rect {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;

        [[nodiscard]] constexpr bool contains(const float x, const float y) const {
            return x >= x0 && x < x1 && y >= y0 && y < y1;
        }
    };

    [[nodiscard]] Rect rect_of(const int column, const float y) const {
        const float x0 = column_x(column);
        return {x0, y, x0 + kCellPx, y + kCellPx};
    }

    // The rect a ref names; a ref that is not on screen answers an empty rect, so
    // a caller can iterate refs without asking whether they exist.
    [[nodiscard]] Rect rect(const CraftSlotRef &ref, const int grid_width) const {
        switch (ref.kind) {
        case CraftSlotKind::Grid:
            if (grid_width <= 0 || ref.index < 0 || ref.index >= grid_width * grid_width) {
                break;
            }
            return rect_of(ref.index % grid_width, craft_y() + static_cast<float>(ref.index / grid_width) * pitch());
        case CraftSlotKind::Result:
            return rect_of(game::kMaxCraftGrid + 1, result_y());
        case CraftSlotKind::Inventory: {
            if (!game::Inventory::is_valid_slot(ref.index)) {
                break;
            }
            const std::optional<game::InventorySection> section = game::section_of_slot(ref.index);
            if (!section.has_value()) {
                break;
            }
            switch (*section) {
            case game::InventorySection::Hotbar:
                return rect_of(ref.index - game::kHotbarFirstSlot, hotbar_y());
            case game::InventorySection::Main: {
                const int offset = ref.index - game::kMainFirstSlot;
                return rect_of(offset % 9, main_y() + static_cast<float>(offset / 9) * pitch());
            }
            case game::InventorySection::Armor:
                // One cell per column across the armour row, in slot order:
                // head, chest, legs, feet.
                return rect_of(ref.index - game::kArmorFirstSlot, armour_y());
            case game::InventorySection::Offhand:
                return rect_of(8, armour_y());
            }
            break;
        }
        case CraftSlotKind::None:
            break;
        }
        return {};
    }

    // Which cell a framebuffer-space point is over. Iterates the cells rather
    // than solving for them: 51 rects is nothing next to a frame, and one loop
    // over the same rect() the drawing uses cannot drift from it. The result cell
    // is tested first - it is the only one whose click does something other than
    // move a stack, so a stale grid rect must not be able to shadow it.
    [[nodiscard]] CraftSlotRef hit(const float x, const float y, const int grid_width) const {
        if (grid_width > 0) {
            if (rect(CraftSlotRef{CraftSlotKind::Result, 0}, grid_width).contains(x, y)) {
                return {CraftSlotKind::Result, 0};
            }
            for (int i = 0; i < grid_width * grid_width; ++i) {
                const CraftSlotRef ref{CraftSlotKind::Grid, i};
                if (rect(ref, grid_width).contains(x, y)) {
                    return ref;
                }
            }
        }
        for (int slot = 0; slot < game::kInventorySlots; ++slot) {
            const CraftSlotRef ref{CraftSlotKind::Inventory, slot};
            if (rect(ref, grid_width).contains(x, y)) {
                return ref;
            }
        }
        return {};
    }

    [[nodiscard]] bool inside_panel(const float x, const float y) const {
        return x >= panel_x0 && x <= panel_x1 && y >= panel_y0 && y <= panel_y1;
    }
};

} // namespace opencraft::client
