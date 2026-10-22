#pragma once

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <optional>

namespace wormedit::ui::layout {

// std::max's operand order also determines the existing fallback for NaN geometry.
enum class content_height_clamp_order_t { minimum_first, available_first };

struct content_height_policy_t {
    float minimum{};
    std::optional<float> reserved;
    bool include_spacing{};
    std::optional<float> maximum;
    content_height_clamp_order_t clamp_order{content_height_clamp_order_t::minimum_first};

    auto operator==(const content_height_policy_t&) const -> bool = default;

    auto height(float content_bottom, float next_row_y, float spacing) const -> float {
        auto available = content_bottom - next_row_y;
        if (reserved) available -= *reserved;
        if (include_spacing) available -= spacing;
        if (maximum) available = std::min(*maximum, available);
        return clamp_order == content_height_clamp_order_t::minimum_first
            ? std::max(minimum, available) : std::max(available, minimum);
    }
};

// Content bounds and the next widget position share Nuklear's logical window coordinates.
inline auto remaining_content_height(nk_context* context, const content_height_policy_t& policy)
    -> float {
    const auto region = nk_window_get_content_region(context);
    return policy.height(region.y + region.h, nk_widget_position(context).y,
                         context->style.window.spacing.y);
}

} // namespace wormedit::ui::layout
