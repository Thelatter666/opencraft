// T-M2: the mob AI's behaviour, headless - the goal stack of game/mob_goal.hpp
// driven over the hand-built world of mob_test_world.hpp.
//
// This is where the card's acceptance items 2 (three distances), 3 (the three
// mobs are playable) and part of 5 (the shared collision) are asserted. The
// authority-side half (spawn ring, caps, requests) is test_mob_spawn.cpp and the
// on-machine half is docs/qa/T-M2-2026-09-18/.

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "mob_test_world.hpp"

#include "opencraft/game/mob_goal.hpp"

namespace {

namespace gam = opencraft::game;
namespace srv = opencraft::server;
namespace phy = opencraft::physics;

using mobtest::MobFixture;

// A custom mob whose three (five, counting the tempt pair) distances are chosen
// by the test. The card's acceptance item 2 asks for "三者取不同值时行为不同",
// and the shipping mobs cannot isolate every field - the grazing mob's sense
// range (10) is larger than its start range (6), so moving the sense range alone
// changes nothing. A purpose-built mob with the ranges swapped is what proves the
// fields are read independently rather than by accident.
struct TestMobSpec {
    const char *id = "test_grazer";
    double sight_range = 16.0;
    double follow_range = 20.0;
    double tempt_range = 8.0;
    double tempt_start_range = 6.0;
    double tempt_stop_range = 8.0;
    double move_speed = 0.2;
    bool breedable = true;
    bool hostile = false;
    // Whether the mob carries the offensive goals. The tempt tests set this
    // false: with a chase goal present the mob walks toward the player for its
    // own reasons, which would close the distance and blur what the tempt
    // thresholds are being asked to do.
    bool offensive = true;
};

[[nodiscard]] gam::MobDef make_test_mob(const MobFixture &fixture, const TestMobSpec &spec) {
    gam::MobDef def;
    def.physics.display_name = "Test Grazer";
    def.physics.half_width = 0.45;
    def.physics.height = 1.4;
    def.physics.gravity = 0.08;
    def.physics.vertical_drag = 0.98;
    def.physics.horizontal_drag = 0.91;
    def.physics.max_health = 10;
    def.physics.entity_class = gam::EntityClass::Mob;
    def.mob_class = spec.hostile ? gam::MobClass::Hostile : gam::MobClass::Passive;
    def.move_speed = spec.move_speed;
    def.sight_range = spec.sight_range;
    def.follow_range = spec.follow_range;
    def.tempt_range = spec.tempt_range;
    def.tempt_start_range = spec.tempt_start_range;
    def.tempt_stop_range = spec.tempt_stop_range;
    def.tempt_item = spec.breedable ? fixture.items.id_of("grain_loaf") : gam::ItemRegistry::kEmptyId;
    def.goals = {
        {gam::GoalKind::Revenge, 1}, {gam::GoalKind::TargetPlayer, 2}, {gam::GoalKind::MeleeAttack, 2},
        {gam::GoalKind::Tempt, 4},   {gam::GoalKind::RandomStroll, 7}, {gam::GoalKind::LookAtPlayer, 8},
    };
    if (!spec.offensive) {
        // Everything a mob does about a PLAYER walking past, removed - so the only
        // goal that can move it is the one under test.
        def.goals = {
            {gam::GoalKind::Tempt, 4},
            {gam::GoalKind::RandomStroll, 7},
            {gam::GoalKind::LookAtPlayer, 8},
        };
    }
    return def;
}

[[nodiscard]] double distance_to(const srv::EntityStore &store, const srv::EntityId id, const glm::dvec3 &point) {
    const srv::Entity *e = store.find(id);
    return e != nullptr ? glm::length(e->position - point) : -1.0;
}

} // namespace

// ── the speed interpretation ────────────────────────────────────────────────

TEST_CASE("mob motion: the movement-speed attribute IS the walking speed in blocks/tick") {
    // ⚠ This test exists because the sources do NOT state the conversion. The
    // claim under test is the one mob_sim.hpp documents: the acceleration is
    // derived so that the friction pipeline's own steady state equals the
    // attribute, and the cross-check is the player's measured 4.317 m/s.
    MobFixture fixture;
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    // 30 blocks away: inside the hunter's ⚖ 35-block notice range, far outside
    // melee reach, so it walks the whole time.
    const gam::ActorPose actor = MobFixture::actor_at(30.5, 0.5);
    (void) fixture.step(actor, gam::Difficulty::Normal, 40); // let the ramp settle

    const double before = fixture.get(wretch).position.x;
    (void) fixture.step(actor, gam::Difficulty::Normal, 20);
    const double travelled = (fixture.get(wretch).position.x - before) / 20.0; // blocks per tick

    // ⚖ research/01 §10.2 gives the hunter 0.23. The tolerance is 5% - it only
    // has to cover the last of the ramp, not a modelling choice.
    CHECK(travelled == doctest::Approx(0.23).epsilon(0.05));
    // And the cross-check the report cites: the player walks 0.21585 blocks/tick
    // (4.317 m/s), so this mob is faster than a walking player and slower than a
    // sprinting one (0.2806 blocks/tick = 5.612 m/s).
    CHECK(travelled > 4.317 / 20.0);
    CHECK(travelled < 5.612 / 20.0);
}

