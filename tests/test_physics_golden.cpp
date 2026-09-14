#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "opencraft/physics/player_physics.hpp"
#include "physics_test_world.hpp"

using opencraft::physics::InputState;
using opencraft::physics::PhysicsConfig;
using opencraft::physics::PlayerState;
using opencraft::physics::step_player;
using physics_test::BoxWorld;

namespace {

constexpr double kPi = 3.14159265358979323846;

// The fixed acceptance world for the golden replay (code-built, docs/01 §8).
// Layout along the x axis at z=0.5, upper surface y=64:
//   [−64,−35) upper ground | pit [−35,−25) with floor top y=54 and a shallow
//   water strip [−32,−28) | [−25,−18) upper ground | pool [−18,−14) water
//   (floor y=62) | [−14,81) upper ground with a wall at x=24.
// Pit side walls (x=−36 and x=−19, y 54..62) and the sub-floor strip
// [−23,−19) keep the pit closed so nothing falls into the void.
BoxWorld golden_world() {
    BoxWorld w;
    w.solid(-64, -36, 63, 63, -16, 16); // upper ground west of the pit
    w.solid(-25, -19, 63, 63, -16, 16); // upper ground between pit and pool
    w.solid(-14, 80, 63, 63, -16, 16);  // upper ground east of the pool
    w.solid(-18, -14, 63, 63, -16, -3); // ground beside the pool (north side)
    w.solid(-18, -14, 63, 63, 2, 16);   // ground beside the pool (south side)
    w.solid(24, 24, 64, 67, -8, 8);     // wall on the east runway
    w.solid(-35, -33, 53, 53, -16, 16); // pit floor west of the water strip, top y=54
    w.solid(-28, -24, 53, 53, -16, 16); // pit floor east of the water strip, top y=54
    w.solid(-23, -20, 53, 53, -16, 16); // sub-floor strip closing the pit east side
    w.solid(-36, -36, 54, 62, -16, 16); // pit west wall
    w.solid(-19, -19, 54, 62, -16, 16); // pit east wall
    w.solid(-32, -29, 52, 52, -2, 2);   // water strip floor (1 deep)
    w.liquid(-32, -29, 53, 53, -2, 2);  // water strip in the pit
    w.solid(-18, -15, 61, 61, -2, 2);   // pool floor
    w.liquid(-18, -15, 62, 63, -2, 2);  // pool water, surface flush with y=64
    return w;
}

struct Leg {
    int ticks;
    double yaw;
    bool forward;
    bool jump;
    bool sneak;
    bool sprint;
};

// ≥600-tick fixed input sequence: walk cruise, wall slide, a there-and-back
// walk along z, sprint plunge into the pool, swim out over the west lip,
// sneak edge clamp at the pit rim, 10-block cliff fall (7 damage), pit water
// wading and bobbing. Yaws are exact multiples of π/2 so sin/cos snap to
// clean ±1/0 (platform-stable golden).
std::vector<Leg> golden_script() {
    const double east = -kPi / 2.0; // faces +X
    const double west = kPi / 2.0;  // faces −X
    const double south = 0.0;       // faces −Z
    const double north = kPi;       // faces +Z
    return {
        {100, east, true, false, false, false}, // walk +X, cruise window
        {5, east, false, false, false, false},  // idle, decelerate
        {55, east, true, false, false, false},  // walk into the wall, slide
        {5, east, false, false, false, false},
        {40, south, true, false, false, false}, // −Z excursion on the east runway
        {42, north, true, false, false, false}, // back +Z into the pool lane (z≈1.2)
        {5, east, false, false, false, false},
        // T-D7: sprint leg shortened from 145 to 137 ticks. The friction
        // migration accelerates much faster (k = 0.546 vs 0.9) and then glides
        // only 0.26 blocks instead of 1.94, so the same input travels further
        // and the plunge leg overshot the pool, silently dropping the
        // fall-damage and sneak-clamp coverage downstream. Measured: at 145 the
        // player ends the plunge 1.6 blocks past the pool's west wall.
        {137, west, true, false, false, true}, // sprint −X, plunge into the pool
        // T-D1: release forward for these ticks so the sprint state machine
        // ends the sprint through the DOCUMENTED forward-release condition.
        // Sprint is intentionally sticky while forward is held (docs/research/05
        // §1.2 does not list "sprint key released" as an end-condition, and the
        // double-tap path runs with no sprint key held at all), so without this
        // gap the next leg's jump is a SPRINT jump instead of the plain jump the
        // leg comment describes. That carried the player past the pit water and
        // silently dropped the fall-damage coverage (hp stayed 20 in every
        // snapshot; the old golden recorded the expected 13).
        // T-D7: 5 → 13 ticks so this leg and the sprint leg still total 150
        // ticks. Keeping the BLOCK length is what preserves snapshot alignment:
        // every later leg keeps its original tick count, so the sneak window
        // still covers t=480..600 and the plunge/water snapshots land on the
        // same 20-tick columns as the T-D1 golden. (T-D1 used the same
        // tick-neutral technique for its own 145 + 5 = 150 split.)
        {13, west, false, false, false, false}, // release forward: sprint ends
        {60, west, true, true, false, false},   // swim up and out the west lip, then settle
        {10, west, false, false, false, false},
        {120, west, true, false, true, false}, // sneak toward the pit rim, clamped
        {10, west, false, false, true, false}, // idle at the rim, still sneaking
        {50, west, true, false, false, false}, // walk off: 10-block fall, 7 damage;
                                               // cross the pit into its water strip
        {60, west, false, true, false, false}, // bob in the pit water
        {30, west, true, false, false, false}, // toward the pit west wall
        {10, west, false, false, false, false},
        {10, west, false, true, false, false}, // final hop
    };
}

InputState leg_input(const Leg &leg, std::uint32_t sequence) {
    InputState in;
    in.yaw = leg.yaw;
    in.forward = leg.forward;
    in.jump = leg.jump;
    in.sneak = leg.sneak;
    in.sprint = leg.sprint;
    in.sequence = sequence;
    return in;
}

std::string snapshot_line(int tick, const PlayerState &s) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "t=%d pos=%.12f,%.12f,%.12f vel=%.12f,%.12f,%.12f ground=%d fall=%.12f pose=%d hp=%.12f seq=%u", tick,
                  s.position.x, s.position.y, s.position.z, s.velocity.x, s.velocity.y, s.velocity.z,
                  s.on_ground ? 1 : 0, s.fall_distance, static_cast<int>(s.pose), s.health, s.last_input_sequence);
    return std::string(buf);
}

