#pragma once

#include <wormedit/core/storyboard.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <limits>
#include <algorithm>
#include <vector>

namespace wormedit {

// Immutable renderer-neutral index. Build it when the storyboard document
// changes, then evaluate it from either an interactive or offline transport.
struct storyboard_preview_source_t {
    [[nodiscard]] auto is_active(std::int64_t time_ms) const noexcept -> bool {
        return begin_time_ms <= time_ms && time_ms <= end_time_ms;
    }
    [[nodiscard]] auto has_sentinel_begin() const noexcept -> bool {
        return begin_time_ms == std::numeric_limits<std::int64_t>::max();
    }
    [[nodiscard]] auto sort_key() const { return std::tie(layer_order, source_order, sprite_index); }
    std::size_t sprite_index{};
    std::uint64_t stable_id{};
    std::int64_t begin_time_ms{};
    std::int64_t end_time_ms{};
    std::uint8_t layer_order{};
    std::uint64_t source_order{};
};

struct storyboard_preview_index_t {
    [[nodiscard]] static auto from(const storyboard_document_model_t& document,
        storyboard_schedule_options_t options = {})
        -> diagnosed_t<std::optional<storyboard_preview_index_t>, std::vector<storyboard_diagnostic_t>>;
    std::vector<storyboard_preview_source_t> sources;
};

using storyboard_preview_index_outcome_t = diagnosed_t<std::optional<storyboard_preview_index_t>, std::vector<storyboard_diagnostic_t>>;
struct storyboard_preview_index_result_t {
    [[nodiscard]] static auto from(storyboard_preview_index_outcome_t&& outcome) -> storyboard_preview_index_result_t;
    std::optional<storyboard_preview_index_t> index;
    std::vector<storyboard_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return index.has_value();
    }
};

[[nodiscard]] auto build_storyboard_preview_index(const storyboard_document_model_t& document,
                                                  storyboard_schedule_options_t options = {})
    -> storyboard_preview_index_result_t;

struct storyboard_draw_instance_t {
    std::uint64_t stable_id{};
    std::uint64_t source_order{};
    std::uint8_t layer_order{};
    std::string_view layer;
    std::string_view origin;
    std::string_view path;
    double x{};
    double y{};
    double scale_x{1.0};
    double scale_y{1.0};
    double rotation_radians{};
    double opacity{1.0};
    double red{255.0};
    double green{255.0};
    double blue{255.0};

    auto operator==(const storyboard_draw_instance_t&) const -> bool = default;
};

struct storyboard_frame_options_t {
    std::size_t maximum_instances{100'000};
    std::size_t maximum_nesting{128};
};

struct storyboard_frame_effect_t {
    [[nodiscard]] auto is_success() const -> bool { return !diagnostic && !arena_error; }
    // std::span has no default equality; compare the live borrowed view through its element values.
    [[nodiscard]] auto operator==(const storyboard_frame_effect_t& other) const -> bool {
        return std::ranges::equal(instances, other.instances) && diagnostic == other.diagnostic &&
            arena_error == other.arena_error;
    }
    std::span<const storyboard_draw_instance_t> instances;
    std::optional<storyboard_diagnostic_t> diagnostic;
    std::optional<failure_code_t> arena_error;
};


// Resets the supplied arena and returns draw data valid until its next reset.
// The document and index must outlive the returned string views.
[[nodiscard]] auto evaluate_storyboard_frame(const storyboard_document_model_t& document,
                                             const storyboard_preview_index_t& index,
                                             std::int64_t time_ms, storyboard_frame_arena_t& arena,
                                             storyboard_frame_options_t options = {})
    -> storyboard_frame_effect_t;

} // namespace wormedit