TEST_CASE("mob motion: a mob walks UP a full block instead of being stopped by it") {
    // ⚖ docs/01 §2: 生物 step height is 1.0 where the player's is 0.6 - a mob
    // walks onto a full block, the player has to jump. This is the engine's own
    // PhysicsConfig::for_entity(Mob) value, driven through the shared
    // physics::sweep_* primitives.
    MobFixture fixture;
    // Long enough that walking around its end is not an option within the tick
    // budget: the mob must STEP over it, which is the only way past.
    (void) fixture.fill(4, 4, 64, 64, -60, 60, 1);
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    const gam::ActorPose actor = MobFixture::actor_at(12.5, 0.5);

    // Watch the whole walk rather than only its end: once past the wall the mob is
    // back on the floor at y = 64, so "was it ever lifted" is the property, not
    // "is it lifted now".
    bool ever_lifted = false;
    for (int i = 0; i < 80; ++i) {
        (void) fixture.step(actor, gam::Difficulty::Normal, 1);
        if (fixture.get(wretch).position.y > 64.5) {
            ever_lifted = true;
        }
    }
    CHECK(ever_lifted);                          // it walked ON TOP of the block
    CHECK(fixture.get(wretch).position.x > 5.0); // and got past it
}

// ── ★ acceptance item 2: the three distances are separate ──────────────────

TEST_CASE("mob perception: the sight range is the NOTICE range") {
    // Shipping content does the isolating here: the hunter notices at 35 and the
    // exploder at 16 (research/11 §1.3.1: 多数生物 16 格, 僵尸系 35 格). At 30 blocks
    // one has a target and the other does not.
    MobFixture fixture;
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    const srv::EntityId blastbud = fixture.add_mob("blastbud", {0.5, 64.0, 30.5});
    const gam::ActorPose actor = MobFixture::actor_at(30.5, 0.5);

    (void) fixture.step(actor, gam::Difficulty::Normal, 40);
    CHECK(fixture.get(wretch).ai.target == srv::kActorId);        // noticed from 30
    CHECK(fixture.get(blastbud).ai.target == srv::Entity::kNoId); // 30 > its 16
}

TEST_CASE("mob perception: follow_range is the GIVE-UP distance, not the notice range") {
    // Two mobs that can both SEE the actor at 14 blocks (sight 16) differ only in
    // follow_range: the shipping hunter's 35 and the test mob's 20. Once the
    // actor steps out to 24 blocks, only the hunter keeps its target.
    MobFixture fixture;
    const std::uint16_t grazer =
        fixture.mobs.register_mob(fixture.types, "test_brief_attention", make_test_mob(fixture, TestMobSpec{}));
    (void) grazer;
    const srv::EntityId short_attention = fixture.add_mob("test_brief_attention", {0.5, 64.0, 0.5});
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 20.5});
    gam::ActorPose actor = MobFixture::actor_at(14.5, 0.5);

    (void) fixture.step(actor, gam::Difficulty::Normal, 30);
    REQUIRE(fixture.get(short_attention).ai.target == srv::kActorId);
    REQUIRE(fixture.get(wretch).ai.target == srv::kActorId);

    // Step back to 24 blocks: past the test mob's 20, well inside the hunter's 35.
    actor.feet.z = 24.5;
    (void) fixture.step(actor, gam::Difficulty::Normal, 30);
    CHECK(fixture.get(short_attention).ai.target == srv::Entity::kNoId); // gave up at 24 > 20
    CHECK(fixture.get(wretch).ai.target == srv::kActorId);               // still coming

    // And a jump the mob cannot close in 30 ticks (30 x 0.23 = 7 blocks) drops
    // the hunter too: 200 > 35.
    actor.feet.z = 200.5;
    (void) fixture.step(actor, gam::Difficulty::Normal, 30);
    CHECK(fixture.get(wretch).ai.target == srv::Entity::kNoId);
}

TEST_CASE("mob tempt: the SENSE range, the START range and the STOP range are three gates") {
    // A mob with the ranges deliberately un-nested: it senses the food within 4,
    // but would start following at 6 and stop at 8. A player 5 blocks away is
    // inside the START radius and OUTSIDE the SENSE radius, so the mob must not
    // follow - which is only true if the sense range is really consulted.
    MobFixture fixture;
    TestMobSpec spec;
    spec.id = "test_short_sense";
    spec.offensive = false;
    spec.tempt_range = 4.0;
    spec.tempt_start_range = 6.0;
    spec.tempt_stop_range = 8.0;
    fixture.mobs.register_mob(fixture.types, "test_short_sense", make_test_mob(fixture, spec));
    const srv::EntityId mob = fixture.add_mob("test_short_sense", {0.5, 64.0, 0.5});
    const std::uint16_t food = fixture.items.id_of("grain_loaf");

    // 5 blocks: within the 6-block start radius, outside the 4-block sense radius.
    gam::ActorPose actor = MobFixture::actor_at(0.5, 5.5, food);
    (void) fixture.step(actor, gam::Difficulty::Normal, 30);
    CHECK_FALSE(gam::goal_running(fixture.get(mob).ai.running_goals, gam::GoalKind::Tempt));
    const double far_start = distance_to(fixture.store, mob, actor.feet);

    // 3 blocks: now it senses the food and starts following, closing the gap.
    actor.feet.z = 3.5;
    (void) fixture.step(actor, gam::Difficulty::Normal, 40);
    CHECK(gam::goal_running(fixture.get(mob).ai.running_goals, gam::GoalKind::Tempt));
    const double near_start = distance_to(fixture.store, mob, actor.feet);
    CHECK(near_start < far_start);

    // Now the STOP range. The mob has closed to the ⚠ approach distance by now, so
    // the actor is placed by the DISTANCE that results (the mob stands ~1.5 short
    // of where the actor was), not by an absolute coordinate.
    const double mob_z = fixture.get(mob).position.z;
    actor.feet.z = mob_z + 5.0; // comfortably inside the 8-block stop range
    (void) fixture.step(actor, gam::Difficulty::Normal, 5);
    CHECK(gam::goal_running(fixture.get(mob).ai.running_goals, gam::GoalKind::Tempt));
    CHECK(distance_to(fixture.store, mob, actor.feet) <= 8.0);

    // ... and well beyond 8 it gives up, even though the player still holds the food.
    actor.feet.z = fixture.get(mob).position.z + 12.0;
    (void) fixture.step(actor, gam::Difficulty::Normal, 5);
    CHECK(distance_to(fixture.store, mob, actor.feet) > 8.0);
    CHECK_FALSE(gam::goal_running(fixture.get(mob).ai.running_goals, gam::GoalKind::Tempt));
}

