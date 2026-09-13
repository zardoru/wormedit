#include <wormedit/core/marker_editor.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error, .code = code, .message = std::move(message)};
}

auto reject(document_command_result_t& result, diagnostic_code_t code, std::string message)
    -> void {
    result.diagnostics.push_back(error(code, std::move(message)));
}

auto valid_utf8(std::string_view text) -> bool {
    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index]);
        if (lead <= 0x7fU) {
            ++index;
            continue;
        }
        std::size_t width{};
        std::uint32_t codepoint{};
        if ((lead & 0xe0U) == 0xc0U) {
            width = 2;
            codepoint = lead & 0x1fU;
        } else if ((lead & 0xf0U) == 0xe0U) {
            width = 3;
            codepoint = lead & 0x0fU;
        } else if ((lead & 0xf8U) == 0xf0U) {
            width = 4;
            codepoint = lead & 0x07U;
        } else {
            return false;
        }
        if (width > text.size() - index) {
            return false;
        }
        for (std::size_t offset = 1; offset < width; ++offset) {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (continuation & 0x3fU);
        }
        const auto minimum = width == 2 ? 0x80U : width == 3 ? 0x800U : 0x10000U;
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return false;
        }
        index += width;
    }
    return true;
}

auto valid_label(std::string_view label, const marker_edit_limits_t& limits, bool allow_empty)
    -> bool {
    return (allow_empty || !label.empty()) && label.size() <= limits.maximum_label_bytes &&
           label.find('\0') == std::string_view::npos && valid_utf8(label);
}

auto checked_add(std::size_t& total, std::size_t value, std::size_t maximum) -> bool {
    if (total > maximum || value > maximum - total) {
        return false;
    }
    total += value;
    return true;
}

auto marker_text_fits(const chart_document_t& chart, const marker_edit_limits_t& limits) -> bool {
    std::size_t total{};
    for (const auto& bookmark : chart.bookmarks) {
        if (!valid_label(bookmark.label, limits, true) ||
            !checked_add(total, bookmark.label.size(), limits.maximum_total_label_bytes)) {
            return false;
        }
    }
    for (const auto& section : chart.sections) {
        if (!valid_label(section.label, limits, true) ||
            !checked_add(total, section.label.size(), limits.maximum_total_label_bytes)) {
            return false;
        }
    }
    return true;
}

template <typename callback_t>
auto visit_chart_object_ids(const chart_document_t& chart, callback_t&& callback) -> bool {
    const auto visit = [&callback](const auto& values) {
        for (const auto& value : values) {
            if (!callback(value.id)) {
                return false;
            }
        }
        return true;
    };
    if (!visit(chart.bgm_source_lanes) || !visit(chart.lane_layouts) || !visit(chart.bpms) ||
        !visit(chart.pauses) || !visit(chart.warps) || !visit(chart.scrolls) ||
        !visit(chart.speeds) || !visit(chart.kiai) || !visit(chart.timing_anchors) ||
        !visit(chart.bookmarks) || !visit(chart.sections) || !visit(chart.bga_events) ||
        !visit(chart.sound_channels)) {
        return false;
    }
    for (const auto& measure : chart.measures) {
        if (!visit(measure.notes) || !visit(measure.bgm_events)) {
            return false;
        }
    }
    for (const auto& channel : chart.sound_channels) {
        if (!visit(channel.events)) {
            return false;
        }
    }
    return true;
}

struct object_id_allocation_t {
    std::optional<object_id_t> id;
    bool over_limit{};
    bool exhausted{};
};

auto next_chart_object_id(const chart_document_t& chart, std::size_t maximum_chart_objects)
    -> object_id_allocation_t {
    std::size_t count{};
    std::uint64_t maximum_id{};
    const auto visit = visit_chart_object_ids(chart, [&](object_id_t id) {
        if (count >= maximum_chart_objects) {
            return false;
        }
        ++count;
        maximum_id = std::max(maximum_id, id.value);
        return true;
    });
    if (!visit || count >= maximum_chart_objects) {
        return {.over_limit = true};
    }
    if (maximum_id == std::numeric_limits<std::uint64_t>::max()) {
        return {.exhausted = true};
    }
    return {.id = object_id_t{maximum_id + 1}};
}

