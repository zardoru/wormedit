#include "../core/text_util.h"
#include <wormedit/core/note_label_editor.h>

#include <algorithm>
#include <ranges>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

using text_util::is_valid_utf8;

struct requested_note_index_t {
    std::unordered_map<std::uint64_t, std::size_t> occurrences;
    std::unordered_map<std::uint64_t, note_t*> notes;
    bool over_limit{};
};

struct requested_note_scan_t {
    const std::unordered_set<std::uint64_t>& requested;
    std::size_t maximum_chart_objects;
    requested_note_index_t index;
    std::size_t object_count{};

    auto record(object_id_t id) -> bool {
        if (object_count >= maximum_chart_objects) {
            index.over_limit = true;
            return false;
        }
        ++object_count;
        if (const auto found = index.occurrences.find(id.value); found != index.occurrences.end()) {
            ++found->second;
        }
        return true;
    }

    auto scan_values(const auto& values) -> bool {
        for (const auto& value : values) {
            if (!record(value.id)) {
                return false;
            }
        }
        return true;
    }

    auto scan_measures(chart_document_t& chart) -> bool {
        for (auto& measure : chart.measures) {
            for (auto& note : measure.notes) {
                if (!record(note.id)) {
                    return false;
                }
                if (requested.contains(note.id.value)) {
                    index.notes.try_emplace(note.id.value, &note);
                }
            }
            if (!scan_values(measure.bgm_events)) {
                return false;
            }
        }
        return true;
    }

    auto scan_channels(const chart_document_t& chart) -> bool {
        for (const auto& channel : chart.sound_channels) {
            if (!scan_values(channel.events)) {
                return false;
            }
        }
        return true;
    }
};

auto index_requested_notes(chart_document_t& chart,
                           const std::unordered_set<std::uint64_t>& requested,
                           std::size_t maximum_chart_objects) -> requested_note_index_t {
    requested_note_scan_t scan{.requested = requested,
                               .maximum_chart_objects = maximum_chart_objects,
                               .index = {},
                               .object_count = 0};
    scan.index.occurrences.reserve(requested.size());
    scan.index.notes.reserve(requested.size());
    for (const auto id : requested) {
        scan.index.occurrences.emplace(id, 0);
    }
    auto within_limit =
        scan.scan_values(chart.bgm_source_lanes) && scan.scan_values(chart.lane_layouts) &&
        scan.scan_values(chart.bpms) && scan.scan_values(chart.pauses) &&
        scan.scan_values(chart.warps) && scan.scan_values(chart.scrolls) &&
        scan.scan_values(chart.speeds) && scan.scan_values(chart.kiai) &&
        scan.scan_values(chart.timing_anchors) && scan.scan_values(chart.bookmarks) &&
        scan.scan_values(chart.sections) && scan.scan_values(chart.bga_events) &&
        scan.scan_values(chart.sound_channels);
    if (within_limit) {
        within_limit = scan.scan_measures(chart);
    }
    if (within_limit) {
        within_limit = scan.scan_channels(chart);
    }
    if (!within_limit) {
        scan.index.over_limit = true;
    }
    return std::move(scan.index);
}

auto total_label_bytes(const chart_document_t& chart, std::size_t maximum)
    -> std::optional<std::size_t> {
    auto total = std::size_t{};
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!note.label.has_value()) {
                continue;
            }
            if (total > maximum || note.label->size() > maximum - total) {
                return std::nullopt;
            }
            total += note.label->size();
        }
    }
    return total;
}

