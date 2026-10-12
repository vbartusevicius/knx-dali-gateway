#pragma once

#include <new>
#include <optional>
#include <type_traits>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace gateway::firmware {

// RAII wrapper over a FreeRTOS queue carrying trivially copyable values between tasks.
template <class T>
class Queue final {
    static_assert(std::is_trivially_copyable_v<T>, "queue items are copied byte-wise by FreeRTOS");

public:
    explicit Queue(unsigned capacity) noexcept : handle_(xQueueCreate(capacity, sizeof(T))) {}
    ~Queue() {
        if (handle_) {
            vQueueDelete(handle_);
        }
    }
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    bool try_push(const T& item) { return handle_ && xQueueSendToBack(handle_, &item, 0) == pdTRUE; }

    bool push(const T& item, TickType_t wait) {
        return handle_ && xQueueSendToBack(handle_, &item, wait) == pdTRUE;
    }

    // Length-1 queue semantic: replace the pending item if there is one.
    bool overwrite(const T& item) { return handle_ && xQueueOverwrite(handle_, &item) == pdTRUE; }

    std::optional<T> pop(TickType_t wait) {
        std::aligned_storage_t<sizeof(T), alignof(T)> storage{};
        if (handle_ && xQueueReceive(handle_, &storage, wait) == pdTRUE) {
            return *std::launder(reinterpret_cast<const T*>(&storage));
        }
        return std::nullopt;
    }

private:
    QueueHandle_t handle_;
};

}
