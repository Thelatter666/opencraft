// T-D60: the crafting recipe engine and the launch set of eleven.
//
// The card's §5.3 asks for exactly these: shapeless in any cell, shaped panning,
// a horizontal mirror hitting, a vertical mirror being REFUSED, a 2x2 grid that
// cannot reach a 3x3 recipe, and a refusal when the material is short. The
// acceptance table of §4.1 is the other half: the eleven recipes the card lists,
// with the two tool tiers built from the SAME shapes and different materials.

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/recipe_registry.hpp"

using opencraft::game::ItemRegistry;
using opencraft::game::ItemStack;
using opencraft::game::kMaxCraftGrid;
using opencraft::game::Recipe;
using opencraft::game::RecipeKind;
using opencraft::game::RecipeRegistry;
using opencraft::game::RecipeResult;

namespace {

// One grid, as a picture. 'P' = sawn planks, 'R' = rubble rock (cobblestone),
// 'S' = a timber stick, 'L' = a timber log, '.' = an empty cell (a '.' inside a
// pattern's box is a cell that must STAY empty).
//
// ⚠ The picture is a CRAFT GRID, so it is square: two rows of two, or three of
// three. A shape's own box (the spade is one column wide and three tall) is what
// the engine CROPS OUT of it - writing the picture as the shape would be writing
// a grid the game cannot have.
struct Grid {
    std::array<ItemStack, kMaxCraftGrid * kMaxCraftGrid> cells{};
    int width = 0;

    [[nodiscard]] const ItemStack *data() const { return cells.data(); }
};

// Guards the picture itself: a test that draws a ragged grid has a bug in the
// TEST, and silently returning an empty grid would make it look like a match
// failure. Returns width 0 instead, and every caller asserts what it expected.
[[nodiscard]] Grid grid_of(const ItemRegistry &items, const std::initializer_list<const char *> rows) {
    Grid grid;
    grid.width = static_cast<int>(rows.size());
    if (grid.width < 1 || grid.width > kMaxCraftGrid) {
        return {};
    }
    int y = 0;
    for (const char *row : rows) {
        const int length = static_cast<int>(std::string(row).size());
        if (length != grid.width) {
            return {};
        }
        for (int x = 0; x < length; ++x) {
            const char token = row[x];
            std::uint16_t item = ItemRegistry::kEmptyId;
            switch (token) {
            case '.':
                break;
            case 'P':
                item = items.id_of("sawn_planks");
                break;
            case 'R':
                item = items.id_of("rubble_rock");
                break;
            case 'S':
                item = items.id_of("timber_stick");
                break;
            case 'L':
                item = items.id_of("timber_log");
                break;
            default:
                return {}; // an unknown letter is a test bug, not a recipe answer
            }
            grid.cells[static_cast<std::size_t>(y * grid.width + x)] = ItemStack::of(item, 1);
        }
        ++y;
    }
    return grid;
}

[[nodiscard]] bool makes_into(const RecipeRegistry &recipes, const ItemRegistry &items, const Grid &grid,
                              const char *result, const int count) {
    const std::optional<RecipeResult> found = recipes.match(grid.data(), grid.width);
    if (!found.has_value()) {
        return false;
    }
    return found->item == items.id_of(result) && found->count == count;
}

[[nodiscard]] bool makes(const RecipeRegistry &recipes, const ItemRegistry &items, const Grid &grid,
                         const char *result) {
    return makes_into(recipes, items, grid, result, 1);
}

} // namespace

// ── the launch set ─────────────────────────────────────────────────────────

