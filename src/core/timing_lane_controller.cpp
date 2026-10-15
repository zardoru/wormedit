#include "timing_event_internal.h"
#include "position_validation.h"
#include "chart_object_ids.h"
#include <wormedit/core/timing_lane_controller.h>

#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/pause_warp_editor.h>
#include <wormedit/core/scroll_speed_editor.h>
#include <wormedit/core/timing_anchor_editor.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace wormedit {
namespace {
using wormedit::reject;

auto is_fixed_initial_bpm(const chart_document_t& chart, object_id_t event) -> bool {
    if (chart.measures.empty()) {
        return false;
    }
    const auto first_measure = chart.measures.front().id;
    const bpm_event_t* initial = nullptr;
    for (const auto& bpm : chart.bpms) {
        if (bpm.position.measure == first_measure && bpm.position.row == 0 &&
            bpm.beats_per_minute.numerator > 0 && bpm.beats_per_minute.denominator != 0 &&
            (initial == nullptr ||
             bpm.provenance.source_order >= initial->provenance.source_order)) {
            initial = &bpm;
        }
    }
    return initial != nullptr && initial->id == event;
}

auto load_existing_event(const chart_document_t& chart, timing_lane_kind_t kind, object_id_t event,
                         local_position_t& position, timing_lane_value_t& value) -> bool {
    switch (kind) {
    case timing_lane_kind_t::bpm: {
        const auto found = std::ranges::find(chart.bpms, event, &bpm_event_t::id);
        if (found == chart.bpms.end()) {
            return false;
        }
        position = found->position;
        value = timing_bpm_value_t::from(*found);
        return true;
    }
    case timing_lane_kind_t::scroll: {
        const auto found = std::ranges::find(chart.scrolls, event, &scroll_event_t::id);
        if (found == chart.scrolls.end()) {
            return false;
        }
        position = found->position;
        value = timing_scroll_value_t::from(*found);
        return true;
    }
    case timing_lane_kind_t::speed: {
        const auto found = std::ranges::find(chart.speeds, event, &speed_event_t::id);
        if (found == chart.speeds.end()) {
            return false;
        }
        position = found->position;
        value = timing_speed_value_t::from(*found);
        return true;
    }
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay: {
        const auto found = std::ranges::find(chart.pauses, event, &pause_event_t::id);
        const auto requested_kind =
            kind == timing_lane_kind_t::stop ? pause_kind_t::stop : pause_kind_t::delay;
        if (found == chart.pauses.end() || found->kind != requested_kind) {
            return false;
        }
        position = found->position;
        value = timing_pause_value_t::from(*found);
        return true;
    }
    case timing_lane_kind_t::warp: {
        const auto found = std::ranges::find(chart.warps, event, &warp_event_t::id);
        if (found == chart.warps.end()) {
            return false;
        }
        position = found->span.begin;
        value = timing_warp_value_t::from(*found);
        return true;
    }
    case timing_lane_kind_t::kiai: {
        const auto found = std::ranges::find(chart.kiai, event, &kiai_event_t::id);
        if (found == chart.kiai.end()) {
            return false;
        }
        position = found->position;
        value = timing_kiai_value_t::from(*found);
        return true;
    }
    case timing_lane_kind_t::anchor: {
        const auto found = std::ranges::find(chart.timing_anchors, event, &timing_anchor_t::id);
        if (found == chart.timing_anchors.end()) {
            return false;
        }
        position = found->position;
        value = timing_anchor_value_t::from(*found);
        return true;
    }
    }
    return false;
}

auto is_value_of_kind(timing_lane_kind_t kind, const timing_lane_value_t& value) -> bool {
    switch (kind) {
    case timing_lane_kind_t::bpm:
        return std::holds_alternative<timing_bpm_value_t>(value);
    case timing_lane_kind_t::scroll:
        return std::holds_alternative<timing_scroll_value_t>(value);
    case timing_lane_kind_t::speed:
        return std::holds_alternative<timing_speed_value_t>(value);
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
        return std::holds_alternative<timing_pause_value_t>(value);
    case timing_lane_kind_t::warp:
        return std::holds_alternative<timing_warp_value_t>(value);
    case timing_lane_kind_t::kiai:
        return std::holds_alternative<timing_kiai_value_t>(value);
    case timing_lane_kind_t::anchor:
        return std::holds_alternative<timing_anchor_value_t>(value);
    }
    return false;
}

auto is_same_row_is_occupied(const chart_document_t& chart, timing_lane_kind_t kind,
                          local_position_t position) -> bool {
    switch (kind) {
    case timing_lane_kind_t::bpm:
        return std::ranges::any_of(
            chart.bpms, [position](const auto& item) { return item.position == position; });
    case timing_lane_kind_t::scroll:
        return std::ranges::any_of(
            chart.scrolls, [position](const auto& item) { return item.position == position; });
    case timing_lane_kind_t::speed:
        return std::ranges::any_of(
            chart.speeds, [position](const auto& item) { return item.position == position; });
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay: {
        const auto requested_kind =
            kind == timing_lane_kind_t::stop ? pause_kind_t::stop : pause_kind_t::delay;
        return std::ranges::any_of(chart.pauses, [position, requested_kind](const auto& item) {
            return item.position == position && item.kind == requested_kind;
        });
    }
    case timing_lane_kind_t::warp:
        return std::ranges::any_of(
            chart.warps, [position](const auto& item) { return item.span.begin == position; });
    case timing_lane_kind_t::kiai:
        return std::ranges::any_of(
            chart.kiai, [position](const auto& item) { return item.position == position; });
    case timing_lane_kind_t::anchor:
        return std::ranges::any_of(chart.timing_anchors, [position](const auto& item) {
            return item.position == position;
        });
    }
    return true;
}

auto selected_event(std::span<const timing_lane_event_ref_t> events, timing_lane_kind_t kind,
                    object_id_t event) -> bool {
    return std::ranges::any_of(events, [kind, event](const auto& selected) {
        return selected.kind == kind && selected.event == event;
    });
}

auto is_target_occupied_by_unselected(const chart_document_t& chart, timing_lane_kind_t kind,
                                   local_position_t position,
                                   std::span<const timing_lane_event_ref_t> events) -> bool {
    const auto occupied = [&](const auto& values) {
        return std::ranges::any_of(values, [&](const auto& item) {
            return item.position == position && !selected_event(events, kind, item.id);
        });
    };
    switch (kind) {
    case timing_lane_kind_t::bpm:
        return occupied(chart.bpms);
    case timing_lane_kind_t::scroll:
        return occupied(chart.scrolls);
    case timing_lane_kind_t::speed:
        return occupied(chart.speeds);
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
        return std::ranges::any_of(chart.pauses, [&](const auto& pause) {
            const auto matches_kind = (kind == timing_lane_kind_t::stop) ==
                                      (pause.kind == pause_kind_t::stop);
            return matches_kind && pause.position == position &&
                   !selected_event(events, kind, pause.id);
        });
    case timing_lane_kind_t::warp:
        return std::ranges::any_of(chart.warps, [&](const auto& warp) {
            return warp.span.begin == position && !selected_event(events, kind, warp.id);
        });
    case timing_lane_kind_t::kiai:
        return occupied(chart.kiai);
    case timing_lane_kind_t::anchor:
        return occupied(chart.timing_anchors);
    }
    return true;
}

auto shifted_row(std::uint64_t row, std::int64_t delta, std::uint64_t total)
    -> std::optional<std::uint64_t> {
    if (delta >= 0) {
        const auto increase = static_cast<std::uint64_t>(delta);
        if (row >= total || increase >= total - row) {
            return std::nullopt;
        }
        return row + increase;
    }
    const auto decrease = static_cast<std::uint64_t>(-(delta + 1)) + 1;
    if (decrease > row) {
        return std::nullopt;
    }
    return row - decrease;
}

auto local_position_at(const measure_index_t& index, std::uint64_t row)
    -> std::optional<local_position_t> {
    const auto found = std::ranges::find_if(index.entries, [row](const auto& entry) {
        return row >= entry.absolute_begin_row && row - entry.absolute_begin_row < entry.row_capacity;
    });
    if (found == index.entries.end()) {
        return std::nullopt;
    }
    return local_position_t{found->id, row - found->absolute_begin_row};
}

auto shifted_position(const measure_index_t& index, local_position_t position,
                      std::int64_t row_delta) -> std::optional<local_position_t> {
    const auto source = index.row_of(position);
    const auto target_row = source ? shifted_row(*source, row_delta, index.total_rows)
                                   : std::nullopt;
    return target_row ? local_position_at(index, *target_row) : std::nullopt;
}

auto shifted_warp_end(const chart_document_t& chart, const measure_index_t& index,
                      object_id_t event, std::int64_t row_delta)
    -> std::optional<local_position_t> {
    const auto warp = std::ranges::find(chart.warps, event, &warp_event_t::id);
    return warp == chart.warps.end() ? std::nullopt
                                     : shifted_position(index, warp->span.end, row_delta);
}

struct prepared_timing_move_t {
    timing_lane_event_ref_t event;
    std::uint64_t source_row{};
    local_position_t target;
    std::optional<local_position_t> warp_end;
};

auto prepare_timing_group_moves(const document_history_t& history, const chart_document_t& chart,
                                const measure_index_t& index,
                                std::span<const timing_lane_event_ref_t> events,
                                std::int64_t row_delta, std::vector<prepared_timing_move_t>& moves,
                                document_command_result_t& result) -> bool {
    for (const auto& event : events) {
        if (std::ranges::any_of(moves, [&](const auto& prepared) {
                return prepared.event.event == event.event;
            })) {
            result.diagnostics = {error(diagnostic_code_t::duplicate_id,
                                        "timing group contains the same event more than once")};
            return false;
        }
        auto preview = timing_lane_pseudonote_preview_t::propose(history, chart.id, wormedit::timing_lane_event_ref_t{event.kind, event.event}, false);
        if (preview.report()->lifecycle.status() != proposal_status_t::active) {
            result.diagnostics = std::move(preview.diagnostics);
            return false;
        }
        if (event.kind == timing_lane_kind_t::bpm && is_fixed_initial_bpm(chart, event.event)) {
            result.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                                        "the initial BPM cannot move with a timing group")};
            return false;
        }
        const auto source = index.row_of(preview.report()->position);
        const auto target = shifted_position(index, preview.report()->position, row_delta);
        const auto warp_end = event.kind == timing_lane_kind_t::warp
                                  ? shifted_warp_end(chart, index, event.event, row_delta)
                                  : std::nullopt;
        if (!source || !target ||
            (event.kind == timing_lane_kind_t::warp && !warp_end)) {
            result.diagnostics = {error(diagnostic_code_t::row_out_of_range,
                                        "timing group move leaves the chart measure geometry")};
            return false;
        }
        if (row_delta != 0 &&
            is_target_occupied_by_unselected(chart, event.kind, *target, events)) {
            result.diagnostics = {error(diagnostic_code_t::collision,
                                        "timing group destination contains another event")};
            return false;
        }
        moves.push_back({event, *source, *target, warp_end});
    }
    return true;
}

