#pragma once

#include <cstdint>

namespace opencraft::core {

// Fixed-step 20 TPS logic clock decoupled from the render loop (docs/03 §1).
// advance() feeds one render frame's real duration and returns how many whole
// 50ms ticks the simulation should run this frame. To bound the death spiral
// after a stall (loading, breakpoint, driver hitch), a single frame runs at
// most kMaxCatchUpTicks; the excess is dropped and counted, never queued, so
// the simulation degrades to slow motion instead of freezing.
class TickClock {
public:
    static constexpr int kTicksPerSecond = 20;
    static constexpr double kTickDurationMs = 1000.0 / kTicksPerSecond; // 50ms
    // 5 ticks = 250ms of catch-up per frame: large enough to absorb ordinary
    // hiccups without losing ticks, small enough that a real stall cannot
    // monopolize the frame budget.
    static constexpr int kMaxCatchUpTicks = 5;

    // Accumulates one frame's real time in ms and returns the number of ticks
    // to execute this frame (0..kMaxCatchUpTicks). Non-positive dt is ignored.
    int advance(double real_dt_ms);

    // Partial-tick interpolation factor in [0, 1): progress toward the next
    // tick boundary, for blending entity/camera state between logic ticks.
    [[nodiscard]] double alpha() const;

    // Ticks executed since construction/reset (int64 per contract).
    [[nodiscard]] std::int64_t tick_count() const { return tick_count_; }

    // Ticks discarded by the anti-spiral cap.
    [[nodiscard]] std::int64_t dropped_ticks() const { return dropped_ticks_; }

    void reset();

private:
    double accumulator_ms_ = 0.0;
    std::int64_t tick_count_ = 0;
    std::int64_t dropped_ticks_ = 0;
};

} // namespace opencraft::core
