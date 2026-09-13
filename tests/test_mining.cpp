#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "opencraft/game/mining.hpp"

using opencraft::game::can_harvest_by_hand;
using opencraft::game::MiningTickResult;
using opencraft::game::MiningTracker;
using opencraft::voxel::BlockRegistry;

namespace {

// Registry with extra synthetic blocks for instant-break coverage.
struct TestRegistry {
    BlockRegistry registry;

    TestRegistry() : registry(BlockRegistry::create_default()) {
        static_cast<void>(registry.register_block("torch_glow", {"Glow Wick", true, false, 0.0F}));
    }

    [[nodiscard]] std::uint16_t id(const char *name) const { return registry.id_of(name); }
};

// Drives `ticks` mining ticks against one fixed target and returns the
// results.
std::vector<MiningTickResult> mine_for(MiningTracker &tracker, std::uint16_t block_id, int ticks) {
    const glm::ivec3 target(1, 2, 3);
    std::vector<MiningTickResult> results;
    for (int i = 0; i < ticks; ++i) {
        results.push_back(tracker.tick(target, block_id, true, true));
    }
    return results;
}

int first_break_tick(const std::vector<MiningTickResult> &results) {
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (results[i].broke) {
            return static_cast<int>(i); // 0-based tick index of the breaking tick
        }
    }
    return -1;
}

} // namespace

TEST_CASE("bare-hand stone mining takes exactly 150 ticks (7.5 s) and is not harvestable") {
    TestRegistry fixtures;
    const auto stone = fixtures.id("stone");
    CHECK_FALSE(can_harvest_by_hand(fixtures.registry, stone));

    MiningTracker tracker(fixtures.registry);
    const auto results = mine_for(tracker, stone, 200);
    // damage = 1 / 1.5 / 100 per tick -> 150 ticks of accumulation.
    CHECK(first_break_tick(results) == 149);
}

TEST_CASE("bare-hand dirt mining takes exactly 15 ticks (0.75 s)") {
    TestRegistry fixtures;
    const auto dirt = fixtures.id("dirt");
    CHECK(can_harvest_by_hand(fixtures.registry, dirt));

    MiningTracker tracker(fixtures.registry);
    const auto results = mine_for(tracker, dirt, 50);
    // damage = 1 / 0.5 / 30 per tick -> 15 ticks.
    CHECK(first_break_tick(results) == 14);
}

TEST_CASE("unbreakable blocks never break and hardness-0 blocks break instantly") {
    TestRegistry fixtures;
    const auto bedrock = fixtures.id("bedrock");

    MiningTracker tracker(fixtures.registry);
    auto results = mine_for(tracker, bedrock, 100);
    CHECK(first_break_tick(results) == -1);
    for (const auto &r : results) {
        CHECK(r.progress == doctest::Approx(0.0F));
    }

    const auto torch = fixtures.id("torch_glow");
    results = mine_for(tracker, torch, 3);
    REQUIRE_FALSE(results.empty());
    CHECK(results[0].broke);
    CHECK(results[0].instant);
    CHECK(results[0].progress == doctest::Approx(0.0F));
}

TEST_CASE("continuous mining enforces the 6-tick between-blocks delay") {
    TestRegistry fixtures;
    const auto dirt = fixtures.id("dirt");

    MiningTracker tracker(fixtures.registry);
    // Hold the button on a dirt wall; the target keeps changing after every
    // break (fresh cell), so every break must be followed by 6 delay ticks.
    // Total rhythm for the second block: 6 delay ticks + 15 mining ticks.
    int ticks = 0;
    int breaks = 0;
    std::vector<int> break_gaps;
    int ticks_since_break = -1;
    glm::ivec3 target(0, 0, 0);
    while (breaks < 2 && ticks < 200) {
        const auto r = tracker.tick(target, dirt, true, true);
        if (r.broke) {
            ++breaks;
            if (ticks_since_break >= 0) {
                break_gaps.push_back(ticks_since_break);
            }
            ticks_since_break = 0;
            ++target.y; // next block in the wall after the break
        } else if (breaks > 0) {
            ++ticks_since_break;
        }
        ++ticks;
    }
    REQUIRE(breaks == 2);
    // Gap between break 1 and break 2 = 6 delay ticks + 14 accumulation
    // ticks (the break lands on the 15th accumulation tick).
    CHECK(break_gaps[0] == 6 + 14);
}

