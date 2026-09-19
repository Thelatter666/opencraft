#include "opencraft/game/recipe_registry.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "opencraft/core/log.hpp"

namespace opencraft::game {

namespace {

// ── shapes, spelled as pictures ───────────────────────────────────────────
// A shape is its own MINIMAL BOUNDING BOX, top row first, so what the eye sees
// is what the matcher compares after it crops the grid. Three tokens:
//
//     'M'  the tier's material (the entry's `material`)
//     'H'  the handle (a stick; the entry's `handle`)
//     '.'  a cell INSIDE the box that must stay empty
//
// ⚠ The box matters: the axe is 2 wide, not 3. Its three rows are MM / MH / .H,
// and the empty cell at the box's bottom-left is what distinguishes a laid-out
// axe from a mirrored one that is missing its material - a 3-wide spelling with
// a trailing empty column would never match anything, because the grid is
// cropped to 2 columns before the comparison.
constexpr const char *kShapePick[kMaxCraftGrid] = {"MMM", ".H.", ".H."};
constexpr const char *kShapeAxe[kMaxCraftGrid] = {"MM", "MH", ".H"};
constexpr const char *kShapeShovel[kMaxCraftGrid] = {"M", "H", "H"};
constexpr const char *kShapeSword[kMaxCraftGrid] = {"M", "M", "H"};
constexpr const char *kShapeStick[kMaxCraftGrid] = {"M", "M", nullptr};
constexpr const char *kShapeBench[kMaxCraftGrid] = {"MM", "MM", nullptr};

// The four tool kinds (research/11 §2.3), each with the ONE shape that all of
// their tiers share: "三档配方形状完全相同，只换材料". The kind is the second word
// of the item id, so `chisel` + the `timber_` prefix + `sawn_planks` is
// timber_chisel and its rock twin is the same entry with two other names.
struct ToolKind {
    const char *kind;
    const char *const *rows;
};

constexpr ToolKind kToolKinds[] = {
    {"chisel", kShapePick},
    {"hewer", kShapeAxe},
    {"spade", kShapeShovel},
    {"edge", kShapeSword},
};

// The two tiers this card ships: the prefix the item ids use, the material the
// tool head is made of, and where that material comes from (§C-5: planks are
// crafted from a log, cobblestone is dug out of stone).
struct ToolTier {
    const char *prefix;
    const char *material;
};

constexpr ToolTier kToolTiers[] = {
    {"timber_", "sawn_planks"},
    {"rock_", "rubble_rock"},
};

// ⚖ The one handle material in the item set (C-5: 木棍).
constexpr const char *kStickItem = "timber_stick";

// The item id in a cell: an empty stack's id IS the reserved empty id, so a
// hand-built {item, 0} stack still reads as "nothing here" to the matcher - the
// cells come from the inventory, which normalises, but a test may not.
[[nodiscard]] std::uint16_t item_in(const ItemStack &cell) {
    return cell.empty() ? ItemRegistry::kEmptyId : cell.item;
}

// The minimal bounding box of the grid's non-empty cells; false when the grid is
// empty (no recipe is made of nothing). This crop is the whole of "平移可命中":
// the grid's position stops mattering the moment its box is taken.
struct GridBox {
    int x0 = 0;
    int y0 = 0;
    int x1 = -1; // x1 < x0 == empty
    int y1 = -1;

    [[nodiscard]] int width() const { return x1 - x0 + 1; }

    [[nodiscard]] int height() const { return y1 - y0 + 1; }

