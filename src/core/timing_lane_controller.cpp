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

auto is_existing_target_current(const chart_document_t& chart,
                             const timing_lane_pseudonote_state_t& preview) -> bool {
    if (!preview.existing_event) {
        return true;
    }
    auto position = local_position_t{};
    auto value = timing_lane_value_t{timing_bpm_value_t{}};
    return load_existing_event(chart, preview.kind, *preview.existing_event, position, value);
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
    const auto source = absolute_row(index, position);
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
        if (preview.report()->status != timing_lane_pseudonote_status_t::editing) {
            result.diagnostics = std::move(preview.diagnostics);
            return false;
        }
        if (event.kind == timing_lane_kind_t::bpm && is_fixed_initial_bpm(chart, event.event)) {
            result.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                                        "the initial BPM cannot move with a timing group")};
            return false;
        }
        const auto source = absolute_row(index, preview.report()->position);
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
    const auto measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures)) {
        diagnostics = std::get<diagnostic_list_t>(measures);
        return false;
    }
    const auto begin_row = absolute_row(std::get<measure_index_t>(measures), position);
    const auto end_row = absolute_row(std::get<measure_index_t>(measures), warp.end);
    if (!begin_row || !end_row || *end_row <= *begin_row) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold, "warp end must be after its start"));
        return false;
    }
    return true;
}

auto validate_value(const chart_document_t& chart, timing_lane_pseudonote_outcome_t& outcome)
    -> bool {
    auto& preview = *outcome.report();
    const auto kind = preview.kind;
    const auto& value = preview.value;
    auto& diagnostics = outcome.diagnostics;
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
        return validate_warp_value(chart, preview.position, *warp, diagnostics);
    }
    return true;
}

auto initial_status(timing_lane_pseudonote_outcome_t& outcome, const document_history_t& history,
                    bool timing_locked) -> bool {
    auto& preview = *outcome.report();
    if (timing_locked) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
               "timing is locked; timing lane edits are unavailable"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        return false;
    }
    if (!outcome.captured_stamp().chart) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id, "timing pseudo-note uses an empty chart ID"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        return false;
    }
    const auto* chart = history.document.find_chart(outcome.captured_stamp().chart);
    if (chart == nullptr) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "timing pseudo-note refers to an unknown chart"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        return false;
    }
    if (!validate_position(*chart, preview.position, outcome.diagnostics, "timing event")) {
        preview.status = timing_lane_pseudonote_status_t::rejected;
        return false;
    }
    if (!is_value_of_kind(preview.kind, preview.value)) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
               "timing pseudo-note value does not match its lane kind"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        return false;
    }
    if (!preview.existing_event && is_same_row_is_occupied(*chart, preview.kind, preview.position)) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::collision,
               "timing lane row already contains an event of this type"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        return false;
    }
    if (!validate_value(*chart, outcome)) {
        preview.status = timing_lane_pseudonote_status_t::rejected;
        return false;
    }
    preview.status = timing_lane_pseudonote_status_t::editing;
    return true;
}

auto cancel_for_revision_or_lock(const document_history_t& history,
                                 timing_lane_pseudonote_outcome_t& outcome, bool timing_locked)
    -> bool {
    auto& preview = *outcome.report();
    if (!outcome.captured_stamp().document.is_current(history.document, document_identity_policy_t::revision_only)) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               stale_action_message("Timing pseudo-note")));
        preview.status = timing_lane_pseudonote_status_t::cancelled;
        return true;
    }
    if (timing_locked) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
               "timing lock changed while a pseudo-note was being edited"));
        preview.status = timing_lane_pseudonote_status_t::cancelled;
        return true;
    }
    return false;
}