TEST_CASE("recipe registry: the launch set is the card's eleven, under unique ids") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    // ⚖ C-5: ten shaped (stick, bench, four tools x two tiers) plus one
    // shapeless (the log).
    CHECK(recipes.size() == std::size_t{11});
    std::vector<std::string> ids;
    int shaped = 0;
    int shapeless = 0;
    for (std::uint16_t id = 0; id < recipes.size(); ++id) {
        const Recipe &recipe = recipes.recipe(id);
        ids.push_back(recipe.id);
        shaped += recipe.kind == RecipeKind::Shaped ? 1 : 0;
        shapeless += recipe.kind == RecipeKind::Shapeless ? 1 : 0;
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end()); // unique
    CHECK(shaped == 10);
    CHECK(shapeless == 1);

    // Every result is a real item, and every ingredient is one too (a typo'd id
    // would have thrown in create_default - this asserts the CONTENT, not the
    // loading).
    for (std::uint16_t id = 0; id < recipes.size(); ++id) {
        const Recipe &recipe = recipes.recipe(id);
        CHECK(items.has_numeric(recipe.result));
        CHECK(recipe.result_count >= 1);
        for (const std::uint16_t ingredient : recipe.ingredients) {
            CHECK(items.has_numeric(ingredient));
        }
        for (const std::uint16_t cell : recipe.pattern) {
            CHECK((cell == ItemRegistry::kEmptyId || items.has_numeric(cell)));
        }
    }
}

TEST_CASE("recipe registry: ids are unique and non-empty, and a broken pattern box is refused") {
    const ItemRegistry items = ItemRegistry::create_default();
    RecipeRegistry recipes;
    Recipe recipe;
    recipe.kind = RecipeKind::Shaped;
    recipe.result = items.id_of("sawn_planks");
    recipe.result_count = 1;
    recipe.width = 2;
    recipe.height = 2;
    recipe.pattern.assign(4, items.id_of("sawn_planks"));

    CHECK(recipes.register_recipe("ok", recipe) == 0);
    CHECK_THROWS_AS(recipes.register_recipe("ok", recipe), std::invalid_argument); // duplicate
    CHECK_THROWS_AS(recipes.register_recipe("", recipe), std::invalid_argument);   // empty
    CHECK(recipes.size() == std::size_t{1});

    // A pattern whose box does not match its own dimensions would match nothing
    // at all - a table bug, so it is refused at registration.
    Recipe broken = recipe;
    broken.pattern.assign(3, items.id_of("sawn_planks"));
    CHECK_THROWS_AS(recipes.register_recipe("broken", broken), std::invalid_argument);

    // A recipe that produces nothing is refused too.
    Recipe empty_result = recipe;
    empty_result.result = ItemRegistry::kEmptyId;
    CHECK_THROWS_AS(recipes.register_recipe("nothing", empty_result), std::invalid_argument);

    CHECK(recipes.has_id("ok"));
    CHECK(recipes.find_id("ok") == 0);
    CHECK_THROWS_AS(recipes.id_of("missing"), std::out_of_range);
    CHECK_THROWS_AS(recipes.recipe(99), std::out_of_range);
}

// ── shapeless ──────────────────────────────────────────────────────────────

TEST_CASE("recipe engine: one log makes four planks, in whichever cell it sits") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    // 2x2: all four cells, one at a time.
    for (int cell = 0; cell < 4; ++cell) {
        std::array<ItemStack, 4> cells{};
        cells[static_cast<std::size_t>(cell)] = ItemStack::of(items.id_of("timber_log"), 1);
        const std::optional<RecipeResult> found = recipes.match(cells.data(), 2);
        REQUIRE(found.has_value());
        CHECK(found->item == items.id_of("sawn_planks"));
        CHECK(found->count == 4);
    }
    // 3x3: the four corners and the centre, so "anywhere" is not just "the
    // cells a 2x2 happens to share".
    for (const int cell : {0, 2, 4, 6, 8}) {
        std::array<ItemStack, 9> cells{};
        cells[static_cast<std::size_t>(cell)] = ItemStack::of(items.id_of("timber_log"), 1);
        CHECK(makes_into(recipes, items, Grid{cells, 3}, "sawn_planks", 4));
    }
}

TEST_CASE("recipe engine: shapeless is a MULTISET - the count and the kinds must both match") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    // Two logs are not one log, however they are arranged.
    const Grid two_logs = grid_of(items, {"LL", ".."});
    CHECK_FALSE(recipes.match(two_logs.data(), two_logs.width).has_value());

    // A log plus a plank is not a log.
    const Grid log_and_plank = grid_of(items, {"LP", ".."});
    CHECK_FALSE(recipes.match(log_and_plank.data(), log_and_plank.width).has_value());

    // An empty grid makes nothing.
    const Grid nothing = grid_of(items, {"..", ".."});
    CHECK_FALSE(recipes.match(nothing.data(), nothing.width).has_value());
    // A grid the picture helper refused (a ragged or oversized drawing) reads as
    // an empty one, never as a shape: width 0 makes every match refuse.
    CHECK(grid_of(items, {"P", "P"}).width == 0);
}

