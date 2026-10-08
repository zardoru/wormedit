#include "sys/compose_quantize_preview.h"

#include <wormedit/core/quantize.h>

#include "core/quantize_geometry.h"

#include <algorithm>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit::sys {
namespace {

struct moved_note_positions_t {
    std::optional<local_position_t> start;
    std::optional<local_position_t> end;
};

struct note_report_candidate_t {
    const note_t* note{};
    local_position_t after_start;
    std::optional<local_position_t> after_end;
    std::optional<std::uint64_t> before_row;
    std::optional<std::uint64_t> after_row;
    std::optional<std::uint64_t> before_end_row;
    std::optional<std::uint64_t> after_end_row;
    bool selected{};
    bool moved{};
    bool collision{};
    bool head_halfway{};
    bool tail_halfway{};
    bool stationary_tail_annotation{};
    std::optional<object_id_t> collision_peer;
};

auto multiply_checked(std::uint64_t left, std::uint64_t right)
    -> std::optional<std::uint64_t> {
    if (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right)
        return std::nullopt;
    return left * right;
}

auto scaled_absolute(const detail::quantize_geometry_t& geometry, local_position_t position,
                     std::uint64_t scale) -> std::optional<std::uint64_t> {
    const auto* entry = geometry.entry(position.measure);
    const auto capacity = entry ? multiply_checked(entry->row_capacity, scale) : std::nullopt;
    const auto begin = entry ? multiply_checked(entry->absolute_begin_row, scale) : std::nullopt;
    if (!entry || !capacity || !begin || position.row >= *capacity ||
        position.row > std::numeric_limits<std::uint64_t>::max() - *begin)
        return std::nullopt;
    return *begin + position.row;
}

auto note_moves(const quantization_result_t& result)
    -> std::unordered_map<std::uint64_t, moved_note_positions_t> {
    std::unordered_map<std::uint64_t, moved_note_positions_t> moves;
    for (const auto& move : result.quantized_moves) {
        auto& note = moves[move.object.value];
        if (move.role == position_role_t::point) note.start = move.after;
        if (move.role == position_role_t::note_end) note.end = move.after;
    }
    return moves;
}

auto halfway_position(const detail::quantize_geometry_t& geometry, local_position_t position,
                      const quantize_request_t& request, std::uint64_t resolution,
                      std::uint64_t scale) -> bool;

auto scaled_local(local_position_t position, std::uint64_t scale)
    -> std::optional<local_position_t> {
    const auto row = multiply_checked(position.row, scale);
    if (!row) return std::nullopt;
    position.row = *row;
    return position;
}

auto note_candidate(const note_t& note, const detail::quantize_geometry_t& index,
                   const std::unordered_map<std::uint64_t, moved_note_positions_t>& moves,
                   const std::unordered_set<std::uint64_t>& selected,
                   const quantize_request_t& request, std::uint64_t resolution,
                   std::uint64_t scale) -> note_report_candidate_t {
    const auto found = moves.find(note.id.value);
    const auto moved = found != moves.end();
    const auto base_after_start = moved && found->second.start
        ? std::optional{*found->second.start} : scaled_local(note.position, scale);
    const auto after_start = base_after_start.value_or(note.position);
    const auto after_end = note.end && moved && found->second.end
        ? found->second.end : (note.end ? scaled_local(*note.end, scale) : std::nullopt);
    const auto is_selected = selected.contains(note.id.value);
    const auto head_halfway = is_selected && halfway_position(
        index, note.position, request, resolution, scale);
    const auto tail_halfway = is_selected && note.end && halfway_position(
        index, *note.end, request, resolution, scale);
    return {
        .note = &note,
        .after_start = after_start,
        .after_end = after_end,
        .before_row = index.absolute(note.position),
        .after_row = base_after_start ? scaled_absolute(index, after_start, scale)
                                      : std::nullopt,
        .before_end_row = note.end ? index.absolute(*note.end) : std::nullopt,
        .after_end_row = after_end ? scaled_absolute(index, *after_end, scale)
                                   : std::nullopt,
        .selected = is_selected,
        .moved = moved,
        .head_halfway = head_halfway,
        .tail_halfway = tail_halfway,
        .stationary_tail_annotation = note.end.has_value() &&
            request.holds == quantization_hold_t::start_only && is_selected,
    };
}

auto note_candidates(const chart_document_t& chart, const detail::quantize_geometry_t& index,
                     const std::unordered_map<std::uint64_t, moved_note_positions_t>& moves,
                     const std::unordered_set<std::uint64_t>& selected,
                     const quantize_request_t& request, std::uint64_t resolution,
                     std::uint64_t scale)
    -> std::vector<note_report_candidate_t> {
    std::vector<note_report_candidate_t> result;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            result.push_back(note_candidate(note, index, moves, selected, request,
                                            resolution, scale));
    }
    return result;
}

