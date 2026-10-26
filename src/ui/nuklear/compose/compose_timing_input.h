#pragma once

#include "ui/nuklear/compose/compose_panel_internal.h"

#include <cstdint>
#include <optional>

void update_timing_stage_drag(const ComposePointerEventContext& input);
void update_kiai_span_drag(const ComposePointerEventContext& input);
void update_recipe_drag(const ComposePointerEventContext& input);

// One picking policy for span gestures and their hover affordances. IDs belong to the family.
enum class compose_span_family_t { kiai, recipe, speed };
struct compose_span_target_t {
    compose_span_family_t family{};
    std::uint64_t id{};
    std::uint8_t edge{};
    static auto from(const wormedit::ui::compose_view_t&, double x, double y, double tolerance)
        -> std::optional<compose_span_target_t>;
    auto operator==(const compose_span_target_t&) const -> bool = default;
};