auto apply_timing_lane_preview(document_command_transaction_t& transaction,
                               const timing_lane_pseudonote_proposal_t& proposal)
    -> document_command_result_t {
    const auto& preview = proposal.payload.report;
    document_command_result_t result{.revision = transaction.history.document.revision};
    switch (preview.kind) {
    case timing_lane_kind_t::bpm: {
        const auto& value = std::get<timing_bpm_value_t>(preview.value);
        if (preview.existing_event) {
            result = apply_bpm_edit(
                transaction,
                wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                    .chart = proposal.stamp.chart,
                    .id = *preview.existing_event,
                    .value = wormedit::bpm_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_bpm_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                    .chart = proposal.stamp.chart,
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
                    .chart = proposal.stamp.chart,
                    .id = *preview.existing_event,
                    .value = wormedit::scroll_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_scroll_speed_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::scroll_edit_value_t>{
                    .chart = proposal.stamp.chart,
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
                    .chart = proposal.stamp.chart,
                    .id = *preview.existing_event,
                    .value = wormedit::speed_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_scroll_speed_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::speed_edit_value_t>{
                    .chart = proposal.stamp.chart,
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
                    .chart = proposal.stamp.chart,
                    .id = *preview.existing_event,
                    .value = wormedit::pause_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_pause_warp_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::pause_edit_value_t>{
                    .chart = proposal.stamp.chart,
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
                    .chart = proposal.stamp.chart,
                    .id = *preview.existing_event,
                    .value = wormedit::warp_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_pause_warp_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::warp_edit_value_t>{
                    .chart = proposal.stamp.chart,
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
                    .chart = proposal.stamp.chart,
                    .id = *preview.existing_event,
                    .value = wormedit::kiai_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_marker_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::kiai_edit_value_t>{
                    .chart = proposal.stamp.chart,
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
                    .chart = proposal.stamp.chart,
                    .id = *preview.existing_event,
                    .value = wormedit::timing_anchor_edit_value_t::from(preview.position, value)});
        } else {
            result = apply_timing_anchor_edit(
                transaction,
                wormedit::insert_edit_t<wormedit::timing_anchor_edit_value_t>{
                    .chart = proposal.stamp.chart,
                    .value = wormedit::timing_anchor_edit_value_t::from(preview.position, value)});
        }
        break;
    }
    }
    return result;
}

} // namespace

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
    const auto measures_result = build_measure_index(chart);
    const auto* measures = std::get_if<measure_index_t>(&measures_result);
    const auto target = measures ? absolute_row(*measures, position) : std::nullopt;
    if (!target) {
        return std::nullopt;
    }
    const bpm_event_t* best = nullptr;
    std::uint64_t best_row = 0;
    for (const auto& bpm : chart.bpms) {
        if (bpm.beats_per_minute.numerator <= 0 || bpm.beats_per_minute.denominator == 0) {
            continue;
        }
        const auto row = absolute_row(*measures, bpm.position);
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
    initial.status = timing_lane_pseudonote_status_t::inactive;
    initial.existing_event.reset();
    initial.normalize_value();
    timing_lane_pseudonote_outcome_t outcome{timing_lane_pseudonote_proposal_t{
        .stamp = chart_stamp_t::from(history.document, chart), .payload = {.report = std::move(initial)}}};
    const auto is_accepted = initial_status(outcome, history, timing_locked);
    outcome.complete(is_accepted);
    return outcome;
}

auto timing_lane_pseudonote_preview_t::propose(const document_history_t& history, chart_id_t chart,
    timing_lane_event_ref_t target, bool timing_locked) -> timing_lane_pseudonote_outcome_t {
    const auto kind = target.kind;
    const auto event = target.event;
    timing_lane_pseudonote_outcome_t outcome{timing_lane_pseudonote_proposal_t{
        .stamp = chart_stamp_t::from(history.document, chart),
        .payload = {.report = {.kind = kind, .existing_event = event}}}};
    auto& preview = *outcome.report();
    if (timing_locked) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
               "timing is locked; timing lane edits are unavailable"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        outcome.complete(false);
        return outcome;
    }
    if (!chart || !event) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "existing timing event uses a reserved zero ID"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        outcome.complete(false);
        return outcome;
    }
    const auto* target_chart = history.document.find_chart(chart);
    if (target_chart == nullptr) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "existing timing event refers to an unknown chart"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        outcome.complete(false);
        return outcome;
    }
    const auto id_count = chart_object_id_count(*target_chart, event, 2);
    if (id_count == 0) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "existing timing event refers to an unknown object"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        outcome.complete(false);
        return outcome;
    }
    if (id_count != 1) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
               "existing timing event ID is ambiguous because it appears more than once"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        outcome.complete(false);
        return outcome;
    }
    if (!load_existing_event(*target_chart, kind, event, preview.position, preview.value)) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
               "existing object does not match the requested timing lane kind"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        outcome.complete(false);
        return outcome;
    }
    const auto is_accepted = initial_status(outcome, history, timing_locked);
    outcome.complete(is_accepted);
    return outcome;
}