auto note_position_key(std::uint64_t row, lane_id_t lane) -> std::pair<std::uint64_t, std::uint64_t> {
    return {row, lane.value};
}

void mark_collision_group(std::vector<note_report_candidate_t>& candidates,
                          const std::vector<std::size_t>& group) {
    if (group.size() < 2) return;
    const auto first = group.front();
    const auto first_key = candidates[first].before_row
        ? note_position_key(*candidates[first].before_row, candidates[first].note->lane)
        : std::pair<std::uint64_t, std::uint64_t>{};
    const auto different = std::ranges::find_if(group, [&](const auto index) {
        const auto& candidate = candidates[index];
        return !candidate.before_row ||
            note_position_key(*candidate.before_row, candidate.note->lane) != first_key;
    });
    if (different == group.end()) return;
    const auto second_key = candidates[*different].before_row
        ? note_position_key(*candidates[*different].before_row, candidates[*different].note->lane)
        : std::pair<std::uint64_t, std::uint64_t>{};
    for (const auto index : group) {
        auto& candidate = candidates[index];
        candidate.collision = true;
        const auto old_key = candidate.before_row
            ? note_position_key(*candidate.before_row, candidate.note->lane)
            : std::pair<std::uint64_t, std::uint64_t>{};
        const auto peer = old_key == first_key ? *different : first;
        candidate.collision_peer = candidates[peer].note->id;
        if (old_key != first_key && old_key != second_key)
            candidate.collision_peer = candidates[first].note->id;
    }
}

void mark_note_collisions(std::vector<note_report_candidate_t>& candidates) {
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<std::size_t>> groups;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        if (candidate.after_row)
            groups[note_position_key(*candidate.after_row, candidate.note->lane)].push_back(index);
    }
    for (const auto& [key, group] : groups) {
        (void)key;
        mark_collision_group(candidates, group);
    }
}

auto report_row(const note_report_candidate_t& candidate) -> quantize_report_row_t {
    return {
        .object = candidate.note->id,
        .lane = candidate.note->lane,
        .start_before_absolute_row = candidate.before_row.value_or(0),
        .start_after_absolute_row = candidate.after_row.value_or(0),
        .end_before_absolute_row = candidate.before_end_row,
        .end_after_absolute_row = candidate.after_end_row,
        .start_before = candidate.note->position,
        .start_after = candidate.after_start,
        .end_before = candidate.note->end,
        .end_after = candidate.after_end,
        .moved = candidate.moved,
        .collision = candidate.collision,
        .collision_peer = candidate.collision_peer,
        .selected = candidate.selected,
        .halfway = candidate.head_halfway || candidate.tail_halfway,
        .head_halfway = candidate.head_halfway,
        .tail_halfway = candidate.tail_halfway,
        .stationary_tail_annotation = candidate.stationary_tail_annotation,
    };
}

auto report_order(const quantize_report_row_t& left, const quantize_report_row_t& right)
    -> bool {
    if (left.start_after_absolute_row != right.start_after_absolute_row)
        return left.start_after_absolute_row < right.start_after_absolute_row;
    if (left.lane != right.lane) return left.lane.value < right.lane.value;
    return left.object.value < right.object.value;
}