TEST_CASE("mob tempt: the grazing mob senses at 10 but only starts following at 6") {
    // ⚖★ research/11 §1.5.6's口径提醒, on the shipping content: the attribute is
    // 10, the behaviour threshold is 6. At 9 blocks the mob holds still; at 5 it
    // walks over. Conflating the two numbers would make one of these wrong.
    MobFixture fixture;
    const srv::EntityId mossback = fixture.add_mob("mossback", {0.5, 64.0, 0.5});
    const std::uint16_t food = fixture.items.id_of("grain_loaf");

    gam::ActorPose actor = MobFixture::actor_at(0.5, 9.5, food);
    (void) fixture.step(actor, gam::Difficulty::Normal, 40);
    CHECK_FALSE(gam::goal_running(fixture.get(mossback).ai.running_goals, gam::GoalKind::Tempt));
    // It may shuffle around a little (the stroll goal), but it must NOT have
    // walked over: following would have closed to approach_distance (1.5).
    CHECK(distance_to(fixture.store, mossback, actor.feet) > 4.0);

    actor.feet.z = 5.5;
    (void) fixture.step(actor, gam::Difficulty::Normal, 40);
    CHECK(gam::goal_running(fixture.get(mossback).ai.running_goals, gam::GoalKind::Tempt));
    CHECK(distance_to(fixture.store, mossback, actor.feet) < 5.0); // it did
}

TEST_CASE("mob tempt: an empty hand is not a lure") {
    // The tempt goal exists exactly while the item is held; putting it away must
    // end the behaviour (research/11 §6.3: 跟随中止 when 玩家不再手持引诱物).
    MobFixture fixture;
    const srv::EntityId mossback = fixture.add_mob("mossback", {0.5, 64.0, 0.5});
    const std::uint16_t food = fixture.items.id_of("grain_loaf");
    const std::uint16_t wrong_item = fixture.items.id_of("greyrock");

    gam::ActorPose actor = MobFixture::actor_at(0.5, 4.5, food);
    (void) fixture.step(actor, gam::Difficulty::Normal, 20);
    REQUIRE(gam::goal_running(fixture.get(mossback).ai.running_goals, gam::GoalKind::Tempt));

    actor.held_item = wrong_item;
    (void) fixture.step(actor, gam::Difficulty::Normal, 3);
    CHECK_FALSE(gam::goal_running(fixture.get(mossback).ai.running_goals, gam::GoalKind::Tempt));

    // And it never started with an empty hand in the first place.
    MobFixture fresh;
    const srv::EntityId other = fresh.add_mob("mossback", {0.5, 64.0, 0.5});
    (void) fresh.step(MobFixture::actor_at(0.5, 3.5, gam::ItemRegistry::kEmptyId), gam::Difficulty::Normal, 40);
    CHECK_FALSE(gam::goal_running(fresh.get(other).ai.running_goals, gam::GoalKind::Tempt));
}

// ── ★ acceptance item 1's live shape: goals coexist on a real mob ──────────

TEST_CASE("mob goals coexist: a hunter chases AND looks, at the same time") {
    // The unit-level proof is test_mob_goal.cpp; this is the same property on a
    // real mob in a real (hand-built) world: while the chase runs, the look goal
    // runs too, and the look goal's output (the yaw) is written.
    MobFixture fixture;
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    const gam::ActorPose actor = MobFixture::actor_at(20.5, 0.5);
    (void) fixture.step(actor, gam::Difficulty::Normal, 40);

    const gam::GoalMask running = fixture.get(wretch).ai.running_goals;
    CHECK(gam::goal_running(running, gam::GoalKind::TargetPlayer));
    CHECK(gam::goal_running(running, gam::GoalKind::MeleeAttack));  // the same-priority pair
    CHECK(gam::goal_running(running, gam::GoalKind::LookAtPlayer)); // the low-priority one
    // ⚠ NOT RandomStroll: its can_use reads "do I already have a target", which is
    // the source's own way of expressing that a chase and a stroll do not fight
    // over where the mob walks (research/11 §1.2.2).
    CHECK_FALSE(gam::goal_running(running, gam::GoalKind::RandomStroll));

    // The look goal's observable output: the mob faces the player.
    const srv::Entity &mob = fixture.get(wretch);
    const glm::dvec3 facing = {-std::sin(mob.ai.yaw), 0.0, -std::cos(mob.ai.yaw)};
    const glm::dvec3 wanted =
        glm::normalize(glm::dvec3{actor.feet.x - mob.position.x, 0.0, actor.feet.z - mob.position.z});
    CHECK(glm::dot(facing, wanted) > 0.99);
}

TEST_CASE("mob goals: an idle mob strolls, and the destination is on the ground") {
    MobFixture fixture;
    const srv::EntityId mossback = fixture.add_mob("mossback", {0.5, 64.0, 0.5});
    (void) fixture.step(std::nullopt, gam::Difficulty::Normal, 60);
    CHECK(gam::goal_running(fixture.get(mossback).ai.running_goals, gam::GoalKind::RandomStroll));
    const glm::dvec3 target = fixture.get(mossback).ai.move_target;
    CHECK(target.y == doctest::Approx(64.0)); // the floor's top face, which is where the feet go
    CHECK(glm::length(target - fixture.get(mossback).position) <= 11.0); // ⚠ 10-block stroll radius + slack
}

