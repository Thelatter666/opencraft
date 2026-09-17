// T-M2: the goal stack's SCHEDULING RULE, asserted directly.
//
// The card's first acceptance item is the one the source warns about hardest
// (research/11 §1.2.2): a goal stack must not be implemented as "run the first
// goal whose canUse is true", because then a zombie only ever retaliates - it
// never strolls and never looks around. The rule lives in one pure function
// (game::scan_goals), so that property is asserted HERE, on the function, rather
// than inferred from a mob's observed behaviour.

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mob_test_world.hpp"

#include "opencraft/game/mob_goal.hpp"

namespace {

namespace gam = opencraft::game;

using gam::GoalKind;
using gam::GoalScanResult;
using gam::GoalVerdicts;
using gam::scan_goals;

[[nodiscard]] std::size_t index_of(const GoalKind kind) {
    return static_cast<std::size_t>(kind);
}

// Verdicts where the named goals may start and continue.
[[nodiscard]] GoalVerdicts verdicts_for(const std::vector<GoalKind> &can_start, const std::vector<GoalKind> &can_keep) {
    GoalVerdicts verdicts;
    for (const GoalKind kind : can_start) {
        verdicts.can_use[index_of(kind)] = true;
    }
    for (const GoalKind kind : can_keep) {
        verdicts.continue_use[index_of(kind)] = true;
    }
    return verdicts;
}

} // namespace

TEST_CASE("mob goal stack: a low-priority goal runs ALONGSIDE a high-priority one") {
    // ★ Acceptance item 1. The scene: a high-priority goal (反击, priority 1) is
    // running and a low-priority one (环视, priority 8) can start. The failure
    // mode this test exists to catch is a scheduler that stops scanning once a
    // goal is running, or that only starts the highest-priority candidate.
    const std::vector<gam::GoalEntry> order = {
        {GoalKind::Revenge, 1}, {GoalKind::TargetPlayer, 2}, {GoalKind::RandomStroll, 7}, {GoalKind::LookAtPlayer, 8}};
    const gam::GoalMask running = gam::goal_bit(GoalKind::Revenge);
    const GoalVerdicts verdicts = verdicts_for({GoalKind::LookAtPlayer}, {GoalKind::Revenge, GoalKind::LookAtPlayer});

    const GoalScanResult scan = scan_goals(running, order, verdicts);
    CHECK(gam::goal_running(scan.running, GoalKind::Revenge));      // the high-priority one keeps running
    CHECK(gam::goal_running(scan.running, GoalKind::LookAtPlayer)); // ... and the low-priority one started
    CHECK(scan.started[index_of(GoalKind::LookAtPlayer)]);
    CHECK_FALSE(scan.stopped[index_of(GoalKind::Revenge)]);
}

TEST_CASE("mob goal stack: every goal that can start does start (not just the first)") {
    // The same rule from the other side: THREE goals can start, at three
    // priorities. A first-match-wins scheduler starts exactly one.
    const std::vector<gam::GoalEntry> order = {
        {GoalKind::MeleeAttack, 2}, {GoalKind::RandomStroll, 7}, {GoalKind::LookAtPlayer, 8}};
    const std::vector<GoalKind> all{GoalKind::MeleeAttack, GoalKind::RandomStroll, GoalKind::LookAtPlayer};
    const GoalScanResult scan = scan_goals(0, order, verdicts_for(all, all));
    CHECK(scan.started[index_of(GoalKind::MeleeAttack)]);
    CHECK(scan.started[index_of(GoalKind::RandomStroll)]);
    CHECK(scan.started[index_of(GoalKind::LookAtPlayer)]);
    CHECK(scan.running == (gam::goal_bit(GoalKind::MeleeAttack) | gam::goal_bit(GoalKind::RandomStroll) |
                           gam::goal_bit(GoalKind::LookAtPlayer)));
}

TEST_CASE("mob goal stack: the same priority can hold two goals, and both run") {
    // research/11 §1.2.2's own zombie table has two goals at priority 2 (主动选目标
    // and 攻击) and two at 8. That is the evidence the source uses to say mutual
    // exclusion is the goals' business, not the scheduler's - so the scheduler
    // must be able to start both.
    const std::vector<gam::GoalEntry> order = {{GoalKind::TargetPlayer, 2}, {GoalKind::MeleeAttack, 2}};
    const std::vector<GoalKind> both{GoalKind::TargetPlayer, GoalKind::MeleeAttack};
    const GoalScanResult scan = scan_goals(0, order, verdicts_for(both, both));
    CHECK(gam::goal_running(scan.running, GoalKind::TargetPlayer));
    CHECK(gam::goal_running(scan.running, GoalKind::MeleeAttack));
}

