#include "sys/compose_quantize_preview.h"

#include <wormedit/core/quantize.h>

#include <algorithm>
#include <map>
#include <ranges>
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
    bool moved{};
    bool collision{};
    std::optional<object_id_t> collision_peer;
};

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

auto note_candidates(const chart_document_t& chart, const measure_index_t& index,
                     const std::unordered_map<std::uint64_t, moved_note_positions_t>& moves)
    -> std::vector<note_report_candidate_t> {
    std::vector<note_report_candidate_t> result;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto found = moves.find(note.id.value);
            const auto moved = found != moves.end();
            const auto after_start = moved && found->second.start
                ? *found->second.start : note.position;
            const auto after_end = note.end && moved && found->second.end
                ? found->second.end : note.end;
            result.push_back({
                .note = &note,
                .after_start = after_start,
                .after_end = after_end,
                .before_row = absolute_row(index, note.position),
                .after_row = absolute_row(index, after_start),
                .moved = moved,
            });
        }
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
        .start_before = candidate.note->position,
        .start_after = candidate.after_start,
        .end_before = candidate.note->end,
        .end_after = candidate.after_end,
        .moved = candidate.moved,
        .collision = candidate.collision,
        .collision_peer = candidate.collision_peer,
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
        if (!candidate.moved && !candidate.collision) continue;
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
    std::unordered_set<std::uint64_t> moved_ids;
    for (const auto& move : result.quantized_moves) moved_ids.insert(move.object.value);
    preview.moved = moved_ids.size();
    preview.unchanged = preview.selected > preview.moved ? preview.selected - preview.moved : 0;
    preview.blocked = result.chart ? 0 : std::max<std::size_t>(result.diagnostics.size(), 1);
    if (!result.chart && !result.diagnostics.empty())
        preview.message = result.diagnostics.front().message;

    const auto built_index = build_measure_index(chart);
    if (std::holds_alternative<measure_index_t>(built_index)) {
        const auto moves = note_moves(result);
        auto candidates = note_candidates(chart, std::get<measure_index_t>(built_index), moves);
        mark_note_collisions(candidates);
        append_report_rows(preview, candidates);
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
    if (!value_ || chart_ != chart.id || revision_ != revision || selected_ != selected ||
        !same_request_settings(request_, request)) {
        value_ = preview_quantize_selection(chart, selected, request);
        chart_ = chart.id;
        revision_ = revision;
        selected_ = selected;
        request_ = request;
        request_.objects.clear();
    }
    return *value_;
}

} // namespace wormedit::sys
