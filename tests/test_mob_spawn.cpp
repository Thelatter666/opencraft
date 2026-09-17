// T-M2: the SPAWN RULES, headless - docs/01 §6 and research/01 §10.3 on top of
// the entity store, against the hand-built world of mob_test_world.hpp.
//
// The card's acceptance item 4 (刷怪: 光照 0 / 24-128 格环带 / >128 消除 / mob cap)
// is asserted here, together with the two rules that come with the roster rather
// than with the ring: passive mobs spawn once per chunk, and a peaceful world
// spawns no hostiles at all.

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mob_test_world.hpp"

#include "opencraft/game/mob_goal.hpp"

namespace {

namespace gam = opencraft::game;
namespace srv = opencraft::server;

using mobtest::MobFixture;

// A world for the spawner: a floor whose top face is y = 64, the whole area dark
// (MobTestWorld defaults to 15, so a spawn test asks for darkness explicitly -
// the light rule is the point of half of these cases).
MobFixture make_dark_spawn_world() {
    MobFixture fixture;
    fixture.world.light_everywhere(0);
    return fixture;
}

// How many of `ids` belong to the given class.
[[nodiscard]] std::size_t count_of_class(const MobFixture &fixture, const std::vector<srv::EntityId> &ids,
                                         const gam::MobClass mob_class) {
    std::size_t count = 0;
    for (const srv::EntityId id : ids) {
        const srv::Entity *entity = fixture.store.find(id);
        if (entity != nullptr) {
            const gam::MobDef *def = fixture.mobs.find(entity->type);
            if (def != nullptr && def->mob_class == mob_class) {
                ++count;
            }
        }
    }
    return count;
}

[[nodiscard]] std::size_t live_of_class(const MobFixture &fixture, const gam::MobClass mob_class) {
    return count_of_class(fixture, fixture.store.live_ids(), mob_class);
}

// One spawn pass on one chunk, with the caps divided over `loaded_chunks`.
[[nodiscard]] srv::MobSpawnResult run_pass(MobFixture &fixture, srv::PassiveChunkLedger &ledger, const int cx,
                                           const int cz, const glm::dvec3 &player, const int loaded_chunks = 289,
                                           const std::uint64_t tick = 1,
                                           const gam::Difficulty difficulty = gam::Difficulty::Normal) {
    return srv::spawn_in_chunk(fixture.store, fixture.world, fixture.mobs, fixture.spawn_rules, ledger, 0x1234ULL,
                               player, difficulty, cx, cz, tick, loaded_chunks);
}

} // namespace

TEST_CASE("spawn: 敌对生成光照等级 0 (docs/01 §6), and light 1 is enough to stop it") {
    // ⚠ research/01 §10.3 phrases the same rule as "光照 ≤ 随机 0–7"; docs/01 §6
    // says 0. The spec wins, and this test is what pins that reading: the whole
    // difference between the two cases below is ONE light level.
    const glm::dvec3 player{0.5, 64.0, 0.5};

    MobFixture dark = make_dark_spawn_world();
    srv::PassiveChunkLedger dark_ledger;
    const srv::MobSpawnResult lit_up = run_pass(dark, dark_ledger, 2, 0, player);
    CHECK(count_of_class(dark, lit_up.spawned, gam::MobClass::Hostile) > 0);

    MobFixture bright = make_dark_spawn_world();
    // Every cell at head height is at level 1 - dark enough to see nothing, bright
    // enough that nothing spawns.
    bright.world.light(-100, 100, 55, 75, -100, 100, 1);
    srv::PassiveChunkLedger bright_ledger;
    const srv::MobSpawnResult blocked = run_pass(bright, bright_ledger, 2, 0, player);
    CHECK(count_of_class(bright, blocked.spawned, gam::MobClass::Hostile) == 0);
}

TEST_CASE("spawn: the 24-128 block ring, measured from the player") {
    // ⚖ docs/01 §6: 玩家 24–128 格环带内可刷. Chunks are 16 blocks wide, which is
    // why the authority probes chunks at Chebyshev distance 2..8 and skips 0..1
    // (part of those is inside the 24-block exclusion zone).
    const glm::dvec3 player{0.5, 64.0, 0.5};
    CHECK_FALSE(srv::spawn_detail::in_ring({8.5, 64.0, 0.5}, player, 24.0, 128.0));
    CHECK(srv::spawn_detail::in_ring({40.5, 64.0, 0.5}, player, 24.0, 128.0));
    CHECK(srv::spawn_detail::in_ring({120.5, 64.0, 0.5}, player, 24.0, 128.0));
    CHECK_FALSE(srv::spawn_detail::in_ring({140.5, 64.0, 0.5}, player, 24.0, 128.0));

    MobFixture fixture = make_dark_spawn_world();
    srv::PassiveChunkLedger ledger;
    const srv::MobSpawnResult pass = run_pass(fixture, ledger, 2, 0, player);
    REQUIRE_FALSE(pass.spawned.empty());
    for (const srv::EntityId id : pass.spawned) {
        const srv::Entity *entity = fixture.store.find(id);
        REQUIRE(entity != nullptr);
        const double distance = glm::length(entity->position - player);
        CHECK(distance >= 24.0);
        CHECK(distance <= 128.0);
    }
}

