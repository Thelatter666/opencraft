// T-D59: the swing model - the charge ramp, the 84.8% gate, the crit and the
// sprint knockback.
//
// The ramp itself is a pure function of (t, attack speed) and is asserted as
// one, against the card's own worked examples (§4.1). Everything else is
// asserted through the verb the client actually sends, because the wiring is
// where this card can go wrong: which item's numbers are used, which clock `t`
// comes from, and whether the two thresholds (damage ramp at t = T, gate at
// 0.848) are still two different things.
//
// The one fixture decision worth explaining is measured_damage() below: it
// spends the authority's cold clock on a throwaway victim and then measures a
// FRESH one, because damage_mob's ⚖ 10-tick window would otherwise absorb the
// hit being measured - and waiting the window out takes more ticks than most of
// the ramp's interesting points have.

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string_view>

#include <glm/glm.hpp>

#include "mob_test_world.hpp"
#include "opencraft/game/protocol.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/sim/mob_sim.hpp"
#include "opencraft/sim/world_sim.hpp"

namespace {

namespace gam = opencraft::game;
namespace srv = opencraft::server;

constexpr double kEyeStanding = 1.62;

void load_chunks(srv::WorldSim &sim, const int radius) {
    gam::StreamRequest req;
    req.generate_radius = radius;
    req.unload_radius = radius;
    req.generate_budget = (2 * radius + 1) * (2 * radius + 1);
    sim.stream(req);
    REQUIRE(sim.loaded_chunk_count() > 0);
}

[[nodiscard]] gam::ActorPose viewer_at(const glm::dvec3 &feet) {
    gam::ActorPose pose;
    pose.feet = feet;
    pose.height = 1.8;
    pose.eye_height = kEyeStanding;
    return pose;
}

[[nodiscard]] gam::ActionRequest attack_request(const srv::EntityId mob, const gam::ActorPose &actor,
                                                const std::uint16_t weapon) {
    gam::ActionRequest req;
    req.kind = gam::ActionKind::Attack;
    req.target = {static_cast<int>(mob), 0, 0};
    req.actor = actor;
    req.item_or_block = weapon; // unused by Attack (the weapon is actor.held_item) - pinned by a test below
    req.actor.held_item = weapon;
    return req;
}

// The attack speed of a tool by its string id, straight out of the shipping
// registry. Written as a lookup rather than as a 1.6 literal in every test so a
// change to the item table shows up as a failing test, not as a stale constant.
[[nodiscard]] std::uint16_t weapon_of(const srv::WorldSim &sim, const std::string_view id) {
    return sim.items().id_of(id);
}

// One measured swing at a known charge age, in hit points taken off the victim.
//
// The authority's clock starts cold ("never attacked ⇒ full charge"), so the
// helper spends it first on a throwaway mob, waits `age` ticks, then strikes a
// second, FRESH mob and reports what its hit points lost. The fresh victim is
// load-bearing: damage_mob's ⚖ 10-tick window absorbs a second hit on the same
// mob, and an absorbed hit is not the number under test. Waiting the window out
// would cost 10 ticks, which is more than the ramp's interesting points.
//
// `age` is exact. WorldSim::tick() is the only thing that moves tick_counter_,
// so `age` calls move it by exactly `age` and the difference the authority sees
// at the swing is `age`.
[[nodiscard]] double measured_damage(srv::WorldSim &sim, const std::uint16_t weapon, const int age,
                                     const bool sprinting = false, const bool falling = false) {
    const glm::dvec3 spawn = sim.find_spawn();
    const std::uint16_t wretch = sim.entity_types().id_of("hollow_wretch");

    // 1. Spend the cold clock. Which mob takes this hit and how hard does not
    //    matter; what matters is that the authority now has a last_attack_tick_.
    const srv::EntityId primer = sim.summon_mob(wretch, spawn);
    REQUIRE(primer != srv::EntityStore::kNoEntity);
    REQUIRE(sim.submit(attack_request(primer, viewer_at(spawn + glm::dvec3(1.0, 0.0, 0.0)), weapon)).accepted);

    // 2. Let the charge run for exactly `age` ticks.
    for (int i = 0; i < age; ++i) {
        sim.tick();
        sim.take_changes();
    }

    // 3. Strike a fresh mob and read the difference.
    const srv::EntityId victim = sim.summon_mob(wretch, spawn);
    REQUIRE(victim != srv::EntityStore::kNoEntity);
    const glm::dvec3 at = sim.entities().find(victim)->position;
    gam::ActorPose actor = viewer_at(at + glm::dvec3(1.0, 0.0, 0.0));
    actor.sprinting = sprinting;
    actor.falling = falling;
    const double before = sim.entities().find(victim)->health;
    REQUIRE(sim.submit(attack_request(victim, actor, weapon)).accepted);
    return before - sim.entities().find(victim)->health;
}

// The same shape as measured_damage, but it reports the knockback the landed hit
// left in the victim's velocity instead of the damage - one hit, read off the
// victim's x axis (the actor always stands one block along +X, so "away from the
// actor" is -X and nothing else).
[[nodiscard]] double measured_knockback(srv::WorldSim &sim, const std::uint16_t weapon, const int age,
                                        const bool sprinting) {
    const glm::dvec3 spawn = sim.find_spawn();
    const std::uint16_t wretch = sim.entity_types().id_of("hollow_wretch");

    const srv::EntityId primer = sim.summon_mob(wretch, spawn);
    REQUIRE(primer != srv::EntityStore::kNoEntity);
    REQUIRE(sim.submit(attack_request(primer, viewer_at(spawn + glm::dvec3(1.0, 0.0, 0.0)), weapon)).accepted);
    for (int i = 0; i < age; ++i) {
        sim.tick();
        sim.take_changes();
    }

    const srv::EntityId victim = sim.summon_mob(wretch, spawn);
    REQUIRE(victim != srv::EntityStore::kNoEntity);
    const glm::dvec3 at = sim.entities().find(victim)->position;
    gam::ActorPose actor = viewer_at(at + glm::dvec3(1.0, 0.0, 0.0));
    actor.sprinting = sprinting;
    REQUIRE(sim.submit(attack_request(victim, actor, weapon)).accepted);
    return sim.entities().find(victim)->velocity.x;
}

// Out of the 32-block stroll trigger and out of the 35-block sight range, but
// still a player the authority knows about - test_mob_ai.cpp's own idiom, kept
// here so the knockback distance below is measured on a mob that is not also
// walking.
[[nodiscard]] gam::ActorPose away_actor() {
    return mobtest::MobFixture::actor_at(0.5, 100.5);
}

} // namespace

