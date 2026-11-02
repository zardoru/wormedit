#include <wormedit/core/storyboard_spline.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace wormedit {
namespace {
using point_t = std::array<double, 2>;
auto duration(const storyboard_spline_t& curve) -> std::uint64_t {
    return static_cast<std::uint64_t>(curve.end_ms) - static_cast<std::uint64_t>(curve.begin_ms);
}
auto is_valid(const storyboard_spline_t& curve) -> bool {
    if (curve.begin_ms >= curve.end_ms || duration(curve) > curve.maximum_duration_ms ||
        curve.dimensions < 1 || curve.dimensions > 2 || !std::isfinite(curve.tolerance) || curve.tolerance <= 0)
        return false;
    for (const auto& point : curve.points)
        for (const auto component : point)
            if (!std::isfinite(component)) return false;
    return true;
}
// Reserve a conservative rounding allowance for both coordinates through at
// most 31 subdivision levels, including chord reconstruction. Subnormal
// arithmetic has an absolute floor instead of a purely relative error.
auto rounding_margin(const storyboard_spline_t& curve) -> double {
    double magnitude = 0;
    for (const auto& point : curve.points)
        for (std::size_t d = 0; d < curve.dimensions; ++d)
            magnitude = std::max(magnitude, std::abs(point[d]));
    constexpr double operations = 8192;
    return std::max(magnitude * (operations * std::numeric_limits<double>::epsilon()),
        operations * std::numeric_limits<double>::denorm_min());
}
auto interpolate(point_t a, point_t b, double progress) -> point_t {
    return {std::lerp(a[0], b[0], progress), std::lerp(a[1], b[1], progress)};
}
auto chord_error(const storyboard_spline_t& curve) -> double {
    double error = 0;
    for (std::size_t i = 1; i < 3; ++i) {
        const auto linear = interpolate(curve.points[0], curve.points[3], static_cast<double>(i) / 3);
        double distance = 0;
        for (std::size_t d = 0; d < curve.dimensions; ++d)
            distance = std::hypot(distance, curve.points[i][d] - linear[d]);
        if (!std::isfinite(distance)) return distance;
        error = std::max(error, distance);
    }
    return error;
}
auto split(const storyboard_spline_t& curve) -> std::pair<storyboard_spline_t, storyboard_spline_t> {
    const auto offset = duration(curve) / 2;
    const auto progress = static_cast<double>(offset) / static_cast<double>(duration(curve));
    const auto a = interpolate(curve.points[0], curve.points[1], progress);
    const auto b = interpolate(curve.points[1], curve.points[2], progress);
    const auto c = interpolate(curve.points[2], curve.points[3], progress);
    const auto d = interpolate(a, b, progress), e = interpolate(b, c, progress);
    const auto middle = interpolate(d, e, progress);
    auto left = curve, right = curve;
    left.end_ms = right.begin_ms = curve.begin_ms + static_cast<std::int64_t>(offset);
    left.points = {curve.points[0], a, d, middle};
    right.points = {middle, e, c, curve.points[3]};
    return {left, right};
}
} // namespace
auto storyboard_spline_t::bake(const std::function<bool()>& consume_work) const -> storyboard_spline_result_t {
    if (!is_valid(*this))
        return std::unexpected(error_t{failure_code_t::invalid_request, "Invalid Bézier points, dimensions, tolerance or time range."});
    const auto margin = rounding_margin(*this);
    if (tolerance <= margin)
        return std::unexpected(error_t{failure_code_t::invalid_request, "Bézier tolerance is below supported binary64 precision for these points."});
    std::vector<storyboard_spline_key_t> output;
    output.reserve(maximum_keys);
    output.push_back({begin_ms, points.front()});
    std::vector<storyboard_spline_t> pending;
    pending.reserve(32);
    pending.push_back(*this);
    while (!pending.empty()) {
        if (consume_work && !consume_work())
            return std::unexpected(error_t{failure_code_t::capacity_exceeded, "Bézier work cancelled or exhausted."});
        const auto curve = pending.back(); pending.pop_back();
        const auto error = chord_error(curve);
        if (!std::isfinite(error))
            return std::unexpected(error_t{failure_code_t::arithmetic_overflow, "Bézier control hull exceeds numeric capacity."});
        if (error <= tolerance - margin) {
            if (output.size() == maximum_keys)
                return std::unexpected(error_t{failure_code_t::capacity_exceeded, "Bézier exceeds 4096 baked keys."});
            output.push_back({curve.end_ms, curve.points.back()});
        } else {
            if (duration(curve) == 1)
                return std::unexpected(error_t{failure_code_t::capacity_exceeded, "Bézier tolerance cannot be met at integer milliseconds."});
            const auto [left, right] = split(curve);
            pending.push_back(right); pending.push_back(left);
        }
    }
    return output;
}
} // namespace wormedit
