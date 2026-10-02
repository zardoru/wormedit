#include <wormedit/core/marker_editor.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <string_view>
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

struct marker_edit_context_t {
    song_document_t& proposed;
    document_command_result_t& result;
    marker_edit_limits_t limits;
    std::string command_label;
    chart_id_t edited_chart;
    bool changed{};
};

struct marker_edit_target_t {
    chart_document_t* chart{};
    measure_index_t index;
};

auto prepare_marker_target(marker_edit_context_t& context, chart_id_t id)
    -> std::optional<marker_edit_target_t> {
    const auto found = std::ranges::find(context.proposed.charts, id, &chart_document_t::id);
    if (!id || found == context.proposed.charts.end() ||
        std::ranges::count(context.proposed.charts, id, &chart_document_t::id) != 1) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "marker edit refers to an unknown or ambiguous chart");
        return std::nullopt;
    }
    if (found->bookmarks.size() > context.limits.maximum_bookmarks ||
        found->sections.size() > context.limits.maximum_sections ||
        found->kiai.size() > context.limits.maximum_kiai_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "selected chart exceeds a marker or kiai event cap");
        return std::nullopt;
    }
    auto index = build_index(*found, context.result);
    if (!index)
        return std::nullopt;
    context.edited_chart = id;
    return marker_edit_target_t{.chart = &*found, .index = std::move(*index)};
}

auto allocate_marker_id(marker_edit_context_t& context, const chart_document_t& chart)
    -> std::optional<object_id_t> {
    const auto allocation = next_chart_object_id(chart, context.limits.maximum_chart_objects);
    if (allocation.id)
        return allocation.id;
    reject(context.result, diagnostic_code_t::invalid_resolution,
           allocation.over_limit ? "selected chart exceeds the object ID allocation cap"
                                 : "chart object ID sequence is exhausted");
    return std::nullopt;
}