TEST_CASE("mob goals: with no viewer nothing perceives, and the mob still lives") {
    // With no actor pose the authority has not been told where the player is, so
    // no targeting goal can start - which is also what keeps the other cards'
    // headless tests unaffected by this one.
    MobFixture fixture;
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    const int age_before = fixture.get(wretch).age;
    (void) fixture.step(std::nullopt, gam::Difficulty::Normal, 30);
    const gam::GoalMask running = fixture.get(wretch).ai.running_goals;
    CHECK_FALSE(gam::goal_running(running, gam::GoalKind::TargetPlayer));
    CHECK_FALSE(gam::goal_running(running, gam::GoalKind::MeleeAttack));
    CHECK_FALSE(gam::goal_running(running, gam::GoalKind::LookAtPlayer));
    CHECK(fixture.get(wretch).age > age_before);
}

TEST_CASE("mob chunks: an unloaded chunk freezes the brain exactly like the drops") {
    // research/11 §4.4's paused-timer rule, applied to mobs: nothing advances
    // while the chunk is out of memory.
    MobFixture fixture;
    const srv::EntityId mossback = fixture.add_mob("mossback", {0.5, 64.0, 0.5});
    fixture.world.set_loaded(0, 0, false);
    const int age = fixture.get(mossback).age;
    const glm::dvec3 position = fixture.get(mossback).position;
    (void) fixture.step(MobFixture::actor_at(3.5, 0.5), gam::Difficulty::Normal, 60);
    CHECK(fixture.get(mossback).age == age);
    CHECK(fixture.get(mossback).position == position);
    CHECK_FALSE(gam::goal_running(fixture.get(mossback).ai.running_goals, gam::GoalKind::Tempt));
}

// ── ★ acceptance item 3a: the passive mob is playable ──────────────────────

TEST_CASE("passive mob: feeding two in-love adults produces a baby after the mating timer") {
    // ⚖ research/11 §6.2: 双方互相寻路靠近（最远 8 格）→ 约 2.5 秒 → 产出幼体.
    MobFixture fixture;
    const srv::EntityId first = fixture.add_mob("mossback", {0.5, 64.0, 0.5});
    const srv::EntityId second = fixture.add_mob("mossback", {0.5, 64.0, 3.5});
    const std::uint16_t food = fixture.items.id_of("grain_loaf");

    CHECK(fixture.get(first).ai.baby == false);
    CHECK(srv::feed_mob(fixture.store, fixture.mobs, first, food) == gam::ActionReject::None);
    CHECK(srv::feed_mob(fixture.store, fixture.mobs, second, food) == gam::ActionReject::None);
    CHECK(fixture.get(first).ai.in_love);
    CHECK(fixture.get(first).ai.love_ticks == 600); // ⚖ 求偶超时 30 秒

    // The pair walks together and the ⚖ 2.5-second timer runs; a baby arrives.
    srv::MobStepResult result;
    for (int i = 0; i < 200 && result.births.empty(); ++i) {
        result = fixture.step(std::nullopt, gam::Difficulty::Normal, 1);
    }
    REQUIRE(result.births.size() == 1);
    CHECK(result.births[0].type == fixture.type_of("mossback"));
    // Both parents go on the ⚖ 5-minute cooldown and out of love mode. The
    // SPAWNER of the birth has not ticked its own timers yet this tick while the
    // other parent already has, so the pair reads 6000 / 5999 - the same rule
    // seen one tick apart, not a discrepancy.
    CHECK(fixture.get(first).ai.breed_cooldown >= 5999);
    CHECK(fixture.get(second).ai.breed_cooldown >= 5999);
    CHECK(fixture.get(first).ai.breed_cooldown <= 6000);
    CHECK(fixture.get(second).ai.breed_cooldown <= 6000);
    CHECK_FALSE(fixture.get(first).ai.in_love);
}

TEST_CASE("passive mob: food rules - wrong item, a baby, and the cooldown are all refused") {
    MobFixture fixture;
    const srv::EntityId adult = fixture.add_mob("mossback", {0.5, 64.0, 0.5});
    const std::uint16_t food = fixture.items.id_of("grain_loaf"); // the mossback's tempt item
    const std::uint16_t wrong = fixture.items.id_of("sunroot");   // also food - but not ITS food

    CHECK(srv::feed_mob(fixture.store, fixture.mobs, adult, wrong) == gam::ActionReject::WrongFood);
    CHECK(srv::feed_mob(fixture.store, fixture.mobs, adult, gam::ItemRegistry::kEmptyId) ==
          gam::ActionReject::NotBreedable);
    CHECK(srv::feed_mob(fixture.store, fixture.mobs, adult, food) == gam::ActionReject::None);
    CHECK(fixture.get(adult).ai.breed_cooldown == 0);

    // A baby cannot breed (research/11 §6.1: 幼体被杀不掉物品也不掉经验 - the adult
    // gate is the same rule seen from the breeding side).
    fixture.get(adult).ai.baby = true;
    fixture.get(adult).ai.in_love = false;
    CHECK(srv::feed_mob(fixture.store, fixture.mobs, adult, food) == gam::ActionReject::MobNotAdult);
    fixture.get(adult).ai.baby = false;

    // And a mob on cooldown is refused: set it directly, the way a previous
    // breeding would have left it.
    fixture.get(adult).ai.breed_cooldown = 6000;
    CHECK(srv::feed_mob(fixture.store, fixture.mobs, adult, food) == gam::ActionReject::BreedingCooldown);

    // A hostile mob takes no food at all.
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 8.5});
    CHECK(srv::feed_mob(fixture.store, fixture.mobs, wretch, food) == gam::ActionReject::NotBreedable);
}