// ── shaped ─────────────────────────────────────────────────────────────────

TEST_CASE("recipe engine: two planks stacked vertically make four sticks") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    CHECK(makes_into(recipes, items, grid_of(items, {"P.", "P."}), "timber_stick", 4));
    // 2x2 with the pair in the left column, and 3x3 with it in the right one.
    CHECK(makes_into(recipes, items, grid_of(items, {"P.", "P."}), "timber_stick", 4));
    CHECK(makes_into(recipes, items, grid_of(items, {".P.", ".P.", "..."}), "timber_stick", 4));
    // ... but NOT side by side: the recipe is a vertical pair, and a horizontal
    // one is the vertical mirror the card forbids (§C-4).
    CHECK_FALSE(recipes.match(grid_of(items, {"PP", ".."}).data(), 2).has_value());
}

TEST_CASE("recipe engine: a shaped recipe pans freely and the crop is what pans it") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    // The bench is a 2x2 block of planks, and it is recognised wherever in the
    // 3x3 grid it is laid down - the four corners of the grid and one interior
    // placement, which is every distinct position a 2x2 box can take.
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            std::array<ItemStack, 9> cells{};
            const std::uint16_t planks = items.id_of("sawn_planks");
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    cells[static_cast<std::size_t>((y + dy) * 3 + (x + dx))] = ItemStack::of(planks, 1);
                }
            }
            INFO("bench at (" << x << ", " << y << ")");
            CHECK(makes(recipes, items, Grid{cells, 3}, "assembly_bench"));
        }
    }
    // Three planks in a row are not a bench.
    CHECK_FALSE(recipes.match(grid_of(items, {"PPP", "...", "..."}).data(), 3).has_value());
}

TEST_CASE("recipe engine: a shaped recipe accepts the HORIZONTAL mirror and refuses the vertical one") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    // ⚖ The axe is the asymmetric shape of the set (MM / MH / .H): its mirror
    // image is what the base game also accepts (research/11:412-414), and it is
    // the only one of the four where the two orientations differ.
    const Grid axe = grid_of(items, {"PP.", "PS.", ".S."});
    REQUIRE(makes(recipes, items, axe, "timber_hewer"));
    // The same shape mirrored left-to-right - material on the RIGHT - still hits.
    CHECK(makes(recipes, items, grid_of(items, {".PP", ".SP", ".S."}), "timber_hewer"));
    // The vertical mirror (material row on the BOTTOM, handle row on top) must
    // NOT hit: the base game has no such recipe and the card (§C-4) forbids the
    // vertical mirror by name.
    CHECK_FALSE(recipes.match(grid_of(items, {".S.", "PS.", "PP."}).data(), 3).has_value());
    // Nor does a shape that is missing a material cell - which is what the
    // pattern's '.' cells are for.
    CHECK_FALSE(recipes.match(grid_of(items, {"PP.", ".S.", ".S."}).data(), 3).has_value());
}

TEST_CASE("recipe engine: a 2x2 grid cannot reach a recipe that needs three rows") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    // The pick's box is 3 wide and the sword's is 3 tall; both need the bench, and
    // a 2x2 grid cannot even draw them. The refusal comes from the box
    // comparison alone, which is what "3x3 配方在 2x2 网格内因尺寸自然不匹配"
    // means (§C-4) - a wooden pick laid out as far as a 2x2 allows ("PP"/"PS") is
    // three planks and a stick, which is not any recipe the set has.
    CHECK_FALSE(recipes.match(grid_of(items, {"PP", "PS"}).data(), 2).has_value());
    CHECK_FALSE(recipes.match(grid_of(items, {"PP", "P."}).data(), 2).has_value()); // an L of planks
    // What DOES fit in a 2x2 is the recipe whose box is 2 wide and 2 tall, and
    // that one is craftable in the pocket grid - which is the base game's own
    // rule (the crafting table is the first thing a new player makes, by hand).
    CHECK(makes(recipes, items, grid_of(items, {"PP", "PP"}), "assembly_bench"));
    // And the 1-wide vertical pair is the stick, so the pocket grid is not
    // restricted to 2-wide shapes.
    CHECK(makes_into(recipes, items, grid_of(items, {"P.", "P."}), "timber_stick", 4));
}

