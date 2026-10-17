#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace gateway::network {

// Bounds-checked, zero-allocation JSON buffer writer.
class JsonWriter final {
public:
    JsonWriter(char* buf, std::size_t cap) noexcept : buf_(buf), cap_(cap) {
        if (buf_ != nullptr && cap_ > 0) {
            buf_[0] = '\0';
        }
    }

    void appendf(const char* fmt, ...) noexcept __attribute__((format(printf, 2, 3))) {
        if (buf_ == nullptr || pos_ >= cap_) {
            return;
        }
        va_list args;
        va_start(args, fmt);
        // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized)
        const int n = std::vsnprintf(buf_ + pos_, cap_ - pos_, fmt, args);
        va_end(args);
        if (n > 0) {
            const auto un = static_cast<std::size_t>(n);
            pos_ = (pos_ + un < cap_) ? (pos_ + un) : (cap_ - 1);
        }
    }

    void append_escaped(const char* raw) noexcept {
        if (raw == nullptr || buf_ == nullptr) {
            return;
        }
        for (const char* p = raw; *p != '\0' && pos_ + 2 < cap_; ++p) {
            if (*p == '"' || *p == '\\') {
                buf_[pos_++] = '\\';
            }
            if (static_cast<unsigned char>(*p) >= 0x20U) {
                buf_[pos_++] = *p;
            }
        }
        if (cap_ > 0) {
            buf_[pos_] = '\0';
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return pos_; }

private:
    char* buf_ = nullptr;
    std::size_t cap_ = 0;
    std::size_t pos_ = 0;
};

}  // namespace gateway::network
