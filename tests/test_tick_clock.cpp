#include <doctest/doctest.h>

#include "opencraft/core/tick_clock.hpp"

using opencraft::core::TickClock;

TEST_CASE("50ms advances exactly one tick") {
    TickClock clock;
    CHECK(clock.advance(50.0) == 1);
    CHECK(clock.tick_count() == 1);
    CHECK(clock.dropped_ticks() == 0);
    CHECK(clock.alpha() == doctest::Approx(0.0));
}

TEST_CASE("one large dt of 300ms demands 6 ticks: 5 run, 1 dropped") {
    TickClock clock;
    const int executed = clock.advance(300.0);
    CHECK(executed == TickClock::kMaxCatchUpTicks);
    CHECK(clock.tick_count() == 5);
    CHECK(clock.dropped_ticks() == 1);
    // Only the sub-tick remainder (<50ms) survives for partial-tick blending.
    CHECK(clock.alpha() == doctest::Approx(0.0));
}

TEST_CASE("continuous small dts lose no ticks") {
    TickClock clock;
    int executed = 0;
    for (int frame = 0; frame < 40; ++frame) {
        const int ticks = clock.advance(10.0);
        CHECK(ticks >= 0);
        CHECK(ticks <= 1);
        executed += ticks;
    }
    CHECK(executed == 8); // 400ms = 8 ticks at 50ms
    CHECK(clock.dropped_ticks() == 0);
    CHECK(clock.tick_count() == 8);
}

TEST_CASE("per-frame cap is enforced and excess is counted") {
    TickClock clock;
    CHECK(clock.advance(1000.0) == 5); // 20 ticks demanded, 5 run
    CHECK(clock.dropped_ticks() == 15);
    CHECK(clock.advance(0.0) == 0);
    CHECK(clock.dropped_ticks() == 15); // no further accumulation
    CHECK(clock.tick_count() == 5);
}

TEST_CASE("alpha remains in range and grows without crossing a tick boundary") {
    TickClock clock;
    double previous = 0.0;
    for (int frame = 0; frame < 4; ++frame) {
        clock.advance(10.0); // 40ms total < 50ms, no tick yet
        const double alpha = clock.alpha();
        CHECK(alpha >= 0.0);
        CHECK(alpha < 1.0);
        CHECK(alpha >= previous);
        previous = alpha;
    }
    CHECK(previous == doctest::Approx(0.8));
}

TEST_CASE("reset returns the clock to its initial state") {
    TickClock clock;
    clock.advance(1000.0);
    clock.reset();
    CHECK(clock.tick_count() == 0);
    CHECK(clock.dropped_ticks() == 0);
    CHECK(clock.alpha() == doctest::Approx(0.0));
    CHECK(clock.advance(50.0) == 1);
}

TEST_CASE("non-positive dt is ignored") {
    TickClock clock;
    CHECK(clock.advance(0.0) == 0);
    CHECK(clock.advance(-100.0) == 0);
    CHECK(clock.tick_count() == 0);
    CHECK(clock.dropped_ticks() == 0);
}