TEST_CASE("spawn: >128 blocks is removed immediately (docs/01 §6)") {
    MobFixture fixture = make_dark_spawn_world();
    const srv::EntityId near_mob = fixture.add_mob("mossback", {0.5, 64.0, 40.5});
    const srv::EntityId at_130 = fixture.add_mob("mossback", {0.5, 64.0, 130.5});
    const srv::EntityId at_200 = fixture.add_mob("mossback", {0.5, 64.0, 200.5});

    const glm::dvec3 player{0.5, 64.0, 0.5};
    const std::size_t removed = srv::despawn_distant_mobs(fixture.store, fixture.mobs, fixture.spawn_rules, player);
    CHECK(removed == 2);
    CHECK(fixture.store.find(near_mob) != nullptr);
    CHECK(fixture.store.find(at_130) == nullptr);
    CHECK(fixture.store.find(at_200) == nullptr);
}

TEST_CASE("spawn: the mob cap is 70 x 可刷区块 / 289, and it is respected") {
    // ⚖ docs/01 §6. 289 is the 17x17 chunk window the figure is defined over, so a
    // smaller loaded set gets a proportionally smaller cap - which is what stops a
    // six-chunk world from holding seventy hostiles.
    const srv::MobSpawnRules rules;
    CHECK(rules.hostile_cap_per_window == doctest::Approx(70.0));
    CHECK(rules.cap_window_chunks == 289);

    MobFixture fixture = make_dark_spawn_world();
    srv::PassiveChunkLedger ledger;
    const glm::dvec3 player{0.5, 64.0, 0.5};
    const srv::MobSpawnResult small = run_pass(fixture, ledger, 2, 0, player, 42); // 70 x 42 / 289 = 10
    CHECK(small.hostile_cap == 10);
    CHECK_FALSE(small.hostile_cap_reached);
    CHECK(count_of_class(fixture, small.spawned, gam::MobClass::Hostile) <= static_cast<std::size_t>(rules.pack_size));

    // A world already at the cap must spawn nothing more.
    MobFixture full = make_dark_spawn_world();
    srv::PassiveChunkLedger full_ledger;
    for (int i = 0; i < 10; ++i) {
        full.add_mob("hollow_wretch", {40.5 + static_cast<double>(i), 64.0, 0.5});
    }
    const srv::MobSpawnResult capped = run_pass(full, full_ledger, 2, 0, player, 42);
    CHECK(capped.hostile_population == 10);
    CHECK(capped.hostile_cap_reached);
    // No HOSTILE spawns - the passive half has its own cap (10 x 42 / 289 = 1) and
    // is free to use it.
    CHECK(count_of_class(full, capped.spawned, gam::MobClass::Hostile) == 0);
    CHECK(live_of_class(full, gam::MobClass::Hostile) == 10);
}

TEST_CASE("spawn: hostiles arrive in packs of at most 4, chosen from the roster") {
    // ⚖ research/01 §10.3: 包中心随机、成员 ±5 格三角形分布偏移, 包大小一般 4.
    MobFixture fixture = make_dark_spawn_world();
    srv::PassiveChunkLedger ledger;
    // One pass fills BOTH halves of a chunk it has not populated yet: the hostile
    // pack and the once-per-chunk passive group. They are counted separately, since
    // pack_size caps each of them.
    const srv::MobSpawnResult pass = run_pass(fixture, ledger, 2, 0, {0.5, 64.0, 0.5}, 289);
    CHECK(fixture.spawn_rules.pack_size == 4);
    const std::size_t hostiles = count_of_class(fixture, pass.spawned, gam::MobClass::Hostile);
    const std::size_t passives = count_of_class(fixture, pass.spawned, gam::MobClass::Passive);
    CHECK(hostiles > 0);
    CHECK(hostiles <= 4);
    CHECK(passives <= 4);

    // Every member stands on the floor, and every member of a given class is a
    // registered mob of that class.
    for (const srv::EntityId id : pass.spawned) {
        const srv::Entity *entity = fixture.store.find(id);
        REQUIRE(entity != nullptr);
        REQUIRE(fixture.mobs.find(entity->type) != nullptr);
        CHECK(entity->position.y == doctest::Approx(64.0));
    }
}