TEST_CASE("passive mob: killing it drops the food and the hide, and the roll is reproducible") {
    // ⚖ research/11 §6.1: 生牛肉 1–3 + 皮革 0–2. This is the drop the card's
    // scope asks for ("掉落食物"), reached through the one damage channel.
    for (int trial = 0; trial < 8; ++trial) {
        MobFixture fixture;
        const srv::EntityId mossback = fixture.add_mob("mossback", {0.5, 64.0, 0.5}, 0xABCDEFULL + trial);
        const std::size_t before = fixture.store.alive_count();
        CHECK(srv::damage_mob(fixture.store, fixture.mobs, mossback, 10.0, srv::kActorId, fixture.rules));
        const std::size_t dropped =
            srv::kill_mob(fixture.store, fixture.types, srv::ItemRules{}, fixture.mobs, mossback);
        CHECK(fixture.store.find(mossback) == nullptr);
        CHECK(fixture.store.alive_count() == before - 1 + dropped);
        CHECK(dropped >= 1); // at least one cut of meat

        int meat = 0;
        int hides = 0;
        fixture.store.for_each_entity([&](const srv::Entity &entity) {
            if (entity.stack.item == fixture.items.id_of("raw_haunch")) {
                meat += entity.stack.count;
            } else if (entity.stack.item == fixture.items.id_of("sturdy_hide")) {
                hides += entity.stack.count;
            }
        });
        CHECK(meat >= 1);
        CHECK(meat <= 3);
        CHECK(hides >= 0);
        CHECK(hides <= 2);
    }

    // Same seed, same drops: the roll comes from the mob's own stream.
    auto drops_for = [](const std::uint64_t seed) {
        MobFixture fixture;
        const srv::EntityId id = fixture.add_mob("mossback", {0.5, 64.0, 0.5}, seed);
        (void) srv::damage_mob(fixture.store, fixture.mobs, id, 10.0, srv::kActorId, fixture.rules);
        (void) srv::kill_mob(fixture.store, fixture.types, srv::ItemRules{}, fixture.mobs, id);
        int total = 0;
        fixture.store.for_each_entity([&](const srv::Entity &entity) { total += entity.stack.count; });
        return total;
    };
    CHECK(drops_for(0x1234ULL) == drops_for(0x1234ULL));
}

TEST_CASE("passive mob: hurt it and it runs away for a few seconds") {
    // ⚖ research/11 §6.3: 受伤 → 随机方向逃跑数秒. ⚠ The speed multiplier is
    // deliberately 1.0 (the source gives none), so this asserts DIRECTION and
    // duration, not a speed boost.
    MobFixture fixture;
    const srv::EntityId mossback = fixture.add_mob("mossback", {0.5, 64.0, 0.5});
    const gam::ActorPose actor = MobFixture::actor_at(0.5, 3.5); // the "attacker" beside it
    (void) srv::damage_mob(fixture.store, fixture.mobs, mossback, 3.0, srv::kActorId, fixture.rules);
    CHECK(fixture.get(mossback).ai.panic_ticks == 60);
    CHECK(fixture.get(mossback).health == doctest::Approx(7.0));

    (void) fixture.step(actor, gam::Difficulty::Normal, 40);
    CHECK(gam::goal_running(fixture.get(mossback).ai.running_goals, gam::GoalKind::Panic));
    // ⚖ research/11 §1.5.7: 逃跑时移动到距畏惧源 4–15 格的位置.
    const double distance = distance_to(fixture.store, mossback, actor.feet);
    CHECK(distance >= 4.0);
    CHECK(distance <= 16.0);

    // The panic ends: after its few seconds the goal stops.
    (void) fixture.step(actor, gam::Difficulty::Normal, 40);
    CHECK_FALSE(gam::goal_running(fixture.get(mossback).ai.running_goals, gam::GoalKind::Panic));
}

// ── ★ acceptance item 3b: the melee hunter ─────────────────────────────────

TEST_CASE("hostile melee: it notices, closes, and lands a ⚖ 3-damage hit") {
    MobFixture fixture;
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    const gam::ActorPose actor = MobFixture::actor_at(0.5, 8.5);

    // Closing the distance: the chase is real, not a teleport.
    (void) fixture.step(actor, gam::Difficulty::Normal, 20);
    const double far = distance_to(fixture.store, wretch, actor.feet);
    (void) fixture.step(actor, gam::Difficulty::Normal, 20);
    const double near = distance_to(fixture.store, wretch, actor.feet);
    CHECK(near < far);

    // Now let it reach melee reach: an event must arrive, with the ⚖ normal
    // difficulty damage.
    srv::MobStepResult result;
    for (int i = 0; i < 60 && result.events.empty(); ++i) {
        result = fixture.step(actor, gam::Difficulty::Normal, 1);
    }
    REQUIRE_FALSE(result.events.empty());
    CHECK(result.events[0].kind == gam::ActorEventKind::MeleeHit);
    CHECK(result.events[0].amount == doctest::Approx(3.0));
    CHECK(result.events[0].source_type == fixture.type_of("hollow_wretch"));

    // ⚖ The difficulty ladder (research/01 §10.2's zombie row).
    MobFixture easy;
    const srv::EntityId easy_mob = easy.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    (void) easy_mob;
    srv::MobStepResult easy_result;
    const gam::ActorPose easy_actor = MobFixture::actor_at(0.5, 3.5);
    for (int i = 0; i < 60 && easy_result.events.empty(); ++i) {
        easy_result = easy.step(easy_actor, gam::Difficulty::Easy, 1);
    }
    REQUIRE_FALSE(easy_result.events.empty());
    CHECK(easy_result.events[0].amount == doctest::Approx(2.5));

    MobFixture hard;
    (void) hard.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    srv::MobStepResult hard_result;
    const gam::ActorPose hard_actor = MobFixture::actor_at(0.5, 3.5);
    for (int i = 0; i < 60 && hard_result.events.empty(); ++i) {
        hard_result = hard.step(hard_actor, gam::Difficulty::Hard, 1);
    }
    REQUIRE_FALSE(hard_result.events.empty());
    CHECK(hard_result.events[0].amount == doctest::Approx(4.5));

    // ⚖ 和平: a hostile mob does no damage at all (docs/01 §7).
    MobFixture peaceful;
    (void) peaceful.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    srv::MobStepResult peaceful_result;
    const gam::ActorPose peaceful_actor = MobFixture::actor_at(0.5, 3.5);
    for (int i = 0; i < 60 && peaceful_result.events.empty(); ++i) {
        peaceful_result = peaceful.step(peaceful_actor, gam::Difficulty::Peaceful, 1);
    }
    // No damage event is emitted at all when the amount would be 0.
    CHECK(peaceful_result.events.empty());
}

