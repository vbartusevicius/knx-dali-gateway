#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace gateway::lab {

// Bounded blocking queue with the same shape as firmware::Queue, backed by
// std::mutex/condition_variable for the Linux lab build.
template <typename T>
class StdQueue final {
public:
    explicit StdQueue(std::size_t capacity) : capacity_(capacity) {}

    bool try_push(const T& item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (items_.size() >= capacity_) {
                return false;
            }
            items_.push_back(item);
        }
        not_empty_.notify_one();
        return true;
    }

    bool push(const T& item, std::uint32_t wait_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto has_room = not_full_.wait_for(lock, std::chrono::milliseconds(wait_ms),
                                                 [&] { return items_.size() < capacity_; });
        if (!has_room) {
            return false;
        }
        items_.push_back(item);
        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    std::optional<T> pop(std::uint32_t wait_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (wait_ms == wait_forever) {
            not_empty_.wait(lock, [&] { return !items_.empty(); });
        } else if (!not_empty_.wait_for(lock, std::chrono::milliseconds(wait_ms),
                                        [&] { return !items_.empty(); })) {
            return std::nullopt;
        }
        auto item = items_.front();
        items_.pop_front();
        lock.unlock();
        not_full_.notify_one();
        return item;
    }

    // For single-slot mailboxes: keep the newest value.
    bool overwrite(const T& item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (items_.size() >= capacity_) {
                items_.front() = item;
            } else {
                items_.push_back(item);
            }
        }
        not_empty_.notify_one();
        return true;
    }

    static constexpr std::uint32_t wait_forever = 0xFFFFFFFFu;

private:
    std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> items_;
};

}
