// T-D60: the harvest tiers and the mining multiplier - the card's §4.1 table,
// line by line, plus the two rules underneath it (the level gate and the "an
// under-tiered tool mines at multiplier 1" reduction).
//
// ⚖ The five rows of §4.1 are the card's own authoritative arithmetic:
//
//   bare hand on stone (level below the requirement)  7.5 s   no drop
//   wooden pick on stone (tier 0 >= 0)                1.125 s rubble x1
//   stone pick on stone (tier 1, speed 4)             0.5625 s rubble x1
//   wooden pick on iron ore (tier 0 < 1)              15 s    no drop
//   stone pick on iron ore (tier 1 >= 1)              1.125 s drop
//   bare hand on a log (hand-harvestable)             3 s     log x1
//
// The seconds are the continuous ⚖ times from docs/01 §4; the tick counts below
// are the integer ones the machine actually takes (ceil of the same quotient),
// and both are asserted, because "1.125 s" and "23 ticks" are the same promise
// written two ways.

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/mining.hpp"
#include "opencraft/voxel/block_registry.hpp"

using opencraft::game::can_harvest_by_hand;
using opencraft::game::can_harvest_with;
using opencraft::game::ItemRegistry;
using opencraft::game::kHandHarvestable;
using opencraft::game::kNoTool;
using opencraft::game::MiningTickResult;
using opencraft::game::MiningTool;
using opencraft::game::MiningTracker;
using opencraft::voxel::BlockRegistry;

namespace {

// The ticks a full block takes, and whether it ever breaks: drives the machine
// against one fixed target until it breaks or the budget runs out.
struct MineOutcome {
    int break_tick = -1; // 0-based index of the breaking tick
    double seconds = 0.0;

    bool broke() const { return break_tick >= 0; }
};

[[nodiscard]] MineOutcome mine(const BlockRegistry &blocks, const std::uint16_t block, const MiningTool tool,
                               const int budget) {
    MiningTracker tracker(blocks);
    const glm::ivec3 target(2, 3, 4);
    for (int i = 0; i < budget; ++i) {
        if (tracker.tick(target, block, true, true, tool).broke) {
            return {i, static_cast<double>(i + 1) / 20.0};
        }
    }
    return {};
}

// The tool a held item provides, exactly the way the tick resolves it.
[[nodiscard]] MiningTool tool_of(const ItemRegistry &items, const char *id) {
    return opencraft::game::mining_tool_of(items.def_of(items.id_of(id)));
}

} // namespace

// ── the requirement table ──────────────────────────────────────────────────

TEST_CASE("mining tiers: the block requirements are the base game's tool gates") {
    const BlockRegistry blocks = BlockRegistry::create_default();
    const auto level = [&](const char *id) { return opencraft::game::harvest_level(blocks, blocks.id_of(id)); };

    // ★ T-D60's reading of the card's table: stone's gate is the WOODEN pick (0),
    // because §4.1 has 木镐挖石 succeed and the whole chain (木镐 → 圆石 → 石镐)
    // runs through it; the hand is below the sheet (kNoTool), which is what keeps
    // 徒手挖石 at 7.5 s with no drop at the same time.
    CHECK(level("stone") == 0);
    CHECK(level("cobblestone") == 0);
    CHECK(level("coal_ore") == 0);
    // ⚖ C-6: 铜/铁矿 = 1 (stone pick), 金/钻石矿 = 2 (iron pick, the card's
    // CONSERVATIVE value for gold - the base game also wants an iron pick).
    CHECK(level("copper_ore") == 1);
    CHECK(level("iron_ore") == 1);
    CHECK(level("gold_ore") == 2);
    CHECK(level("diamond_ore") == 2);
    CHECK(level("obsidian") == 3);

    // Everything a hand can take reports the hand's own value - which is also
    // what an unknown id and air report.
    for (const char *id : {"dirt", "grass_block", "sand", "gravel", "sandstone", "log", "leaves", "planks", "glass",
                           "snow_block", "assembly_bench", "air"}) {
        INFO(id);
        CHECK(level(id) == kHandHarvestable);
    }
    CHECK(level("bedrock") > 3); // unbreakable: no tier reaches it
    CHECK_FALSE(can_harvest_by_hand(blocks, blocks.id_of("bedrock")));
    CHECK_FALSE(opencraft::game::can_harvest_with(blocks, blocks.id_of("bedrock"), MiningTool{4, 100.0}));
}