void append_report_rows(quantize_preview_t& preview,
                        const std::vector<note_report_candidate_t>& candidates) {
    for (const auto& candidate : candidates) {
        if (!candidate.moved && !candidate.collision &&
            !(preview.drawable && candidate.stationary_tail_annotation)) continue;
        ++preview.report_total_rows;
        if (candidate.collision) ++preview.colliding_objects;
        if (preview.report_rows.size() < quantize_report_row_capacity)
            preview.report_rows.push_back(report_row(candidate));
    }
    preview.report_truncated = preview.report_total_rows > preview.report_rows.size();
    std::ranges::sort(preview.report_rows, report_order);
}

auto same_request_settings(const quantize_request_t& left, const quantize_request_t& right)
    -> bool {
    return left.divisions == right.divisions && left.space == right.space &&
        left.tie == right.tie && left.collisions == right.collisions &&
        left.holds == right.holds &&
        left.allow_resolution_promotion == right.allow_resolution_promotion;
}

auto halfway_position(const detail::quantize_geometry_t& geometry, local_position_t position,
                      const quantize_request_t& request, std::uint64_t resolution,
                      std::uint64_t scale) -> bool {
    const auto* entry = geometry.entry(position.measure);
    const auto absolute = geometry.absolute(position);
    if (!entry || !absolute || request.divisions == 0)
        return false;
    const auto authored = request.space == quantization_space_t::authored_measure;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (scale == 0 || entry->row_capacity > maximum / scale || *absolute > maximum / scale)
        return false;
    const auto capacity = authored ? entry->row_capacity * scale : resolution;
    if (capacity % request.divisions != 0)
        return false;
    const auto step = capacity / request.divisions;
    if (step == 0 || step % 2 != 0)
        return false;
    const auto coordinate = (authored ? position.row : *absolute) * scale;
    return coordinate % step == step / 2;
}

void append_stationary_tail_row(quantize_preview_t& preview,
                                const detail::quantize_geometry_t& geometry,
                                const note_t& note, bool head_halfway, bool tail_halfway,
                                std::uint64_t scale) {
    if (!note.end) return;
    const auto after_start = scaled_local(note.position, scale);
    const auto after_end = scaled_local(*note.end, scale);
    const auto before_row = geometry.absolute(note.position);
    const auto after_row = after_start ? scaled_absolute(geometry, *after_start, scale)
                                       : std::nullopt;
    const auto before_end_row = geometry.absolute(*note.end);
    const auto after_end_row = after_end ? scaled_absolute(geometry, *after_end, scale)
                                         : std::nullopt;
    if (!after_start || !after_end || !before_row || !after_row || !before_end_row ||
        !after_end_row)
        return;
    ++preview.report_total_rows;
    if (preview.report_rows.size() < quantize_report_row_capacity) {
        preview.report_rows.push_back({
            .object = note.id,
            .lane = note.lane,
            .start_before_absolute_row = *before_row,
            .start_after_absolute_row = *after_row,
            .end_before_absolute_row = *before_end_row,
            .end_after_absolute_row = *after_end_row,
            .start_before = note.position,
            .start_after = *after_start,
            .end_before = note.end,
            .end_after = *after_end,
            .moved = false,
            .collision = false,
            .selected = true,
            .halfway = head_halfway || tail_halfway,
            .head_halfway = head_halfway,
            .tail_halfway = tail_halfway,
            .stationary_tail_annotation = true,
        });
    }
}

void count_selected_note(quantize_preview_t& preview, const note_t& note,
                         std::unordered_set<std::uint64_t>& selected,
                         const detail::quantize_geometry_t& geometry,
                         const quantize_request_t& request, std::uint64_t resolution,
                         std::uint64_t scale, bool retain_stationary_tails) {
    if (selected.erase(note.id.value) == 0) return;
    if (note.end) ++preview.holds;
    const auto head = halfway_position(geometry, note.position, request, resolution, scale);
    const auto tail = note.end && halfway_position(geometry, *note.end, request,
                                                    resolution, scale);
    if (head || (tail && request.holds == quantization_hold_t::both_endpoints))
        ++preview.halfway_notes;
    if (retain_stationary_tails && note.end)
        append_stationary_tail_row(preview, geometry, note, head, tail, scale);
}