auto validate_position(const chart_document_t& chart, local_position_t position,
                       diagnostic_list_t& diagnostics, std::string_view name) -> bool {
    return validate_chart_position(chart, position, diagnostics, name);
}

auto validate_warp_value(const chart_document_t& chart, local_position_t position,
                         const timing_warp_value_t& warp, diagnostic_list_t& diagnostics) -> bool {
    if (!is_valid_warp_encoding(warp.source_encoding)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                    "warp event has an unknown source encoding"));
        return false;
    }
    diagnostic_list_t position_diagnostics;
    if (!validate_position(chart, warp.end, position_diagnostics, "warp end")) {
        diagnostics = std::move(position_diagnostics);
        return false;
    }
    const auto measures = measure_index_t::from(chart);
    if (!measures) {
        diagnostics = measures.error();
        return false;
    }
    const auto begin_row = measures->row_of(position);
    const auto end_row = measures->row_of(warp.end);
    if (!begin_row || !end_row || *end_row <= *begin_row) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold, "warp end must be after its start"));
        return false;
    }
    return true;
}

} // namespace

auto timing_lane_pseudonote_preview_t::validate_initial_state(timing_lane_pseudonote_outcome_t& outcome,
    const document_history_t& history, bool timing_locked) -> bool {
    auto& preview = *outcome.report();
    if (timing_locked) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
               std::string(timing_lane_pseudonote_preview_t::begin_locked_message)));
        return false;
    }
    if (!outcome.captured_stamp().chart) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id, "timing pseudo-note uses an empty chart ID"));
        return false;
    }
    const auto* chart = history.document.find_chart(outcome.captured_stamp().chart);
    if (chart == nullptr) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "timing pseudo-note refers to an unknown chart"));
        return false;
    }
    if (!validate_position(*chart, preview.position, outcome.diagnostics, "timing event")) {
        return false;
    }
    if (!is_value_of_kind(preview.kind, preview.value)) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
               "timing pseudo-note value does not match its lane kind"));
        return false;
    }
    if (!preview.existing_event && is_same_row_is_occupied(*chart, preview.kind, preview.position)) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::collision,
               "timing lane row already contains an event of this type"));
        return false;
    }
    return preview.validate_value(*chart, outcome.diagnostics);
}