TEST_CASE("mining tiers: each tool tier reaches exactly one rung further") {
    const BlockRegistry blocks = BlockRegistry::create_default();
    const ItemRegistry items = ItemRegistry::create_default();

    // The two shipped tiers, and what each of them can and cannot take.
    const MiningTool hand{};
    const MiningTool timber = tool_of(items, "timber_chisel");
    const MiningTool rock = tool_of(items, "rock_chisel");
    CHECK(hand.tier == kNoTool);
    CHECK(hand.speed == doctest::Approx(1.0));
    CHECK(timber.tier == 0);
    CHECK(timber.speed == doctest::Approx(2.0)); // ⚖ §5.1: 木 2
    CHECK(rock.tier == 1);
    CHECK(rock.speed == doctest::Approx(4.0)); // ⚖ §5.1: 石 4

    struct Gate {
        const char *block;
        bool hand;
        bool timber;
        bool rock;
    };

    const Gate gates[] = {
        {"dirt", true, true, true},       // no tool needed at all
        {"log", true, true, true},        //
        {"stone", false, true, true},     // wood and up
        {"coal_ore", false, true, true},  //
        {"iron_ore", false, false, true}, // stone and up
        {"copper_ore", false, false, true},
        {"gold_ore", false, false, false},    // iron and up: beyond this card
        {"diamond_ore", false, false, false}, //
        {"obsidian", false, false, false},    // diamond and up
    };
    for (const Gate &gate : gates) {
        const std::uint16_t block = blocks.id_of(gate.block);
        INFO(gate.block);
        CHECK(can_harvest_with(blocks, block, hand) == gate.hand);
        CHECK(can_harvest_with(blocks, block, timber) == gate.timber);
        CHECK(can_harvest_with(blocks, block, rock) == gate.rock);
    }
}

// ── §4.1, line by line ─────────────────────────────────────────────────────

TEST_CASE("mining tiers: §4.1 row 1 - a bare hand on stone takes 7.5 s and drops nothing") {
    const BlockRegistry blocks = BlockRegistry::create_default();
    const std::uint16_t stone = blocks.id_of("stone");
    CHECK_FALSE(can_harvest_by_hand(blocks, stone));

    // 1.5 x 5 / 1 = 7.5 s, and the machine is unchanged from T008: 150 ticks.
    const MineOutcome out = mine(blocks, stone, MiningTool{}, 400);
    REQUIRE(out.broke());
    CHECK(out.break_tick == 149);
    CHECK(out.seconds == doctest::Approx(7.5));
    CHECK_FALSE(can_harvest_with(blocks, stone, MiningTool{})); // hence: no drop
}

TEST_CASE("mining tiers: §4.1 row 2 - a wooden pick on stone takes 1.125 s and drops a block") {
    const BlockRegistry blocks = BlockRegistry::create_default();
    const ItemRegistry items = ItemRegistry::create_default();
    const std::uint16_t stone = blocks.id_of("stone");
    const MiningTool pick = tool_of(items, "timber_chisel");
    REQUIRE(can_harvest_with(blocks, stone, pick));

    // 1.5 x 1.5 / 2 = 1.125 s: the continuous ⚖ time, and ceil() of it in ticks.
    const MineOutcome out = mine(blocks, stone, pick, 200);
    REQUIRE(out.broke());
    CHECK(out.break_tick == 22); // 23 ticks = 1.15 s, the first tick at or past 1.125 s
    CHECK(out.seconds == doctest::Approx(1.15));
}