auto timing_lane_pseudonote_preview_t::revise(timing_lane_pseudonote_outcome_t previous,
    const document_history_t& history, timing_lane_value_t value, bool timing_locked)
    -> timing_lane_pseudonote_outcome_t {
    auto* state = previous.report();
    if (!state) return previous;
    auto& preview = *state;
    if (preview.status != timing_lane_pseudonote_status_t::editing) {
        previous.complete(false);
        return previous;
    }
    if (cancel_for_revision_or_lock(history, previous, timing_locked)) {
        previous.complete(false);
        return previous;
    }
    const auto* chart = history.document.find_chart(previous.captured_stamp().chart);
    if (chart == nullptr) {
        previous.diagnostics.clear();
        previous.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "timing pseudo-note refers to an unknown chart"));
        preview.status = timing_lane_pseudonote_status_t::cancelled;
        previous.complete(false);
        return previous;
    }
    if (!is_existing_target_current(*chart, preview)) {
        previous.diagnostics.clear();
        previous.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "existing timing event no longer matches the staged target"));
        preview.status = timing_lane_pseudonote_status_t::cancelled;
        previous.complete(false);
        return previous;
    }
    preview.set_value(std::move(value));
    previous.diagnostics.clear();
    if (!preview.existing_event && is_same_row_is_occupied(*chart, preview.kind, preview.position)) {
        previous.diagnostics.clear();
        previous.diagnostics.push_back(error(diagnostic_code_t::collision,
               "timing lane row already contains an event of this type"));
        previous.complete(false);
        return previous;
    }
    const auto is_accepted = validate_position(*chart, preview.position, previous.diagnostics, "timing event") &&
                             validate_value(*chart, previous);
    previous.complete(is_accepted);
    return previous;
}

auto timing_lane_pseudonote_preview_t::revise(timing_lane_pseudonote_outcome_t previous,
    const document_history_t& history, local_position_t exact_snapped_position, bool timing_locked)
    -> timing_lane_pseudonote_outcome_t {
    auto* state = previous.report();
    if (!state) return previous;
    auto& preview = *state;
    if (preview.status != timing_lane_pseudonote_status_t::editing || !preview.existing_event) {
        previous.complete(false);
        return previous;
    }
    if (cancel_for_revision_or_lock(history, previous, timing_locked)) {
        previous.complete(false);
        return previous;
    }
    const auto* chart = history.document.find_chart(previous.captured_stamp().chart);
    if (chart == nullptr || !is_existing_target_current(*chart, preview)) {
        previous.diagnostics.clear();
        previous.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "existing timing event no longer matches the staged target"));
        preview.status = timing_lane_pseudonote_status_t::cancelled;
        previous.complete(false);
        return previous;
    }
    if (preview.kind == timing_lane_kind_t::bpm &&
        is_fixed_initial_bpm(*chart, *preview.existing_event) &&
        exact_snapped_position != preview.position) {
        previous.diagnostics.clear();
        previous.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
               "the initial BPM is fixed at chart row zero"));
        previous.complete(false);
        return previous;
    }
    preview.position = exact_snapped_position;
    previous.diagnostics.clear();
    const auto is_accepted = validate_position(*chart, preview.position, previous.diagnostics, "timing event") &&
                             validate_value(*chart, previous);
    previous.complete(is_accepted);
    return previous;
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
    const auto indexed = build_measure_index(*chart);
    if (!std::holds_alternative<measure_index_t>(indexed)) {
        result.diagnostics = std::get<diagnostic_list_t>(indexed);
        return result;
    }
    std::vector<prepared_timing_move_t> moves;
    moves.reserve(events.size());
    if (!prepare_timing_group_moves(history, *chart, std::get<measure_index_t>(indexed), events,
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
        if (move.warp_end && preview.report()->status == timing_lane_pseudonote_status_t::editing) {
            std::get<timing_warp_value_t>(preview.report()->value).end = *move.warp_end;
        }
        if (preview.report()->status != timing_lane_pseudonote_status_t::editing ||
            !(preview = timing_lane_pseudonote_preview_t::revise(std::move(preview), staged, move.target, false)).value.has_value()) {
            result.diagnostics = std::move(preview.diagnostics);
            return result;
        }
        auto applied = apply_timing_lane_pseudonote(staged, preview, false);
        if (!applied) {
            result.diagnostics = std::move(applied.diagnostics);
            return result;
        }
    }
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(std::move(staged.document));
    return transaction.commit("Move timing markers");
}