auto timing_lane_pseudonote_state_t::validate_value(const chart_document_t& chart,
    diagnostic_list_t& diagnostics) const -> bool {
    if (!is_value_of_kind(kind, value)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                    "timing pseudo-note value does not match its lane kind"));
        return false;
    }

    if (const auto* bpm = std::get_if<timing_bpm_value_t>(&value)) {
        if (bpm->beats_per_minute.denominator == 0 || bpm->beats_per_minute.numerator <= 0) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
                                        "BPM must be a positive exact rational"));
            return false;
        }
    } else if (const auto* scroll = std::get_if<timing_scroll_value_t>(&value)) {
        if (scroll->multiplier.denominator == 0) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
                                        "scroll multiplier requires a nonzero denominator"));
            return false;
        }
    } else if (const auto* speed = std::get_if<timing_speed_value_t>(&value)) {
        if (speed->multiplier.denominator == 0 || speed->multiplier.numerator < 0 ||
            speed->transition_beats.denominator == 0) {
            diagnostics.push_back(
                error(diagnostic_code_t::invalid_rational,
                      "speed multiplier and transition must be nonnegative exact rationals"));
            return false;
        }
    } else if (const auto* pause = std::get_if<timing_pause_value_t>(&value)) {
        if (pause->duration.denominator == 0 || pause->duration.numerator < 0) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
                                        "pause duration must be a nonnegative exact rational"));
            return false;
        }
        if (!is_valid_pause_unit(pause->source_unit) ||
            (pause->source_unit == pause_unit_t::bms_stop_ticks &&
             kind != timing_lane_kind_t::stop)) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                        "BMS STOP ticks are valid only for stop events"));
            return false;
        }
    } else if (const auto* warp = std::get_if<timing_warp_value_t>(&value)) {
        return validate_warp_value(chart, position, *warp, diagnostics);
    }
    return true;
}

