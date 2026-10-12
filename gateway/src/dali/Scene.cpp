#include "dali/Scene.hpp"

#include <cstdint>
#include <optional>

namespace gateway::dali {

Scene::Scene(std::uint8_t value) noexcept : value_(value) {}

std::optional<Scene> Scene::from(unsigned value) noexcept {
    if (value >= capacity) {
        return std::nullopt;
    }
    return Scene{static_cast<std::uint8_t>(value)};
}

std::uint8_t Scene::value() const noexcept {
    return value_;
}

}
