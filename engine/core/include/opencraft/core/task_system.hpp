#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace opencraft::core {

// Minimal MPSC worker pool (docs/03 §2 seed). Fixed worker count (defaults to
// hardware_concurrency - 1, min 1), submit(fn) -> std::future<T>, and the
// destructor drains all pending tasks before joining. Priorities, cancellation
// and task graphs are deferred to a later task on purpose.
// Workers only ever run callables handed to them; queueing world-mutating
// work is the caller's contract to avoid (docs/03 §2).
class TaskSystem {
public:
    TaskSystem() : TaskSystem(suggested_worker_count()) {}

    explicit TaskSystem(std::size_t worker_count) : worker_count_(worker_count == 0 ? 1 : worker_count) {
        workers_.reserve(worker_count_);
        for (std::size_t i = 0; i < worker_count_; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~TaskSystem() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            draining_ = true;
        }
        work_available_.notify_all();
        for (std::thread &worker : workers_) {
            worker.join();
        }
    }

    TaskSystem(const TaskSystem &) = delete;
    TaskSystem &operator=(const TaskSystem &) = delete;
    TaskSystem(TaskSystem &&) = delete;
    TaskSystem &operator=(TaskSystem &&) = delete;

    // Enqueue a callable and get its result via std::future. Exceptions thrown
    // by the task propagate through the future.
    template <typename F>
    auto submit(F &&fn) -> std::future<std::invoke_result_t<std::decay_t<F>>> {
        using Result = std::invoke_result_t<std::decay_t<F>>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(fn));
        std::future<Result> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push([task] { (*task)(); });
        }
        work_available_.notify_one();
        return future;
    }

    [[nodiscard]] std::size_t worker_count() const { return worker_count_; }

    [[nodiscard]] static std::size_t suggested_worker_count() {
        const unsigned hardware = std::thread::hardware_concurrency();
        return hardware > 1 ? static_cast<std::size_t>(hardware - 1) : 1;
    }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                work_available_.wait(lock, [this] { return draining_ || !tasks_.empty(); });
                if (tasks_.empty()) {
                    // draining_ is set and the queue is fully drained.
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::size_t worker_count_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable work_available_;
    bool draining_ = false;
};

} // namespace opencraft::core