void timing_lane_pseudonote_state_t::normalize_value() {
    if (auto* pause = std::get_if<timing_pause_value_t>(&value))
        pause->kind = kind == timing_lane_kind_t::delay ? pause_kind_t::delay : pause_kind_t::stop;
    if (auto* warp = std::get_if<timing_warp_value_t>(&value); warp && existing_event)
        warp->is_source_encoding_replaced = false;
}

void timing_lane_pseudonote_state_t::set_value(timing_lane_value_t&& next) {
    value = std::move(next);
    normalize_value();
}

auto effective_bpm_at(const chart_document_t& chart, local_position_t position)
    -> std::optional<signed_rational_t> {
    const auto measures_result = measure_index_t::from(chart);
    const auto target = measures_result ? measures_result->row_of(position) : std::nullopt;
    if (!target) {
        return std::nullopt;
    }
    const bpm_event_t* best = nullptr;
    std::uint64_t best_row = 0;
    for (const auto& bpm : chart.bpms) {
        if (bpm.beats_per_minute.numerator <= 0 || bpm.beats_per_minute.denominator == 0) {
            continue;
        }
        const auto row = measures_result->row_of(bpm.position);
        if (!row || *row > *target) {
            continue;
        }
        if (best == nullptr || *row > best_row ||
            (*row == best_row && bpm.provenance.source_order >= best->provenance.source_order)) {
            best = &bpm;
            best_row = *row;
        }
    }
    return best ? std::optional{best->beats_per_minute} : std::nullopt;
}

auto timing_lane_pseudonote_preview_t::propose(const document_history_t& history, chart_id_t chart,
    timing_lane_pseudonote_state_t initial, bool timing_locked) -> timing_lane_pseudonote_outcome_t {
    initial.lifecycle = {};
    initial.existing_event.reset();
    initial.normalize_value();
    timing_lane_pseudonote_outcome_t outcome{timing_lane_pseudonote_proposal_t{
        .stamp = chart_stamp_t::from(history.document, chart), .payload = {.report = std::move(initial)}}};
    const auto is_accepted = validate_initial_state(outcome, history, timing_locked);
    outcome.begin(is_accepted);
    return outcome;
}