TEST_CASE("mob goal stack: a goal stops only on its own continueUse, never because a peer runs") {
    // A running low-priority goal is NOT stopped by a running high-priority one
    // (that would be the same mistake wearing a different hat).
    const std::vector<gam::GoalEntry> order = {{GoalKind::Revenge, 1}, {GoalKind::RandomStroll, 7}};
    const gam::GoalMask running = gam::goal_bit(GoalKind::Revenge) | gam::goal_bit(GoalKind::RandomStroll);
    const GoalScanResult scan =
        scan_goals(running, order, verdicts_for({}, {GoalKind::Revenge, GoalKind::RandomStroll}));
    CHECK(scan.running == running);
    CHECK_FALSE(scan.stopped[index_of(GoalKind::RandomStroll)]);

    // And when its own continueUse goes false, it stops - the one thing that
    // stops a goal.
    const GoalScanResult stopped = scan_goals(running, order, verdicts_for({}, {GoalKind::Revenge}));
    CHECK(gam::goal_running(stopped.running, GoalKind::Revenge));
    CHECK_FALSE(gam::goal_running(stopped.running, GoalKind::RandomStroll));
    CHECK(stopped.stopped[index_of(GoalKind::RandomStroll)]);
}

TEST_CASE("mob goal stack: stop then restart inside one scan") {
    // canUse and continueUse are INDEPENDENT verdicts. A goal whose condition
    // fails may stop and, if the same scan's canUse says it may start (a
    // different condition!), it restarts. The base game's tempt goal is exactly
    // this shape: it starts at 6 blocks and continues to 10.
    const std::vector<gam::GoalEntry> order = {{GoalKind::Tempt, 4}};
    const GoalScanResult scan = scan_goals(gam::goal_bit(GoalKind::Tempt), order, verdicts_for({GoalKind::Tempt}, {}));
    CHECK(scan.stopped[index_of(GoalKind::Tempt)]);
    CHECK(scan.started[index_of(GoalKind::Tempt)]);
    CHECK(gam::goal_running(scan.running, GoalKind::Tempt)); // the restart wins: it is running again
}

TEST_CASE("mob goal stack: priority orders the scan, and the order is total") {
    // Priority decides the ORDER callbacks are applied in (a higher-priority goal
    // writes shared state first, and a lower one reads that decision this tick),
    // never WHETHER a goal runs. The tiebreak matters because the source's own
    // tables hold ties.
    const std::vector<gam::GoalEntry> unsorted = {
        {GoalKind::LookAtPlayer, 8}, {GoalKind::MeleeAttack, 2}, {GoalKind::Revenge, 1}, {GoalKind::TargetPlayer, 2}};
    const std::vector<gam::GoalEntry> sorted = gam::sorted_goals(unsorted);
    REQUIRE(sorted.size() == 4);
    CHECK(sorted[0].kind == GoalKind::Revenge);
    CHECK(sorted[1].priority == 2);
    CHECK(sorted[2].priority == 2);
    CHECK(sorted[3].kind == GoalKind::LookAtPlayer);
    // The tie is broken by GoalKind, so the order does not depend on how the
    // table was written.
    CHECK(static_cast<int>(sorted[1].kind) < static_cast<int>(sorted[2].kind));
    // And sorting is idempotent - scanning the same table twice cannot reorder it.
    const std::vector<gam::GoalEntry> twice = gam::sorted_goals(sorted);
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        CHECK(twice[i].kind == sorted[i].kind);
    }
}

TEST_CASE("mob goal stack: a mob's table is content, and an absent goal is absent") {
    // goal_priority returns 0 for a goal the mob does not carry - which is how
    // "the passive roster has no TargetPlayer goal at all" is expressed.
    const std::vector<gam::GoalEntry> passive = {{GoalKind::Panic, 1}, {GoalKind::RandomStroll, 7}};
    CHECK(gam::goal_priority(passive, GoalKind::Panic) == 1);
    CHECK(gam::goal_priority(passive, GoalKind::RandomStroll) == 7);
    CHECK(gam::goal_priority(passive, GoalKind::TargetPlayer) == 0);
    CHECK(gam::goal_priority(passive, GoalKind::MeleeAttack) == 0);
}

