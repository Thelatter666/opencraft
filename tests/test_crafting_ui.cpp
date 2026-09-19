// T-D60: the crafting screen's model - the cursor stack, the click gestures, the
// closing rule, the layout's hit-test - and the C-3 input gate that keeps a world
// verb from being submitted while a screen is open.
//
// Everything here is driven through the same functions the client calls
// (crafting_click / crafting_close / CraftingLayout::hit / submit_world_verb),
// which is the point of keeping them in a GL-free header: a headless test can
// move items around a grid and press buttons at coordinates.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "crafting_ui.hpp"
#include "opencraft/game/inventory.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"
#include "opencraft/game/protocol.hpp"
#include "opencraft/game/recipe_registry.hpp"
#include "tick.hpp"

using opencraft::client::CraftingClickOut;
using opencraft::client::CraftingLayout;
using opencraft::client::CraftingMode;
using opencraft::client::CraftingState;
using opencraft::client::CraftSlotKind;
using opencraft::client::CraftSlotRef;
using opencraft::game::ActionKind;
using opencraft::game::ActionRequest;
using opencraft::game::ActionResult;
using opencraft::game::Inventory;
using opencraft::game::ItemRegistry;
using opencraft::game::ItemStack;
using opencraft::game::RecipeRegistry;

namespace {

struct Fixture {
    ItemRegistry items = ItemRegistry::create_default();
    RecipeRegistry recipes = RecipeRegistry::create_default(items);
    Inventory inventory{items};
    CraftingState state;

    Fixture() { state.open_mode(CraftingMode::Bench); }

    [[nodiscard]] std::uint16_t id(const char *item) const { return items.id_of(item); }

    [[nodiscard]] CraftSlotRef grid(const int index) const { return {CraftSlotKind::Grid, index}; }

    [[nodiscard]] CraftSlotRef cell(const int slot) const { return {CraftSlotKind::Inventory, slot}; }

    [[nodiscard]] CraftSlotRef result() const { return {CraftSlotKind::Result, 0}; }

    [[nodiscard]] CraftingClickOut click(const CraftSlotRef &ref, const bool right = false) {
        return opencraft::client::crafting_click(state, inventory, recipes, ref, right);
    }
};

// A stub authority that RECORDS every submission. The gate's whole claim is
// "nothing is submitted", so counting is the evidence; the interface's other four
// verbs have nothing to do with it and answer empty values.
class CountingAuthority final : public opencraft::game::IAuthority {
public:
    [[nodiscard]] ActionResult submit(const ActionRequest &request) override {
        submissions.push_back(request);
        ActionResult accepted;
        accepted.accepted = true;
        return accepted;
    }

    void tick() override {}

    [[nodiscard]] opencraft::game::WorldChanges take_changes() override { return {}; }

    std::size_t autosave_pass() override { return 0; }

    [[nodiscard]] opencraft::game::StreamResult stream(const opencraft::game::StreamRequest &) override { return {}; }

    std::vector<ActionRequest> submissions;
};

[[nodiscard]] ActionRequest request_of(const ActionKind kind) {
    ActionRequest request;
    request.kind = kind;
    return request;
}

} // namespace

// ── the mode ───────────────────────────────────────────────────────────────

TEST_CASE("crafting: the two surfaces are 2x2 and 3x3, and only a bench opens the big one") {
    CHECK(CraftingMode::None != CraftingMode::Pocket);
    CHECK(crafting_open(CraftingMode::None) == false);
    CHECK(crafting_open(CraftingMode::Pocket) == true);
    CHECK(crafting_open(CraftingMode::Bench) == true);
    CHECK(craft_grid_width(CraftingMode::None) == 0);
    CHECK(craft_grid_width(CraftingMode::Pocket) == 2);
    CHECK(craft_grid_width(CraftingMode::Bench) == 3);

    const auto blocks = opencraft::voxel::BlockRegistry::create_default();
    CHECK(opencraft::client::station_screen_for(blocks, blocks.id_of("assembly_bench")) == CraftingMode::Bench);
    // Every other block - including the ones a player is most likely to be
    // holding when they right-click one - opens nothing.
    for (const char *id : {"stone", "planks", "dirt", "glass", "cobblestone"}) {
        CHECK(opencraft::client::station_screen_for(blocks, blocks.id_of(id)) == CraftingMode::None);
    }
    CHECK(opencraft::client::station_screen_for(blocks, opencraft::game::kNoBlock) == CraftingMode::None);
}

// ── the cursor stack (C-1) ─────────────────────────────────────────────────