auto timing_lane_pseudonote_preview_t::propose(const document_history_t& history, chart_id_t chart,
    timing_lane_event_ref_t target, bool timing_locked) -> timing_lane_pseudonote_outcome_t {
    const auto kind = target.kind;
    const auto event = target.event;
    timing_lane_pseudonote_outcome_t outcome{timing_lane_pseudonote_proposal_t{
        .stamp = chart_stamp_t::from(history.document, chart),
        .payload = {.report = {.kind = kind, .existing_event = event}}}};
    outcome.begin(load_initial_event(outcome, history, timing_locked));
    return outcome;
}

auto timing_lane_pseudonote_preview_t::load_initial_event(timing_lane_pseudonote_outcome_t& outcome,
    const document_history_t& history, bool timing_locked) -> bool {
    auto& preview = *outcome.report();
    const auto kind = preview.kind;
    const auto event = *preview.existing_event;
    const auto chart = outcome.captured_stamp().chart;
    if (timing_locked) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
               std::string(timing_lane_pseudonote_preview_t::begin_locked_message)));
        return false;
    }
    if (!chart || !event) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "existing timing event uses a reserved zero ID"));
        return false;
    }
    const auto* target_chart = history.document.find_chart(chart);
    if (target_chart == nullptr) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "existing timing event refers to an unknown chart"));
        return false;
    }
    const auto id_count = chart_object_id_count(*target_chart, event, 2);
    if (id_count == 0) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "existing timing event refers to an unknown object"));
        return false;
    }
    if (id_count != 1) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
               "existing timing event ID is ambiguous because it appears more than once"));
        return false;
    }
    if (!load_existing_event(*target_chart, kind, event, preview.position, preview.value)) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
               "existing object does not match the requested timing lane kind"));
        return false;
    }
    return validate_initial_state(outcome, history, timing_locked);
}

auto timing_lane_pseudonote_preview_t::validate_target(const song_document_t& document,
    chart_id_t selected, proposal_operation_t, const timing_lane_revision_t& request) const
    -> result_t<const chart_document_t*, diagnostic_t> {
    const auto* chart = document.find_chart(selected);
    if (!chart) {
        const auto message = std::holds_alternative<local_position_t>(request)
            ? "existing timing event no longer matches the staged target"
            : "timing pseudo-note refers to an unknown chart";
        return std::unexpected(error(diagnostic_code_t::invalid_id, message));
    }
    if (report.existing_event) {
        local_position_t position;
        timing_lane_value_t value{timing_bpm_value_t{}};
        if (!load_existing_event(*chart, report.kind, *report.existing_event, position, value))
            return std::unexpected(error(diagnostic_code_t::invalid_id,
                "existing timing event no longer matches the staged target"));
    }
    return chart;
}

auto timing_lane_pseudonote_preview_t::can_revise(const timing_lane_revision_t& request) const noexcept -> bool {
    return !std::holds_alternative<local_position_t>(request) || report.existing_event.has_value();
}

auto timing_lane_pseudonote_preview_t::revise(proposal_revision_scope_t<chart_stamp_t>& scope,
    timing_lane_revision_t request) -> bool {
    const auto& chart = *scope.target();
    auto& diagnostics = scope.diagnostics();
    if (const auto* position = std::get_if<local_position_t>(&request)) {
        if (report.kind == timing_lane_kind_t::bpm &&
            is_fixed_initial_bpm(chart, *report.existing_event) && *position != report.position) {
            diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                "the initial BPM is fixed at chart row zero"));
            return false;
        }
        report.position = *position;
    } else report.set_value(std::get<timing_lane_value_t>(std::move(request)));
    if (!report.existing_event && is_same_row_is_occupied(chart, report.kind, report.position)) {
        diagnostics.push_back(error(diagnostic_code_t::collision,
            "timing lane row already contains an event of this type"));
        return false;
    }
    return validate_position(chart, report.position, diagnostics, "timing event") &&
        report.validate_value(chart, diagnostics);
}

