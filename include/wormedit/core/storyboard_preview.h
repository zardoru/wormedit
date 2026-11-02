#pragma once

#include <wormedit/core/storyboard.h>
#include <wormedit/core/visual_layout.h>
#include <wormedit/core/osbx_document.h>

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

struct osbx_draw_instance_t;

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

struct storyboard_preview_sample_source_t {
    std::size_t sample_index{};
    storyboard_sample_t captured;
    storyboard_event_t event;
};

struct storyboard_preview_index_t {
    [[nodiscard]] static auto from(const storyboard_document_model_t& document,
        storyboard_schedule_options_t options = {})
        -> diagnosed_t<std::optional<storyboard_preview_index_t>, std::vector<storyboard_diagnostic_t>>;
    std::vector<storyboard_preview_source_t> sources;
    storyboard_schedule_t schedule;
    std::vector<storyboard_preview_sample_source_t> sample_sources;
    // Returned pointer borrows the supplied immutable document, never this
    // index's movable schedule. A changed model or event fails closed.
    [[nodiscard]] auto sample_for(const storyboard_document_model_t&, const storyboard_event_t&,
        storyboard_conditional_layer_t = storyboard_conditional_layer_t::pass) const -> const storyboard_sample_t*;
};

using storyboard_preview_index_outcome_t = diagnosed_t<std::optional<storyboard_preview_index_t>, std::vector<storyboard_diagnostic_t>>;

struct storyboard_draw_instance_t {
    [[nodiscard]] auto is_valid() const -> bool;
    [[nodiscard]] static auto from(const osbx_document_t& document, const osbx_node_t& node,
        const osbx_image_t& image) -> storyboard_draw_instance_t;
    [[nodiscard]] auto with_evaluation(const osbx_draw_instance_t& evaluated) const
        -> storyboard_draw_instance_t;
    // Animation frame paths borrow the supplied current frame arena. Static paths
    // continue to borrow the immutable source; no image catalog retains a borrow.
    [[nodiscard]] auto with_evaluation(const osbx_draw_instance_t& evaluated,
        storyboard_frame_arena_t& arena) const -> result_t<storyboard_draw_instance_t, failure_code_t>;
    // Legacy OSB canonical sprite index, validated with stable_id by media consumers.
    std::size_t document_order{};
    std::uint64_t stable_id{};
    std::uint64_t source_order{};
    std::uint8_t layer_order{};
    std::string_view layer;
    std::string_view origin;
    std::string_view path;
    visual_affine_t transform;
    double logical_width{640};
    double logical_height{480};
    std::optional<osbx_blend_t> blend; // Unset preserves the OSB texture blend state.
    double opacity{1.0};
    double red{255.0};
    double green{255.0};
    double blue{255.0};

    auto operator==(const storyboard_draw_instance_t&) const -> bool = default;
};

struct storyboard_frame_options_t {
    std::size_t maximum_instances{100'000};
    std::size_t maximum_nesting{128};
    storyboard_conditional_layer_t conditional_layer{storyboard_conditional_layer_t::pass};
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
// Selected Animation paths also borrow this arena until reset. Other string
// views borrow the immutable document/index, which must outlive the frame.
[[nodiscard]] auto evaluate_storyboard_frame(const storyboard_document_model_t& document,
                                             const storyboard_preview_index_t& index,
                                             std::int64_t time_ms, storyboard_frame_arena_t& arena,
                                             storyboard_frame_options_t options = {})
    -> storyboard_frame_effect_t;

[[nodiscard]] auto evaluate_storyboard_frame(const storyboard_document_model_t& document,
    const storyboard_preview_index_t& index, storyboard_time_t time,
    storyboard_frame_arena_t& arena, storyboard_frame_options_t options = {})
    -> storyboard_frame_effect_t;

} // namespace wormedit
