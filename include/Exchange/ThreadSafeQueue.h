// A generic, thread-safe FIFO queue used for the Producer-Consumer pipeline between threads.
// Templated to reuse for both Order (instrument queues) and ExecutionReport (output queue) without code duplication.
#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

// Template classes must be defined in headers so the compiler can
// instantiate them in each translation unit that uses them.
// Keeping everything here avoids the complexity of explicit instantiation.
template<typename T>
class ThreadSafeQueue {
public:
    // explicit to Prevents accidental implicit conversions.
    explicit ThreadSafeQueue() = default;

    // Non-copyable as copying a mutex is leading to an undefined behaviour.
    // The queue manages a unique synchronisation resource.
    ThreadSafeQueue(const ThreadSafeQueue&)            = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // Push an item — called by producer thread(s).
    // 'T&& (universal reference) + std::move' because the caller transfers
    // ownership of the item into the queue with zero copies.
    void push(T&& item) {
        {
            // Lock the mutex to protect the queue during modification.
            // std::lock_guard uses RAII — automatically releases the lock
            // when it goes out of scope, even if an exception is thrown.
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(item));
        }
        // Notify AFTER releasing lock avoids waking a waiter only
        // to have it immediately block again on the mutex.
        condVar_.notify_one();
    }

    // Blocking pop — called by consumer thread(s).
    // Returns false when the queue is done (sentinel received).
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);

        // Condition variables can wake spuriously (OS signals waiting threads
        // for no reason), so we re-check the actual condition each time we wake.
        // The lambda ensures we only proceed when the queue has data or is done.
        condVar_.wait(lock, [this] {
            return !queue_.empty() || done_;
        });

        if (queue_.empty()) {
            return false;  // Queue is drained and marked done
        }

        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // Non-blocking try-pop — used when consumers want to drain remaining items after producer signals done.
    bool tryPop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // Signal that no more items will be pushed. Wakes all waiters.
    void setDone() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        condVar_.notify_all();  // Wake ALL consumers so they can exit
    }

    bool isDone() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return done_ && queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex      mutex_;    // WHY mutable: allows locking in const methods
    std::condition_variable condVar_;
    std::queue<T>           queue_;
    bool                    done_ = false;
};