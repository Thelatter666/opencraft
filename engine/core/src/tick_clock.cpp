#include "opencraft/core/tick_clock.hpp"

namespace opencraft::core {

int TickClock::advance(double real_dt_ms) {
    if (real_dt_ms > 0.0) {
        accumulator_ms_ += real_dt_ms;
    }

    int executed = 0;
    while (accumulator_ms_ >= kTickDurationMs && executed < kMaxCatchUpTicks) {
        accumulator_ms_ -= kTickDurationMs;
        ++tick_count_;
        ++executed;
    }

    if (accumulator_ms_ >= kTickDurationMs) {
        const auto excess = static_cast<std::int64_t>(accumulator_ms_ / kTickDurationMs);
        dropped_ticks_ += excess;
        accumulator_ms_ -= static_cast<double>(excess) * kTickDurationMs;
    }

    return executed;
}

double TickClock::alpha() const {
    return accumulator_ms_ / kTickDurationMs;
}

void TickClock::reset() {
    accumulator_ms_ = 0.0;
    tick_count_ = 0;
    dropped_ticks_ = 0;
}

} // namespace opencraft::core