auto chart_object_id_count(const chart_document_t& chart, object_id_t sought) -> std::size_t {
    auto count = std::size_t{};
    (void)visit_chart_object_ids(chart, [&](object_id_t id) {
        if (id == sought) {
            ++count;
        }
        return true;
    });
    return count;
}

auto validate_chart_object_id(const chart_document_t& chart, object_id_t id,
                              document_command_result_t& result, std::string_view type) -> bool {
    if (!id) {
        reject(result, diagnostic_code_t::invalid_id,
               std::string(type) + " command uses the reserved zero object ID");
        return false;
    }
    const auto count = chart_object_id_count(chart, id);
    if (count == 0) {
        reject(result, diagnostic_code_t::invalid_id,
               std::string(type) + " command refers to an unknown object");
        return false;
    }
    if (count != 1) {
        reject(result, diagnostic_code_t::duplicate_id,
               std::string(type) + " ID is ambiguous because it appears more than once");
        return false;
    }
    return true;
}

auto validate_position(const chart_document_t& chart, local_position_t position,
                       const measure_index_t& index, document_command_result_t& result,
                       std::string_view description) -> bool {
    if (!absolute_row(index, position).has_value()) {
        reject(result, diagnostic_code_t::row_out_of_range,
               std::string(description) +
                   " position is outside the selected chart's measure geometry");
        return false;
    }
    (void)chart;
    return true;
}

auto build_index(const chart_document_t& chart, document_command_result_t& result)
    -> std::optional<measure_index_t> {
    auto built = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built)) {
        result.diagnostics = std::get<diagnostic_list_t>(std::move(built));
        return std::nullopt;
    }
    return std::get<measure_index_t>(std::move(built));
}

auto validate_bookmark_anchor(const chart_document_t& chart, marker_anchor_t anchor,
                              const std::optional<local_position_t>& position,
                              const std::optional<std::int64_t>& audio_milliseconds,
                              const measure_index_t& index, document_command_result_t& result)
    -> bool {
    if (anchor == marker_anchor_t::beat) {
        if (!position.has_value() || audio_milliseconds.has_value()) {
            reject(result, diagnostic_code_t::invalid_id,
                   "beat-anchored bookmark requires exactly one beat position");
            return false;
        }
        return validate_position(chart, *position, index, result, "bookmark");
    }
    if (anchor == marker_anchor_t::audio_time) {
        if (position.has_value() || !audio_milliseconds.has_value()) {
            reject(result, diagnostic_code_t::invalid_id,
                   "audio-time bookmark requires exactly one audio millisecond position");
            return false;
        }
        return true;
    }
    reject(result, diagnostic_code_t::invalid_id, "bookmark has an unknown anchor kind");
    return false;
}

auto validate_section_anchor(const chart_document_t& chart, marker_anchor_t anchor,
                             const std::optional<position_span_t>& beat_span,
                             const std::optional<std::pair<std::int64_t, std::int64_t>>& audio_span,
                             const measure_index_t& index, document_command_result_t& result)
    -> bool {
    if (anchor == marker_anchor_t::beat) {
        if (!beat_span.has_value() || audio_span.has_value()) {
            reject(result, diagnostic_code_t::invalid_id,
                   "beat-anchored section requires exactly one beat span");
            return false;
        }
        if (!validate_position(chart, beat_span->begin, index, result, "section") ||
            !validate_position(chart, beat_span->end, index, result, "section")) {
            return false;
        }
        const auto begin = absolute_row(index, beat_span->begin);
        const auto end = absolute_row(index, beat_span->end);
        if (!begin.has_value() || !end.has_value() || *end <= *begin) {
            reject(result, diagnostic_code_t::invalid_hold,
                   "beat-anchored section end must be after its start");
            return false;
        }
        return true;
    }
    if (anchor == marker_anchor_t::audio_time) {
        if (beat_span.has_value() || !audio_span.has_value()) {
            reject(result, diagnostic_code_t::invalid_id,
                   "audio-time section requires exactly one audio millisecond span");
            return false;
        }
        if (audio_span->second <= audio_span->first) {
            reject(result, diagnostic_code_t::invalid_hold,
                   "audio-time section end must be after its start");
            return false;
        }
        return true;
    }
    reject(result, diagnostic_code_t::invalid_id, "section has an unknown anchor kind");
    return false;
}