auto validate_marker_label(marker_edit_context_t& context, std::string_view label,
                           std::string_view marker) -> bool {
    if (valid_label(label, context.limits, true))
        return true;
    reject(context.result, diagnostic_code_t::unsupported_feature,
           std::string(marker) +
               " label is invalid UTF-8, contains NUL, or exceeds its byte cap");
    return false;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const insert_bookmark_action_t& edit) {
    if (chart.bookmarks.size() >= context.limits.maximum_bookmarks) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "bookmark insertion would exceed the configured event cap");
        return;
    }
    if (!validate_marker_label(context, edit.label, "bookmark") ||
        !validate_bookmark_anchor(chart, edit.anchor, edit.position,
                                  edit.audio_milliseconds, index, context.result))
        return;
    const auto id = allocate_marker_id(context, chart);
    if (!id)
        return;
    chart.bookmarks.push_back({.id = *id, .label = edit.label, .anchor = edit.anchor,
                               .position = edit.position,
                               .audio_milliseconds = edit.audio_milliseconds});
    context.command_label = "Insert bookmark";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const update_bookmark_action_t& edit) {
    if (!validate_marker_label(context, edit.label, "bookmark") ||
        !validate_bookmark_anchor(chart, edit.anchor, edit.position,
                                  edit.audio_milliseconds, index, context.result) ||
        !validate_chart_object_id(chart, edit.bookmark, context.result, "bookmark"))
        return;
    const auto found = std::ranges::find(chart.bookmarks, edit.bookmark, &bookmark_t::id);
    if (found == chart.bookmarks.end()) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "bookmark update refers to an object that is not a bookmark");
        return;
    }
    context.changed = found->label != edit.label || found->anchor != edit.anchor ||
                      found->position != edit.position ||
                      found->audio_milliseconds != edit.audio_milliseconds;
    if (!context.changed)
        return;
    found->label = edit.label;
    found->anchor = edit.anchor;
    found->position = edit.position;
    found->audio_milliseconds = edit.audio_milliseconds;
    context.command_label = "Update bookmark";
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t&,
                         const delete_bookmark_action_t& edit) {
    if (!validate_chart_object_id(chart, edit.bookmark, context.result, "bookmark"))
        return;
    const auto found = std::ranges::find(chart.bookmarks, edit.bookmark, &bookmark_t::id);
    if (found == chart.bookmarks.end()) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "bookmark deletion refers to an object that is not a bookmark");
        return;
    }
    std::erase_if(chart.bookmarks, [&](const auto& value) { return value.id == edit.bookmark; });
    context.command_label = "Delete bookmark";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const insert_section_action_t& edit) {
    if (chart.sections.size() >= context.limits.maximum_sections) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "section insertion would exceed the configured event cap");
        return;
    }
    if (!validate_marker_label(context, edit.label, "section") ||
        !validate_section_anchor(chart, edit.anchor, edit.beat_span,
                                 edit.audio_millisecond_span, index, context.result))
        return;
    const auto id = allocate_marker_id(context, chart);
    if (!id)
        return;
    chart.sections.push_back({.id = *id, .label = edit.label, .anchor = edit.anchor,
                              .beat_span = edit.beat_span,
                              .audio_millisecond_span = edit.audio_millisecond_span});
    context.command_label = "Insert section";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const update_section_action_t& edit) {
    if (!validate_marker_label(context, edit.label, "section") ||
        !validate_section_anchor(chart, edit.anchor, edit.beat_span,
                                 edit.audio_millisecond_span, index, context.result) ||
        !validate_chart_object_id(chart, edit.section, context.result, "section"))
        return;
    const auto found = std::ranges::find(chart.sections, edit.section, &section_t::id);
    if (found == chart.sections.end()) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "section update refers to an object that is not a section");
        return;
    }
    context.changed = found->label != edit.label || found->anchor != edit.anchor ||
                      !same_span(found->beat_span, edit.beat_span) ||
                      found->audio_millisecond_span != edit.audio_millisecond_span;
    if (!context.changed)
        return;
    found->label = edit.label;
    found->anchor = edit.anchor;
    found->beat_span = edit.beat_span;
    found->audio_millisecond_span = edit.audio_millisecond_span;
    context.command_label = "Update section";
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t&,
                         const delete_section_action_t& edit) {
    if (!validate_chart_object_id(chart, edit.section, context.result, "section"))
        return;
    const auto found = std::ranges::find(chart.sections, edit.section, &section_t::id);
    if (found == chart.sections.end()) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "section deletion refers to an object that is not a section");
        return;
    }
    std::erase_if(chart.sections, [&](const auto& value) { return value.id == edit.section; });
    context.command_label = "Delete section";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const insert_kiai_event_action_t& edit) {
    if (chart.kiai.size() >= context.limits.maximum_kiai_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "kiai insertion would exceed the configured event cap");
        return;
    }
    if (!validate_position(chart, edit.position, index, context.result, "kiai event"))
        return;
    const auto id = allocate_marker_id(context, chart);
    if (!id)
        return;
    const auto source_order = next_kiai_source_order(chart);
    if (!source_order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "kiai source-order sequence is exhausted");
        return;
    }
    chart.kiai.push_back({.id = *id, .position = edit.position, .enabled = edit.enabled,
                          .retained_effect_bits = edit.retained_effect_bits,
                          .provenance = {.source_order = *source_order}});
    context.command_label = "Insert kiai event";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const update_kiai_event_action_t& edit) {
    if (!validate_position(chart, edit.position, index, context.result, "kiai event") ||
        !validate_chart_object_id(chart, edit.event, context.result, "kiai event"))
        return;
    const auto found = std::ranges::find(chart.kiai, edit.event, &kiai_event_t::id);
    if (found == chart.kiai.end()) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "kiai update refers to an object that is not a kiai event");
        return;
    }
    context.changed = found->position != edit.position || found->enabled != edit.enabled;
    if (!context.changed)
        return;
    found->position = edit.position;
    found->enabled = edit.enabled;
    context.command_label = "Update kiai event";
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t&,
                         const delete_kiai_event_action_t& edit) {
    if (!validate_chart_object_id(chart, edit.event, context.result, "kiai event"))
        return;
    const auto found = std::ranges::find(chart.kiai, edit.event, &kiai_event_t::id);
    if (found == chart.kiai.end()) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "kiai deletion refers to an object that is not a kiai event");
        return;
    }
    std::erase_if(chart.kiai, [&](const auto& value) { return value.id == edit.event; });
    context.command_label = "Delete kiai event";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index, const edit_kiai_span_action_t& edit) {
    const auto before = chart.kiai;
    const auto allocate = [&]() { return allocate_marker_id(context, chart); };
    if (const auto error = apply_kiai_span_edit(chart, index, edit.edit, allocate,
                                                context.limits.maximum_kiai_events)) {
        if (context.result.diagnostics.empty())
            reject(context.result, diagnostic_code_t::invalid_resolution, *error);
        return;
    }
    const auto same_event = [](const kiai_event_t& left, const kiai_event_t& right) {
        return left.id == right.id && left.position == right.position && left.enabled == right.enabled;
    };
    context.changed = !std::ranges::equal(before, chart.kiai, same_event);
    context.command_label = "Edit kiai area";
}

template <typename Edit>
auto apply_marker_operation(marker_edit_context_t& context, const Edit& edit) -> void {
    auto target = prepare_marker_target(context, edit.chart);
    if (target)
        apply_marker_action(context, *target->chart, target->index, edit);
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
    marker_edit_context_t context{.proposed = proposed,
                                  .result = result,
                                  .limits = limits,
                                  .command_label = {},
                                  .edited_chart = {},
                                  .changed = false};
    std::visit([&](const auto& edit) { apply_marker_operation(context, edit); }, action);

    if (!result.diagnostics.empty() || !context.changed) {
        return result;
    }
    const auto chart = std::ranges::find(proposed.charts, context.edited_chart,
                                         &chart_document_t::id);
    if (chart == proposed.charts.end() || !marker_text_fits(*chart, limits)) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "marker labels exceed the configured UTF-8 text limits");
        return result;
    }
    return commit_document_command(history, std::move(proposed), std::move(context.command_label));
}

} // namespace wormedit