auto validate_note_label_request(const set_note_label_action_t& action,
                                 note_label_edit_limits_t limits,
                                 document_command_result_t& result) -> bool {
    if (limits.maximum_selected_notes == 0 || limits.maximum_label_bytes == 0 ||
        limits.maximum_total_label_bytes == 0 || limits.maximum_chart_objects == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "note-label editor limits must be positive");
        return false;
    }
    if (action.notes.empty() || action.notes.size() > limits.maximum_selected_notes) {
        reject(result, diagnostic_code_t::invalid_resolution,
               action.notes.empty() ? "note-label edit requires at least one note"
                                    : "note-label selection exceeds its configured cap");
        return false;
    }
    if (action.label.has_value() &&
        (!text_util::is_valid_bounded_text(*action.label, limits.maximum_label_bytes))) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "note label must be bounded UTF-8 without embedded NUL bytes");
        return false;
    }
    return true;
}

auto stage_note_labels(chart_document_t& chart, const set_note_label_action_t& action,
    const note_label_edit_limits_t& limits) -> attempt_t<document_edit_effect_t> {
    std::unordered_set<std::uint64_t> requested;
    for (const auto id : action.notes) {
        if (!id) return {.diagnostics = {error(diagnostic_code_t::invalid_id,
            "note-label edit uses the reserved zero note ID")}};
        if (!requested.insert(id.value).second)
            return {.diagnostics = {error(diagnostic_code_t::duplicate_id,
                "note-label edit contains the same note identity more than once")}};
    }
    auto indexed = index_requested_notes(chart, requested, limits.maximum_chart_objects);
    if (indexed.over_limit)
        return {.diagnostics = {error(diagnostic_code_t::invalid_resolution,
            "selected chart exceeds the note-label object scan cap")}};
    bool changed{};
    for (const auto id : action.notes) {
        const auto found = indexed.notes.find(id.value);
        const auto count = indexed.occurrences.at(id.value);
        if (count == 0 || found == indexed.notes.end())
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, "note-label edit refers to an unknown note")}};
        if (count != 1) return {.diagnostics = {error(diagnostic_code_t::duplicate_id,
            "note-label edit contains an ambiguous note identity")}};
        changed = changed || found->second->label != action.label;
        found->second->label = action.label;
    }
    if (!changed) return {.value = document_edit_effect_t{}};
    if (!total_label_bytes(chart, limits.maximum_total_label_bytes))
        return {.diagnostics = {error(diagnostic_code_t::invalid_resolution,
            "note labels exceed the configured aggregate byte cap")}};
    return {.value = document_edit_effect_t{.changed = changed,
        .command_label = action.label ? "Set note label" : "Clear note label"}};
}

} // namespace

auto apply_note_label_edit(document_history_t& history,
    const set_note_label_action_t& action, note_label_edit_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (!validate_note_label_request(action, limits, result)) return result;
    document_command_transaction_t transaction(history);
    const auto edit = [&](chart_document_t& chart) { return stage_note_labels(chart, action, limits); };
    auto staged = transaction.has_bms_program(action.chart)
        ? transaction.stage_bms_native_objects(action.chart, action.notes, bms_existing_edit_kind_t::update, edit)
        : transaction.stage(action.chart, "note-label edit refers to an unknown chart",
            [&](chart_document_t& chart, song_document_t&) { return edit(chart); });
    return transaction.finish_execution(std::move(staged));
}

auto find_notes_by_label(const chart_document_t& chart, std::string_view text)
    -> std::vector<object_id_t> {
    std::vector<object_id_t> found;
    if (text.empty()) {
        return found;
    }
    for (const auto& measure : chart.measures) {
        std::vector<const note_t*> matches;
        for (const auto& note : measure.notes) {
            if (note.label.has_value() &&
                !std::ranges::search(*note.label, text,
                    static_cast<bool (*)(char, char) noexcept>(text_util::ascii_iequal)).empty()) {
                matches.push_back(&note);
            }
        }
        std::ranges::stable_sort(matches, [](const auto* left, const auto* right) {
            return std::tie(left->position.row, left->id.value) <
                   std::tie(right->position.row, right->id.value);
        });
        for (const auto* note : matches) {
            found.push_back(note->id);
        }
    }
    return found;
}

} // namespace wormedit