std::vector<std::string> run_golden_replay() {
    BoxWorld world = golden_world();
    PlayerState s;
    s.position = {0.5, 64.0, 0.5};
    s.on_ground = true;
    s.fall_peak_y = 64.0;

    std::vector<std::string> lines;
    lines.push_back(snapshot_line(0, s));
    int tick = 0;
    for (const Leg &leg : golden_script()) {
        for (int i = 0; i < leg.ticks; ++i) {
            step_player(s, leg_input(leg, static_cast<std::uint32_t>(tick)), world);
            ++tick;
            if (tick % 20 == 0) {
                lines.push_back(snapshot_line(tick, s));
            }
        }
    }
    return lines;
}

std::uint64_t fnv1a64(const std::vector<std::string> &lines) {
    std::uint64_t h = 14695981039346656037ULL;
    for (const std::string &line : lines) {
        for (const char c : line) {
            h ^= static_cast<std::uint8_t>(c);
            h *= 1099511628211ULL;
        }
        h ^= static_cast<std::uint8_t>('\n');
        h *= 1099511628211ULL;
    }
    return h;
}

std::string golden_path() {
    const std::string dir = OPENCRAFT_GOLDEN_DIR;
    return dir + "/physics_golden_replay_v1.txt";
}

} // namespace

TEST_CASE("golden replay: fixed 762-tick input sequence on the fixed world") {
    const std::vector<std::string> actual = run_golden_replay();
    const std::uint64_t hash = fnv1a64(actual);
    char hash_buf[32];
    std::snprintf(hash_buf, sizeof(hash_buf), "fnv1a64=%016llx", static_cast<unsigned long long>(hash));

    const bool update = std::getenv("OPENCRAFT_PHYSICS_UPDATE_GOLDEN") != nullptr;
    if (update) {
        FILE *f = std::fopen(golden_path().c_str(), "w");
        REQUIRE(f != nullptr);
        // NOTE (T-D7 report S-7): the "660 ticks" figure was a historical
        // inaccuracy carried since T007 — the script actually runs 762 ticks
        // (snapshot grid t=0..760). The header is generated from this literal,
        // so correcting it here (and regenerating) keeps file and truth aligned.
        std::fprintf(
            f, "# opencraft physics golden replay v1 (T007): 762 ticks, world+script in test_physics_golden.cpp\n");
        for (const std::string &line : actual) {
            std::fprintf(f, "%s\n", line.c_str());
        }
        std::fprintf(f, "%s\n", hash_buf);
        std::fclose(f);
        MESSAGE("golden replay updated at " << golden_path());
        return;
    }

    FILE *f = std::fopen(golden_path().c_str(), "r");
    REQUIRE_MESSAGE(f != nullptr, "cannot open " << golden_path());
    std::vector<std::string> expected;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), f) != nullptr) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        if (!line.empty() && line[0] != '#') {
            expected.push_back(line);
        }
    }
    std::fclose(f);

    REQUIRE(expected.size() == actual.size() + 1); // + trailing hash line
    const std::string &expected_hash = expected.back();
    CHECK_MESSAGE(expected_hash == hash_buf, "hash mismatch: expected " << expected_hash << " got " << hash_buf);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (expected[i] != actual[i]) {
            MESSAGE("first divergence at line " << i << ":\n  expected: " << expected[i]
                                                << "\n  actual:   " << actual[i]);
            REQUIRE(expected[i] == actual[i]);
        }
    }
    CHECK(true);
}