TEST_CASE("mining tiers: §4.1 row 3 - a stone pick on stone is twice as fast again") {
    const BlockRegistry blocks = BlockRegistry::create_default();
    const ItemRegistry items = ItemRegistry::create_default();
    const MiningTool pick = tool_of(items, "rock_chisel");
    REQUIRE(can_harvest_with(blocks, blocks.id_of("stone"), pick));

    // 1.5 x 1.5 / 4 = 0.5625 s.
    const std::uint16_t stone = blocks.id_of("stone");
    const MineOutcome out = mine(blocks, stone, pick, 200);
    REQUIRE(out.broke());
    CHECK(out.break_tick == 11); // 12 ticks = 0.6 s

    // The two picks in one place, in ticks: 12 against 23 - the ceil() of 11.25
    // and of 22.5. Both are the first whole tick at or past the ⚖ time, which is
    // why the ratio is "about two" and not exactly two.
    const MineOutcome timber = mine(blocks, stone, tool_of(items, "timber_chisel"), 200);
    REQUIRE(timber.broke());
    CHECK(out.break_tick + 1 == 12);
    CHECK(timber.break_tick + 1 == 23);
}

TEST_CASE("mining tiers: §4.1 row 4 - iron ore needs the stone tier, and the wood tier gets nothing") {
    const BlockRegistry blocks = BlockRegistry::create_default();
    const ItemRegistry items = ItemRegistry::create_default();
    const std::uint16_t ore = blocks.id_of("iron_ore");
    const MiningTool timber = tool_of(items, "timber_chisel");
    const MiningTool rock = tool_of(items, "rock_chisel");
    REQUIRE_FALSE(can_harvest_with(blocks, ore, timber)); // tier 0 < 1
    REQUIRE(can_harvest_with(blocks, ore, rock));         // tier 1 >= 1

    // 3.0 x 5 / 1 = 15 s, and the gear's multiplier does NOT apply: an
    // under-tiered tool mines at 1 (research/01 §5.3's "挖得极慢且无掉落").
    const MineOutcome slow = mine(blocks, ore, timber, 400);
    REQUIRE(slow.broke());
    CHECK(slow.break_tick == 299);
    CHECK(slow.seconds == doctest::Approx(15.0));

    // 3.0 x 1.5 / 4 = 1.125 s once the tier is there. (The card's own table
    // prints 0.5625 s for this cell, which is the STONE row's number - 3.0 is
    // iron ore's hardness, not stone's 1.5. The gate the row is about, "wood no
    // / stone yes", is asserted above; this is its correct time.)
    const MineOutcome fast = mine(blocks, ore, rock, 200);
    REQUIRE(fast.broke());
    CHECK(fast.break_tick == 22);
    CHECK(fast.seconds == doctest::Approx(1.15));
}

TEST_CASE("mining tiers: §4.1 row 5 - a bare hand on a log is unchanged at 3 s with a drop") {
    const BlockRegistry blocks = BlockRegistry::create_default();
    const std::uint16_t log = blocks.id_of("log");
    REQUIRE(can_harvest_by_hand(blocks, log));

    // 2.0 x 1.5 / 1 = 3 s - the same number the T008 machine produced, which is
    // what "徒手行为逐字节不变" means for a block the hand can take.
    const MineOutcome out = mine(blocks, log, MiningTool{}, 200);
    REQUIRE(out.broke());
    CHECK(out.break_tick == 59);
    CHECK(out.seconds == doctest::Approx(3.0));

    // A tool that merely HAS a tier does not change the time when its multiplier
    // is 1: the divisor is the same and the branch is the same.
    const MineOutcome with_hand_like_tool = mine(blocks, log, MiningTool{0, 1.0}, 200);
    REQUIRE(with_hand_like_tool.broke());
    CHECK(with_hand_like_tool.break_tick == out.break_tick);
}

