#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <future>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

#include "opencraft/core/task_system.hpp"

using opencraft::core::TaskSystem;

namespace {

// Deterministic workload: no timing assertions anywhere, completion only.
std::int64_t square_sum(std::int64_t n) {
    std::int64_t sum = 0;
    for (std::int64_t i = 1; i <= n; ++i) {
        sum += i * i;
    }
    return sum;
}

} // namespace

TEST_CASE("concurrent submits from multiple threads all complete with correct results") {
    TaskSystem system;

    constexpr int kThreads = 4;
    constexpr int kTasksPerThread = 25;
    // One container per producer: vectors are not thread-safe, and this test
    // must race only on the TaskSystem queue, not on the test's own bookkeeping.
    std::vector<std::vector<std::future<std::int64_t>>> per_thread_futures(kThreads);

    std::atomic<int> start_barrier{kThreads};
    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back(
            [&futures = per_thread_futures[static_cast<std::size_t>(t)], &system, &start_barrier, t] {
                // All producers race into submit() together to stress the queue lock.
                --start_barrier;
                while (start_barrier.load() > 0) {
                    std::this_thread::yield();
                }
                futures.reserve(kTasksPerThread);
                for (int i = 1; i <= kTasksPerThread; ++i) {
                    futures.push_back(system.submit([t, i] { return square_sum(t * 100 + i); }));
                }
            });
    }
    for (std::thread &producer : producers) {
        producer.join();
    }

    for (int t = 0; t < kThreads; ++t) {
        auto &futures = per_thread_futures[static_cast<std::size_t>(t)];
        REQUIRE(futures.size() == kTasksPerThread);
        for (int i = 1; i <= kTasksPerThread; ++i) {
            const std::int64_t expected = square_sum(t * 100 + i);
            CHECK(futures[static_cast<std::size_t>(i - 1)].get() == expected);
        }
    }
}

TEST_CASE("destructor drains every pending task") {
    std::vector<std::future<int>> futures;
    {
        TaskSystem system;
        for (int i = 0; i < 32; ++i) {
            futures.push_back(system.submit([i] { return i * 2; }));
        }
        // system destroyed here: queued tasks must run before workers join.
    }
    for (int i = 0; i < 32; ++i) {
        REQUIRE(futures[static_cast<std::size_t>(i)].valid());
        CHECK(futures[static_cast<std::size_t>(i)].get() == i * 2);
    }
}

TEST_CASE("task exceptions propagate through the future") {
    TaskSystem system;
    auto future = system.submit([]() -> int { throw std::runtime_error("boom"); });
    CHECK_THROWS_AS(static_cast<void>(future.get()), std::runtime_error);
}

TEST_CASE("worker count defaults to hardware_concurrency - 1 and is configurable") {
    CHECK(TaskSystem::suggested_worker_count() >= 1);
    TaskSystem single{1};
    CHECK(single.worker_count() == 1);
    TaskSystem four{4};
    CHECK(four.worker_count() == 4);
    CHECK(TaskSystem{0}.worker_count() == 1); // zero clamps to one
}
