// Jet Force Gemini PS4: replaces moodycamel's lock-free ConcurrentQueue (thirdparty/concurrentqueue) with a mutex queue.
//
// On the console an item enqueued by the game thread now and then became invisible to the consumer: the graphics
// thread kept taking the VI thread's screen updates but never the game's graphics task, and the game waited for it
// forever (1 of ~1500 tasks; enqueued N+1, dequeued N). moodycamel keeps one sub-queue per producer thread, identified
// by the address of a thread_local, and recycles them through thread_local destructors; the console's TLS and thread
// lifecycle do not give it what it expects. The queues here carry a few hundred items a second at most, so a mutex
// costs nothing. Same API as the subset the runtime and the port use.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

// What moodycamel's lightweightsemaphore.h (still used) expects from the original header.
#ifndef MOODYCAMEL_DELETE_FUNCTION
#define MOODYCAMEL_DELETE_FUNCTION = delete
#endif

namespace moodycamel {

template <typename T, typename Traits = void>
class ConcurrentQueue {
public:
    ConcurrentQueue() = default;
    explicit ConcurrentQueue(size_t) {}
    ConcurrentQueue(const ConcurrentQueue&) = delete;
    ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

    bool enqueue(const T& item) {
        std::lock_guard<std::mutex> lock{ mutex_ };
        items_.push_back(item);
        return true;
    }
    bool enqueue(T&& item) {
        std::lock_guard<std::mutex> lock{ mutex_ };
        items_.push_back(std::move(item));
        return true;
    }
    bool try_enqueue(const T& item) { return enqueue(item); }
    bool try_enqueue(T&& item) { return enqueue(std::move(item)); }

    template <typename U>
    bool try_dequeue(U& out) {
        std::lock_guard<std::mutex> lock{ mutex_ };
        if (items_.empty()) return false;
        out = std::move(items_.front());
        items_.pop_front();
        return true;
    }

    size_t size_approx() const {
        std::lock_guard<std::mutex> lock{ mutex_ };
        return items_.size();
    }

protected:
    mutable std::mutex mutex_;
    std::deque<T> items_;
};

} // namespace moodycamel
