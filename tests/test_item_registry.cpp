#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <doctest/doctest.h>

#include "opencraft/game/item_registry.hpp"

namespace {

using opencraft::game::EquipSlot;
using opencraft::game::is_standard_stack_limit;
using opencraft::game::ItemDef;
using opencraft::game::ItemRegistry;
using opencraft::game::kStackLimitLarge;
using opencraft::game::kStackLimitMedium;
using opencraft::game::kStackLimitSingle;

// The item forms of the twenty blocks BlockRegistry::create_default() ships
// (T-I1: "现有 20 个方块的物品形态"). Listing them here keeps the launch set
// honest without freezing its total size for later content cards.
constexpr const char *kBlockFormIds[] = {
    "loam_clod",     "sod_loam",    "greyrock",    "rubble_rock", "fine_grit",   "pebble_grit", "grit_slab",
    "timber_log",    "leaf_canopy", "sawn_planks", "clear_pane",  "still_water", "underrock",   "char_ore",
    "verdigris_ore", "ferrous_ore", "auric_ore",   "lucent_ore",  "rime_block",  "duskglass",
};

} // namespace

TEST_CASE("item registry reserves the empty id as a real entry") {
    const auto registry = ItemRegistry::create_default();
    CHECK(ItemRegistry::kEmptyId == 0);
    CHECK(registry.empty() == ItemRegistry::kEmptyId);
    CHECK(registry.find_id("empty").value_or(99) == ItemRegistry::kEmptyId);
    CHECK(registry.string_of(ItemRegistry::kEmptyId) == "empty");
    CHECK(registry.has_numeric(ItemRegistry::kEmptyId));
    const auto &def = registry.def_of(ItemRegistry::kEmptyId);
    CHECK(def.display_name == "Empty");
    CHECK(def.max_stack == kStackLimitSingle);
    CHECK(def.equip == EquipSlot::None);
}

TEST_CASE("item registry assigns dense numeric ids and supports both query directions") {
    auto registry = ItemRegistry::create_default();
    const auto first_new = registry.register_item("test_a", {"Test A", kStackLimitLarge, EquipSlot::None});
    const auto second_new = registry.register_item("test_b", {"Test B", kStackLimitMedium, EquipSlot::Chest});
    CHECK(first_new == registry.size() - 2);
    CHECK(second_new == first_new + 1);
    CHECK(first_new > ItemRegistry::kEmptyId);

    CHECK(registry.find_id("test_a") == first_new);
    CHECK(registry.id_of("test_b") == second_new);
    CHECK(registry.string_of(first_new) == "test_a");
    CHECK(registry.string_of(second_new) == "test_b");

    const auto &second = registry.def_of(second_new);
    CHECK(second.display_name == "Test B");
    CHECK(second.max_stack == kStackLimitMedium);
    CHECK(second.equip == EquipSlot::Chest);

    CHECK(registry.has_id("test_a"));
    CHECK_FALSE(registry.has_id("not_an_item"));
    CHECK(registry.has_numeric(first_new));
    CHECK_FALSE(registry.has_numeric(static_cast<std::uint16_t>(registry.size())));
}

TEST_CASE("item registry rejects empty and duplicate ids") {
    auto registry = ItemRegistry::create_default();
    const auto before = registry.size();
    CHECK_THROWS_AS(registry.register_item("", {"Nameless", kStackLimitLarge, EquipSlot::None}), std::invalid_argument);
    CHECK_THROWS_AS(registry.register_item("loam_clod", {"Copy", kStackLimitLarge, EquipSlot::None}),
                    std::invalid_argument);
    CHECK_THROWS_AS(registry.register_item("empty", {"Copy", kStackLimitLarge, EquipSlot::None}),
                    std::invalid_argument);
    // A rejected registration leaves the registry untouched: no wasted dense
    // id, no half-inserted entry.
    CHECK(registry.size() == before);
    CHECK_FALSE(registry.has_id(""));

    CHECK_FALSE(registry.find_id("no_such_item").has_value());
    CHECK_THROWS_AS([&] { static_cast<void>(registry.id_of("no_such_item")); }(), std::out_of_range);
    CHECK_THROWS_AS([&] { static_cast<void>(registry.string_of(0xFFFF)); }(), std::out_of_range);
    CHECK_THROWS_AS([&] { static_cast<void>(registry.def_of(0xFFFF)); }(), std::out_of_range);
}