auto same_span(const std::optional<position_span_t>& left,
               const std::optional<position_span_t>& right) -> bool {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() || (left->begin == right->begin && left->end == right->end);
}

auto next_kiai_source_order(const chart_document_t& chart) -> std::optional<std::size_t> {
    if (chart.kiai.empty()) {
        return 0;
    }
    auto maximum = std::size_t{};
    for (const auto& event : chart.kiai) {
        maximum = std::max(maximum, event.provenance.source_order);
    }
    if (maximum == std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    return maximum + 1;
}

} // namespace

auto apply_marker_edit(document_history_t& history, std::uint64_t expected_revision,
                       const marker_edit_action_t& action, marker_edit_limits_t limits)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "marker editor document revision is stale");
        return result;
    }
    if (limits.maximum_bookmarks == 0 || limits.maximum_sections == 0 ||
        limits.maximum_kiai_events == 0 || limits.maximum_chart_objects == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "marker and event caps must be positive");
        return result;
    }

    auto proposed = history.document;
    std::string command_label;
    bool changed{};
    std::visit(
        [&](const auto& edit) {
            using edit_t = std::remove_cvref_t<decltype(edit)>;
            const auto chart =
                std::ranges::find(proposed.charts, edit.chart, &chart_document_t::id);
            if (!edit.chart || chart == proposed.charts.end() ||
                std::ranges::count(proposed.charts, edit.chart, &chart_document_t::id) != 1) {
                reject(result, diagnostic_code_t::invalid_id,
                       "marker edit refers to an unknown or ambiguous chart");
                return;
            }
            if (chart->bookmarks.size() > limits.maximum_bookmarks ||
                chart->sections.size() > limits.maximum_sections ||
                chart->kiai.size() > limits.maximum_kiai_events) {
                reject(result, diagnostic_code_t::invalid_resolution,
                       "selected chart exceeds a marker or kiai event cap");
                return;
            }
            const auto index = build_index(*chart, result);
            if (!index.has_value()) {
                return;
            }

            if constexpr (std::is_same_v<edit_t, insert_bookmark_action_t>) {
                if (chart->bookmarks.size() >= limits.maximum_bookmarks) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "bookmark insertion would exceed the configured event cap");
                    return;
                }
                if (!valid_label(edit.label, limits, true)) {
                    reject(
                        result, diagnostic_code_t::unsupported_feature,
                        "bookmark label is invalid UTF-8, contains NUL, or exceeds its byte cap");
                    return;
                }
                if (!validate_bookmark_anchor(*chart, edit.anchor, edit.position,
                                              edit.audio_milliseconds, *index, result)) {
                    return;
                }
                const auto allocation = next_chart_object_id(*chart, limits.maximum_chart_objects);
                if (!allocation.id.has_value()) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           allocation.over_limit
                               ? "selected chart exceeds the object ID allocation cap"
                               : "chart object ID sequence is exhausted");
                    return;
                }
                chart->bookmarks.push_back({.id = *allocation.id,
                                            .label = edit.label,
                                            .anchor = edit.anchor,
                                            .position = edit.position,
                                            .audio_milliseconds = edit.audio_milliseconds});
                command_label = "Insert bookmark";
                changed = true;
            } else if constexpr (std::is_same_v<edit_t, update_bookmark_action_t>) {
                if (!valid_label(edit.label, limits, true)) {
                    reject(
                        result, diagnostic_code_t::unsupported_feature,
                        "bookmark label is invalid UTF-8, contains NUL, or exceeds its byte cap");
                    return;
                }
                if (!validate_bookmark_anchor(*chart, edit.anchor, edit.position,
                                              edit.audio_milliseconds, *index, result) ||
                    !validate_chart_object_id(*chart, edit.bookmark, result, "bookmark")) {
                    return;
                }
                const auto found =
                    std::ranges::find(chart->bookmarks, edit.bookmark, &bookmark_t::id);
                if (found == chart->bookmarks.end()) {
                    reject(result, diagnostic_code_t::invalid_id,
                           "bookmark update refers to an object that is not a bookmark");
                    return;
                }
                changed = found->label != edit.label || found->anchor != edit.anchor ||
                          found->position != edit.position ||
                          found->audio_milliseconds != edit.audio_milliseconds;
                if (!changed) {
                    return;
                }
                found->label = edit.label;
                found->anchor = edit.anchor;
                found->position = edit.position;
                found->audio_milliseconds = edit.audio_milliseconds;
                command_label = "Update bookmark";
            } else if constexpr (std::is_same_v<edit_t, delete_bookmark_action_t>) {
                if (!validate_chart_object_id(*chart, edit.bookmark, result, "bookmark")) {
                    return;
                }
                const auto found =
                    std::ranges::find(chart->bookmarks, edit.bookmark, &bookmark_t::id);
                if (found == chart->bookmarks.end()) {
                    reject(result, diagnostic_code_t::invalid_id,
                           "bookmark deletion refers to an object that is not a bookmark");
                    return;
                }
                std::erase_if(chart->bookmarks,
                              [&](const auto& value) { return value.id == edit.bookmark; });
                command_label = "Delete bookmark";
                changed = true;
            } else if constexpr (std::is_same_v<edit_t, insert_section_action_t>) {
                if (chart->sections.size() >= limits.maximum_sections) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "section insertion would exceed the configured event cap");
                    return;
                }
                if (!valid_label(edit.label, limits, true)) {
                    reject(result, diagnostic_code_t::unsupported_feature,
                           "section label is invalid UTF-8, contains NUL, or exceeds its byte cap");
                    return;
                }
                if (!validate_section_anchor(*chart, edit.anchor, edit.beat_span,
                                             edit.audio_millisecond_span, *index, result)) {
                    return;
                }
                const auto allocation = next_chart_object_id(*chart, limits.maximum_chart_objects);
                if (!allocation.id.has_value()) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           allocation.over_limit
                               ? "selected chart exceeds the object ID allocation cap"
                               : "chart object ID sequence is exhausted");
                    return;
                }
                chart->sections.push_back({.id = *allocation.id,
                                           .label = edit.label,
                                           .anchor = edit.anchor,
                                           .beat_span = edit.beat_span,
                                           .audio_millisecond_span = edit.audio_millisecond_span});
                command_label = "Insert section";
                changed = true;
            } else if constexpr (std::is_same_v<edit_t, update_section_action_t>) {
                if (!valid_label(edit.label, limits, true)) {
                    reject(result, diagnostic_code_t::unsupported_feature,
                           "section label is invalid UTF-8, contains NUL, or exceeds its byte cap");
                    return;
                }
                if (!validate_section_anchor(*chart, edit.anchor, edit.beat_span,
                                             edit.audio_millisecond_span, *index, result) ||
                    !validate_chart_object_id(*chart, edit.section, result, "section")) {
                    return;
                }
                const auto found = std::ranges::find(chart->sections, edit.section, &section_t::id);
                if (found == chart->sections.end()) {
                    reject(result, diagnostic_code_t::invalid_id,
                           "section update refers to an object that is not a section");
                    return;
                }
                changed = found->label != edit.label || found->anchor != edit.anchor ||
                          !same_span(found->beat_span, edit.beat_span) ||
                          found->audio_millisecond_span != edit.audio_millisecond_span;
                if (!changed) {
                    return;
                }
                found->label = edit.label;
                found->anchor = edit.anchor;
                found->beat_span = edit.beat_span;
                found->audio_millisecond_span = edit.audio_millisecond_span;
                command_label = "Update section";
            } else if constexpr (std::is_same_v<edit_t, delete_section_action_t>) {
                if (!validate_chart_object_id(*chart, edit.section, result, "section")) {
                    return;
                }
                const auto found = std::ranges::find(chart->sections, edit.section, &section_t::id);
                if (found == chart->sections.end()) {
                    reject(result, diagnostic_code_t::invalid_id,
                           "section deletion refers to an object that is not a section");
                    return;
                }
                std::erase_if(chart->sections,
                              [&](const auto& value) { return value.id == edit.section; });
                command_label = "Delete section";
                changed = true;
            } else if constexpr (std::is_same_v<edit_t, insert_kiai_event_action_t>) {
                if (chart->kiai.size() >= limits.maximum_kiai_events) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "kiai insertion would exceed the configured event cap");
                    return;
                }
                if (!validate_position(*chart, edit.position, *index, result, "kiai event")) {
                    return;
                }
                const auto allocation = next_chart_object_id(*chart, limits.maximum_chart_objects);
                if (!allocation.id.has_value()) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           allocation.over_limit
                               ? "selected chart exceeds the object ID allocation cap"
                               : "chart object ID sequence is exhausted");
                    return;
                }
                const auto source_order = next_kiai_source_order(*chart);
                if (!source_order.has_value()) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "kiai source-order sequence is exhausted");
                    return;
                }
                chart->kiai.push_back({.id = *allocation.id,
                                       .position = edit.position,
                                       .enabled = edit.enabled,
                                       .retained_effect_bits = edit.retained_effect_bits,
                                       .provenance = {.source_order = *source_order}});
                command_label = "Insert kiai event";
                changed = true;
            } else if constexpr (std::is_same_v<edit_t, update_kiai_event_action_t>) {
                if (!validate_position(*chart, edit.position, *index, result, "kiai event") ||
                    !validate_chart_object_id(*chart, edit.event, result, "kiai event")) {
                    return;
                }
                const auto found = std::ranges::find(chart->kiai, edit.event, &kiai_event_t::id);
                if (found == chart->kiai.end()) {
                    reject(result, diagnostic_code_t::invalid_id,
                           "kiai update refers to an object that is not a kiai event");
                    return;
                }
                changed = found->position != edit.position || found->enabled != edit.enabled;
                if (!changed) {
                    return;
                }
                found->position = edit.position;
                found->enabled = edit.enabled;
                command_label = "Update kiai event";
            } else {
                if (!validate_chart_object_id(*chart, edit.event, result, "kiai event")) {
                    return;
                }
                const auto found = std::ranges::find(chart->kiai, edit.event, &kiai_event_t::id);
                if (found == chart->kiai.end()) {
                    reject(result, diagnostic_code_t::invalid_id,
                           "kiai deletion refers to an object that is not a kiai event");
                    return;
                }
                std::erase_if(chart->kiai,
                              [&](const auto& value) { return value.id == edit.event; });
                command_label = "Delete kiai event";
                changed = true;
            }
        },
        action);

    if (!result.diagnostics.empty() || !changed) {
        return result;
    }
    const auto chart = std::ranges::find(
        proposed.charts, std::visit([](const auto& edit) { return edit.chart; }, action),
        &chart_document_t::id);
    if (chart == proposed.charts.end() || !marker_text_fits(*chart, limits)) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "marker labels exceed the configured UTF-8 text limits");
        return result;
    }
    return commit_document_command(history, std::move(proposed), std::move(command_label));
}

} // namespace wormedit
