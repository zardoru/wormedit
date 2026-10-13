#pragma once
#include "ui/field/color.h"
#include "wormedit/nuklear_config.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wormedit::ui::chrome {
inline auto color_byte(double value) -> nk_byte {
    return static_cast<nk_byte>(std::lround(std::clamp(value, 0.0, 255.0)));
}
inline auto unit_to_byte(float value) -> nk_byte {
    return color_byte(std::clamp(value, 0.0f, 1.0f) * 255.0f);
}
inline auto with_alpha(nk_color color, nk_byte alpha) -> nk_color {
    color.a = alpha;
    return color;
}
inline auto percent_to_alpha(std::uint32_t percent, nk_byte maximum = 255) -> nk_byte {
    return static_cast<nk_byte>((std::min(percent, std::uint32_t{100}) * maximum + 50) / 100);
}
} // namespace wormedit::ui::chrome
