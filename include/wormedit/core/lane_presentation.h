#pragma once
#include <wormedit/core/document.h>
#include <array>
#include <optional>
#include <vector>
#include <utility>
namespace wormedit {
// Shared field geometry for projection and picking.
[[nodiscard]] auto editor_note_head_bounds(double row_y, double height, lane_glyph_t glyph,
    bool downscroll = false) -> std::pair<double, double>;
[[nodiscard]] auto editor_presented_lane_bounds(const lane_layout_t& layout,
    std::size_t columns, std::size_t column, double left, double width, double gutter)
    -> std::pair<double, double>;
// Scratch visuals include playable osu SpecialStyle columns; authored roles stay intact.
[[nodiscard]] auto has_editor_lane_scratch_class(const lane_definition_t& lane) -> bool;

// View-only scratch sides: null retains authored ordering, false left, true right.
using editor_scratch_presentation_t = std::array<std::optional<bool>, 2>;
[[nodiscard]] auto is_editor_scratch_presentation_active(const lane_layout_t& layout,
    const editor_scratch_presentation_t& sides) -> bool;

[[nodiscard]] auto editor_presented_lanes(const lane_layout_t& layout,
    const editor_scratch_presentation_t& sides = {}) -> std::vector<const lane_definition_t*>;
[[nodiscard]] auto editor_presented_column(const lane_layout_t& layout, lane_id_t lane,
    const editor_scratch_presentation_t& sides) -> std::int64_t;
[[nodiscard]] auto editor_lane_across_presented_layouts(const lane_layout_t& source,
    const lane_layout_t& target, lane_id_t lane, const editor_scratch_presentation_t& sides)
    -> std::optional<lane_id_t>;
[[nodiscard]] auto editor_lane_at_presented_column(const lane_layout_t& layout, std::int64_t column,
    const editor_scratch_presentation_t& sides) -> const lane_definition_t*;
}