TEST_CASE("crafting: left click takes a whole stack, puts it down, merges and exchanges") {
    Fixture f;
    f.inventory.set_slot(9, ItemStack::of(f.id("greyrock"), 64));
    f.inventory.set_slot(10, ItemStack::of(f.id("greyrock"), 3));
    f.inventory.set_slot(11, ItemStack::of(f.id("sawn_planks"), 5));

    // Take the whole stack.
    const CraftingClickOut took = f.click(f.cell(9));
    CHECK(took.changed);
    CHECK(f.state.cursor.item == f.id("greyrock"));
    CHECK(f.state.cursor.count == 64);
    CHECK(f.inventory.slot(9).empty());

    // Same item: merge as much as FITS, and the rest stays on the cursor - which
    // is Inventory::move_slot's own rule, applied to the cell the pointer is on.
    const CraftingClickOut merged = f.click(f.cell(10));
    CHECK(merged.changed);
    CHECK(f.inventory.slot(10).count == 64);
    CHECK(f.state.cursor.count == 3);

    // Different item: exchange, both ways, or neither.
    const CraftingClickOut swapped = f.click(f.cell(11));
    CHECK(swapped.changed);
    CHECK(f.inventory.slot(11).item == f.id("greyrock"));
    CHECK(f.inventory.slot(11).count == 3);
    CHECK(f.state.cursor.item == f.id("sawn_planks"));
    CHECK(f.state.cursor.count == 5);

    // An empty cell takes the whole stack back.
    const CraftingClickOut placed = f.click(f.cell(12));
    CHECK(placed.changed);
    CHECK(f.inventory.slot(12).item == f.id("sawn_planks"));
    CHECK(f.inventory.slot(12).count == 5);
    CHECK(f.state.cursor.empty());
}

TEST_CASE("crafting: right click takes HALF (rounded up) and puts ONE back") {
    Fixture f;
    f.inventory.set_slot(9, ItemStack::of(f.id("greyrock"), 7));

    const CraftingClickOut half = f.click(f.cell(9), /*right=*/true);
    CHECK(half.changed);
    CHECK(f.state.cursor.count == 4); // ceil(7/2), not floor
    CHECK(f.inventory.slot(9).count == 3);

    // Put one back, one unit at a time: the cell is the same item, so each click
    // adds exactly one and the cursor drains.
    f.click(f.cell(9), /*right=*/true);
    CHECK(f.inventory.slot(9).count == 4);
    CHECK(f.state.cursor.count == 3);
    f.click(f.cell(9), /*right=*/true);
    CHECK(f.inventory.slot(9).count == 5);
    CHECK(f.state.cursor.count == 2);

    // Putting one into an EMPTY cell opens a fresh stack of one.
    f.click(f.cell(10), /*right=*/true);
    CHECK(f.inventory.slot(10).item == f.id("greyrock"));
    CHECK(f.inventory.slot(10).count == 1);
    CHECK(f.state.cursor.count == 1);

    // A single unit is not halved into nothing: ceil(1/2) = 1.
    f.inventory.set_slot(11, ItemStack::of(f.id("sawn_planks"), 1));
    f.state.cursor = ItemStack{};
    f.click(f.cell(11), /*right=*/true);
    CHECK(f.state.cursor.count == 1);
    CHECK(f.inventory.slot(11).empty());

    // A right click on a DIFFERENT item's cell does nothing at all - which is
    // what makes "put one" safe to repeat.
    f.inventory.set_slot(12, ItemStack::of(f.id("char_lump"), 4));
    const CraftingClickOut refused = f.click(f.cell(12), /*right=*/true);
    CHECK_FALSE(refused.changed);
    CHECK(f.inventory.slot(12).count == 4);
    CHECK(f.state.cursor.count == 1);
}

TEST_CASE("crafting: a full cell refuses 'put one' and keeps the cursor's unit") {
    Fixture f;
    f.inventory.set_slot(9, ItemStack::of(f.id("greyrock"), 64));
    f.state.cursor = ItemStack::of(f.id("greyrock"), 2);

    const CraftingClickOut out = f.click(f.cell(9), /*right=*/true);
    CHECK(out.refused);
    CHECK_FALSE(out.changed);
    CHECK(f.inventory.slot(9).count == 64);
    CHECK(f.state.cursor.count == 2);
}