TEST_CASE("hostile melee: it needs line of sight, not just proximity") {
    // ⚖ research/11 §1.5.1: 近战必要条件 = 攻击盒相交 AND 清晰视线. The player stands
    // inside a block one step away: inside melee reach, with no line of sight - so
    // the mob closes and swings at nothing.
    MobFixture fixture;
    (void) fixture.fill(1, 1, 64, 65, -3, 3, 1); // a wall between the mob's column and the actor's
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    const gam::ActorPose actor = MobFixture::actor_at(1.6, 0.5); // inside that wall
    srv::MobStepResult result = fixture.step(actor, gam::Difficulty::Normal, 60);
    CHECK(result.events.empty());

    // And the mob is stopped BY the wall rather than walking into it: 1.0 - 0.3.
    CHECK(fixture.get(wretch).position.x <= 0.7001);
}

TEST_CASE("hostile melee: damage forms a grudge that outranks noticing") {
    // ⚖ research/11 §1.2.2: 反击 sits at priority 1, ABOVE 主动选目标 (2). A mob that
    // was hit goes after the hitter even when it cannot see them.
    MobFixture fixture;
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    // The actor stands inside a solid block, so no line of sight is possible.
    (void) fixture.fill(20, 20, 64, 65, -2, 2, 1);
    const gam::ActorPose actor = MobFixture::actor_at(20.5, 0.5);

    (void) fixture.step(actor, gam::Difficulty::Normal, 30);
    CHECK(fixture.get(wretch).ai.sees_actor == false);
    CHECK(fixture.get(wretch).ai.target == srv::Entity::kNoId); // it cannot see anyone

    (void) srv::damage_mob(fixture.store, fixture.mobs, wretch, 2.0, srv::kActorId, fixture.rules);
    (void) fixture.step(actor, gam::Difficulty::Normal, 5);
    CHECK(fixture.get(wretch).ai.revenge_on == srv::kActorId);
    CHECK(fixture.get(wretch).ai.target == srv::kActorId); // chasing anyway

    // The grudge expires (⚠ 600 ticks: the source names the goal, not its
    // duration), and the target goes with it.
    fixture.get(wretch).ai.revenge_ticks = 1;
    (void) fixture.step(actor, gam::Difficulty::Normal, 6);
    CHECK(fixture.get(wretch).ai.revenge_on == srv::Entity::kNoId);
    CHECK(fixture.get(wretch).ai.target == srv::Entity::kNoId);
}

TEST_CASE("hostile melee: the invulnerability window is 10 ticks and settles only the excess") {
    // ⚖ research/11 §1.5.1: 受击后的无敌帧 10 tick；期间伤害 ≤ 原伤害则免疫，更高的
    // 伤害只结算差值.
    MobFixture fixture;
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    CHECK(fixture.get(wretch).health == doctest::Approx(20.0));

    CHECK_FALSE(srv::damage_mob(fixture.store, fixture.mobs, wretch, 5.0, srv::kActorId, fixture.rules));
    CHECK(fixture.get(wretch).health == doctest::Approx(15.0));
    // Equal or smaller: ignored entirely.
    CHECK_FALSE(srv::damage_mob(fixture.store, fixture.mobs, wretch, 5.0, srv::kActorId, fixture.rules));
    CHECK(fixture.get(wretch).health == doctest::Approx(15.0));
    CHECK_FALSE(srv::damage_mob(fixture.store, fixture.mobs, wretch, 3.0, srv::kActorId, fixture.rules));
    CHECK(fixture.get(wretch).health == doctest::Approx(15.0));
    // Larger: only the difference lands.
    CHECK_FALSE(srv::damage_mob(fixture.store, fixture.mobs, wretch, 8.0, srv::kActorId, fixture.rules));
    CHECK(fixture.get(wretch).health == doctest::Approx(12.0));

    // Past the window, a normal hit lands in full again.
    (void) fixture.step(std::nullopt, gam::Difficulty::Normal, 11);
    CHECK_FALSE(srv::damage_mob(fixture.store, fixture.mobs, wretch, 5.0, srv::kActorId, fixture.rules));
    CHECK(fixture.get(wretch).health == doctest::Approx(7.0));

    // And enough damage kills it.
    CHECK(srv::damage_mob(fixture.store, fixture.mobs, wretch, 20.0, srv::kActorId, fixture.rules));
}

// ── ★ acceptance item 3c: the exploder ─────────────────────────────────────

