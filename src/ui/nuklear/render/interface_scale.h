#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace wormedit::ui::nuklear {
// Window points are converted once at the frontend boundary. Renderer geometry stays logical.
struct interface_scale_t {
    std::uint32_t percent{100};
    [[nodiscard]] static auto from(std::uint32_t requested) -> std::optional<interface_scale_t> {
        if (requested && (requested < 100 || requested > 200)) return std::nullopt;
        return interface_scale_t{requested ? requested : 100};
    }
    // The layout is designed for at least this many logical units (the window's minimum size).
    static constexpr int minimum_logical_width = 800;
    static constexpr int minimum_logical_height = 560;
    // Caps the requested scale so a window of these points keeps the minimum logical layout;
    // never below 100%. An unknown size keeps the request.
    [[nodiscard]] auto fitted(int window_width, int window_height) const -> interface_scale_t {
        if (window_width <= 0 || window_height <= 0) return *this;
        const auto fit = std::min(window_width * 100 / minimum_logical_width,
                                  window_height * 100 / minimum_logical_height);
        return {std::clamp(static_cast<std::uint32_t>(std::max(fit, 0)), std::uint32_t{100}, percent)};
    }
    [[nodiscard]] auto factor() const -> float { return static_cast<float>(percent) / 100.0f; }
    [[nodiscard]] auto logical(float point) const -> float { return point / factor(); }
    [[nodiscard]] auto logical_extent(int points) const -> int {
        return std::max(1, static_cast<int>(std::lround(logical(static_cast<float>(points)))));
    }
};
}