void populate_selection_counts(quantize_preview_t& preview, const chart_document_t& chart,
                               const detail::quantize_geometry_t& geometry,
                               const quantize_request_t& request,
                               const quantization_result_t& result, std::uint64_t scale) {
    std::unordered_set<std::uint64_t> selected;
    for (const auto id : request.objects)
        selected.insert(id.value);
    if (selected.empty())
        return;
    const auto resolution = result.promoted_to.value_or(chart.divisions_per_standard_measure);
    const auto retain_stationary_tails = result.chart.has_value() &&
        result.quantized_moves.empty() && request.holds == quantization_hold_t::start_only;
    for (const auto& measure : chart.measures) {
        if (selected.empty()) break;
        for (const auto& note : measure.notes) {
            count_selected_note(preview, note, selected, geometry, request, resolution,
                                scale, retain_stationary_tails);
            if (selected.empty()) break;
        }
    }
    preview.report_truncated = preview.report_total_rows > preview.report_rows.size();
    std::ranges::sort(preview.report_rows, report_order);
}

auto selected_collision_note_ids(const std::vector<note_report_candidate_t>& candidates)
    -> std::unordered_set<std::uint64_t> {
    std::set<std::pair<std::uint64_t, std::uint64_t>> selected_collision_keys;
    for (const auto& candidate : candidates) {
        if (candidate.selected && candidate.collision && candidate.after_row)
            selected_collision_keys.insert(
                note_position_key(*candidate.after_row, candidate.note->lane));
    }
    std::unordered_set<std::uint64_t> result;
    for (const auto& candidate : candidates) {
        if (!candidate.collision || !candidate.after_row) continue;
        if (selected_collision_keys.contains(
                note_position_key(*candidate.after_row, candidate.note->lane)))
            result.insert(candidate.note->id.value);
    }
    return result;
}

auto collision_only_failure(const quantization_result_t& result,
                            const std::unordered_set<std::uint64_t>& eligible_note_ids,
                            const std::unordered_set<std::uint64_t>& layout_ids) -> bool {
    return !result.chart && !result.diagnostics.empty() &&
        std::ranges::all_of(result.diagnostics, [&](const auto& diagnostic) {
            return diagnostic.code == diagnostic_code_t::collision && diagnostic.object &&
                eligible_note_ids.contains(diagnostic.object->value) &&
                !layout_ids.contains(diagnostic.object->value);
        });
}

auto candidates_have_bounded_geometry(
    const std::vector<note_report_candidate_t>& candidates) -> bool {
    return std::ranges::all_of(candidates, [](const auto& candidate) {
        return candidate.before_row && candidate.after_row &&
            (!candidate.note->end ||
             (candidate.before_end_row && candidate.after_end_row));
    });
}

void populate_status(quantize_preview_t& preview, const quantization_result_t& result) {
    std::unordered_set<std::uint64_t> moved_ids;
    for (const auto& move : result.quantized_moves) moved_ids.insert(move.object.value);
    preview.moved = moved_ids.size();
    preview.unchanged = preview.selected > preview.moved ? preview.selected - preview.moved : 0;
    preview.blocked = result.chart ? 0 : std::max<std::size_t>(result.diagnostics.size(), 1);
    if (!result.chart && !result.diagnostics.empty())
        preview.message = result.diagnostics.front().message;
}

void set_report_resolution(quantize_preview_t& preview, const chart_document_t& chart,
                           const quantization_result_t& result) {
    preview.source_resolution = chart.divisions_per_standard_measure;
    preview.display_resolution = result.promoted_to.value_or(chart.divisions_per_standard_measure);
    if (chart.divisions_per_standard_measure != 0 &&
        preview.display_resolution % chart.divisions_per_standard_measure == 0) {
        preview.resolution_scale = preview.display_resolution / chart.divisions_per_standard_measure;
    } else {
        preview.resolution_scale = 0;
    }
}

