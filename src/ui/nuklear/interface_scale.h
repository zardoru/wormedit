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
    [[nodiscard]] auto factor() const -> float { return static_cast<float>(percent) / 100.0f; }
    [[nodiscard]] auto logical(float point) const -> float { return point / factor(); }
    [[nodiscard]] auto logical_extent(int points) const -> int {
        return std::max(1, static_cast<int>(std::lround(logical(static_cast<float>(points)))));
    }
};
}
