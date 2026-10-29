#pragma once

// Small types the editor interaction headers share: gesture kinds, viewport limits, selection
// scope and objects, area geometry and paint cells.

#include <wormedit/core/position_types.h>
#include <wormedit/core/vocabulary.h>

#include <compare>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wormedit {

struct chart_document_t;
struct measure_index_t;
struct editor_interaction_setup_t;
struct editor_edit_context_t;
struct hold_start_t;
struct editor_interaction_result_t;

enum class editor_gesture_kind_t {
    hold, duration, move, paint, semantic_paint, area, range, resource_paint
};

[[nodiscard]] auto editor_bga_visible_lane_mask(const chart_document_t& chart,
                                               std::uint8_t revealed) -> std::uint8_t;

inline constexpr double minimum_pixels_per_standard_measure = 24.0;
inline constexpr double maximum_pixels_per_standard_measure = 4096.0;
inline constexpr double default_pixels_per_standard_measure = 192.0;
inline constexpr double editor_cursor_edge_margin_fraction = 0.2;

// Scrolling may run past either end of the chart by the edge margin, so the playback
// anchor (edge margin from the chart-start side) reaches both the first and last rows.
template <typename T>
[[nodiscard]] constexpr auto editor_maximum_viewport_scroll(T content_height, T viewport_height,
    T end_margin_fraction = static_cast<T>(editor_cursor_edge_margin_fraction))
    -> T {
    const auto maximum =
        content_height - viewport_height * end_margin_fraction;
    return maximum > T{} ? maximum : T{};
}

// Following may extend the chart-start margin to its validated anchor. The chart-end
// margin may shrink to that anchor; ordinary callers retain the original 20 percent.
[[nodiscard]] inline auto is_editor_viewport_margins_valid(double minimum_fraction,
                                                       double end_fraction) noexcept -> bool {
    return std::isfinite(minimum_fraction) && minimum_fraction >= editor_cursor_edge_margin_fraction &&
           minimum_fraction <= 1.0 && std::isfinite(end_fraction) && end_fraction >= 0.0 &&
           end_fraction <= editor_cursor_edge_margin_fraction;
}

struct note_paint_target_t {
    lane_id_t lane;
    local_position_t position;
    // Virtual cells retain an absolute row until the stroke materializes their measures.
    std::optional<std::uint64_t> canvas_row;

    [[nodiscard]] auto absolute_row(const measure_index_t& index) const
        -> std::optional<std::uint64_t>;

    auto operator==(const note_paint_target_t&) const -> bool = default;
};

enum class editor_area_selection_mode_t {
    replace,
    add,
    toggle,
};

// Track scope is deliberately explicit: the traditional lane-field gesture
// remains note-only unless the frontend opts into chart-event tracks.
struct editor_selection_scope_t {
    bool notes{true};
    bool bgm{};
    bool timing{};
    bool bga{};
    // Bar lines are selectable only where a measure lane shows them.
    bool bar_lines{};

    // Every object kind, bar lines included. Build other scopes with designated initializers;
    // positional construction silently leaves a newly added kind out.
    [[nodiscard]] static constexpr auto every_object() -> editor_selection_scope_t {
        return {.notes = true, .bgm = true, .timing = true, .bga = true, .bar_lines = true};
    }

    auto operator==(const editor_selection_scope_t&) const -> bool = default;
};

// IDs are only unique within a chart object family; retaining the family in
// selection state prevents collisions between (for example) note 17 and BGA 17.
struct editor_selection_object_t {
    editor_selection_object_kind_t kind{editor_selection_object_kind_t::note};
    object_id_t id;

    auto operator==(const editor_selection_object_t&) const -> bool = default;
    auto operator<=>(const editor_selection_object_t&) const = default;
};

// Frontend-projected, canonical-source-Y rectangles for non-note canvas objects.
struct editor_area_object_geometry_t {
    editor_selection_object_t object;
    double left_pixels{};
    double top_pixels{};
    double right_pixels{};
    double bottom_pixels{};
};

struct editor_area_geometry_projection_t {
    double viewport_scroll_pixels{};
    double pixels_per_standard_measure{};
    double viewport_height_pixels{};
};

struct editor_area_geometry_snapshot_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    std::vector<editor_area_object_geometry_t> objects;
    std::optional<editor_area_geometry_projection_t> projection;
};

struct paint_cell_key_t {
    lane_id_t lane;
    std::uint64_t absolute_row{};

    auto operator==(const paint_cell_key_t&) const -> bool = default;
};

struct paint_cell_hash_t {
    [[nodiscard]] auto operator()(paint_cell_key_t cell) const noexcept -> std::size_t;
};

inline constexpr std::size_t maximum_note_paint_targets = 50'000;

enum class editor_field_scroll_direction_t : std::uint8_t {
    up,
    down,
};

struct canvas_row_span_t {
    std::uint64_t begin_row{};
    std::uint64_t end_row{};

    auto operator==(const canvas_row_span_t&) const -> bool = default;
};

} // namespace wormedit