TEST_CASE("item registry rejects a non-positive stack limit") {
    auto registry = ItemRegistry::create_default();
    const auto before = registry.size();
    CHECK_THROWS_AS(registry.register_item("zero_stack", {"Zero", 0, EquipSlot::None}), std::invalid_argument);
    CHECK_THROWS_AS(registry.register_item("negative_stack", {"Negative", -3, EquipSlot::None}), std::invalid_argument);
    CHECK(registry.size() == before);
    // One unit is legal: it is the third documented tier.
    const auto single = registry.register_item("single_stack", {"Single", kStackLimitSingle, EquipSlot::None});
    CHECK(registry.def_of(single).max_stack == kStackLimitSingle);
}

TEST_CASE("item registry default set covers the twenty block item forms plus samples of each tier") {
    const auto registry = ItemRegistry::create_default();
    CHECK(registry.size() >= 21); // the reserved entry plus the twenty block forms

    for (const char *id : kBlockFormIds) {
        INFO("block form item: " << id);
        REQUIRE(registry.has_id(id));
        CHECK(registry.def_of(registry.id_of(id)).max_stack == kStackLimitLarge);
    }

    // Every documented tier has a sample: 64 (blocks), 16 (an empty vessel),
    // 1 (a filled vessel and the first tool tier).
    CHECK(registry.def_of(registry.id_of("empty_vessel")).max_stack == kStackLimitMedium);
    CHECK(registry.def_of(registry.id_of("water_vessel")).max_stack == kStackLimitSingle);
    CHECK(registry.def_of(registry.id_of("timber_chisel")).max_stack == kStackLimitSingle);
    CHECK(registry.def_of(registry.id_of("rime_pearl")).max_stack == kStackLimitMedium);

    // Armour: exactly one item per body part, nothing else equippable.
    CHECK(registry.def_of(registry.id_of("timber_headguard")).equip == EquipSlot::Head);
    CHECK(registry.def_of(registry.id_of("timber_cuirass")).equip == EquipSlot::Chest);
    CHECK(registry.def_of(registry.id_of("timber_greaves")).equip == EquipSlot::Legs);
    CHECK(registry.def_of(registry.id_of("timber_treads")).equip == EquipSlot::Feet);
    CHECK(registry.def_of(registry.id_of("loam_clod")).equip == EquipSlot::None);

    // Display names are original (docs/04 red lines 2/5) and never blank.
    CHECK(registry.def_of(registry.id_of("underrock")).display_name == "Underrock");
    CHECK(registry.def_of(registry.id_of("duskglass")).display_name == "Duskglass");
    for (std::uint16_t id = 1; id < registry.size(); ++id) {
        INFO("item " << registry.string_of(id));
        CHECK_FALSE(registry.def_of(id).display_name.empty());
    }
}

TEST_CASE("item registry default stack limits stay inside the three documented tiers") {
    const auto registry = ItemRegistry::create_default();
    int large = 0;
    int medium = 0;
    int single = 0;
    for (std::uint16_t id = 1; id < registry.size(); ++id) {
        INFO("item " << registry.string_of(id));
        const int max_stack = registry.def_of(id).max_stack;
        REQUIRE(is_standard_stack_limit(max_stack));
        large += max_stack == kStackLimitLarge ? 1 : 0;
        medium += max_stack == kStackLimitMedium ? 1 : 0;
        single += max_stack == kStackLimitSingle ? 1 : 0;
    }
    CHECK(large > 0);
    CHECK(medium > 0);
    CHECK(single > 0);
}

TEST_CASE("item registry looks up a string_view slice without a copy") {
    const auto registry = ItemRegistry::create_default();
    const std::string buffer = "prefix/loam_clod:suffix";
    const std::string_view slice(buffer.data() + 7, 9);
    CHECK(slice == "loam_clod");
    const auto found = registry.find_id(slice);
    REQUIRE(found.has_value());
    CHECK(*found == registry.id_of("loam_clod"));
    CHECK(registry.id_of(slice) == found);
}