    [[nodiscard]] bool empty() const { return x1 < x0 || y1 < y0; }
};

[[nodiscard]] GridBox grid_box(const ItemStack *cells, const int grid_width) {
    GridBox box;
    for (int y = 0; y < grid_width; ++y) {
        for (int x = 0; x < grid_width; ++x) {
            if (item_in(cells[y * grid_width + x]) == ItemRegistry::kEmptyId) {
                continue;
            }
            if (box.empty()) {
                box = GridBox{x, y, x, y};
                continue;
            }
            box.x0 = std::min(box.x0, x);
            box.y0 = std::min(box.y0, y);
            box.x1 = std::max(box.x1, x);
            box.y1 = std::max(box.y1, y);
        }
    }
    return box;
}

// Compares the cropped grid against the pattern, and against the pattern
// MIRRORED horizontally (research/11:412-414: 可水平镜像、不可垂直镜像 - the
// vertical mirror is simply not one of the two orientations tried here, which is
// how it is forbidden).
[[nodiscard]] bool shaped_matches(const Recipe &recipe, const ItemStack *cells, const int grid_width,
                                  const GridBox &box) {
    if (box.width() != recipe.width || box.height() != recipe.height) {
        return false;
    }
    bool direct = true;
    bool mirrored = true;
    for (int y = 0; y < recipe.height; ++y) {
        for (int x = 0; x < recipe.width; ++x) {
            const std::uint16_t here = item_in(cells[(box.y0 + y) * grid_width + (box.x0 + x)]);
            if (direct && here != recipe.pattern[static_cast<std::size_t>(y * recipe.width + x)]) {
                direct = false;
            }
            if (mirrored &&
                here != recipe.pattern[static_cast<std::size_t>(y * recipe.width + (recipe.width - 1 - x))]) {
                mirrored = false;
            }
        }
        if (!direct && !mirrored) {
            return false;
        }
    }
    return direct || mirrored;
}

[[nodiscard]] bool shapeless_matches(const Recipe &recipe, const ItemStack *cells, const int grid_width) {
    std::vector<std::uint16_t> found;
    for (int i = 0; i < grid_width * grid_width; ++i) {
        const std::uint16_t item = item_in(cells[i]);
        if (item != ItemRegistry::kEmptyId) {
            found.push_back(item);
        }
    }
    std::sort(found.begin(), found.end());
    return found == recipe.ingredients;
}

// Builds a shaped recipe from a picture. The pattern's box is measured off the
// rows themselves (width = the first row's length, height = the rows before the
// first nullptr), so a shape cannot disagree with its own dimensions.
[[nodiscard]] Recipe make_shaped(const ItemRegistry &items, const std::string &id, const char *result,
                                 const int result_count, const char *const rows[kMaxCraftGrid], const char *material,
                                 const char *handle) {
    Recipe recipe;
    recipe.id = id;
    recipe.kind = RecipeKind::Shaped;
    recipe.result = items.id_of(result);
    recipe.result_count = result_count;
    recipe.height = 0;
    while (recipe.height < kMaxCraftGrid && rows[recipe.height] != nullptr) {
        ++recipe.height;
    }
    if (recipe.height == 0) {
        throw std::invalid_argument("shaped recipe has no rows: " + id);
    }
    recipe.width = static_cast<int>(std::strlen(rows[0]));
    recipe.pattern.assign(static_cast<std::size_t>(recipe.width * recipe.height), ItemRegistry::kEmptyId);
    for (int y = 0; y < recipe.height; ++y) {
        if (static_cast<int>(std::strlen(rows[y])) != recipe.width) {
            throw std::invalid_argument("shaped recipe rows differ in width: " + id);
        }
        for (int x = 0; x < recipe.width; ++x) {
            const char token = rows[y][x];
            std::uint16_t item = ItemRegistry::kEmptyId;
            if (token == 'M') {
                item = items.id_of(material);
            } else if (token == 'H') {
                if (handle == nullptr) {
                    throw std::invalid_argument("shaped recipe uses a handle it does not declare: " + id);
                }
                item = items.id_of(handle);
            } else if (token != '.') {
                throw std::invalid_argument("unknown pattern token in shaped recipe: " + id);
            }
            recipe.pattern[static_cast<std::size_t>(y * recipe.width + x)] = item;
        }
    }
    return recipe;
}

[[nodiscard]] Recipe make_shapeless(const ItemRegistry &items, const std::string &id, const char *result,
                                    const int result_count, const std::vector<const char *> &ingredients) {
    Recipe recipe;
    recipe.id = id;
    recipe.kind = RecipeKind::Shapeless;
    recipe.result = items.id_of(result);
    recipe.result_count = result_count;
    for (const char *ingredient : ingredients) {
        recipe.ingredients.push_back(items.id_of(ingredient));
    }
    // Sorted ONCE, here: the matcher sorts what it finds, so a shapeless recipe
    // is a multiset by construction rather than by the author remembering to
    // write its ingredients in order.
    std::sort(recipe.ingredients.begin(), recipe.ingredients.end());
    return recipe;
}

} // namespace

RecipeRegistry RecipeRegistry::create_default(const ItemRegistry &items) {
    RecipeRegistry registry;

    // ── 1. 1 原木 → 4 木板, anywhere (research/11 §2.1 #1: 无序) ──────────────
    // The head of the whole chain: it is the only recipe a player with no tools
    // and no table can run, and it is shapeless - the log's cell is irrelevant.
    registry.register_recipe("sawn_planks_from_timber_log",
                             make_shapeless(items, "sawn_planks_from_timber_log", "sawn_planks", 4, {"timber_log"}));

    // ── 2. 2 木板竖排 → 4 木棍 (research/11 §2.1 #2: 有序，B2+B3) ─────────────
    registry.register_recipe(
        "timber_stick_from_sawn_planks",
        make_shaped(items, "timber_stick_from_sawn_planks", "timber_stick", 4, kShapeStick, "sawn_planks", nullptr));

    // ── 3. 4 木板 2×2 → 1 工作台 (C-5 #3) ───────────────────────────────────
    // ⚖ The card rules the TYPE: shaped, with a fixed 2×2 footprint. research/11
    // calls it 无序 in the same row that gives four fixed cells, which cannot both
    // be meant (the type annotation and the fixed footprint contradict each other
    // - 冲突 ②, PM 裁 shaped); a 2×2 block is also what makes the bench
    // distinguishable from four loose planks on the bench's own grid.
    registry.register_recipe("assembly_bench_from_sawn_planks",
                             make_shaped(items, "assembly_bench_from_sawn_planks", "assembly_bench", 1, kShapeBench,
                                         "sawn_planks", nullptr));

    // ── 4..11. the four tools × two tiers (research/11 §2.3, ⚖ verified) ────
    // One loop, two materials: the shapes are the same DATA for both tiers, which
    // is the machine-checkable form of "三档配方形状完全相同，只换材料".
    for (const ToolTier &tier : kToolTiers) {
        for (const ToolKind &kind : kToolKinds) {
            const std::string result = std::string(tier.prefix) + kind.kind;
            const std::string id = result + "_from_" + tier.material;
            registry.register_recipe(id,
                                     make_shaped(items, id, result.c_str(), 1, kind.rows, tier.material, kStickItem));
        }
    }
    return registry;
}

std::uint16_t RecipeRegistry::register_recipe(std::string id, Recipe recipe) {
    if (id.empty()) {
        throw std::invalid_argument("recipe id must not be empty");
    }
    if (numeric_by_id_.contains(id)) {
        throw std::invalid_argument("recipe id already registered: " + id);
    }
    if (recipe.result == ItemRegistry::kEmptyId || recipe.result_count < 1) {
        throw std::invalid_argument("recipe must produce something: " + id);
    }
    if (recipe.kind == RecipeKind::Shaped) {
        if (recipe.width < 1 || recipe.height < 1 || recipe.width > kMaxCraftGrid || recipe.height > kMaxCraftGrid ||
            recipe.pattern.size() != static_cast<std::size_t>(recipe.width * recipe.height)) {
            throw std::invalid_argument("shaped recipe's pattern box is malformed: " + id);
        }
    } else if (recipe.ingredients.empty()) {
        throw std::invalid_argument("shapeless recipe has no ingredients: " + id);
    }
    recipe.id = id;
    const auto numeric_id = static_cast<std::uint16_t>(recipes_.size());
    recipes_.push_back(std::move(recipe));
    id_by_numeric_.push_back(id);
    numeric_by_id_.emplace(std::move(id), numeric_id);
    return numeric_id;
}

bool RecipeRegistry::has_id(std::string_view id) const {
    return numeric_by_id_.contains(id);
}

std::optional<std::uint16_t> RecipeRegistry::find_id(std::string_view id) const {
    const auto it = numeric_by_id_.find(id);
    if (it == numeric_by_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::uint16_t RecipeRegistry::id_of(std::string_view id) const {
    const auto found = find_id(id);
    if (!found) {
        throw std::out_of_range("unknown recipe id: " + std::string(id));
    }
    return *found;
}

const Recipe &RecipeRegistry::recipe(const std::uint16_t numeric_id) const {
    if (numeric_id >= recipes_.size()) {
        throw std::out_of_range("unknown recipe numeric id");
    }
    return recipes_[numeric_id];
}

std::optional<RecipeResult> RecipeRegistry::match(const ItemStack *cells, const int grid_width) const {
    if (cells == nullptr || grid_width < 1 || grid_width > kMaxCraftGrid) {
        return std::nullopt;
    }
    const GridBox box = grid_box(cells, grid_width);
    if (box.empty()) {
        return std::nullopt; // nothing in the grid makes nothing
    }
    const Recipe *found = nullptr;
    for (const Recipe &recipe : recipes_) {
        const bool hit = recipe.kind == RecipeKind::Shaped ? shaped_matches(recipe, cells, grid_width, box)
                                                           : shapeless_matches(recipe, cells, grid_width);
        if (!hit) {
            continue;
        }
        if (found != nullptr) {
            // 无解即拒绝: two recipes for one grid is a content bug, and answering
            // it by rule (lowest id wins) would hide it behind a plausible result.
            OC_LOG_WARN("crafting grid matches more than one recipe ({} and {}); refusing", found->id, recipe.id);
            return std::nullopt;
        }
        found = &recipe;
    }
    if (found == nullptr) {
        return std::nullopt;
    }
    return RecipeResult{found->result, found->result_count};
}

void RecipeRegistry::consume_one(ItemStack *cells, const int grid_width) {
    if (cells == nullptr) {
        return;
    }
    for (int i = 0; i < grid_width * grid_width; ++i) {
        cells[i].shrink(1);
    }
}

} // namespace opencraft::game
