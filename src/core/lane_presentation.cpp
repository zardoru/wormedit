#include <wormedit/core/lane_presentation.h>
#include <algorithm>
#include <tuple>
#include <unordered_map>
namespace wormedit {
auto editor_note_head_bounds(double row_y, double height, lane_glyph_t glyph,
                             bool downscroll) -> std::pair<double, double> {
    if (glyph != lane_glyph_t::bar && glyph != lane_glyph_t::square)
        return {row_y - height / 2.0, row_y + height / 2.0};
    return downscroll ? std::pair{row_y - height, row_y} : std::pair{row_y, row_y + height};
}
auto editor_presented_lane_bounds(const lane_layout_t& layout, std::size_t columns,
    std::size_t column, double left, double width, double gutter) -> std::pair<double, double> {
    const auto split = layout.split_stage && layout.split_after_column
        ? std::min<std::size_t>(*layout.split_after_column, columns) : columns;
    const auto gap = split > 0 && split < columns ? gutter : 0.0;
    const auto lane_width = (width - gap) / static_cast<double>(std::max<std::size_t>(1, columns));
    const auto begin = left + lane_width * column + (column >= split ? gap : 0.0);
    return {begin, begin + lane_width};
}
auto editor_lane_has_scratch_class(const lane_definition_t& lane) -> bool {
    return lane.role == lane_role_t::scratch ||
        (lane.role == lane_role_t::playable && lane.key_color == lane_key_color_t::scratch);
}
namespace {
auto editable(const lane_definition_t& lane) -> bool {
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible &&
           lane.role != lane_role_t::mine;
}
auto side(const lane_definition_t& lane, const editor_scratch_presentation_t& sides)
    -> std::optional<bool> {
    return sides[lane.stage == stage_group_t::right ? 1 : 0];
}
auto category(const lane_definition_t& lane, const editor_scratch_presentation_t& sides) -> int {
    const auto placement = side(lane, sides);
    if (!editor_lane_has_scratch_class(lane) || !placement) return 0;
    return *placement ? 1 : -1;
}
}
auto editor_scratch_presentation_active(const lane_layout_t& layout,
                                         const editor_scratch_presentation_t& sides) -> bool {
    return std::ranges::any_of(layout.lanes, [&](const auto& lane) {
        return editor_lane_has_scratch_class(lane) && side(lane, sides).has_value();
    });
}
auto editor_presented_lanes(const lane_layout_t& layout, const editor_scratch_presentation_t& sides)
    -> std::vector<const lane_definition_t*> {
    std::vector<const lane_definition_t*> lanes;
    for (const auto& lane : layout.lanes) if (editable(lane)) lanes.push_back(&lane);
    const auto has_stage = [&](stage_group_t stage) {
        return std::ranges::any_of(lanes, [stage](const auto* lane) { return lane->stage == stage; });
    };
    // The gap flag is independent of semantic stage identity (BMSON beat-DP has no gap).
    const auto grouped = layout.split_stage || (has_stage(stage_group_t::left) && has_stage(stage_group_t::right));
    std::ranges::sort(lanes, [&](const auto* left, const auto* right) {
        if (grouped && left->stage != right->stage) return left->stage < right->stage;
        return std::tuple{category(*left, sides), left->display_order, left->id.value} <
               std::tuple{category(*right, sides), right->display_order, right->id.value};
    });
    return lanes;
}
auto editor_presented_column(const lane_layout_t& layout, lane_id_t lane,
                              const editor_scratch_presentation_t& sides) -> std::int64_t {
    if (!editor_scratch_presentation_active(layout, sides)) {
        const auto found = std::ranges::find(layout.lanes, lane, &lane_definition_t::id);
        return found != layout.lanes.end() && editable(*found)
            ? static_cast<std::int64_t>(found->display_order) : -1;
    }
    const auto lanes = editor_presented_lanes(layout, sides);
    const auto found = std::ranges::find(lanes, lane, [](const auto* item) { return item->id; });
    if (found == lanes.end()) return -1;
    return std::distance(lanes.begin(), found);
}
auto editor_lane_across_presented_layouts(const lane_layout_t& source, const lane_layout_t& target,
                                          lane_id_t lane, const editor_scratch_presentation_t& sides)
    -> std::optional<lane_id_t> {
    const auto original = std::ranges::find(source.lanes, lane, &lane_definition_t::id);
    if (original == source.lanes.end()) return std::nullopt;
    const auto source_column = editor_presented_column(source, lane, sides);
    std::unordered_map<std::uint64_t, std::int64_t> columns;
    if (editor_scratch_presentation_active(target, sides)) {
        const auto ordered = editor_presented_lanes(target, sides);
        for (std::size_t index = 0; index < ordered.size(); ++index)
            columns.emplace(ordered[index]->id.value, index);
    }
    const auto column = [&](const auto& candidate) -> std::int64_t {
        return columns.empty() ? candidate.display_order : columns.at(candidate.id.value);
    };
    const lane_definition_t* same_column{};
    const lane_definition_t* same_role{};
    const lane_definition_t* first{};
    for (const auto& candidate : target.lanes) {
        if (!editable(candidate)) continue;
        if (!first || column(candidate) < column(*first)) first = &candidate;
        if (!same_role && candidate.role == original->role) same_role = &candidate;
        if (column(candidate) != source_column) continue;
        if (candidate.role == original->role) return candidate.id;
        if (!same_column) same_column = &candidate;
    }
    const auto* chosen = same_column ? same_column : same_role ? same_role : first;
    return chosen ? std::optional{chosen->id} : std::nullopt;
}
auto editor_lane_at_presented_column(const lane_layout_t& layout, std::int64_t column,
                                     const editor_scratch_presentation_t& sides)
    -> const lane_definition_t* {
    if (!editor_scratch_presentation_active(layout, sides)) {
        const auto found = std::ranges::find_if(layout.lanes, [&](const auto& lane) {
            return editable(lane) && static_cast<std::int64_t>(lane.display_order) == column;
        });
        return found == layout.lanes.end() ? nullptr : &*found;
    }
    const auto lanes = editor_presented_lanes(layout, sides);
    return column >= 0 && static_cast<std::size_t>(column) < lanes.size() ? lanes[column] : nullptr;
}
}