TEST_CASE("exploder: 3 blocks and a sight line light a 30-tick fuse, then it detonates") {
    // ⚖ research/11 §1.5.5.
    MobFixture fixture;
    const srv::EntityId blastbud = fixture.add_mob("blastbud", {0.5, 64.0, 0.5});
    const gam::ActorPose actor = MobFixture::actor_at(0.5, 2.5); // 2 blocks: inside the ⚖ 3-block trigger

    (void) fixture.step(actor, gam::Difficulty::Normal, 25);
    CHECK(gam::goal_running(fixture.get(blastbud).ai.running_goals, gam::GoalKind::Swell));
    CHECK(fixture.get(blastbud).ai.fuse > 0);
    CHECK(fixture.get(blastbud).ai.fuse < 30);

    const int fuse_after_25 = fixture.get(blastbud).ai.fuse;
    srv::MobStepResult result;
    for (int i = 0; i < 40 && result.dead.empty(); ++i) {
        result = fixture.step(actor, gam::Difficulty::Normal, 1);
    }
    REQUIRE_FALSE(result.dead.empty());
    CHECK(result.dead[0] == blastbud);
    REQUIRE_FALSE(result.blasts.empty());
    CHECK(result.blasts[0].radius == doctest::Approx(6.0)); // ⚠ 2 x power
    REQUIRE_FALSE(result.events.empty());
    CHECK(result.events[0].kind == gam::ActorEventKind::Explosion);
    CHECK(result.events[0].amount > 0.0);
    CHECK(fuse_after_25 + 5 == 30); // it burned down at one tick per tick
}

TEST_CASE("exploder: stepping back to 7 blocks rolls the fuse back, and losing sight cancels it") {
    // ⚖ 保持视线时拉开 7 格即可取消；引信计时器会回退 (research/11 §1.5.5).
    MobFixture fixture;
    const srv::EntityId blastbud = fixture.add_mob("blastbud", {0.5, 64.0, 0.5});
    gam::ActorPose actor = MobFixture::actor_at(0.5, 2.5);

    (void) fixture.step(actor, gam::Difficulty::Normal, 15);
    const int lit = fixture.get(blastbud).ai.fuse;
    REQUIRE(lit > 5);

    // Out to 5 blocks: inside the ⚖ 7-block cancel ring, so the fuse RECEDES.
    actor.feet.z = 5.5;
    (void) fixture.step(actor, gam::Difficulty::Normal, 4);
    const int receded = fixture.get(blastbud).ai.fuse;
    CHECK(receded < lit);
    CHECK(receded > 0);
    CHECK(fixture.store.find(blastbud) != nullptr);

    // Back inside 3: it lights up again from where it got to.
    actor.feet.z = 2.5;
    (void) fixture.step(actor, gam::Difficulty::Normal, 3);
    CHECK(fixture.get(blastbud).ai.fuse > receded);
}

TEST_CASE("exploder: losing the sight line cancels the fuse outright") {
    // The player stays 2.5 blocks away - INSIDE the ⚖ 3-block trigger - and only
    // the wall between them changes, so this isolates the line of sight half of
    // the rule from the distance half.
    MobFixture fixture;
    (void) fixture.fill(-3, 3, 64, 65, 2, 2, 1); // a wall at z = 2, between mob (z 0.5) and actor (z 3.0)
    const srv::EntityId blastbud = fixture.add_mob("blastbud", {0.5, 64.0, 0.5});
    const gam::ActorPose actor = MobFixture::actor_at(0.5, 3.0); // 2.5 blocks: inside the trigger, behind the wall

    (void) fixture.step(actor, gam::Difficulty::Normal, 20);
    CHECK(fixture.get(blastbud).ai.fuse == 0);
    CHECK_FALSE(gam::goal_running(fixture.get(blastbud).ai.running_goals, gam::GoalKind::Swell));
    CHECK(fixture.store.find(blastbud) != nullptr); // nothing detonated

    // Same geometry without the wall: the fuse lights, which is what makes the
    // previous assertion about the SIGHT LINE rather than about the distance.
    MobFixture open;
    const srv::EntityId open_mob = open.add_mob("blastbud", {0.5, 64.0, 0.5});
    (void) open.step(MobFixture::actor_at(0.5, 3.0), gam::Difficulty::Normal, 10);
    CHECK(open.get(open_mob).ai.fuse > 0);
}

// ── the shared collision (acceptance item 5's runtime half) ────────────────

TEST_CASE("mob motion: a mob is stopped by a wall taller than one block") {
    // The step-assist is 1.0 (the engine's mob step height) and nothing more: a
    // two-block wall must stop a walking mob, exactly as docs/01 §2 says.
    MobFixture fixture;
    (void) fixture.fill(3, 3, 64, 65, -6, 6, 1); // two blocks tall
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    (void) fixture.step(MobFixture::actor_at(12.5, 0.5), gam::Difficulty::Normal, 80);
    const srv::Entity &mob = fixture.get(wretch);
    CHECK(mob.position.x < 3.0);  // never got past
    CHECK(mob.position.y < 66.0); // and never climbed it
}

TEST_CASE("mob motion: gravity brings an unsupported mob down to the floor") {
    MobFixture fixture;
    const srv::EntityId mossback = fixture.add_mob("mossback", {0.5, 70.0, 0.5});
    (void) fixture.step(std::nullopt, gam::Difficulty::Normal, 40);
    CHECK(fixture.get(mossback).position.y == doctest::Approx(64.0)); // the floor's top face
    CHECK(fixture.get(mossback).on_ground);
}

// ── T-D46: the knockback, the mob's half ────────────────────────────────────

// A pose 95 blocks away from the fixture's floor at the origin. A mob with NO
// actor known strolls on purpose - RandomStroll's verdict reads "nobody has told
// me where the player is" as near enough (research/11 §1.5.1 only gates the
// wander on a player WITHIN 32 blocks) - so a case that wants to measure motion
// it caused itself hands the fixture this pose instead: out of the 32-block
// stroll trigger, out of the 35-block sight range, and still a known player.
[[nodiscard]] gam::ActorPose away_actor() {
    return MobFixture::actor_at(0.5, 100.5);
}

