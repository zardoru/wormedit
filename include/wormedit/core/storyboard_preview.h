#pragma once

#include <wormedit/core/storyboard.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace wormedit {

// Immutable renderer-neutral index. Build it when the storyboard document
// changes, then evaluate it from either an interactive or offline transport.
struct storyboard_preview_source_t {
    std::size_t sprite_index{};
    std::uint64_t stable_id{};
    std::int64_t begin_time_ms{};
    std::int64_t end_time_ms{};
    std::uint8_t layer_order{};
    std::uint64_t source_order{};
};

struct storyboard_preview_index_t {
    std::vector<storyboard_preview_source_t> sources;
};

struct storyboard_preview_index_result_t {
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
};

struct storyboard_frame_options_t {
    std::size_t maximum_instances{100'000};
    std::size_t maximum_nesting{128};
};

struct storyboard_frame_result_t {
    std::span<const storyboard_draw_instance_t> instances;
    std::optional<storyboard_diagnostic_t> diagnostic;
    storyboard_arena_error_t arena_error{storyboard_arena_error_t::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return !diagnostic.has_value() && arena_error == storyboard_arena_error_t::none;
    }
};

// Resets the supplied arena and returns draw data valid until its next reset.
// The document and index must outlive the returned string views.
[[nodiscard]] auto evaluate_storyboard_frame(const storyboard_document_model_t& document,
                                             const storyboard_preview_index_t& index,
                                             std::int64_t time_ms, storyboard_frame_arena_t& arena,
                                             storyboard_frame_options_t options = {})
    -> storyboard_frame_result_t;

} // namespace wormedit
