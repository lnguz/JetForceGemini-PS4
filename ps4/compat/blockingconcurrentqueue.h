// Jet Force Gemini PS4: replaces moodycamel's BlockingConcurrentQueue with a mutex queue (see concurrentqueue.h here
// for why). The blocking waits use an untimed condition variable or, for timed waits, short sleeps against
// steady_clock: libkernel's pthread_cond_timedwait is not relied on.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <thread>

#include "concurrentqueue.h"     // ps4/compat (the mutex queue)
#include "lightweightsemaphore.h" // moodycamel's, still used directly by the runtime

namespace moodycamel {

template <typename T, typename Traits = void>
class BlockingConcurrentQueue : public ConcurrentQueue<T, Traits> {
    using Base = ConcurrentQueue<T, Traits>;

public:
    BlockingConcurrentQueue() = default;
    explicit BlockingConcurrentQueue(size_t) {}

    bool enqueue(const T& item) {
        Base::enqueue(item);
        cond_.notify_one();
        return true;
    }
    bool enqueue(T&& item) {
        Base::enqueue(std::move(item));
        cond_.notify_one();
        return true;
    }
    bool try_enqueue(const T& item) { return enqueue(item); }
    bool try_enqueue(T&& item) { return enqueue(std::move(item)); }

    template <typename U>
    void wait_dequeue(U& out) {
        std::unique_lock<std::mutex> lock{ this->mutex_ };
        while (this->items_.empty()) cond_.wait(lock);
        out = std::move(this->items_.front());
        this->items_.pop_front();
    }

    template <typename U>
    bool wait_dequeue_timed(U& out, std::int64_t timeout_usecs) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(timeout_usecs);
        for (;;) {
            if (this->try_dequeue(out)) return true;
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return false;
            const auto left = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now);
            std::this_thread::sleep_for(left < std::chrono::microseconds(200) ? left : std::chrono::microseconds(200));
        }
    }

    template <typename U, typename Rep, typename Period>
    bool wait_dequeue_timed(U& out, const std::chrono::duration<Rep, Period>& timeout) {
        return wait_dequeue_timed(out, (std::int64_t)std::chrono::duration_cast<std::chrono::microseconds>(timeout).count());
    }

private:
    std::condition_variable cond_;
};

} // namespace moodycamel
