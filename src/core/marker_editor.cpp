#include "timing_event_internal.h"
#include "../core/text_util.h"
#include "chart_object_ids.h"
#include "checked_math.h"
#include "position_validation.h"
#include <wormedit/core/marker_editor.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

using text_util::is_valid_utf8;

auto is_valid_label(std::string_view label, const marker_edit_limits_t& limits, bool allow_empty)
    -> bool {
    return (allow_empty || !label.empty()) && text_util::is_valid_bounded_text(label, limits.maximum_label_bytes);
}

auto checked_add(std::size_t& total, std::size_t value, std::size_t maximum) -> bool {
    return checked_math::account_size(total, value, maximum);
}

auto is_marker_text_within_limit(const chart_document_t& chart, const marker_edit_limits_t& limits) -> bool {
    std::size_t total{};
    for (const auto& bookmark : chart.bookmarks) {
        if (!is_valid_label(bookmark.label, limits, true) ||
            !checked_add(total, bookmark.label.size(), limits.maximum_total_label_bytes)) {
            return false;
        }
    }
    for (const auto& section : chart.sections) {
        if (!is_valid_label(section.label, limits, true) ||
            !checked_add(total, section.label.size(), limits.maximum_total_label_bytes)) {
            return false;
        }
    }
    return true;
}

constexpr target_query_policy_t bookmark_target_policy{
    .messages = target_query_messages_t::from_object("bookmark"),
    .scope = target_uniqueness_scope_t::chart_objects,
    .order = target_query_order_t::identity_first};
constexpr target_query_policy_t section_target_policy{
    .messages = target_query_messages_t::from_object("section"),
    .scope = target_uniqueness_scope_t::chart_objects,
    .order = target_query_order_t::identity_first};
constexpr target_query_policy_t kiai_target_policy{
    .messages = target_query_messages_t::from_object("kiai event"),
    .scope = target_uniqueness_scope_t::chart_objects,
    .order = target_query_order_t::identity_first};

