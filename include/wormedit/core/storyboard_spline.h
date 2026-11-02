#pragma once

#include <wormedit/core/result.h>
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace wormedit {
struct storyboard_spline_key_t {
    std::int64_t time_ms{};
    std::array<double, 2> value{};
    auto operator==(const storyboard_spline_key_t&) const -> bool = default;
};
using storyboard_spline_result_t = result_t<std::vector<storyboard_spline_key_t>, error_t<failure_code_t>>;

// A cubic Bézier in value space, with uniform parameter progress over signed
// integer milliseconds. Rotation uses continuous degrees; scale uses factors.
// Baking certifies the component-space Euclidean error over each entire chord,
// using the Bézier control hull, and refuses an unachievable integer-time bound.
// Output is ordinary linear keys, with no persisted curve or runtime scheduler.
struct storyboard_spline_t {
    static constexpr std::size_t maximum_keys = 4096;
    static constexpr std::uint64_t maximum_duration_ms = 2'147'483'647;
    std::int64_t begin_ms{}, end_ms{1000};
    std::array<std::array<double, 2>, 4> points{};
    std::size_t dimensions{2};
    double tolerance{0.5};
    [[nodiscard]] auto bake(const std::function<bool()>& consume_work = {}) const -> storyboard_spline_result_t;
    auto operator==(const storyboard_spline_t&) const -> bool = default;
};
} // namespace wormedit
