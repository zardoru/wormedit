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

} // namespace

auto apply_note_label_edit(document_history_t& history, std::uint64_t expected_revision,
                           const set_note_label_action_t& action, note_label_edit_limits_t limits)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "note-label editor document revision is stale");
        return result;
    }
    if (!validate_note_label_request(action, limits, result)) {
        return result;
    }

    auto transaction = document_command_transaction_t::from(history);

    auto& proposed = transaction.copy_document();
    const auto chart = proposed.find_chart(action.chart);
    if (!action.chart || chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id, "note-label edit refers to an unknown chart");
        return result;
    }

    std::unordered_set<std::uint64_t> requested;
    requested.reserve(action.notes.size());
    for (const auto id : action.notes) {
        if (!id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "note-label edit uses the reserved zero note ID");
            return result;
        }
        if (!requested.insert(id.value).second) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "note-label edit contains the same note identity more than once");
            return result;
        }
    }
    auto indexed = index_requested_notes(*chart, requested, limits.maximum_chart_objects);
    if (indexed.over_limit) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "selected chart exceeds the note-label object scan cap");
        return result;
    }

    std::vector<note_t*> notes;
    notes.reserve(action.notes.size());
    for (const auto id : action.notes) {
        const auto chart_occurrences = indexed.occurrences.at(id.value);
        if (chart_occurrences == 0) {
            reject(result, diagnostic_code_t::invalid_id,
                   "note-label edit refers to an unknown note");
            return result;
        }
        if (chart_occurrences != 1) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "note-label edit contains an ambiguous note identity");
            return result;
        }
        const auto found_note = indexed.notes.find(id.value);
        if (found_note == indexed.notes.end()) {
            reject(result, diagnostic_code_t::invalid_id,
                   "note-label edit refers to an unknown note");
            return result;
        }
        notes.push_back(found_note->second);
    }

    const auto changed =
        std::ranges::any_of(notes, [&](const auto* note) { return note->label != action.label; });
    if (!changed) {
        return result;
    }
    for (auto* note : notes) {
        note->label = action.label;
    }
    if (!total_label_bytes(*chart, limits.maximum_total_label_bytes).has_value()) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "note labels exceed the configured aggregate byte cap");
        return result;
    }
    return transaction.commit(action.label.has_value() ? "Set note label"
                                                            : "Clear note label");
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