auto moved_report_is_drawable(const quantize_preview_t& preview,
                              const quantize_request_t& request,
                              const quantization_result_t& result,
                              const std::vector<note_report_candidate_t>& candidates,
                              const chart_document_t& chart) -> bool {
    if (!candidates_have_bounded_geometry(candidates)) return false;
    if (result.chart) return true;
    if (request.tie == quantization_tie_t::reject && preview.halfway_notes != 0) return false;
    const auto eligible_collision_ids = selected_collision_note_ids(candidates);
    std::unordered_set<std::uint64_t> layout_ids;
    for (const auto& layout : chart.lane_layouts) layout_ids.insert(layout.id.value);
    return collision_only_failure(result, eligible_collision_ids, layout_ids);
}

void populate_moved_report(quantize_preview_t& preview, const chart_document_t& chart,
                           const std::vector<object_id_t>& selected,
                           const quantize_request_t& request,
                           const quantization_result_t& result,
                           const detail::quantize_geometry_t& geometry) {
    const auto moves = note_moves(result);
    if (moves.empty()) {
        preview.geometry_valid = result.chart.has_value() && preview.resolution_scale != 0;
        preview.drawable = preview.geometry_valid;
        return;
    }
    if (preview.resolution_scale == 0) return;
    std::unordered_set<std::uint64_t> selected_ids;
    for (const auto id : selected) selected_ids.insert(id.value);
    auto candidates = note_candidates(chart, geometry, moves, selected_ids, request,
                                      preview.display_resolution, preview.resolution_scale);
    mark_note_collisions(candidates);
    preview.geometry_valid = moved_report_is_drawable(preview, request, result,
                                                       candidates, chart);
    preview.drawable = preview.geometry_valid;
    append_report_rows(preview, candidates);
}

void populate_geometry_report(quantize_preview_t& preview, const chart_document_t& chart,
                              const std::vector<object_id_t>& selected,
                              const quantize_request_t& request,
                              const quantization_result_t& result,
                              const detail::quantize_geometry_t& geometry) {
    if (preview.resolution_scale == 0) return;
    populate_selection_counts(preview, chart, geometry, request, result,
                              preview.resolution_scale);
    populate_moved_report(preview, chart, selected, request, result, geometry);
}

} // namespace

auto preview_quantize_selection(const chart_document_t& chart,
                                const std::vector<object_id_t>& selected,
                                std::uint64_t snap_denominator) -> quantize_preview_t {
    return preview_quantize_selection(chart, selected,
                                      quantize_request_t{.divisions = snap_denominator});
}

auto preview_quantize_selection(const chart_document_t& chart,
                                const std::vector<object_id_t>& selected,
                                quantize_request_t request) -> quantize_preview_t {
    quantize_preview_t preview{.selected = selected.size()};
    if (selected.empty()) {
        preview.message = "Select notes to preview quantization.";
        return preview;
    }
    request.objects = selected;
    const auto result = quantize_chart_exact(chart, request);
    populate_status(preview, result);
    set_report_resolution(preview, chart, result);

    const auto built_index = build_measure_index(chart);
    if (std::holds_alternative<measure_index_t>(built_index)) {
        const detail::quantize_geometry_t geometry{std::get<measure_index_t>(built_index)};
        populate_geometry_report(preview, chart, selected, request, result, geometry);
    }
    return preview;
}

auto quantize_preview_cache_t::get(const chart_document_t& chart, std::uint64_t revision,
                                   const std::vector<object_id_t>& selected,
                                   std::uint64_t snap_denominator) -> const quantize_preview_t& {
    return get(chart, revision, selected,
               quantize_request_t{.divisions = snap_denominator});
}

auto quantize_preview_cache_t::get(const chart_document_t& chart, std::uint64_t revision,
                                   const std::vector<object_id_t>& selected,
                                   const quantize_request_t& request) -> const quantize_preview_t& {
    if (!value_ || source_ != &chart || chart_ != chart.id || revision_ != revision ||
        selected_ != selected || !same_request_settings(request_, request)) {
        value_ = preview_quantize_selection(chart, selected, request);
        source_ = &chart;
        chart_ = chart.id;
        revision_ = revision;
        selected_ = selected;
        request_ = request;
        request_.objects.clear();
    }
    return *value_;
}

} // namespace wormedit::sys