TEST_CASE("mining tiers: a bare-handed swing is the T008 machine, tick for tick") {
    const BlockRegistry blocks = BlockRegistry::create_default();
    // The four bare-handed cases the T008 tests freeze, re-checked through the
    // new entry point that names a tool: the numbers must not have moved.
    CHECK(mine(blocks, blocks.id_of("dirt"), MiningTool{}, 100).break_tick == 14);    // 0.75 s
    CHECK(mine(blocks, blocks.id_of("stone"), MiningTool{}, 400).break_tick == 149);  // 7.5 s
    CHECK(mine(blocks, blocks.id_of("log"), MiningTool{}, 200).break_tick == 59);     // 3.0 s
    CHECK(mine(blocks, blocks.id_of("bedrock"), MiningTool{}, 100).break_tick == -1); // never
}

// ── the tool set's own numbers ─────────────────────────────────────────────

TEST_CASE("mining tiers: the eight tools carry the sheet's two tiers, and nothing else does") {
    const ItemRegistry items = ItemRegistry::create_default();
    const char *const timber[] = {"timber_chisel", "timber_hewer", "timber_spade", "timber_edge"};
    const char *const rock[] = {"rock_chisel", "rock_hewer", "rock_spade", "rock_edge"};

    for (const char *id : timber) {
        INFO(id);
        CHECK(items.def_of(items.id_of(id)).mining_tier == 0);
        CHECK(items.def_of(items.id_of(id)).mining_speed == doctest::Approx(2.0));
    }
    for (const char *id : rock) {
        INFO(id);
        CHECK(items.def_of(items.id_of(id)).mining_tier == 1);
        CHECK(items.def_of(items.id_of(id)).mining_speed == doctest::Approx(4.0));
    }

    // ⚖ C-5's attack numbers for the rock tier (research/01 §6.1's 基础伤害 line),
    // at the timber set's per-KIND speeds. The kill counts are the card's §7.5
    // arithmetic: a 20 HP Hollow Wretch falls to a rock blade in 4 hits, to a
    // timber blade in 5, and to a fist in 20.
    struct Weapon {
        const char *id;
        double damage;
        double speed;
    };

    const Weapon rock_weapons[] = {
        {"rock_edge", 5.0, 1.6}, {"rock_hewer", 9.0, 0.8}, {"rock_chisel", 3.0, 1.2}, {"rock_spade", 3.5, 1.0}};
    for (const Weapon &weapon : rock_weapons) {
        INFO(weapon.id);
        const opencraft::game::ItemDef &def = items.def_of(items.id_of(weapon.id));
        CHECK(def.attack_damage == doctest::Approx(weapon.damage));
        CHECK(def.attack_speed == doctest::Approx(weapon.speed));
    }
    const auto hits_to_kill = [](const double damage) { return static_cast<int>(std::ceil(20.0 / damage)); };
    CHECK(hits_to_kill(5.0) == 4);  // rock blade
    CHECK(hits_to_kill(4.0) == 5);  // timber blade, unchanged from T-D59
    CHECK(hits_to_kill(9.0) == 3);  // rock axe
    CHECK(hits_to_kill(1.0) == 20); // a fist

    // Everything that is not a tool mines like a hand: a stick, a block, food,
    // armour. That is the DEFAULT of the two fields, and the reason a future
    // content card cannot accidentally hand out a pickaxe tier.
    for (const char *id : {"timber_stick", "assembly_bench", "greyrock", "sunroot", "timber_cuirass", "empty_vessel"}) {
        INFO(id);
        CHECK(items.def_of(items.id_of(id)).mining_tier == kNoTool);
        CHECK(items.def_of(items.id_of(id)).mining_speed == doctest::Approx(1.0));
    }
    // The empty hand itself: the item the reserved id names.
    CHECK(items.def_of(ItemRegistry::kEmptyId).mining_tier == kNoTool);
    CHECK(items.def_of(ItemRegistry::kEmptyId).mining_speed == doctest::Approx(1.0));
}
