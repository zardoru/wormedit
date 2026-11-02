#include <wormedit/core/storyboard_gizmo.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace wormedit {
namespace {
auto is_finite(visual_point_t point) -> bool {
    return std::isfinite(point.x) && std::isfinite(point.y);
}
auto constrained_delta(visual_point_t delta, storyboard_gizmo_axis_t axis, bool shift) -> visual_point_t {
    if (axis == storyboard_gizmo_axis_t::both && shift)
        axis = std::abs(delta.x) >= std::abs(delta.y) ? storyboard_gizmo_axis_t::x : storyboard_gizmo_axis_t::y;
    if (axis == storyboard_gizmo_axis_t::x) delta.y = 0;
    if (axis == storyboard_gizmo_axis_t::y) delta.x = 0;
    return delta;
}
auto scale_value(std::span<const double> value, visual_point_t start, visual_point_t point,
    storyboard_gizmo_axis_t axis, bool shift) -> std::vector<double> {
    const auto x = axis == storyboard_gizmo_axis_t::y ? 1 : point.x / start.x;
    const auto y = axis == storyboard_gizmo_axis_t::x ? 1 : point.y / start.y;
    if (!shift) return {value[0] * x, value[1] * y};
    const auto factor = axis == storyboard_gizmo_axis_t::y ? y : axis == storyboard_gizmo_axis_t::x ? x :
        std::abs(x - 1) >= std::abs(y - 1) ? x : y;
    return {value[0] * factor, value[1] * factor};
}
} // namespace

auto storyboard_gizmo_drag_t::from(osbx_transform_kind_t kind, const visual_affine_t& world_side,
    std::span<const double> value, visual_point_t pointer, storyboard_gizmo_axis_t axis)
    -> result_t<storyboard_gizmo_drag_t, failure_code_t> {
    if (kind == osbx_transform_kind_t::matrix || kind == osbx_transform_kind_t::skew)
        return std::unexpected(failure_code_t::unsupported);
    const auto inverse = world_side.inverse();
    const auto components = kind == osbx_transform_kind_t::rotate ? 1U : 2U;
    if (!inverse || !is_finite(pointer) || value.size() != components ||
        !std::ranges::all_of(value, [](double v) { return std::isfinite(v); }))
        return std::unexpected(failure_code_t::invalid_geometry);
    const auto start = inverse->apply(pointer);
    if (!is_finite(start)) return std::unexpected(failure_code_t::invalid_geometry);
    if (kind == osbx_transform_kind_t::rotate && start.x == 0 && start.y == 0)
        return std::unexpected(failure_code_t::invalid_geometry);
    if (kind == osbx_transform_kind_t::scale &&
        ((axis != storyboard_gizmo_axis_t::y && start.x == 0) ||
         (axis != storyboard_gizmo_axis_t::x && start.y == 0)))
        return std::unexpected(failure_code_t::invalid_geometry);
    storyboard_gizmo_drag_t result;
    result.kind_ = kind; result.axis_ = axis; result.inverse_ = *inverse;
    result.start_ = start; result.value_.assign(value.begin(), value.end());
    result.angle_ = std::atan2(start.y, start.x);
    return result;
}

auto storyboard_gizmo_drag_t::revise(visual_point_t pointer, bool shift)
    -> result_t<std::vector<double>, failure_code_t> {
    const auto point = inverse_.apply(pointer);
    if (!is_finite(pointer) || !is_finite(point)) return std::unexpected(failure_code_t::invalid_geometry);
    std::vector<double> result;
    if (kind_ == osbx_transform_kind_t::rotate) {
        if (point.x == 0 && point.y == 0) return std::unexpected(failure_code_t::invalid_geometry);
        const auto angle = std::atan2(point.y, point.x);
        turns_ += std::remainder(angle - angle_, 2 * std::numbers::pi);
        angle_ = angle;
        const auto degrees = turns_ * 180 / std::numbers::pi;
        result = {value_[0] + (shift ? std::round(degrees / 15) * 15 : degrees)};
    } else if (kind_ == osbx_transform_kind_t::scale) {
        result = scale_value(value_, start_, point, axis_, shift);
    } else {
        const auto delta = constrained_delta({point.x - start_.x, point.y - start_.y}, axis_, shift);
        result = {value_[0] + delta.x, value_[1] + delta.y};
    }
    if (!std::ranges::all_of(result, [](double v) { return std::isfinite(v); }))
        return std::unexpected(failure_code_t::arithmetic_overflow);
    if (kind_ == osbx_transform_kind_t::scale && (result[0] == 0 || result[1] == 0))
        return std::unexpected(failure_code_t::invalid_geometry);
    return result;
}
} // namespace wormedit