TEST_CASE("recipe engine: an under-supplied grid simply makes nothing") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    // The pickaxe wants three materials; two is not a recipe, it is a mistake.
    CHECK_FALSE(recipes.match(grid_of(items, {"PP.", ".S.", ".S."}).data(), 3).has_value());
    // And a stray item where the pattern wants nothing kills the match too.
    CHECK_FALSE(recipes.match(grid_of(items, {"PPP", "PSP", ".S."}).data(), 3).has_value());
}

// ── the four tools, two tiers ──────────────────────────────────────────────

TEST_CASE("recipe engine: the four tool shapes are shared by both tiers, and the material picks the tier") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    struct ToolCase {
        const char *timber;
        const char *rock;
        Grid grid_planks;
        Grid grid_cobble;
    };

    // Built by hand because a Grid holds ItemStacks, not pictures; the shapes are
    // spelled once per tier so a copy-paste slip cannot hide.
    const std::array<ToolCase, 4> cases{{
        // The pick's head row is full; the axe's is an L; the spade is a single
        // head cell over two handle cells; the sword is two head cells. Each is
        // drawn in the 3x3 grid the bench offers, in the top-left pan position -
        // the engine crops them to their own boxes.
        {"timber_chisel", "rock_chisel", grid_of(items, {"PPP", ".S.", ".S."}), grid_of(items, {"RRR", ".S.", ".S."})},
        {"timber_hewer", "rock_hewer", grid_of(items, {"PP.", "PS.", ".S."}), grid_of(items, {"RR.", "RS.", ".S."})},
        {"timber_spade", "rock_spade", grid_of(items, {"P..", "S..", "S.."}), grid_of(items, {"R..", "S..", "S.."})},
        {"timber_edge", "rock_edge", grid_of(items, {"P..", "P..", "S.."}), grid_of(items, {"R..", "R..", "S.."})},
    }};
    for (const ToolCase &tool : cases) {
        INFO("timber " << tool.timber << " / rock " << tool.rock);
        CHECK(makes(recipes, items, tool.grid_planks, tool.timber));
        CHECK(makes(recipes, items, tool.grid_cobble, tool.rock));
        // The materials are NOT interchangeable: cobblestone does not make a
        // timber tool and planks do not make a rock one (the two tiers differ in
        // their material and in nothing else - C-5).
        const std::optional<RecipeResult> planks = recipes.match(tool.grid_planks.data(), 3);
        REQUIRE(planks.has_value());
        CHECK(planks->item != items.id_of(tool.rock));
        const std::optional<RecipeResult> cobble = recipes.match(tool.grid_cobble.data(), 3);
        REQUIRE(cobble.has_value());
        CHECK(cobble->item != items.id_of(tool.timber));
    }
}

// ── consumption ────────────────────────────────────────────────────────────

TEST_CASE("recipe engine: one craft costs one unit out of every non-empty cell") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    // The card's §4.1 arithmetic: an axe is 3 planks + 2 sticks in five cells, so
    // one craft spends exactly one unit from each of those five cells.
    std::array<ItemStack, 9> cells{};
    const std::uint16_t planks = items.id_of("sawn_planks");
    const std::uint16_t stick = items.id_of("timber_stick");
    cells[0] = ItemStack::of(planks, 3);
    cells[1] = ItemStack::of(planks, 1);
    cells[3] = ItemStack::of(planks, 2);
    cells[4] = ItemStack::of(stick, 5);
    cells[7] = ItemStack::of(stick, 1);
    REQUIRE(recipes.match(cells.data(), 3).has_value());

    RecipeRegistry::consume_one(cells.data(), 3);
    CHECK(cells[0].count == 2);
    CHECK(cells[1].empty()); // a lone unit is consumed, not left at zero
    CHECK(cells[3].count == 1);
    CHECK(cells[4].count == 4);
    CHECK(cells[7].empty());
    // The cells the pattern does not use are untouched.
    CHECK(cells[2].empty());
    // ★ And an emptied cell BREAKS the shape: the pattern wants five non-empty
    // cells, so spending a cell's last unit stops the match until the player
    // refills it. That is "材料不足 = 无产物" seen from the other side - and it is
    // why the client crafts only through a grid that matches (an unmatched grid
    // draws an empty result cell, so this consumption cannot even start).
    CHECK_FALSE(recipes.match(cells.data(), 3).has_value());

    // A second craft spends the last plank in cells[3] and another stick out of
    // cells[4]; the grid is down to two cells and still matches nothing.
    RecipeRegistry::consume_one(cells.data(), 3);
    CHECK(cells[3].empty());
    CHECK(cells[4].count == 3);
    CHECK(cells[0].count == 1);
    CHECK_FALSE(recipes.match(cells.data(), 3).has_value());
}