TEST_CASE("hostile melee: the melee event names the ATTACKER, not where the hit landed") {
    // Contract ⑥ needs a direction: the client pushes the player away from
    // whoever hit them, and this event is the only channel it has (the player is
    // not an entity the authority can be asked about). The event therefore
    // carries the attacker's own feet - the same thing the Explosion event has
    // always carried (its blast centre) - and the client keeps the death drop
    // under the player by recording the corpse at its own position.
    MobFixture fixture;
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    const gam::ActorPose actor = MobFixture::actor_at(0.5, 2.0);

    srv::MobStepResult result;
    glm::dvec3 at_step_start{0.0, 0.0, 0.0};
    for (int i = 0; i < 60 && result.events.empty(); ++i) {
        at_step_start = fixture.get(wretch).position;
        result = fixture.step(actor, gam::Difficulty::Normal, 1);
    }
    REQUIRE_FALSE(result.events.empty());
    CHECK(result.events[0].kind == gam::ActorEventKind::MeleeHit);
    // Exactly the attacker's position at the moment of the swing (the goals run
    // before the motion, so the step may have moved it since - that is why the
    // position is sampled before the step).
    CHECK(result.events[0].position == at_step_start);
    // …and emphatically NOT the victim's, which is what it used to be.
    CHECK(result.events[0].position != actor.feet);
    CHECK(glm::length(result.events[0].position - actor.feet) > 0.5);
}

TEST_CASE("mob knockback: a mob shoved into a wall neither passes through nor keeps the speed") {
    // §5.2 8 (contract ⑥ / C-4). The impulse obeys the motion contract that is
    // already there: the sweep clamps the box to the wall's face and the
    // component it just lost is zeroed (mob_sim.hpp's step_mob_motion).
    MobFixture fixture;
    // A three-block wall at x = 10 (its east face is the plane x = 11), plus the
    // fixture's own floor with the feet at y = 64.
    fixture.fill(10, 10, 64, 66, 0, 20, 1);
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {11.5, 64.0, 5.5});
    const gam::EntityDef &def = fixture.types.def_of(fixture.type_of("hollow_wretch"));
    REQUIRE(def.half_width > 0.0);
    REQUIRE(11.5 - def.half_width > 11.0); // the mob starts clear of the wall

    // Shoved west, into the wall, by an attacker further east.
    srv::knock_back(fixture.get(wretch), {13.5, 64.0, 5.5}, 0.4);
    CHECK(fixture.get(wretch).velocity.x == doctest::Approx(-0.4));

    (void) fixture.step(away_actor(), gam::Difficulty::Normal, 1);
    // The sweep stopped the box flush against the face (x = 11) instead of
    // letting it through, and the component it lost is zeroed rather than kept.
    CHECK(fixture.get(wretch).position.x - def.half_width == doctest::Approx(11.0));
    CHECK(fixture.get(wretch).velocity.x == doctest::Approx(0.0));

    // And it stays there: nothing accumulates behind the wall.
    (void) fixture.step(away_actor(), gam::Difficulty::Normal, 20);
    const srv::Entity &mob = fixture.get(wretch);
    CHECK(mob.position.x - def.half_width >= 11.0 - 1e-9);
    CHECK(mob.position.x == doctest::Approx(11.0 + def.half_width));
    CHECK(mob.position.z == doctest::Approx(5.5)); // no sideways slip from the clamp
}

TEST_CASE("mob knockback: the ⚖ 0.4 impulse carries a standing mob about 0.88 blocks") {
    // The number §7.4 asks the report to give: the impulse is a SPEED, and what it
    // is worth in DISTANCE comes out of the motion contract that is already there
    // (damping 0.91 x 0.6 on ordinary ground, applied after each move). Nothing is
    // damaged here, so there is no grudge and no chase: the only forces are the
    // impulse and gravity.
    MobFixture fixture;
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    srv::knock_back(fixture.get(wretch), {0.5, 64.0, -1.5}, 0.4); // attacked from -Z, pushed +Z
    const glm::dvec3 start = fixture.get(wretch).position;

    (void) fixture.step(away_actor(), gam::Difficulty::Normal, 1);
    CHECK(fixture.get(wretch).position.z - start.z == doctest::Approx(0.4)); // the whole speed, first tick

    (void) fixture.step(away_actor(), gam::Difficulty::Normal, 60);
    const double pushed = fixture.get(wretch).position.z - start.z;
    INFO("pushed " << pushed << " blocks");
    // 0.4 / (1 - 0.546) = 0.881; the momentum threshold cuts the geometric tail,
    // which is the deficit at the low end of the window.
    CHECK(pushed > 0.85);
    CHECK(pushed < 0.89);
    CHECK(fixture.get(wretch).position.x == doctest::Approx(start.x)); // straight along the axis it was hit on
    CHECK(fixture.get(wretch).position.y == doctest::Approx(64.0));    // and it never left the floor
}

TEST_CASE("mob knockback: an unsupported shove is horizontal only and lands on the floor") {
    // C-4: no vertical component - a mob pushed off a ledge falls because of
    // gravity, not because the hit launched it.
    MobFixture fixture;
    const srv::EntityId mossback = fixture.add_mob("mossback", {0.5, 64.0, 0.5});
    srv::knock_back(fixture.get(mossback), {0.5, 64.0, -1.5}, 0.4); // attacked from -Z
    CHECK(fixture.get(mossback).velocity == glm::dvec3(0.0, 0.0, 0.4));
    (void) fixture.step(away_actor(), gam::Difficulty::Normal, 20);
    const srv::Entity &mob = fixture.get(mossback);
    CHECK(mob.position.y == doctest::Approx(64.0)); // never left the floor
    CHECK(mob.position.z > 0.5);                    // pushed the way it was hit
    CHECK(mob.position.x == doctest::Approx(0.5));
}