auto move_timing_lane_event_group(document_history_t& history, chart_id_t chart_id,
                                  std::span<const timing_lane_event_ref_t> events,
                                  std::int64_t row_delta,
                                  bool timing_locked) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    constexpr std::size_t maximum_group_events = 4096;
    if (timing_locked) {
        result.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                                    "timing is locked; timing group moves are unavailable")};
        return result;
    }
    if (events.empty() || events.size() > maximum_group_events) {
        result.diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                    "timing group must contain 1 to 4096 events")};
        return result;
    }
    const auto* chart = history.document.find_chart(chart_id);
    if (chart == nullptr) {
        result.diagnostics = {error(diagnostic_code_t::invalid_id,
                                    "timing group move refers to an unknown chart")};
        return result;
    }
    const auto indexed = measure_index_t::from(*chart);
    if (!indexed) {
        result.diagnostics = indexed.error();
        return result;
    }
    std::vector<prepared_timing_move_t> moves;
    moves.reserve(events.size());
    if (!prepare_timing_group_moves(history, *chart, *indexed, events,
                                    row_delta, moves, result)) {
        return result;
    }
    if (row_delta == 0) {
        return result;
    }
    std::ranges::stable_sort(moves, [row_delta](const auto& left, const auto& right) {
        return row_delta > 0 ? left.source_row > right.source_row
                             : left.source_row < right.source_row;
    });
    auto staged = document_history_t::from(history.document);
    for (const auto& move : moves) {
        auto preview = timing_lane_pseudonote_preview_t::propose(staged, chart_id, wormedit::timing_lane_event_ref_t{move.event.kind, move.event.event}, false);
        if (move.warp_end && preview.report()->lifecycle.status() == proposal_status_t::active) {
            std::get<timing_warp_value_t>(preview.report()->value).end = *move.warp_end;
        }
        if (preview.report()->lifecycle.status() != proposal_status_t::active ||
            !(preview = timing_lane_pseudonote_preview_t::revise(std::move(preview), staged, move.target, false)).value.has_value()) {
            result.diagnostics = std::move(preview.diagnostics);
            return result;
        }
        auto applied = apply_proposal(staged, preview, false);
        if (!applied) {
            result.diagnostics = std::move(applied.diagnostics);
            return result;
        }
    }
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(std::move(staged.document));
    return transaction.commit("Move timing markers");
}

auto timing_lane_pseudonote_preview_t::validate_candidate(const chart_document_t& chart,
    const proposal_application_input_t& input, bool has_applicable_state,
    diagnostic_list_t& diagnostics) const -> bool {
    if (report.kind == timing_lane_kind_t::bpm && report.existing_event &&
        is_fixed_initial_bpm(chart, *report.existing_event) &&
        report.position != local_position_t{chart.measures.front().id, 0}) {
        diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
            "the initial BPM is fixed at chart row zero"));
        return false;
    }
    if (!report.existing_event && is_same_row_is_occupied(chart, report.kind, report.position)) {
        diagnostics.push_back(error(diagnostic_code_t::collision,
            "timing lane row already contains an event of this type"));
        return false;
    }
    if (!has_applicable_state) {
        diagnostics.assign(input.diagnostics.begin(), input.diagnostics.end());
        return false;
    }
    return validate_position(chart, report.position, diagnostics, "timing event") &&
        report.validate_value(chart, diagnostics);
}