auto apply_timing_lane_pseudonote(document_history_t& history,
                                    timing_lane_pseudonote_outcome_t& outcome, bool timing_locked)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    auto* state = outcome.report();
    if (!state || state->status != timing_lane_pseudonote_status_t::editing) {
        result.diagnostics = outcome.diagnostics;
        return result;
    }
    auto& preview = *state;
    auto transaction = outcome.editor()->transaction(
        history, "Timing pseudo-note", document_identity_policy_t::revision_only);
    if (!transaction.is_ready()) {
        result = transaction.commit({});
        outcome.diagnostics = result.diagnostics;
        preview.status = timing_lane_pseudonote_status_t::cancelled;
        outcome.complete(false);
        return result;
    }
    if (timing_locked) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
               "timing lock changed while a pseudo-note was being edited"));
        preview.status = timing_lane_pseudonote_status_t::cancelled;
        result.diagnostics = outcome.diagnostics;
        outcome.complete(false);
        return result;
    }
    const auto* chart = history.document.find_chart(outcome.captured_stamp().chart);
    if (chart == nullptr) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "timing pseudo-note refers to an unknown chart"));
        preview.status = timing_lane_pseudonote_status_t::cancelled;
        result.diagnostics = outcome.diagnostics;
        outcome.complete(false);
        return result;
    }
    if (!is_existing_target_current(*chart, preview)) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
               "existing timing event no longer matches the staged target"));
        preview.status = timing_lane_pseudonote_status_t::cancelled;
        result.diagnostics = outcome.diagnostics;
        outcome.complete(false);
        return result;
    }
    if (preview.kind == timing_lane_kind_t::bpm && preview.existing_event &&
        is_fixed_initial_bpm(*chart, *preview.existing_event) &&
        preview.position != local_position_t{chart->measures.front().id, 0}) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
               "the initial BPM is fixed at chart row zero"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        result.diagnostics = outcome.diagnostics;
        outcome.complete(false);
        return result;
    }
    if (!preview.existing_event && is_same_row_is_occupied(*chart, preview.kind, preview.position)) {
        outcome.diagnostics.clear();
        outcome.diagnostics.push_back(error(diagnostic_code_t::collision,
               "timing lane row already contains an event of this type"));
        preview.status = timing_lane_pseudonote_status_t::rejected;
        result.diagnostics = outcome.diagnostics;
        outcome.complete(false);
        return result;
    }
    if (!outcome.value) {
        preview.status = timing_lane_pseudonote_status_t::rejected;
        result.diagnostics = outcome.diagnostics;
        return result;
    }
    outcome.diagnostics.clear();
    if (!validate_position(*chart, preview.position, outcome.diagnostics, "timing event") ||
        !validate_value(*chart, outcome)) {
        preview.status = timing_lane_pseudonote_status_t::rejected;
        result.diagnostics = outcome.diagnostics;
        outcome.complete(false);
        return result;
    }

    result = apply_timing_lane_preview(transaction, *outcome.value);
    outcome.diagnostics = result.diagnostics;
    preview.status = result.applied || result.diagnostics.empty()
        ? timing_lane_pseudonote_status_t::committed
        : timing_lane_pseudonote_status_t::rejected;
    outcome.complete(preview.status == timing_lane_pseudonote_status_t::committed);
    return result;
}

void cancel_timing_lane_pseudonote(timing_lane_pseudonote_outcome_t& outcome) noexcept {
    auto* state = outcome.report();
    if (state && state->status == timing_lane_pseudonote_status_t::editing) {
        state->status = timing_lane_pseudonote_status_t::cancelled;
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
    if (preview.report()->status != timing_lane_pseudonote_status_t::editing) {
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
    if (preview.report()->status != timing_lane_pseudonote_status_t::editing) {
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
    return apply_timing_lane_pseudonote(history, preview, timing_locked);
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