TEST_CASE("instant blocks skip the between-blocks delay when held") {
    TestRegistry fixtures;
    const auto torch = fixtures.id("torch_glow");

    MiningTracker tracker(fixtures.registry);
    // A wall of instant blocks: held button breaks one per tick, no 6-tick
    // rhythm anywhere.
    int breaks = 0;
    glm::ivec3 target(0, 0, 0);
    for (int i = 0; i < 8; ++i) {
        const auto r = tracker.tick(target, torch, true, true);
        if (r.broke) {
            ++breaks;
            ++target.y;
        }
    }
    CHECK(breaks == 8);
}

TEST_CASE("switching targets resets accumulated progress") {
    TestRegistry fixtures;
    const auto dirt = fixtures.id("dirt"); // 15 ticks to break

    MiningTracker tracker(fixtures.registry);
    // 10 ticks on block A (2/3 progress)...
    const glm::ivec3 a(0, 0, 0);
    for (int i = 0; i < 10; ++i) {
        static_cast<void>(tracker.tick(a, dirt, true, true));
    }
    CHECK(tracker.progress() == doctest::Approx(10.0 / 15.0));

    // ...then moving to block B starts from scratch.
    const glm::ivec3 b(0, 1, 0);
    auto r = tracker.tick(b, dirt, true, true);
    CHECK(tracker.progress() == doctest::Approx(1.0 / 15.0));
    CHECK_FALSE(r.broke);

    // Another 13 ticks on B breaks it: 1 + 13 = 14... 15th tick breaks.
    int extra = 0;
    while (!r.broke && extra < 40) {
        r = tracker.tick(b, dirt, true, true);
        ++extra;
    }
    CHECK(r.broke);
    CHECK(extra == 14);
}

TEST_CASE("releasing the button resets progress but the target may resume") {
    TestRegistry fixtures;
    const auto dirt = fixtures.id("dirt");

    MiningTracker tracker(fixtures.registry);
    const glm::ivec3 target(4, 5, 6);
    for (int i = 0; i < 10; ++i) {
        static_cast<void>(tracker.tick(target, dirt, true, true));
    }
    // Release for a tick: progress gone.
    static_cast<void>(tracker.tick(target, dirt, true, false));
    CHECK(tracker.progress() == doctest::Approx(0.0F));
    // Re-press: full 15 ticks needed again.
    int ticks = 0;
    bool broke = false;
    while (ticks < 40 && !broke) {
        broke = tracker.tick(target, dirt, true, true).broke;
        ++ticks;
    }
    CHECK(broke);
    CHECK(ticks == 15);
}

TEST_CASE("crack stage follows progress in ten steps") {
    TestRegistry fixtures;
    const auto dirt = fixtures.id("dirt");

    MiningTracker tracker(fixtures.registry);
    const glm::ivec3 target(0, 0, 0);
    for (int i = 0; i < 14; ++i) {
        const auto r = tracker.tick(target, dirt, true, true);
        CHECK(r.crack_stage == (i + 1) * 10 / 15);
        CHECK(r.progress == doctest::Approx(static_cast<float>(i + 1) / 15.0F));
    }
}

TEST_CASE("unbreakable check and tool requirement table match the launch set") {
    TestRegistry fixtures;
    for (const char *tool_required :
         {"stone", "cobblestone", "coal_ore", "copper_ore", "iron_ore", "gold_ore", "diamond_ore", "obsidian"}) {
        CHECK_FALSE(can_harvest_by_hand(fixtures.registry, fixtures.id(tool_required)));
    }
    for (const char *hand_ok :
         {"dirt", "grass_block", "sand", "gravel", "sandstone", "log", "leaves", "planks", "glass", "snow_block"}) {
        CHECK(can_harvest_by_hand(fixtures.registry, fixtures.id(hand_ok)));
    }
}