auto timing_lane_pseudonote_preview_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope,
    const proposal_application_input_t& input, bool has_applicable_state) -> document_command_result_t {
    auto& transaction = scope.transaction();
    document_command_result_t result{.revision = transaction.history.document.revision};
    if (!validate_candidate(*scope.target(), input, has_applicable_state, result.diagnostics)) {
        scope.invalidate_preparation();
        return result;
    }
    const auto& preview = report;
    switch (preview.kind) {
    case timing_lane_kind_t::bpm: {
        const auto& value = std::get<timing_bpm_value_t>(preview.value);
        if (preview.existing_event) {
            result = apply_bpm_edit(
                transaction,
                wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .id = *preview.existing_event,
                    .value = wormedit::bpm_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_bpm_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .value = wormedit::bpm_edit_value_t::from(preview.position, value)});
        }
        break;
    }
    case timing_lane_kind_t::scroll: {
        const auto& value = std::get<timing_scroll_value_t>(preview.value);
        if (preview.existing_event) {
            result = apply_scroll_speed_edit(
                transaction,
                wormedit::update_edit_t<wormedit::scroll_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .id = *preview.existing_event,
                    .value = wormedit::scroll_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_scroll_speed_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::scroll_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .value = wormedit::scroll_edit_value_t::from(preview.position, value)});
        }
        break;
    }
    case timing_lane_kind_t::speed: {
        const auto& value = std::get<timing_speed_value_t>(preview.value);
        if (preview.existing_event) {
            result = apply_scroll_speed_edit(
                transaction,
                wormedit::update_edit_t<wormedit::speed_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .id = *preview.existing_event,
                    .value = wormedit::speed_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_scroll_speed_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::speed_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .value = wormedit::speed_edit_value_t::from(preview.position, value)});
        }
        break;
    }
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay: {
        const auto& value = std::get<timing_pause_value_t>(preview.value);
        if (preview.existing_event) {
            result = apply_pause_warp_edit(
                transaction,
                wormedit::update_edit_t<wormedit::pause_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .id = *preview.existing_event,
                    .value = wormedit::pause_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_pause_warp_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::pause_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .value = wormedit::pause_edit_value_t::from(preview.position, value)});
        }
        break;
    }
    case timing_lane_kind_t::warp: {
        const auto& value = std::get<timing_warp_value_t>(preview.value);
        if (preview.existing_event) {
            result = apply_pause_warp_edit(
                transaction,
                wormedit::update_edit_t<wormedit::warp_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .id = *preview.existing_event,
                    .value = wormedit::warp_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_pause_warp_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::warp_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .value = wormedit::warp_edit_value_t::from(preview.position, value)});
        }
        break;
    }
    case timing_lane_kind_t::kiai: {
        const auto& value = std::get<timing_kiai_value_t>(preview.value);
        if (preview.existing_event) {
            result = apply_marker_edit(
                transaction,
                wormedit::update_edit_t<wormedit::kiai_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .id = *preview.existing_event,
                    .value = wormedit::kiai_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_marker_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::kiai_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .value = wormedit::kiai_edit_value_t::from(preview.position, value)});
        }
        break;
    }
    case timing_lane_kind_t::anchor: {
        const auto& value = std::get<timing_anchor_value_t>(preview.value);
        if (preview.existing_event) {
            result = apply_timing_anchor_edit(
                transaction,
                wormedit::update_edit_t<wormedit::timing_anchor_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .id = *preview.existing_event,
                    .value = wormedit::timing_anchor_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_timing_anchor_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::timing_anchor_edit_value_t>{
                    .chart = scope.captured_stamp().chart,
                    .value = wormedit::timing_anchor_edit_value_t::from(preview.position, value)});
        }
        break;
    }
    }
    return result;
}

auto apply_proposal(document_history_t& history,
    timing_lane_pseudonote_outcome_t& outcome, bool timing_locked) -> document_command_result_t {
    auto* pending = outcome.editor();
    if (!pending) return document_command_transaction_t::rejection(history.document.revision, outcome.diagnostics);
    const bool was_active = pending->payload.report.lifecycle.is_active();
    auto result = apply_proposal(history, *pending,
        proposal_application_input_t{.is_timing_locked = timing_locked, .diagnostics = outcome.diagnostics},
        outcome.value.has_value());
    if (was_active) {
        outcome.diagnostics = result.diagnostics;
        outcome.complete(pending->payload.report.lifecycle.status() == proposal_status_t::applied);
    }
    return result;
}

void cancel_proposal(timing_lane_pseudonote_outcome_t& outcome) noexcept {
    auto* state = outcome.report();
    if (state && state->lifecycle.cancel()) {
        outcome.diagnostics.clear();
        outcome.complete(false);
    }
}

