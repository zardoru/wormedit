#pragma once

#include "wormedit/nuklear_config.h"

#include <optional>
#include <string_view>
#include <vector>
#include <string>

namespace wormedit::ui::nuklear::tooltip {

struct text_plan_t {
    float outer_width{};
    float content_height{};
    std::vector<std::string> lines;
};

struct placement_t {
    struct nk_rect outer_bounds{};
    struct nk_vec2 offset{};
};

// The frontend sets the logical app viewport once before drawing each frame.
void set_viewport(struct nk_rect viewport) noexcept;
// The viewport last set; empty before the first frame. Surfaces clamp shadow scissors to it.
[[nodiscard]] auto viewport() noexcept -> const struct nk_rect&;

// Measures and wraps text to the largest readable width allowed by the viewport.
// Explicit newlines are kept, and long words split at UTF-8 codepoint boundaries.
[[nodiscard]] auto plan_text(nk_context* context, std::string_view text,
                             float preferred_outer_width = 420.0F)
    -> std::optional<text_plan_t>;

// content_height excludes the tooltip window's padding and border.
[[nodiscard]] auto place(nk_context* context, float outer_width, float content_height)
    -> std::optional<placement_t>;
[[nodiscard]] auto begin(nk_context* context, const placement_t& placement) -> bool;
void draw_text(nk_context* context, const text_plan_t& plan);
void end(nk_context* context);

// Convenience path for the common text-only tooltip.
void text(nk_context* context, std::string_view value,
          float preferred_outer_width = 420.0F);

// True when the pointer is over `bounds` and the window being drawn is the topmost window under
// the pointer. A window drawn over `bounds` (another dialog, a popup) hides the tooltip, while
// the widget underneath keeps its own hover feedback.
[[nodiscard]] auto hovered(const nk_context* context, struct nk_rect bounds) -> bool;

} // namespace wormedit::ui::nuklear::tooltip