TEST_CASE("crafting: the armour cells go through the inventory's own slot rule") {
    Fixture f;
    f.state.cursor = ItemStack::of(f.id("timber_headguard"), 1);
    const int head = opencraft::game::armor_slot_index(opencraft::game::ArmorSlot::Head);

    // A headguard fits the head cell: "put the whole stack down" is also the
    // "wear it" path, because set_slot runs the EquipSlot rule (T-I1's).
    const CraftingClickOut worn = f.click(f.cell(head));
    CHECK(worn.changed);
    CHECK(f.inventory.slot(head).item == f.id("timber_headguard"));
    CHECK(f.state.cursor.empty());

    // A pickaxe does not, and the refusal leaves BOTH sides alone (no half-move).
    f.state.cursor = ItemStack::of(f.id("timber_chisel"), 1);
    const CraftingClickOut refused = f.click(f.cell(head));
    CHECK(refused.refused);
    CHECK_FALSE(refused.changed);
    CHECK(f.inventory.slot(head).item == f.id("timber_headguard"));
    CHECK(f.state.cursor.item == f.id("timber_chisel"));

    // And the exchange refuses as a WHOLE when only one direction is legal.
    const CraftingClickOut swap = f.click(f.cell(head));
    CHECK_FALSE(swap.changed);
    CHECK(f.inventory.slot(head).item == f.id("timber_headguard"));
    CHECK(f.state.cursor.item == f.id("timber_chisel"));
}

// ── crafting ───────────────────────────────────────────────────────────────

TEST_CASE("crafting: clicking the result consumes the grid and puts the product on the cursor") {
    Fixture f;
    // The bench: four planks in a 2x2 block.
    for (const int index : {0, 1, 3, 4}) {
        f.state.grid[index] = ItemStack::of(f.id("sawn_planks"), 2);
    }
    const std::optional<opencraft::game::RecipeResult> result = craft_result_of(f.state, f.recipes);
    REQUIRE(result.has_value());
    CHECK(result->item == f.id("assembly_bench"));
    CHECK(result->count == 1);

    const CraftingClickOut out = f.click(f.result());
    CHECK(out.crafted);
    CHECK(out.crafted_count == 1);
    CHECK(f.state.cursor.item == f.id("assembly_bench"));
    CHECK(f.state.cursor.count == 1);
    // One unit out of each of the four cells, and only one.
    for (const int index : {0, 1, 3, 4}) {
        CHECK(f.state.grid[index].count == 1);
    }
    // The grid is untouched where the pattern has nothing.
    CHECK(f.state.grid[2].empty());

    // Same product again: it merges onto the cursor instead of replacing it.
    f.click(f.result());
    CHECK(f.state.cursor.count == 2);
    CHECK(f.state.grid[0].count == 0);
    // With the planks gone the shape no longer matches: the result cell is empty
    // and clicking it does nothing.
    CHECK_FALSE(craft_result_of(f.state, f.recipes).has_value());
    const CraftingClickOut nothing = f.click(f.result());
    CHECK_FALSE(nothing.changed);
    CHECK(f.state.cursor.count == 2);
}

TEST_CASE("crafting: a full cursor of something else refuses the craft without consuming anything") {
    Fixture f;
    f.state.grid[0] = ItemStack::of(f.id("timber_log"), 3);
    f.state.cursor = ItemStack::of(f.id("greyrock"), 64); // nothing fits on that

    const CraftingClickOut out = f.click(f.result());
    CHECK(out.refused == false);
    CHECK_FALSE(out.changed);
    CHECK_FALSE(out.crafted);
    CHECK(f.state.grid[0].count == 3); // nothing was spent
}

TEST_CASE("crafting: the pocket grid runs the same recipes at 2x2") {
    Fixture f;
    f.state.open_mode(CraftingMode::Pocket);
    // The pocket 2x2 is where the chain STARTS: a log becomes planks with no
    // station at all, and two planks become the stick that every tool needs.
    f.state.grid[0] = ItemStack::of(f.id("timber_log"), 1);

    const CraftingClickOut planks = f.click(f.result());
    CHECK(planks.crafted);
    CHECK(planks.crafted_count == 4);
    CHECK(f.state.cursor.item == f.id("sawn_planks"));
    CHECK(f.state.grid[0].empty());

    // Put them back down as a vertical pair and make sticks.
    f.state.cursor = ItemStack{};
    f.state.grid[0] = ItemStack::of(f.id("sawn_planks"), 1);
    f.state.grid[2] = ItemStack::of(f.id("sawn_planks"), 1);
    const CraftingClickOut sticks = f.click(f.result());
    CHECK(sticks.crafted);
    CHECK(sticks.crafted_count == 4);
    CHECK(f.state.cursor.item == f.id("timber_stick"));
    CHECK(f.state.cursor.count == 4);
}

// ── closing (C-1) ──────────────────────────────────────────────────────────

