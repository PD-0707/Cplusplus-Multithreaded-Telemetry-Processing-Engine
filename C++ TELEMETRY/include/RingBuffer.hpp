#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <cstddef>
#include <atomic>

/**
 * @brief Thread-Safe Bounded Circular Buffer (Ring Buffer)
 * 
 * Supports Multiple-Producer Multiple-Consumer (MPMC) synchronization using
 * std::mutex and std::condition_variable. Includes graceful shutdown semantics
 * to prevent thread deadlocks upon termination.
 * 
 * @tparam T The element type stored in the buffer
 * @tparam Capacity Compile-time fixed maximum capacity of the ring
 */
template <typename T, size_t Capacity>
class RingBuffer {
    static_assert(Capacity > 0, "RingBuffer Capacity must be greater than 0");

private:
    std::vector<T> buffer_;
    size_t head_ = 0;         // Write index (Producer)
    size_t tail_ = 0;         // Read index (Consumer)
    size_t size_ = 0;         // Current count of valid elements
    bool is_shutdown_ = false; // Flag indicating pipeline termination

    mutable std::mutex mtx_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;

public:
    RingBuffer() : buffer_(Capacity) {}

    // Destructor ensures all blocked threads are released
    ~RingBuffer() {
        shutdown();
    }

    // Non-copyable, non-movable to prevent concurrent lifecycle issues
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /**
     * @brief Blocking push - waits until buffer has available capacity or is shut down.
     * @param item Const reference to the item to insert.
     * @return true if successfully inserted, false if buffer was shut down.
     */
    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(mtx_);
        
        // Wait until there is space available OR the buffer is shut down
        not_full_.wait(lock, [this]() {
            return size_ < Capacity || is_shutdown_;
        });

        if (is_shutdown_) {
            return false;
        }

        buffer_[head_] = item;
        head_ = (head_ + 1) % Capacity;
        ++size_;

        // Signal waiting consumer
        not_empty_.notify_one();
        return true;
    }

    /**
     * @brief Blocking push with move semantics for efficiency.
     */
    bool push(T&& item) {
        std::unique_lock<std::mutex> lock(mtx_);

        not_full_.wait(lock, [this]() {
            return size_ < Capacity || is_shutdown_;
        });

        if (is_shutdown_) {
            return false;
        }

        buffer_[head_] = std::move(item);
        head_ = (head_ + 1) % Capacity;
        ++size_;

        not_empty_.notify_one();
        return true;
    }

    /**
     * @brief Blocking pop - waits until an item is available or buffer is shut down.
     * @return std::optional<T> containing the item, or std::nullopt if shut down and empty.
     */
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mtx_);

        // Wait until there is data available OR the buffer is shut down
        not_empty_.wait(lock, [this]() {
            return size_ > 0 || is_shutdown_;
        });

        // Drain remaining items even if shutdown was requested
        if (size_ == 0 && is_shutdown_) {
            return std::nullopt;
        }

        T item = std::move(buffer_[tail_]);
        tail_ = (tail_ + 1) % Capacity;
        --size_;

        // Signal waiting producer
        not_full_.notify_one();
        return item;
    }

    /**
     * @brief Non-blocking try_pop - returns immediately if buffer is empty.
     */
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (size_ == 0) {
            return std::nullopt;
        }

        T item = std::move(buffer_[tail_]);
        tail_ = (tail_ + 1) % Capacity;
        --size_;

        not_full_.notify_one();
        return item;
    }

    /**
     * @brief Signals shutdown to all threads. Wakes up all waiting producers and consumers.
     */
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            is_shutdown_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    /**
     * @brief Check whether shutdown has been triggered.
     */
    bool is_shutdown() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return is_shutdown_;
    }

    /**
     * @brief Thread-safe query of current item count.
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return size_;
    }

    /**
     * @brief Thread-safe check if buffer is empty.
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return size_ == 0;
    }

    /**
     * @brief Get buffer maximum capacity.
     */
    constexpr size_t capacity() const {
        return Capacity;
    }
};