TEST_CASE("spawn: 被动生物按区块一次性生成 (docs/01 §6)") {
    // The ledger is what makes "once per chunk" true. A second pass over the same
    // chunk adds nothing; a different chunk still gets its own population.
    MobFixture fixture = make_dark_spawn_world();
    // Bright everywhere: hostiles are impossible, so what is left is exactly the
    // per-chunk passive path.
    fixture.world.light_everywhere(15);
    srv::PassiveChunkLedger ledger;
    const glm::dvec3 player{0.5, 64.0, 0.5};

    const srv::MobSpawnResult first = run_pass(fixture, ledger, 2, 0, player);
    const std::size_t after_first = live_of_class(fixture, gam::MobClass::Passive);
    CHECK(first.spawned.size() > 0);
    CHECK(after_first == first.spawned.size());
    CHECK(ledger.seen(2, 0));

    const srv::MobSpawnResult second = run_pass(fixture, ledger, 2, 0, player, 289, 2);
    CHECK(second.spawned.empty());
    CHECK(live_of_class(fixture, gam::MobClass::Passive) == after_first);

    const srv::MobSpawnResult other_chunk = run_pass(fixture, ledger, -2, 0, player, 289, 2);
    CHECK_FALSE(other_chunk.spawned.empty());
    CHECK(ledger.seen(-2, 0));
}

TEST_CASE("spawn: a peaceful world has no hostiles at all (docs/01 §7)") {
    MobFixture fixture = make_dark_spawn_world();
    srv::PassiveChunkLedger ledger;
    const srv::MobSpawnResult result =
        run_pass(fixture, ledger, 2, 0, {0.5, 64.0, 0.5}, 289, 1, gam::Difficulty::Peaceful);
    CHECK(count_of_class(fixture, result.spawned, gam::MobClass::Hostile) == 0);
    CHECK(live_of_class(fixture, gam::MobClass::Hostile) == 0);
    // Peaceful removes the hostile roster, not the spawner: the passive half still
    // ran on this chunk.
    CHECK(result.spawned.size() > 0);
    CHECK(ledger.seen(2, 0));
}

TEST_CASE("spawn: nothing spawns in a chunk that is not in memory") {
    MobFixture fixture = make_dark_spawn_world();
    fixture.world.set_loaded(2, 0, false);
    srv::PassiveChunkLedger ledger;
    const srv::MobSpawnResult pass = run_pass(fixture, ledger, 2, 0, {0.5, 64.0, 0.5});
    CHECK(pass.spawned.empty());
    CHECK_FALSE(ledger.seen(2, 0)); // and the chunk is not marked as populated either
}

TEST_CASE("spawn: a position needs a floor, headroom, and no liquid") {
    // The geometry rule the probe and the per-member re-check share. The pack
    // offsets members from the probed centre, so this IS re-checked per member -
    // and this test is the reason it is.
    MobFixture fixture = make_dark_spawn_world();
    const gam::MobDef &wretch = *fixture.mobs.find_by_id("hollow_wretch");

    // Good: floor below, two air cells, dry.
    CHECK(srv::spawn_detail::position_allows(fixture.world, wretch, {4.5, 64.0, 4.5}));
    // Head height occupied.
    fixture.fill(8, 8, 64, 65, 8, 8, 1);
    CHECK_FALSE(srv::spawn_detail::position_allows(fixture.world, wretch, {8.5, 64.0, 8.5}));
    // No floor below.
    CHECK_FALSE(srv::spawn_detail::position_allows(fixture.world, wretch, {0.5, 200.0, 0.5}));
    // A hostile mob also refuses a cell that is not pitch dark, a passive one does
    // not care about light at all.
    MobFixture lit = make_dark_spawn_world();
    lit.world.light(-100, 100, 55, 75, -100, 100, 4);
    const gam::MobDef &mossback = *lit.mobs.find_by_id("mossback");
    CHECK_FALSE(srv::spawn_detail::position_allows(lit.world, wretch, {4.5, 64.0, 4.5}));
    CHECK(srv::spawn_detail::position_allows(lit.world, mossback, {4.5, 64.0, 4.5}));
}

TEST_CASE("spawn: the roster is read from the registry, by class") {
    // The spawner holds no per-mob knowledge: it asks the registry for the class it
    // is filling. This is what makes "add a mob = add a table row" true for
    // spawning as well as for the AI.
    MobFixture fixture;
    const std::vector<std::uint16_t> hostiles = fixture.mobs.entity_types(gam::MobClass::Hostile);
    const std::vector<std::uint16_t> passives = fixture.mobs.entity_types(gam::MobClass::Passive);
    REQUIRE(hostiles.size() == 2);
    REQUIRE(passives.size() == 1);
    CHECK(fixture.mobs.find(hostiles[0])->mob_class == gam::MobClass::Hostile);
    CHECK(fixture.mobs.find(passives[0])->mob_class == gam::MobClass::Passive);
    CHECK(std::string(fixture.types.string_of(passives[0])) == "mossback");
    CHECK(hostiles[0] < hostiles[1]); // ascending: the spawner's pick is reproducible
}
