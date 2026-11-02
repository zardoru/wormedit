#pragma once

#include <array>
#include <functional>
#include <span>
#include <vector>
#include <wormedit/core/result.h>

namespace wormedit {
// Adapted from storybrew common/Animations/KeyframedValue.cs, MIT, Damnae,
// commit 15367cc7dd9d556f2b3113cc196afea99778a760. Unlike upstream we use
// doubles, an explicit RDP stack and retain both ends of trailing equal runs.
struct simplify_key_t {
    double time_ms{};
    std::array<double, 3> value{};
    auto operator==(const simplify_key_t&) const -> bool = default;
};
using simplify_projection_t = std::function<std::array<double, 3>(const simplify_key_t&)>;
using simplify_result_t = result_t<std::vector<std::size_t>, error_t<failure_code_t>>;
struct simplify_limits_t {
    static constexpr std::size_t maximum_input_keys = 65'536;
    std::size_t maximum_work{8'000'000};
    // Called for each unit of native work, including validation and metric scans.
    // Return false for cancellation or an exhausted caller budget. Must not throw.
    std::function<bool()> consume_work;
};
// Strictly increasing finite times and finite projected values are required.
// Results index the original input, in stable order; ties keep the first maximum.
[[nodiscard]] auto simplify_equal_runs(std::span<const simplify_key_t>, simplify_limits_t = {})
    -> simplify_result_t;
[[nodiscard]] auto simplify_rdp_1d(std::span<const simplify_key_t>, double tolerance,
                                   const simplify_projection_t&, simplify_limits_t = {})
    -> simplify_result_t;
[[nodiscard]] auto simplify_rdp_2d(std::span<const simplify_key_t>, double tolerance,
                                   const simplify_projection_t&, simplify_limits_t = {})
    -> simplify_result_t;
[[nodiscard]] auto simplify_rdp_3d(std::span<const simplify_key_t>, double tolerance,
                                   const simplify_projection_t&, simplify_limits_t = {})
    -> simplify_result_t;

// CommandGenerator.cs at the same upstream commit. Scale projection is scale *
// image size in pixels; rotation is radians, color is 0..255. Generic RDP never
// rounds. The emitter owns decimal serialization and uses these same defaults.
struct simplify_emit_policy_t {
    double tolerance;
    int decimals;
    auto operator==(const simplify_emit_policy_t&) const -> bool = default;
};
inline constexpr simplify_emit_policy_t simplify_position{1, 1};
inline constexpr simplify_emit_policy_t simplify_scale{0.1, 2};
inline constexpr simplify_emit_policy_t simplify_rotation{0.001, 3};
inline constexpr simplify_emit_policy_t simplify_color{2, 0};
inline constexpr simplify_emit_policy_t simplify_opacity{0.01, 2};
inline constexpr double simplify_corner_error_px = 0.5;
inline constexpr double simplify_opacity_error = 1.0 / 255;
inline constexpr std::size_t simplify_keys_per_100ms = 4;
// Decimal ties round to even, as upstream Math.Round. Invalid decimal counts
// (outside 0..15) return NaN, rejected by metric verification.
[[nodiscard]] auto round_simplify_value(double, int decimals) -> double;
struct simplify_metric_t {
    std::size_t dimensions{1};
    double tolerance{simplify_position.tolerance};
    simplify_projection_t project;
    // Produce exactly the values that OSB decimal serialization reconstructs.
    // Called once per dense key, never on an already rounded value.
    simplify_projection_t emit;
    // Normalized acceptance error (<=1). The bake owner measures transformed
    // corners / simplify_corner_error_px and opacity / simplify_opacity_error.
    // This evaluates the original dense sample against rounded reconstruction.
    std::function<double(const simplify_key_t&, const std::array<double, 3>&)> error;
};
// RDP followed by bounded worst-key reinsertion. Checks all dense samples and
// retained emitted endpoints. A half-open rolling 100ms window permits four
// keys. Rejects impossible rounding, density and work budgets explicitly.
// Returned indices reference unrounded input; round only when emitting. This
// verifies a dense sampled track, not unsampled extrema of its source function.
[[nodiscard]] auto simplify_to_metric(std::span<const simplify_key_t>, const simplify_metric_t&,
                                      simplify_limits_t = {}) -> simplify_result_t;
} // namespace wormedit
