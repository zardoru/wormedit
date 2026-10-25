#pragma once

#include <wormedit/core/export_cut_axis.h>

#include <cstdint>

namespace wormedit {

struct editor_interaction_state_t;
struct editor_interaction_view_model_t;

// Frame-local projection over the retained source-row axis. The state/view
// owns the cut map; callers keep this borrowed projection within that lifetime.
struct editor_axis_projection_t {
    const export_cut_axis_t* cut_axis{};
    long double pixels_per_row{};
    double viewport_scroll_pixels{};
    double pixels_per_standard_measure{};
    std::uint64_t divisions_per_standard_measure{};

    [[nodiscard]] static auto from(const editor_interaction_state_t& state, std::uint64_t divisions)
        -> editor_axis_projection_t;
    [[nodiscard]] static auto from(const editor_interaction_view_model_t& view, std::uint64_t divisions)
        -> editor_axis_projection_t;
    [[nodiscard]] auto y_for_row(long double source_row) const -> double;
    [[nodiscard]] auto row_at_y(long double source_y, bool before_seam = false) const -> long double;
    [[nodiscard]] auto content_height(long double source_rows) const -> double;
    [[nodiscard]] auto contains_row(long double source_row) const -> bool;
};

} // namespace wormedit