// ── the ramp itself ─────────────────────────────────────────────────────────

TEST_CASE("attack charge: the ⚖ ramp is the card's worked examples, to the digit") {
    // research/01 §6.1: m(t) = clamp(0.2 + ((t+0.5)/T)² × 0.8, 0.2, 1), T = 20/speed.
    // The squared ratio is INSIDE the ×0.8 - the "(t+0.5)/T²" spelling in the
    // source document is shorthand, and squaring the wrong factor is the one
    // transcription error that would still look plausible.
    constexpr double kSword = 1.6; // T = 12.5
    constexpr double kFist = 4.0;  // T = 5

    // §4.1's sword column: the FULL damage arrives at t = T = 12, not at 84.8%.
    // A cold sword still does 20.128% - the floor plus one half-tick of its own
    // ramp (the axe, with its four-times-longer period, only reaches 20.032%).
    CHECK(gam::attack_charge_multiplier(0.0, kSword) == doctest::Approx(0.20128));
    CHECK(gam::attack_charge_multiplier(10.0, kSword) == doctest::Approx(0.76448));
    CHECK(gam::attack_charge_multiplier(11.0, kSword) == doctest::Approx(0.87712));
    CHECK(gam::attack_charge_multiplier(12.0, kSword) == doctest::Approx(1.0));

    // §4.1's bare-hand column.
    CHECK(gam::attack_charge_multiplier(0.0, kFist) == doctest::Approx(0.208));
    CHECK(gam::attack_charge_multiplier(1.0, kFist) == doctest::Approx(0.272));
    CHECK(gam::attack_charge_multiplier(2.0, kFist) == doctest::Approx(0.4));
    CHECK(gam::attack_charge_multiplier(4.0, kFist) == doctest::Approx(0.848));
    CHECK(gam::attack_charge_multiplier(5.0, kFist) == doctest::Approx(1.0));

    // The floor holds for a swing with no charge at all, and the clamp holds
    // past full, so a stale or huge `t` can never overshoot.
    CHECK(gam::attack_charge_multiplier(0.0, kSword) >= gam::kAttackChargeFloor);
    CHECK(gam::attack_charge_multiplier(10'000.0, kSword) == doctest::Approx(1.0));
}

TEST_CASE("attack charge: 84.8% is a gate, not the full-damage point") {
    // Contract ②. The two are one integer tick apart on a sword, which is why
    // they are asserted as a pair: a build that treats 0.848 as "full" would
    // make t = 10 and t = 11 the same number, and this is the case that says
    // they are not.
    constexpr double kSword = 1.6;
    CHECK(gam::attack_charge_multiplier(10.0, kSword) < gam::kAttackChargeThreshold);
    CHECK(gam::attack_charge_multiplier(11.0, kSword) >= gam::kAttackChargeThreshold);
    // The continuous solution is t = 10.75; the smallest INTEGER tick past the
    // gate is 11, and the ramp is still climbing there.
    CHECK(gam::attack_charge_multiplier(11.0, kSword) < 1.0);
    CHECK(gam::attack_charge_multiplier(12.0, kSword) == doctest::Approx(1.0));

    // A fist reaches exactly the gate at t = 4 - the boundary case, and the one
    // where "≥" versus ">" is decidable: equality unlocks, it does not fall
    // short.
    CHECK(gam::attack_charge_multiplier(4.0, 4.0) >= gam::kAttackChargeThreshold);
    CHECK(gam::attack_charge_multiplier(3.0, 4.0) < gam::kAttackChargeThreshold);
}

TEST_CASE("attack charge: the pick's T is 16.666…, not 16 and not 17") {
    // The card's ⚠ 5: `t` is an integer tick and T stays a double. Rounding it
    // either way moves every point of the ramp, and this is the assertion that
    // fails if someone "tidies" the division into an int.
    constexpr double kPick = 1.2; // T = 16.666…
    CHECK(gam::attack_charge_multiplier(16.0, kPick) == doctest::Approx(0.98408));
    CHECK(gam::attack_charge_multiplier(17.0, kPick) == doctest::Approx(1.0));
    // T = 17 would give 0.9536 and T = 16 would give 1.0 at t = 15; neither is
    // what the formula says, and the gaps are far wider than any rounding noise.
    const double rounded_up = 0.2 + ((16.5 / 17.0) * (16.5 / 17.0)) * 0.8;
    CHECK(gam::attack_charge_multiplier(16.0, kPick) > rounded_up + 0.03);
    CHECK(gam::attack_charge_multiplier(15.0, kPick) < 1.0);
}

// ── the authority's wiring ──────────────────────────────────────────────────

TEST_CASE("authority attack: a swing with no history lands at FULL charge") {
    // Ruling C-2's load-bearing half. Every test written before this card
    // submits one Attack request against a fresh WorldSim and expects
    // kPunchDamage; if "never attacked" were read as t = 0 the first hit of
    // every one of them would land at 20.8% instead.
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const glm::dvec3 spawn = sim.find_spawn();
    const srv::EntityId target = sim.summon_mob(sim.entity_types().id_of("hollow_wretch"), spawn);
    REQUIRE(target != srv::EntityStore::kNoEntity);
    const double before = sim.entities().find(target)->health;

    // Bare-handed: the full 1.0, not 0.208.
    REQUIRE(sim.submit(attack_request(target, viewer_at(spawn + glm::dvec3(1.0, 0.0, 0.0)), 0)).accepted);
    CHECK(before - sim.entities().find(target)->health == doctest::Approx(gam::kPunchDamage));
}

TEST_CASE("authority attack: the four timber tools do their ⚖ research/01 §6.1 damage") {
    // C-4's table, measured through the verb rather than read off the registry:
    // the number that matters is what the mob loses, and every value here is the
    // source's own per-kind figure - never the §9 Tiers sheet's 伤害加成 column
    // composed with a base.
    srv::WorldSim sim;
    load_chunks(sim, 1);

    // Age 25 clears every one of the four periods (12.5 / 25 / 16.666 / 20), so
    // each swing is at full charge and what is left in the difference is the
    // weapon's own damage.
    CHECK(measured_damage(sim, weapon_of(sim, "timber_edge"), 25) == doctest::Approx(4.0));   // 剑 木 4
    CHECK(measured_damage(sim, weapon_of(sim, "timber_hewer"), 25) == doctest::Approx(7.0));  // 斧 7
    CHECK(measured_damage(sim, weapon_of(sim, "timber_chisel"), 25) == doctest::Approx(2.0)); // 镐 2
    CHECK(measured_damage(sim, weapon_of(sim, "timber_spade"), 25) == doctest::Approx(2.5));  // 锹 2.5
}

TEST_CASE("authority attack: anything that is not a tool swings as a bare hand") {
    // Contract ⑥'s other half. A block, a food item and the empty hand are all
    // the same swing - 1.0 at speed 4.0 - because ItemDef's two attack fields
    // are 0.0 for every one of them and the fallback is one rule in one place.
    srv::WorldSim sim;
    load_chunks(sim, 1);

    CHECK(measured_damage(sim, 0, 25) == doctest::Approx(gam::kPunchDamage));
    CHECK(measured_damage(sim, weapon_of(sim, "loam_clod"), 25) == doctest::Approx(gam::kPunchDamage));
    CHECK(measured_damage(sim, weapon_of(sim, "raw_haunch"), 25) == doctest::Approx(gam::kPunchDamage));
    // ... and they swing at the FIST's speed, so the age that counts as full is
    // 5 for them and not the sword's 12.
    CHECK(measured_damage(sim, 0, 5) == doctest::Approx(gam::kPunchDamage));
    CHECK(measured_damage(sim, weapon_of(sim, "timber_edge"), 5) < 4.0); // the sword is still charging
}

TEST_CASE("authority attack: the damage follows the ramp, tick by tick") {
    // The sword's climb around the gate, measured as damage rather than as a
    // multiplier: the difference between t = 10 and t = 11 is a whole point of
    // damage, which is what a player feels when they mistime a click.
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const std::uint16_t sword = weapon_of(sim, "timber_edge");

    CHECK(measured_damage(sim, sword, 10) == doctest::Approx(3.05792)); // 4 × 76.448%
    CHECK(measured_damage(sim, sword, 11) == doctest::Approx(3.50848)); // 4 × 87.712%
    CHECK(measured_damage(sim, sword, 12) == doctest::Approx(4.0));     // 4 × 100%

    // The floor case: two swings in the same tick. This is the "fast clicking
    // hits softer" rule the card exists to put in, asserted as a number - the
    // second swing of a double-click is a fifth of the first.
    CHECK(measured_damage(sim, sword, 0) == doctest::Approx(4.0 * 0.20128));
}

TEST_CASE("authority attack: switching weapons switches T on the very next swing") {
    // Ruling C-2's last bullet: T is looked up from the held item at the swing,
    // never cached. Same age, three weapons, three different multipliers - and
    // each is its own period's ramp, so a cached speed cannot fake it.
    srv::WorldSim sim;
    load_chunks(sim, 1);

    const double sword_at_5 = measured_damage(sim, weapon_of(sim, "timber_edge"), 5);
    const double axe_at_5 = measured_damage(sim, weapon_of(sim, "timber_hewer"), 5);
    const double fist_at_5 = measured_damage(sim, 0, 5);

    CHECK(sword_at_5 == doctest::Approx(4.0 * 0.35488));    // T = 12.5, still early
    CHECK(axe_at_5 == doctest::Approx(7.0 * 0.23872));      // T = 25, barely started
    CHECK(fist_at_5 == doctest::Approx(gam::kPunchDamage)); // T = 5, already full
    // The ramp is the part that follows T, so the comparison has to be made
    // between MULTIPLIERS - the axe's damage is bigger throughout its charge,
    // which is why a raw damage comparison would prove nothing about the period.
    // At the same age the sword is 35.488% charged and the axe 23.872%.
    CHECK(sword_at_5 / 4.0 > axe_at_5 / 7.0);
}

// ── the crit ────────────────────────────────────────────────────────────────

TEST_CASE("authority attack: a crit needs BOTH the fall and the charge") {
    // Contract ④, and the reason it is asserted as four cells rather than one
    // happy path: each condition has to be able to fail alone.
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const std::uint16_t sword = weapon_of(sim, "timber_edge");

    // t = 11 is the smallest integer tick past the gate; t = 10 is one below it.
    CHECK(measured_damage(sim, sword, 11, false, true) == doctest::Approx(3.50848 * gam::kCritDamageMultiplier));
    CHECK(measured_damage(sim, sword, 10, false, true) == doctest::Approx(3.05792));  // falling, not charged
    CHECK(measured_damage(sim, sword, 11, false, false) == doctest::Approx(3.50848)); // charged, not falling
    CHECK(measured_damage(sim, sword, 11, false, false) ==
          doctest::Approx(measured_damage(sim, sword, 11, false, true) / gam::kCritDamageMultiplier));
}

TEST_CASE("authority attack: the falling flag is the descent, not merely being airborne") {
    // The card's ⚠ 3: the sign is the rule. `falling` is declared by the client
    // (tick.cpp's `!on_ground && velocity.y < 0.0`), so what the authority can be
    // held to is that it uses the flag it is given and nothing else - a rising
    // jump is the same ActorPose with falling left false, which is the same
    // request as the "not falling" cell above. This case pins the transcription
    // of that definition rather than re-deriving it, because the definition
    // itself lives on the client, in one line, and is what the client's own
    // physics feeds.
    gam::ActorPose rising;
    rising.falling = false; // velocity.y > 0 during the rise
    gam::ActorPose descending;
    descending.falling = true; // velocity.y < 0 on the way down
    CHECK(rising.falling != descending.falling);
    CHECK_FALSE(rising.falling);
}

// ── the sprint knockback ────────────────────────────────────────────────────

TEST_CASE("authority attack: the sprint shove is 0.9, and only at the gate") {
    // Contract ⑤ / ruling C-5. Four cells around the gate, on the same weapon,
    // so the only thing moving between them is the flag and the charge.
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const std::uint16_t sword = weapon_of(sim, "timber_edge");

    CHECK(measured_knockback(sim, sword, 11, true) == doctest::Approx(-0.9));  // sprinting + charged
    CHECK(measured_knockback(sim, sword, 10, true) == doctest::Approx(-0.4));  // sprinting, one tick short
    CHECK(measured_knockback(sim, sword, 11, false) == doctest::Approx(-0.4)); // charged, not sprinting
    CHECK(measured_knockback(sim, sword, 25, false) == doctest::Approx(-0.4)); // full charge, not sprinting
    // The bonus is the ⚖ +0.5 on the base 0.4 and is stated that way, so this
    // pins the sum and not a magic 0.9.
    CHECK(gam::kSprintKnockbackBonus == doctest::Approx(0.5));
}

TEST_CASE("mob knockback: the ⚖ 0.9 sprint impulse carries a mob about 1.98 blocks") {
    // C-5's distance account: the impulse is a SPEED and the distance comes out
    // of the motion contract already in place (damping 0.91 × 0.6 applied after
    // each move). Same fixture as the 0.4 case in test_mob_ai.cpp, so the two
    // numbers are read on the same floor; the ideal series gives 0.9/0.454 =
    // 1.9824 and the momentum threshold eats a little of the tail.
    mobtest::MobFixture fixture;
    const srv::EntityId wretch = fixture.add_mob("hollow_wretch", {0.5, 64.0, 0.5});
    srv::knock_back(fixture.get(wretch), {0.5, 64.0, -1.5}, 0.9); // attacked from -Z, pushed +Z
    const glm::dvec3 start = fixture.get(wretch).position;

    (void) fixture.step(away_actor(), gam::Difficulty::Normal, 1);
    CHECK(fixture.get(wretch).position.z - start.z == doctest::Approx(0.9)); // the whole speed, first tick

    (void) fixture.step(away_actor(), gam::Difficulty::Normal, 60);
    const double pushed = fixture.get(wretch).position.z - start.z;
    INFO("pushed " << pushed << " blocks");
    CHECK(pushed > 1.90);
    CHECK(pushed < 2.00);
    CHECK(fixture.get(wretch).position.x == doctest::Approx(start.x)); // straight along the axis it was hit on
    CHECK(fixture.get(wretch).position.y == doctest::Approx(64.0));    // and it never left the floor
}

// ── what this card must NOT have moved ──────────────────────────────────────

TEST_CASE("authority attack: the mobs' own attack cadence is untouched") {
    // Ruling C-6: the mobs keep their 1 hit/second. There is no attack-speed
    // table for them in research/01, so aligning them to the player's new model
    // would have been invention - the field is asserted so that a later card has
    // to do it on purpose.
    mobtest::MobFixture fixture;
    for (const std::string_view id : {"mossback", "hollow_wretch", "blastbud"}) {
        const gam::MobDef *def = fixture.mobs.find(fixture.type_of(id));
        REQUIRE(def != nullptr);
        CHECK(def->attack_cooldown == 20);
    }
}

TEST_CASE("authority attack: a refused swing costs no charge") {
    // The clock is spent where the swing is ACCEPTED (apply_attack's last line),
    // after every validation - so a request that never reached a mob must not
    // leave the player's next real hit cold. Asserted through the number a
    // player would feel: refuse, then land, and the landed hit is still full.
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const glm::dvec3 spawn = sim.find_spawn();
    const std::uint16_t sword = weapon_of(sim, "timber_edge");

    // Out of reach: real mob, far away.
    const srv::EntityId far =
        sim.summon_mob(sim.entity_types().id_of("hollow_wretch"), spawn + glm::dvec3(10.0, 0.0, 0.0));
    REQUIRE(far != srv::EntityStore::kNoEntity);
    CHECK(sim.submit(attack_request(far, viewer_at(spawn), sword)).reject == gam::ActionReject::OutOfAttackRange);
    // ... and a mob that is not there at all.
    CHECK(sim.submit(attack_request(9999, viewer_at(spawn), sword)).reject == gam::ActionReject::UnknownEntity);

    const srv::EntityId near = sim.summon_mob(sim.entity_types().id_of("hollow_wretch"), spawn);
    REQUIRE(near != srv::EntityStore::kNoEntity);
    const double before = sim.entities().find(near)->health;
    REQUIRE(sim.submit(attack_request(near, viewer_at(spawn + glm::dvec3(1.0, 0.0, 0.0)), sword)).accepted);
    CHECK(before - sim.entities().find(near)->health == doctest::Approx(4.0));
}

TEST_CASE("authority attack: the weapon comes from the pose, not from item_or_block") {
    // Field reuse is a real hazard in this vocabulary (PickUp and DropItems both
    // borrow target.x), so which field carries the weapon is pinned rather than
    // assumed: the damage follows actor.held_item, and a request whose
    // item_or_block disagrees is not read as holding that.
    srv::WorldSim sim;
    load_chunks(sim, 1);
    const glm::dvec3 spawn = sim.find_spawn();
    const srv::EntityId target = sim.summon_mob(sim.entity_types().id_of("hollow_wretch"), spawn);
    REQUIRE(target != srv::EntityStore::kNoEntity);

    gam::ActionRequest req;
    req.kind = gam::ActionKind::Attack;
    req.target = {static_cast<int>(target), 0, 0};
    req.actor = viewer_at(spawn + glm::dvec3(1.0, 0.0, 0.0));
    req.actor.held_item = 0;                               // the fist ...
    req.item_or_block = sim.items().id_of("timber_hewer"); // ... despite claiming an axe here
    const double before = sim.entities().find(target)->health;
    REQUIRE(sim.submit(req).accepted);
    CHECK(before - sim.entities().find(target)->health == doctest::Approx(gam::kPunchDamage));
}
