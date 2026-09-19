#pragma once

// T-D60: the crafting recipes, as data (docs/01 §5: 合成 2×2（背包）/3×3（工作台）；
// research/11 §2 is the ⚖ source for the launch shapes).
//
// Two semantics, and the card's contract ③ fixes both:
//
//   * SHAPED - the grid is cropped to its minimal bounding box and compared
//     against the pattern's own box. Panning is free (the crop is what removes
//     the position) and so is a HORIZONTAL MIRROR, which is how the base game
//     treats a laid-out recipe (research/11:412-414, ⚖ verified). A VERTICAL
//     mirror is NOT allowed: it is a different recipe there, and the card rules
//     it out by name. Cells inside the box that the pattern leaves '.' must be
//     empty in the grid - that is what makes the pickaxe (XXX/.S./.S.) different
//     from a grid whose head row is not full.
//   * SHAPELESS - the multiset of the grid's non-empty contents, position-free
//     (1 原木 → 4 木板 wherever the log sits).
//
// No engine, no world, no inventory: a matcher over item ids, so both the
// pocket grid and the bench grid are the same call with a different width, and
// a test can drive it without a client.
//
// The table is data-driven and follows ItemRegistry's conventions exactly
// (string ids resolved once at build time, duplicate/empty ids throw, a dense
// numeric id per recipe) because that is the shape the rest of the item layer
// already has.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"

namespace opencraft::game {

// The bench's grid; the pocket grid is the same matcher with width 2.
inline constexpr int kMaxCraftGrid = 3;

enum class RecipeKind : std::uint8_t {
    Shaped = 0,
    Shapeless,
};

// One recipe. Shaped entries fill width/height/pattern (row-major, kEmptyId for
// a cell that must be EMPTY - not for a cell that is simply outside the
// pattern); shapeless entries fill `ingredients` (ascending, one id per unit).
// The unused half stays empty, which is what lets one struct serve both and one
// registry hold them.
struct Recipe {
    std::string id;
    RecipeKind kind = RecipeKind::Shaped;
    std::uint16_t result = ItemRegistry::kEmptyId;
    int result_count = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint16_t> pattern;
    std::vector<std::uint16_t> ingredients;
};

// What a grid makes: the produced stack, before anything is consumed.
struct RecipeResult {
    std::uint16_t item = ItemRegistry::kEmptyId;
    int count = 0;

    [[nodiscard]] constexpr bool empty() const { return item == ItemRegistry::kEmptyId || count <= 0; }

    [[nodiscard]] friend constexpr bool operator==(const RecipeResult &lhs, const RecipeResult &rhs) = default;
};

// String-id to runtime recipe mapping, mirroring ItemRegistry/BlockRegistry.
class RecipeRegistry {
public:
    // The launch set (11 recipes: the card's §C-5 table). Resolves every item id
    // against `items` and throws std::out_of_range if one is missing - the same
    // fail-loudly-on-content-drift contract the item table's block links use.
    [[nodiscard]] static RecipeRegistry create_default(const ItemRegistry &items);

    RecipeRegistry() = default;

    // Returns the recipe's numeric id; std::invalid_argument for an empty or
    // duplicate id, plus for a shaped recipe whose pattern box does not match
    // its own width/height (a table bug that would otherwise match nothing).
    std::uint16_t register_recipe(std::string id, Recipe recipe);

    [[nodiscard]] bool has_id(std::string_view id) const;
    [[nodiscard]] std::optional<std::uint16_t> find_id(std::string_view id) const;
    [[nodiscard]] std::uint16_t id_of(std::string_view id) const;
    [[nodiscard]] const Recipe &recipe(std::uint16_t numeric_id) const;

    [[nodiscard]] std::size_t size() const { return recipes_.size(); }

    // What the grid makes, or nullopt. `cells` is row-major, `grid_width` x
    // `grid_width` (2 for the pocket grid, 3 for the bench), and a cell may hold
    // an empty stack.
    //
    // nullopt means "refuse", and it covers both halves of the card's 无解即拒绝:
    // nothing matched, or MORE than one recipe did. The second case cannot happen
    // in the launch set (the card's §4.1 accounting says the 11 are mutually
    // exclusive) - if a later card adds overlapping recipes it is reported as a
    // WARN and the grid refuses, because silently picking the lower id would turn
    // a content bug into a mystery for the player.
    [[nodiscard]] std::optional<RecipeResult> match(const ItemStack *cells, int grid_width) const;

    // The cost of one craft: one unit out of EVERY non-empty cell (the card's
    // §2.3 ruling). Same row-major array `match` read; a cell holding five units
    // loses one, exactly like a cell holding one. Shaped and shapeless do not
    // differ here, because cropping cannot reach a cell outside the bounding box
    // and every cell inside it is non-empty by construction.
    static void consume_one(ItemStack *cells, int grid_width);

private:
    std::unordered_map<std::string, std::uint16_t, StringHash, std::equal_to<>> numeric_by_id_;
    std::vector<std::string> id_by_numeric_;
    std::vector<Recipe> recipes_;
};

} // namespace opencraft::game