auto validate_position(const chart_document_t& chart, local_position_t position,
                       const measure_index_t& index, document_command_result_t& result,
                       std::string_view description) -> bool {
    (void)chart;
    return validate_chart_position(index, position, result.diagnostics, description);
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
        const auto begin = index.row_of(beat_span->begin);
        const auto end = index.row_of(beat_span->end);
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

auto span_equal(const std::optional<position_span_t>& left,
               const std::optional<position_span_t>& right) -> bool {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() || (left->begin == right->begin && left->end == right->end);
}



struct marker_edit_context_t {
    static constexpr std::string_view invalid_limits_message = "marker and event caps must be positive";
    static auto has_valid_limits(const marker_edit_limits_t& limits) -> bool {
        return limits.maximum_bookmarks != 0 && limits.maximum_sections != 0 &&
            limits.maximum_kiai_events != 0 && limits.maximum_chart_objects != 0;
    }

    song_document_t& proposed;
    document_command_result_t& result;
    marker_edit_limits_t limits;
    std::string command_label{};
    chart_id_t edited_chart{};
    bool changed{};

    auto postvalidate() -> bool {
        const auto chart = proposed.find_chart(edited_chart);
        if (chart != nullptr && is_marker_text_within_limit(*chart, limits)) return true;
        reject(result, diagnostic_code_t::unsupported_feature,
               "marker labels exceed the configured UTF-8 text limits");
        return false;
    }

    template <typename Edit> void operator()(const Edit& edit);

};

struct marker_edit_target_t {
    chart_document_t* chart{};
    measure_index_t index;
};

auto prepare_marker_target(marker_edit_context_t& context, chart_id_t id)
    -> std::optional<marker_edit_target_t> {
    const auto found = context.proposed.find_chart(id);
    if (!id || found == nullptr ||
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
    auto index = measure_index_t::from(*found);
    if (!index) {
        context.result.diagnostics = std::move(index.error());
        return std::nullopt;
    }
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
    if (is_valid_label(label, context.limits, true))
        return true;
    reject(context.result, diagnostic_code_t::unsupported_feature,
           std::string(marker) +
               " label is invalid UTF-8, contains NUL, or exceeds its byte cap");
    return false;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const wormedit::insert_edit_t<wormedit::bookmark_edit_value_t>& edit) {
    if (chart.bookmarks.size() >= context.limits.maximum_bookmarks) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "bookmark insertion would exceed the configured event cap");
        return;
    }
    if (!validate_marker_label(context, edit.value.label, "bookmark") ||
        !validate_bookmark_anchor(chart, edit.value.anchor, edit.value.position,
                                  edit.value.audio_milliseconds, index, context.result))
        return;
    const auto id = allocate_marker_id(context, chart);
    if (!id)
        return;
    chart.bookmarks.push_back({.id = *id,
                               .label = edit.value.label,
                               .anchor = edit.value.anchor,
                               .position = edit.value.position,
                               .audio_milliseconds = edit.value.audio_milliseconds});
    context.command_label = "Insert bookmark";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const wormedit::update_edit_t<wormedit::bookmark_edit_value_t>& edit) {
    if (!validate_marker_label(context, edit.value.label, "bookmark") ||
        !validate_bookmark_anchor(chart, edit.value.anchor, edit.value.position,
                                  edit.value.audio_milliseconds, index, context.result))
        return;
    auto* found = chart.unique_target(&chart_document_t::bookmarks, edit.id,
        context.result.diagnostics, bookmark_target_policy,
        "bookmark update refers to an object that is not a bookmark");
    if (found == nullptr) return;
    context.changed = found->label != edit.value.label || found->anchor != edit.value.anchor ||
                      found->position != edit.value.position ||
                      found->audio_milliseconds != edit.value.audio_milliseconds;
    if (!context.changed)
        return;
    found->label = edit.value.label;
    found->anchor = edit.value.anchor;
    found->position = edit.value.position;
    found->audio_milliseconds = edit.value.audio_milliseconds;
    context.command_label = "Update bookmark";
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t&,
                         const wormedit::delete_edit_t<wormedit::bookmark_edit_value_t>& edit) {
    auto* found = chart.unique_target(&chart_document_t::bookmarks, edit.id,
        context.result.diagnostics, bookmark_target_policy,
        "bookmark deletion refers to an object that is not a bookmark");
    if (found == nullptr) return;
    std::erase_if(chart.bookmarks, [&](const auto& value) { return value.id == edit.id; });
    context.command_label = "Delete bookmark";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const wormedit::insert_edit_t<wormedit::section_edit_value_t>& edit) {
    if (chart.sections.size() >= context.limits.maximum_sections) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "section insertion would exceed the configured event cap");
        return;
    }
    if (!validate_marker_label(context, edit.value.label, "section") ||
        !validate_section_anchor(chart, edit.value.anchor, edit.value.beat_span,
                                 edit.value.audio_millisecond_span, index, context.result))
        return;
    const auto id = allocate_marker_id(context, chart);
    if (!id)
        return;
    chart.sections.push_back({.id = *id,
                              .label = edit.value.label,
                              .anchor = edit.value.anchor,
                              .beat_span = edit.value.beat_span,
                              .audio_millisecond_span = edit.value.audio_millisecond_span});
    context.command_label = "Insert section";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const wormedit::update_edit_t<wormedit::section_edit_value_t>& edit) {
    if (!validate_marker_label(context, edit.value.label, "section") ||
        !validate_section_anchor(chart, edit.value.anchor, edit.value.beat_span,
                                 edit.value.audio_millisecond_span, index, context.result))
        return;
    auto* found = chart.unique_target(&chart_document_t::sections, edit.id,
        context.result.diagnostics, section_target_policy,
        "section update refers to an object that is not a section");
    if (found == nullptr) return;
    context.changed = found->label != edit.value.label || found->anchor != edit.value.anchor ||
                      !span_equal(found->beat_span, edit.value.beat_span) ||
                      found->audio_millisecond_span != edit.value.audio_millisecond_span;
    if (!context.changed)
        return;
    found->label = edit.value.label;
    found->anchor = edit.value.anchor;
    found->beat_span = edit.value.beat_span;
    found->audio_millisecond_span = edit.value.audio_millisecond_span;
    context.command_label = "Update section";
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t&,
                         const wormedit::delete_edit_t<wormedit::section_edit_value_t>& edit) {
    auto* found = chart.unique_target(&chart_document_t::sections, edit.id,
        context.result.diagnostics, section_target_policy,
        "section deletion refers to an object that is not a section");
    if (found == nullptr) return;
    std::erase_if(chart.sections, [&](const auto& value) { return value.id == edit.id; });
    context.command_label = "Delete section";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const wormedit::insert_edit_t<wormedit::kiai_edit_value_t>& edit) {
    if (chart.kiai.size() >= context.limits.maximum_kiai_events) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "kiai insertion would exceed the configured event cap");
        return;
    }
    if (!validate_position(chart, edit.value.position, index, context.result, "kiai event"))
        return;
    const auto id = allocate_marker_id(context, chart);
    if (!id)
        return;
    const auto source_order = next_source_order(chart.kiai);
    if (!source_order) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "kiai source-order sequence is exhausted");
        return;
    }
    chart.kiai.push_back({.id = *id,
                          .position = edit.value.position,
                          .enabled = edit.value.settings.enabled,
                          .retained_effect_bits = edit.value.settings.retained_effect_bits,
                          .provenance = {.source_order = *source_order}});
    context.command_label = "Insert kiai event";
    context.changed = true;
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t& index,
                         const wormedit::update_edit_t<wormedit::kiai_edit_value_t>& edit) {
    if (!validate_position(chart, edit.value.position, index, context.result, "kiai event"))
        return;
    auto* found = chart.unique_target(&chart_document_t::kiai, edit.id,
        context.result.diagnostics, kiai_target_policy,
        "kiai update refers to an object that is not a kiai event");
    if (found == nullptr) return;
    context.changed =
        found->position != edit.value.position || found->enabled != edit.value.settings.enabled;
    if (!context.changed)
        return;
    found->position = edit.value.position;
    found->enabled = edit.value.settings.enabled;
    context.command_label = "Update kiai event";
}

void apply_marker_action(marker_edit_context_t& context, chart_document_t& chart,
                         const measure_index_t&,
                         const wormedit::delete_edit_t<wormedit::kiai_edit_value_t>& edit) {
    auto* found = chart.unique_target(&chart_document_t::kiai, edit.id,
        context.result.diagnostics, kiai_target_policy,
        "kiai deletion refers to an object that is not a kiai event");
    if (found == nullptr) return;
    std::erase_if(chart.kiai, [&](const auto& value) { return value.id == edit.id; });
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
void marker_edit_context_t::operator()(const Edit& edit) {
    auto target = prepare_marker_target(*this, edit.chart);
    if (target) apply_marker_action(*this, *target->chart, target->index, edit);
}


} // namespace

auto apply_marker_edit(document_history_t& history,
    const marker_edit_action_t& action, marker_edit_limits_t limits) -> document_command_result_t {
    return document_command_transaction_t::from(history).execute<marker_edit_context_t>(action, limits);
}

auto apply_marker_edit(document_command_transaction_t& transaction,
                       const marker_edit_action_t& action, marker_edit_limits_t limits)
    -> document_command_result_t {
    return transaction.execute<marker_edit_context_t>(action, limits);
}

} // namespace wormedit