TEST_CASE("mob goal stack: the launch roster's tables match research/11 §1.2.2") {
    // The zombie table the source prints: 反击 1 / 主动选目标 2 / 攻击 2 / 游荡 7 /
    // 环视 8. Asserting the shipping table against it is what keeps a later
    // content edit from quietly reordering the AI.
    mobtest::MobFixture fixture;
    const gam::MobDef *wretch = fixture.mobs.find_by_id("hollow_wretch");
    REQUIRE(wretch != nullptr);
    CHECK(gam::goal_priority(wretch->goals, GoalKind::Revenge) == 1);
    CHECK(gam::goal_priority(wretch->goals, GoalKind::TargetPlayer) == 2);
    CHECK(gam::goal_priority(wretch->goals, GoalKind::MeleeAttack) == 2);
    CHECK(gam::goal_priority(wretch->goals, GoalKind::RandomStroll) == 7);
    CHECK(gam::goal_priority(wretch->goals, GoalKind::LookAtPlayer) == 8);

    // The grazing mob has no offensive goal at all, and the exploder has the
    // fuse where the melee attack would be.
    const gam::MobDef *mossback = fixture.mobs.find_by_id("mossback");
    REQUIRE(mossback != nullptr);
    CHECK(gam::goal_priority(mossback->goals, GoalKind::TargetPlayer) == 0);
    CHECK(gam::goal_priority(mossback->goals, GoalKind::MeleeAttack) == 0);
    CHECK(gam::goal_priority(mossback->goals, GoalKind::Panic) == 1);

    const gam::MobDef *blastbud = fixture.mobs.find_by_id("blastbud");
    REQUIRE(blastbud != nullptr);
    CHECK(gam::goal_priority(blastbud->goals, GoalKind::Swell) == 3);
    CHECK(gam::goal_priority(blastbud->goals, GoalKind::MeleeAttack) == 0);
}

TEST_CASE("mob goal stack: the three distances and the classes are separate fields") {
    // ★ Acceptance item 2's DATA half (the behaviour half is in test_mob_ai.cpp):
    // the three distances exist as three independent fields with three different
    // shipping values, so "they cannot be conflated" is checkable without any
    // simulation at all.
    mobtest::MobFixture fixture;
    const gam::MobDef *mossback = fixture.mobs.find_by_id("mossback");
    const gam::MobDef *wretch = fixture.mobs.find_by_id("hollow_wretch");
    const gam::MobDef *blastbud = fixture.mobs.find_by_id("blastbud");
    REQUIRE(mossback != nullptr);
    REQUIRE(wretch != nullptr);
    REQUIRE(blastbud != nullptr);

    // ⚖ 16 is the "most mobs notice you" figure; the grazing mob's idle range.
    CHECK(mossback->sight_range == doctest::Approx(16.0));
    // ⚖ 35 is the zombie-line notice AND follow distance (research/11 §1.5.2).
    CHECK(wretch->sight_range == doctest::Approx(35.0));
    CHECK(wretch->follow_range == doctest::Approx(35.0));
    CHECK(blastbud->sight_range == doctest::Approx(16.0));

    // The tempt triple, all three present and distinct in ROLE: the attribute
    // range it senses, the range it starts following, the range it stops.
    CHECK(mossback->tempt_range == doctest::Approx(10.0));
    CHECK(mossback->tempt_start_range == doctest::Approx(6.0));
    CHECK(mossback->tempt_stop_range == doctest::Approx(10.0));
    CHECK(mossback->tempt_item == fixture.items.id_of("grain_loaf"));
    CHECK_FALSE(mossback->is_breedable() == false); // it IS breedable
    CHECK_FALSE(wretch->is_breedable());            // a hunter is not tempted by anything
    CHECK(wretch->tempt_range == doctest::Approx(0.0));

    // ⚖ docs/01 §6's damage ladder, from research/01 §10.2's zombie row. Note the
    // easy value is 2.5, NOT the 3 that docs/01 §6's generic ×1 ladder would give
    // - the source row wins (see the report's card-face finding).
    CHECK(wretch->damage_for(gam::Difficulty::Easy) == doctest::Approx(2.5));
    CHECK(wretch->damage_for(gam::Difficulty::Normal) == doctest::Approx(3.0));
    CHECK(wretch->damage_for(gam::Difficulty::Hard) == doctest::Approx(4.5));
    CHECK(wretch->damage_for(gam::Difficulty::Peaceful) == doctest::Approx(0.0));

    // ⚖ research/11 §1.5.5's fuse, all four numbers.
    CHECK(blastbud->fuse_trigger_range == doctest::Approx(3.0));
    CHECK(blastbud->fuse_cancel_range == doctest::Approx(7.0));
    CHECK(blastbud->fuse_ticks == 30);
    CHECK(blastbud->explosion_power == doctest::Approx(3.0));
    CHECK(blastbud->is_explosive());
    CHECK_FALSE(wretch->is_explosive());

    // ⚖ research/11 §4.1: mobs move on the walking-body numbers, NOT the drop's.
    const gam::EntityDef &drop = fixture.types.def_of(fixture.types.id_of("item"));
    CHECK(drop.entity_class == gam::EntityClass::Item);
    CHECK(wretch->physics.entity_class == gam::EntityClass::Mob);
    CHECK(wretch->physics.gravity == doctest::Approx(0.08));
    CHECK(wretch->physics.horizontal_drag == doctest::Approx(0.91));
    CHECK_FALSE(drop.gravity == doctest::Approx(wretch->physics.gravity));
    CHECK_FALSE(drop.horizontal_drag == doctest::Approx(wretch->physics.horizontal_drag));
}
