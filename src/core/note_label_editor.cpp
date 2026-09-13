#include <wormedit/core/note_label_editor.h>

#include <ranges>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error, .code = code, .message = std::move(message)};
}

void reject(document_command_result_t& result, diagnostic_code_t code, std::string message) {
    result.diagnostics.push_back(error(code, std::move(message)));
}

auto valid_utf8(std::string_view text) -> bool {
    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t trailing{};
        std::uint32_t codepoint{};
        if (lead <= 0x7fU) {
            trailing = 0;
            codepoint = lead;
        } else if (lead >= 0xc2U && lead <= 0xdfU) {
            trailing = 1;
            codepoint = lead & 0x1fU;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            trailing = 2;
            codepoint = lead & 0x0fU;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            trailing = 3;
            codepoint = lead & 0x07U;
        } else {
            return false;
        }
        if (index + trailing >= text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= trailing; ++offset) {
            const auto byte = static_cast<unsigned char>(text[index + offset]);
            if ((byte & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (byte & 0x3fU);
        }
        if ((trailing == 1 && codepoint < 0x80U) || (trailing == 2 && codepoint < 0x800U) ||
            (trailing == 3 && codepoint < 0x10000U) ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint > 0x10ffffU) {
            return false;
        }
        index += trailing + 1;
    }
    return true;
}

struct requested_note_index_t {
    std::unordered_map<std::uint64_t, std::size_t> occurrences;
    std::unordered_map<std::uint64_t, note_t*> notes;
    bool over_limit{};
};

auto index_requested_notes(chart_document_t& chart,
                           const std::unordered_set<std::uint64_t>& requested,
                           std::size_t maximum_chart_objects) -> requested_note_index_t {
    requested_note_index_t index;
    index.occurrences.reserve(requested.size());
    index.notes.reserve(requested.size());
    for (const auto id : requested) {
        index.occurrences.emplace(id, 0);
    }
    auto object_count = std::size_t{};
    const auto record = [&](object_id_t id) {
        if (object_count >= maximum_chart_objects) {
            index.over_limit = true;
            return false;
        }
        ++object_count;
        if (const auto found = index.occurrences.find(id.value); found != index.occurrences.end()) {
            ++found->second;
        }
        return true;
    };
    const auto scan = [&](const auto& values) {
        for (const auto& value : values) {
            if (!record(value.id)) {
                return false;
            }
        }
        return true;
    };
    auto within_limit =
        scan(chart.bgm_source_lanes) && scan(chart.lane_layouts) && scan(chart.bpms) &&
        scan(chart.pauses) && scan(chart.warps) && scan(chart.scrolls) && scan(chart.speeds) &&
        scan(chart.kiai) && scan(chart.timing_anchors) && scan(chart.bookmarks) &&
        scan(chart.sections) && scan(chart.bga_events) && scan(chart.sound_channels);
    if (within_limit) {
        for (auto& measure : chart.measures) {
            for (auto& note : measure.notes) {
                if (!record(note.id)) {
                    within_limit = false;
                    break;
                }
                if (requested.contains(note.id.value)) {
                    index.notes.try_emplace(note.id.value, &note);
                }
            }
            if (!within_limit || !scan(measure.bgm_events)) {
                within_limit = false;
                break;
            }
        }
    }
    if (within_limit) {
        for (const auto& channel : chart.sound_channels) {
            if (!scan(channel.events)) {
                within_limit = false;
                break;
            }
        }
    }
    if (!within_limit) {
        index.over_limit = true;
    }
    return index;
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
    if (limits.maximum_selected_notes == 0 || limits.maximum_label_bytes == 0 ||
        limits.maximum_total_label_bytes == 0 || limits.maximum_chart_objects == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "note-label editor limits must be positive");
        return result;
    }
    if (action.notes.empty() || action.notes.size() > limits.maximum_selected_notes) {
        reject(result, diagnostic_code_t::invalid_resolution,
               action.notes.empty() ? "note-label edit requires at least one note"
                                    : "note-label selection exceeds its configured cap");
        return result;
    }
    if (action.label.has_value() &&
        (action.label->size() > limits.maximum_label_bytes ||
         action.label->find('\0') != std::string::npos || !valid_utf8(*action.label))) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "note label must be bounded UTF-8 without embedded NUL bytes");
        return result;
    }

    auto proposed = history.document;
    const auto chart = std::ranges::find(proposed.charts, action.chart, &chart_document_t::id);
    if (!action.chart || chart == proposed.charts.end()) {
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
    return commit_document_command(history, std::move(proposed),
                                   action.label.has_value() ? "Set note label"
                                                            : "Clear note label");
}

} // namespace wormedit