TEST_CASE("crafting: closing hands the cursor AND the grid back to the inventory") {
    Fixture f;
    f.state.open_mode(CraftingMode::Pocket);
    f.state.cursor = ItemStack::of(f.id("sawn_planks"), 7);
    f.state.grid[0] = ItemStack::of(f.id("timber_log"), 2);
    f.state.grid[3] = ItemStack::of(f.id("timber_stick"), 4);
    f.inventory.set_slot(0, ItemStack::of(f.id("greyrock"), 10));

    REQUIRE(opencraft::client::crafting_close(f.state, f.inventory));
    CHECK_FALSE(f.state.open());
    CHECK(f.state.cursor.empty());
    CHECK(f.state.grid[0].empty());
    CHECK(f.state.grid[3].empty());
    // The log is in the hotbar (storage, hotbar first), the planks and the sticks
    // alongside it, and nothing was lost: every unit is now in the inventory.
    CHECK(f.inventory.count_of(f.id("timber_log")) == 2);
    CHECK(f.inventory.count_of(f.id("sawn_planks")) == 7);
    CHECK(f.inventory.count_of(f.id("timber_stick")) == 4);
    CHECK(f.inventory.count_of(f.id("greyrock")) == 10);
}

TEST_CASE("crafting: a close that would not fit is REFUSED and changes nothing") {
    Fixture f;
    f.state.open_mode(CraftingMode::Pocket);
    // Fill every storage cell with a different item, so nothing more can land.
    const char *const fillers[] = {"greyrock", "loam_clod", "fine_grit",  "pebble_grit", "rime_block",
                                   "char_ore", "sod_loam",  "clear_pane", "duskglass",   "grit_slab"};
    for (int slot = opencraft::game::kHotbarFirstSlot; slot < opencraft::game::kArmorFirstSlot; ++slot) {
        REQUIRE(f.inventory.set_slot(slot, ItemStack::of(f.id(fillers[slot % 10]), 64)));
    }
    f.state.cursor = ItemStack::of(f.id("timber_log"), 1);

    // 无解即拒绝, the screen stays open (the card's 取简 ruling: no dropping on
    // the floor, no deleting).
    CHECK_FALSE(opencraft::client::crafting_close(f.state, f.inventory));
    CHECK(f.state.open());
    CHECK(f.state.cursor.item == f.id("timber_log"));
    CHECK(f.state.cursor.count == 1);
}

// ── the layout ─────────────────────────────────────────────────────────────

TEST_CASE("crafting layout: every cell is hit-testable and no two cells overlap") {
    const CraftingLayout layout = CraftingLayout::make(1280.0f, 720.0f);
    REQUIRE(layout.panel_x0 > 0.0f);
    REQUIRE(layout.panel_y1 < 720.0f);

    // Ask each cell at its own centre: the answer must be that cell, for every
    // cell of both grid sizes and for all 41 inventory cells. This is the test
    // that would have caught the armour column sharing a rect with the main
    // grid's first cell.
    for (const int grid_width : {2, 3}) {
        const CraftSlotRef result{CraftSlotKind::Result, 0};
        const CraftingLayout::Rect rect = layout.rect(result, grid_width);
        CHECK(layout.hit((rect.x0 + rect.x1) * 0.5f, (rect.y0 + rect.y1) * 0.5f, grid_width) == result);
        CHECK(layout.inside_panel((rect.x0 + rect.x1) * 0.5f, (rect.y0 + rect.y1) * 0.5f));
        for (int i = 0; i < grid_width * grid_width; ++i) {
            const CraftSlotRef ref{CraftSlotKind::Grid, i};
            const CraftingLayout::Rect cell = layout.rect(ref, grid_width);
            INFO("grid " << grid_width << " cell " << i);
            CHECK(layout.hit((cell.x0 + cell.x1) * 0.5f, (cell.y0 + cell.y1) * 0.5f, grid_width) == ref);
        }
    }
    for (int slot = 0; slot < opencraft::game::kInventorySlots; ++slot) {
        const CraftSlotRef ref{CraftSlotKind::Inventory, slot};
        const CraftingLayout::Rect cell = layout.rect(ref, 3);
        INFO("inventory cell " << slot);
        CHECK(layout.hit((cell.x0 + cell.x1) * 0.5f, (cell.y0 + cell.y1) * 0.5f, 3) == ref);
    }

    // The panel's own rect answers None: that is the click that closes the
    // screen (main.cpp), so it must not be a cell.
    CHECK(layout.hit(1.0f, 1.0f, 3).kind == CraftSlotKind::None);
    CHECK_FALSE(layout.inside_panel(1.0f, 1.0f));
    // A cell of the 3x3 grid that the 2x2 does not have answers None at 2x2, so a
    // pocket screen cannot be clicked where its cells are not drawn.
    const CraftSlotRef bottom_right{CraftSlotKind::Grid, 8};
    const CraftingLayout::Rect corner = layout.rect(bottom_right, 3);
    CHECK(layout.hit((corner.x0 + corner.x1) * 0.5f, (corner.y0 + corner.y1) * 0.5f, 2).kind == CraftSlotKind::None);
}