TEST_CASE("recipe registry: a grid that matches two recipes is refused, not guessed") {
    const ItemRegistry items = ItemRegistry::create_default();
    RecipeRegistry recipes = RecipeRegistry::create_default(items);

    // The launch set cannot produce this (the card's §4.1 accounting says the
    // eleven are mutually exclusive), so the ambiguity is built: two shapeless
    // recipes for the same single ingredient, with different outputs.
    Recipe first;
    first.kind = RecipeKind::Shapeless;
    first.result = items.id_of("timber_stick");
    first.result_count = 1;
    first.ingredients.push_back(items.id_of("sawn_planks"));
    recipes.register_recipe("ambiguous_a", first);
    Recipe second = first;
    second.result = items.id_of("timber_log");
    recipes.register_recipe("ambiguous_b", second);

    const Grid grid = grid_of(items, {"P.", ".."});
    // 无解即拒绝: the answer is "nothing", never "whichever id is lower".
    CHECK_FALSE(recipes.match(grid.data(), grid.width).has_value());
}

TEST_CASE("recipe registry: every launch recipe matches its own shape, and only it") {
    const ItemRegistry items = ItemRegistry::create_default();
    const RecipeRegistry recipes = RecipeRegistry::create_default(items);

    // Each recipe is laid into a grid from ITS OWN pattern, with its own
    // ingredients, and the match must come back as that recipe's result. This is
    // the card's §C-4 claim - "首发 11 条经卡面 §4.1 互斥核算，不存在冲突" -
    // checked by the machine instead of by the accounting: if two recipes
    // overlapped, match() would refuse (its 0-match and 2-match answers are the
    // same nullopt), and this test would fail on the affected recipe.
    for (std::uint16_t id = 0; id < recipes.size(); ++id) {
        const Recipe &recipe = recipes.recipe(id);
        INFO("recipe " << recipe.id);
        std::array<ItemStack, kMaxCraftGrid * kMaxCraftGrid> cells{};
        int width = 0;
        if (recipe.kind == RecipeKind::Shaped) {
            // The pattern's own box, in the top-left pan position of the SMALLEST
            // square grid that can hold it - 2x2 for the bench and the stick, the
            // bench's 3x3 for the tools. That is what makes this test also cover
            // "the pocket grid is enough for the bench".
            width = (recipe.width <= 2 && recipe.height <= 2) ? 2 : kMaxCraftGrid;
            for (int y = 0; y < recipe.height; ++y) {
                for (int x = 0; x < recipe.width; ++x) {
                    const std::uint16_t item = recipe.pattern[static_cast<std::size_t>(y * recipe.width + x)];
                    if (item != ItemRegistry::kEmptyId) {
                        cells[static_cast<std::size_t>(y * width + x)] = ItemStack::of(item, 1);
                    }
                }
            }
        } else {
            width = kMaxCraftGrid;
            // Any cell will do for a shapeless recipe: the first ones.
            for (std::size_t i = 0; i < recipe.ingredients.size(); ++i) {
                cells[i] = ItemStack::of(recipe.ingredients[i], 1);
            }
        }
        const std::optional<RecipeResult> found = recipes.match(cells.data(), width);
        REQUIRE(found.has_value());
        CHECK(found->item == recipe.result);
        CHECK(found->count == recipe.result_count);
        // And the grid it matched is one a player could actually lay out: every
        // ingredient it names is a real item (already checked above) and the
        // pattern's size fits the surface it was drawn on.
        CHECK(recipe.width <= width);
        CHECK(recipe.height <= width);
    }
}