auto delete_existing_timing_lane_event(document_history_t& history, chart_id_t chart,
                                       timing_lane_kind_t kind, object_id_t event, bool timing_locked)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (timing_locked) {
        result.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                                    "timing is locked; timing lane edits are unavailable")};
        return result;
    }
    const auto preview = timing_lane_pseudonote_preview_t::propose(history, chart, wormedit::timing_lane_event_ref_t{kind, event}, false);
    if (preview.report()->lifecycle.status() != proposal_status_t::active) {
        result.diagnostics = preview.diagnostics;
        return result;
    }
    if (kind == timing_lane_kind_t::bpm &&
        is_fixed_initial_bpm(*history.document.find_chart(chart), event)) {
        result.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                                    "the initial BPM cannot be deleted")};
        return result;
    }
    switch (kind) {
    case timing_lane_kind_t::bpm:
        return apply_bpm_edit(history, wormedit::delete_edit_t<wormedit::bpm_edit_value_t>{
                                           .chart = chart, .id = event});
    case timing_lane_kind_t::scroll:
        return apply_scroll_speed_edit(
            history,
            wormedit::delete_edit_t<wormedit::scroll_edit_value_t>{.chart = chart, .id = event});
    case timing_lane_kind_t::speed:
        return apply_scroll_speed_edit(
            history,
            wormedit::delete_edit_t<wormedit::speed_edit_value_t>{.chart = chart, .id = event});
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
        return apply_pause_warp_edit(history, wormedit::delete_edit_t<wormedit::pause_edit_value_t>{
                                                  .chart = chart, .id = event});
    case timing_lane_kind_t::warp:
        return apply_pause_warp_edit(history, wormedit::delete_edit_t<wormedit::warp_edit_value_t>{
                                                  .chart = chart, .id = event});
    case timing_lane_kind_t::kiai:
        return apply_marker_edit(history, wormedit::delete_edit_t<wormedit::kiai_edit_value_t>{
                                              .chart = chart, .id = event});
    case timing_lane_kind_t::anchor:
        return apply_timing_anchor_edit(
            history, wormedit::delete_edit_t<wormedit::timing_anchor_edit_value_t>{.chart = chart,
                                                                                   .id = event});
    }
    return result;
}

auto edit_speed_event_span(document_history_t& history, chart_id_t chart, object_id_t event,
                           const speed_span_edit_t& edit, bool timing_locked)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    auto preview = timing_lane_pseudonote_preview_t::propose(history, chart, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::speed, event}, timing_locked);
    if (preview.report()->lifecycle.status() != proposal_status_t::active) {
        result.diagnostics = preview.diagnostics.empty()
            ? diagnostic_list_t{error(diagnostic_code_t::invalid_id, "the speed event is unavailable")}
            : preview.diagnostics;
        return result;
    }
    if (edit.position &&
        !(preview = timing_lane_pseudonote_preview_t::revise(std::move(preview), history, *edit.position, timing_locked)).value.has_value()) {
        result.diagnostics = preview.diagnostics;
        return result;
    }
    if (edit.transition) {
        auto value = std::get<timing_speed_value_t>(preview.report()->value);
        value.transition_beats = *edit.transition;
        if (!(preview = timing_lane_pseudonote_preview_t::revise(std::move(preview), history, value, timing_locked)).value.has_value()) {
            result.diagnostics = preview.diagnostics;
            return result;
        }
    }
    return apply_proposal(history, preview, timing_locked);
}

auto delete_timing_lane_event_group(document_history_t& history, chart_id_t chart_id,
                                    std::span<const timing_lane_event_ref_t> events, bool timing_locked)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    constexpr std::size_t maximum_group_events = 4096;
    if (timing_locked) {
        result.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                                    "timing is locked; timing group deletion is unavailable")};
        return result;
    }
    if (events.empty() || events.size() > maximum_group_events) {
        result.diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                    "timing group must contain 1 to 4096 events")};
        return result;
    }
    const auto* chart = history.document.find_chart(chart_id);
    if (chart == nullptr) {
        result.diagnostics = {error(diagnostic_code_t::invalid_id,
                                    "timing group deletion refers to an unknown chart")};
        return result;
    }
    // The fixed initial BPM stays; the rest of a mixed selection is still deleted.
    std::vector<timing_lane_event_ref_t> doomed;
    doomed.reserve(events.size());
    for (const auto& event : events) {
        if (event.kind == timing_lane_kind_t::bpm && is_fixed_initial_bpm(*chart, event.event))
            continue;
        doomed.push_back(event);
    }
    if (doomed.empty()) {
        result.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                                    "the initial BPM cannot be deleted")};
        return result;
    }
    auto staged = document_history_t::from(history.document);
    for (const auto& event : doomed) {
        auto deleted = delete_existing_timing_lane_event(staged, chart_id, event.kind, event.event, false);
        if (!deleted) {
            result.diagnostics = std::move(deleted.diagnostics);
            return result;
        }
    }
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(std::move(staged.document));
    return transaction.commit("Delete timing markers");
}

} // namespace wormedit
