#pragma once

#include <wormedit/core/result.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace wormedit {

enum class storyboard_trigger_kind_t : std::uint8_t { hit_sound, passing, failing };

// Supported legacy trigger grammar only; no firing or gameplay inference.
struct storyboard_trigger_name_t {
    std::string value;
    [[nodiscard]] static auto from(std::string_view text) -> std::optional<storyboard_trigger_name_t>;
    [[nodiscard]] auto kind() const noexcept -> std::optional<storyboard_trigger_kind_t>;
    [[nodiscard]] auto is_valid() const noexcept -> bool { return kind().has_value(); }
    auto operator==(const storyboard_trigger_name_t&) const -> bool = default;
};

// Shared by preview, video and engine adapters. Neither hides both branches.
enum class storyboard_conditional_layer_t : std::uint8_t { pass, fail, neither };

[[nodiscard]] inline auto is_storyboard_layer_visible(std::string_view layer,
    storyboard_conditional_layer_t choice) noexcept -> bool {
    if (layer == "Pass") return choice == storyboard_conditional_layer_t::pass;
    if (layer == "Fail") return choice == storyboard_conditional_layer_t::fail;
    return true;
}

// Whole milliseconds plus a nonnegative fraction smaller than one millisecond.
// Keeping the integer part separate preserves adjacent keys near INT64_MAX.
struct storyboard_time_t {
    [[nodiscard]] static auto from_frame(std::uint64_t frame_index, std::uint32_t numerator,
        std::uint32_t denominator = 1) -> result_t<storyboard_time_t, failure_code_t>;
    std::int64_t milliseconds{};
    std::uint32_t remainder{};
    std::uint32_t denominator{1};
    [[nodiscard]] static auto is_before(storyboard_time_t a, storyboard_time_t b) noexcept -> bool {
        return a.milliseconds != b.milliseconds ? a.milliseconds < b.milliseconds :
            std::uint64_t{a.remainder} * b.denominator < std::uint64_t{b.remainder} * a.denominator;
    }
    [[nodiscard]] auto fraction() const -> double {
        return static_cast<double>(remainder) / denominator;
    }
    [[nodiscard]] auto is_valid() const -> bool {
        return denominator != 0 && remainder < denominator;
    }
    auto operator==(const storyboard_time_t&) const -> bool = default;
};

struct storyboard_trigger_firing_t {
    storyboard_trigger_name_t name;
    storyboard_time_t time;
    auto operator==(const storyboard_trigger_firing_t&) const -> bool = default;
};

// Sealed before frame evaluation. Queries borrow immutable owned events; the
// caller retains this value through all frames/op-space queries using it.
class storyboard_trigger_scenario_t {
  public:
    [[nodiscard]] static auto from(std::span<const storyboard_trigger_firing_t>,
        std::size_t maximum_firings = 250'000) -> result_t<storyboard_trigger_scenario_t, failure_code_t>;
    [[nodiscard]] auto latest(const storyboard_trigger_name_t&, std::optional<storyboard_time_t> begin,
        std::optional<storyboard_time_t> end, storyboard_time_t query) const noexcept -> std::optional<storyboard_time_t>;
    [[nodiscard]] auto size() const noexcept -> std::size_t { return firings_.size(); }
  private:
    std::vector<storyboard_trigger_firing_t> firings_;
};

struct storyboard_origin_entry_t;
struct storyboard_origin_t {
    [[nodiscard]] static auto catalog() -> std::span<const storyboard_origin_entry_t>;
    double x{0.5};
    double y{0.5};
    [[nodiscard]] static auto from(std::string_view name) -> std::optional<storyboard_origin_t>;
};

struct storyboard_origin_entry_t {
    std::string_view name;
    storyboard_origin_t value;
};

enum class osbx_node_kind_t { group, sprite, animation, sample };
enum class osbx_layer_t { background, fail, pass, foreground };
inline constexpr std::array<std::string_view, 4> osbx_layer_names{"Background", "Fail", "Pass", "Foreground"};
static_assert(osbx_layer_names.size() == static_cast<std::size_t>(osbx_layer_t::foreground) + 1);
enum class osbx_blend_t { normal, additive };
enum class osbx_transform_kind_t { translate, rotate, scale, skew, matrix, pivot };

struct osbx_step_easing_t {
    auto operator==(const osbx_step_easing_t&) const -> bool = default;
};
struct osbx_linear_easing_t {
    auto operator==(const osbx_linear_easing_t&) const -> bool = default;
};
struct osbx_osb_easing_t {
    std::int32_t value{};
    auto operator==(const osbx_osb_easing_t&) const -> bool = default;
};
struct osbx_cubic_bezier_t {
    double x1{};
    double y1{};
    double x2{1};
    double y2{1};
    auto operator==(const osbx_cubic_bezier_t&) const -> bool = default;
};
using osbx_easing_t = std::variant<osbx_step_easing_t, osbx_linear_easing_t,
                                 osbx_osb_easing_t, osbx_cubic_bezier_t>;

enum class osbx_track_kind_t { transform, opacity, color, visibility, blend };
struct osbx_track_target_t {
    std::string node;
    osbx_track_kind_t kind{osbx_track_kind_t::opacity};
    std::string op; // Required only for transform.
    auto operator==(const osbx_track_target_t&) const -> bool = default;
};
struct osbx_key_target_t {
    osbx_track_target_t track;
    std::string key;
    auto operator==(const osbx_key_target_t&) const -> bool = default;
};

// An inclusive envelope, separate from authored visibility keys. At end_ms,
// only the exact integer instant is included, never a positive fraction.
struct osbx_lifetime_t {
    std::int64_t begin_ms{}, end_ms{};
    [[nodiscard]] auto contains(storyboard_time_t time) const -> bool;
    auto operator==(const osbx_lifetime_t&) const -> bool = default;
};

} // namespace wormedit
