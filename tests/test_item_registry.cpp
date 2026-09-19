#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <doctest/doctest.h>

#include "opencraft/game/item_registry.hpp"
#include "opencraft/voxel/block_registry.hpp"

namespace {

using opencraft::game::EquipSlot;
using opencraft::game::is_standard_stack_limit;
using opencraft::game::ItemDef;
using opencraft::game::ItemRegistry;
using opencraft::game::kNoBlock;
using opencraft::game::kStackLimitLarge;
using opencraft::game::kStackLimitMedium;
using opencraft::game::kStackLimitSingle;

// The item forms of the launch blocks BlockRegistry::create_default() ships.
// Listing them here keeps the launch set honest without freezing its total
// size for later content cards.
//
// Nineteen, not twenty: T-I1 ruling S-3 removed `still_water`, because water
// is not an item in this game (it is carried in a vessel).
//
// ★ T-D60 appended `assembly_bench`: its block is the card's one new block, and
// a block with no item form is placeable but unobtainable. The list grew; the
// rule it feeds - "exactly these items carry a block id, and every other block
// is covered by one of them" - did not change.
constexpr const char *kBlockFormIds[] = {
    "loam_clod",   "sod_loam",    "greyrock",    "rubble_rock", "fine_grit", "pebble_grit",    "grit_slab",
    "timber_log",  "leaf_canopy", "sawn_planks", "clear_pane",  "underrock", "char_ore",       "verdigris_ore",
    "ferrous_ore", "auric_ore",   "lucent_ore",  "rime_block",  "duskglass", "assembly_bench",
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

TEST_CASE("item registry default set covers the block item forms plus samples of each tier") {
    const auto registry = ItemRegistry::create_default();
    CHECK(registry.size() >= 20); // the reserved entry plus the nineteen block forms
    // T-I1 ruling S-3: water is not an item in this game, so its item form is
    // gone for good rather than renamed.
    CHECK_FALSE(registry.has_id("still_water"));

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

TEST_CASE("item registry block link names a real block or the non-zero sentinel") {
    const auto registry = ItemRegistry::create_default();
    const auto blocks = opencraft::voxel::BlockRegistry::create_default();

    // The sentinel must not collide with a legal block id: 0 is air (T-I1
    // ruling S-2 forbids using it for "places nothing").
    CHECK(kNoBlock != opencraft::voxel::BlockRegistry::kAirId);
    CHECK(kNoBlock == 0xFFFF);

    // The reserved empty entry never places anything either.
    CHECK(registry.def_of(ItemRegistry::kEmptyId).block == kNoBlock);

    // Exactly the block-form items carry a block id, and it is always a real
    // block that is not air; every other item answers kNoBlock.
    for (std::uint16_t id = 1; id < registry.size(); ++id) {
        const std::string &name = registry.string_of(id);
        const std::uint16_t block = registry.def_of(id).block;
        const bool is_block_form = std::any_of(std::begin(kBlockFormIds), std::end(kBlockFormIds),
                                               [&](const char *candidate) { return name == candidate; });
        INFO("item " << name << " block " << block);
        if (is_block_form) {
            CHECK(block != kNoBlock);
            REQUIRE(blocks.has_numeric(block));
            CHECK(block != opencraft::voxel::BlockRegistry::kAirId);
        } else {
            CHECK(block == kNoBlock);
        }
    }

    // Spot checks that the link points at the intended block, not merely at
    // some registered one.
    CHECK(registry.def_of(registry.id_of("greyrock")).block == blocks.id_of("stone"));
    CHECK(registry.def_of(registry.id_of("loam_clod")).block == blocks.id_of("dirt"));
    CHECK(registry.def_of(registry.id_of("rime_block")).block == blocks.id_of("snow_block"));
    CHECK(registry.def_of(registry.id_of("duskglass")).block == blocks.id_of("obsidian"));

    // Water is the one launch block with no item form (S-3): nothing may place
    // it, which is what "water is carried, never picked up" looks like in the
    // data.
    for (std::uint16_t id = 1; id < registry.size(); ++id) {
        INFO("item " << registry.string_of(id));
        CHECK(registry.def_of(id).block != blocks.id_of("water"));
    }

    // The item set is exactly the item forms of the launch blocks, water
    // excepted: every other block is placed by one of them.
    for (std::uint16_t block = 1; block < blocks.size(); ++block) {
        if (block == blocks.id_of("water")) {
            continue;
        }
        INFO("block " << blocks.string_of(block));
        const bool covered = std::any_of(std::begin(kBlockFormIds), std::end(kBlockFormIds), [&](const char *id) {
            return registry.def_of(registry.id_of(id)).block == block;
        });
        CHECK(covered);
    }
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

// ── T-D46: the armour numbers ───────────────────────────────────────────────

TEST_CASE("item registry: the four timber_* pieces carry the leather tier's armour") {
    // ⚖ T-D46 ruling C-3 (research/01 §2): the leather set is 7 points over the
    // four pieces, split 1/2/3/1 (head/chest/legs/feet), with toughness 0 - and
    // the base game gives leather and iron no toughness at all, so the 0 is the
    // sourced value rather than a placeholder.
    const auto registry = ItemRegistry::create_default();
    const auto points = [&](const char *id) { return registry.def_of(registry.id_of(id)).armor_points; };
    const auto toughness = [&](const char *id) { return registry.def_of(registry.id_of(id)).armor_toughness; };

    CHECK(points("timber_headguard") == doctest::Approx(1.0));
    CHECK(points("timber_cuirass") == doctest::Approx(2.0));
    CHECK(points("timber_greaves") == doctest::Approx(3.0));
    CHECK(points("timber_treads") == doctest::Approx(1.0));
    CHECK(points("timber_headguard") + points("timber_cuirass") + points("timber_greaves") + points("timber_treads") ==
          doctest::Approx(7.0)); // ⚖ 皮革 7 全套

    for (const char *id : {"timber_headguard", "timber_cuirass", "timber_greaves", "timber_treads"}) {
        INFO("piece " << id);
        CHECK(toughness(id) == doctest::Approx(0.0));
    }
}

TEST_CASE("item registry: nothing outside the four armour pieces contributes armour") {
    // C-3 in one assertion: this card gives the EXISTING four pieces the leather
    // numbers and adds no armour item. The iron tier (15 = 2/5/6/2) is a later
    // content card's four entries, and a wood-tier value in between is what
    // T-R2's R-2 ruling forbids.
    const auto registry = ItemRegistry::create_default();
    std::vector<std::string> armoured;
    for (std::uint16_t id = 1; id < registry.size(); ++id) {
        const ItemDef &def = registry.def_of(id);
        if (def.armor_points > 0.0) {
            armoured.push_back(registry.string_of(id));
            // Armour only ever sits in an armour cell, and each piece names its
            // own body part (the inventory enforces the pairing).
            CHECK(def.equip != EquipSlot::None);
            CHECK(def.max_stack == kStackLimitSingle);
        }
        // The toughness term is 0 for the whole launch set (leather AND iron are
        // 0 in the base game), so nothing may register a non-zero one.
        CHECK(def.armor_toughness == doctest::Approx(0.0));
    }
    CHECK(armoured.size() == 4);
    CHECK(armoured ==
          std::vector<std::string>{"timber_headguard", "timber_cuirass", "timber_greaves", "timber_treads"});
}