TEST_CASE("crafting layout: the sections are the ones a player expects") {
    const CraftingLayout layout = CraftingLayout::make(1280.0f, 720.0f);
    // The hotbar row is below the main rows, and the armour row is above both -
    // the same top-to-bottom order the inventory has had since T-I1.
    const CraftingLayout::Rect hotbar0 = layout.rect({CraftSlotKind::Inventory, 0}, 3);
    const CraftingLayout::Rect main0 = layout.rect({CraftSlotKind::Inventory, opencraft::game::kMainFirstSlot}, 3);
    const CraftingLayout::Rect head = layout.rect({CraftSlotKind::Inventory, opencraft::game::kArmorFirstSlot}, 3);
    const CraftingLayout::Rect offhand = layout.rect({CraftSlotKind::Inventory, opencraft::game::kOffhandFirstSlot}, 3);
    CHECK(hotbar0.y0 > main0.y1);
    CHECK(main0.y0 > head.y1);
    // The armour row reads head, chest, legs, feet left to right, and the offhand
    // sits on the same row at the far end.
    const CraftingLayout::Rect chest = layout.rect({CraftSlotKind::Inventory, opencraft::game::kArmorFirstSlot + 1}, 3);
    CHECK(chest.x0 > head.x0);
    CHECK(chest.y0 == head.y0);
    CHECK(offhand.x0 > chest.x1);
    CHECK(offhand.y0 == head.y0);
    // Every cell of the 3x3 grid is up in the craft area, above the armour row.
    CHECK(layout.rect({CraftSlotKind::Grid, 8}, 3).y1 <= head.y0);
    // And the result cell is clear of the grid's own columns.
    CHECK(layout.rect({CraftSlotKind::Result, 0}, 3).x0 > layout.rect({CraftSlotKind::Grid, 2}, 3).x1);
}

// ── C-3: the gate ──────────────────────────────────────────────────────────

TEST_CASE("crafting gate: with a screen open NOTHING is submitted, for any verb") {
    CountingAuthority authority;
    // Every verb the tick owns, one of each kind - the gate is not about Dig
    // alone, it is about the whole vocabulary (attack and feed included).
    const ActionKind kinds[] = {ActionKind::Dig,        ActionKind::PlaceBlock, ActionKind::PourWater,
                                ActionKind::ScoopWater, ActionKind::PickUp,     ActionKind::Attack,
                                ActionKind::Feed,       ActionKind::DropItems};

    // Screen closed: the request reaches the authority, which is what makes the
    // "nothing arrived" half of this test mean something.
    for (const ActionKind kind : kinds) {
        const ActionResult result =
            opencraft::client::submit_world_verb(authority, /*ui_open_flag=*/false, request_of(kind));
        CHECK(result.accepted);
    }
    CHECK(authority.submissions.size() == std::size(kinds));

    // ★ Screen open: not "sent and refused" - NOT SENT. The authority sees
    // nothing at all for any of them, and the caller gets a plain refusal.
    for (const ActionKind kind : kinds) {
        const ActionResult result =
            opencraft::client::submit_world_verb(authority, /*ui_open_flag=*/true, request_of(kind));
        CHECK_FALSE(result.accepted);
        CHECK(result.reject == opencraft::game::ActionReject::None); // dropped, not judged
    }
    CHECK(authority.submissions.size() == std::size(kinds)); // unchanged: nothing was added
}

TEST_CASE("crafting gate: the flag IS the screen, so the tick and the panel cannot disagree") {
    // ui_open(ctx) is derived from the screen's own mode rather than kept as a
    // second bool: the two halves of the card (the tick's verbs and the panel's
    // clicks) read the same object. This asserts the derivation, which is what
    // makes "a screen is open" and "the verbs are off" the same statement.
    CraftingState state;
    CHECK_FALSE(state.open());
    CHECK_FALSE(crafting_open(state.mode));
    state.open_mode(CraftingMode::Pocket);
    CHECK(state.open());
    CHECK(crafting_open(state.mode));
    CHECK(craft_grid_width(state.mode) == 2);
    state.open_mode(CraftingMode::Bench);
    CHECK(state.open());
    CHECK(craft_grid_width(state.mode) == 3);
}
