#include "../core/text_util.h"
#include "chart_object_ids.h"
#include "checked_math.h"
#include "position_validation.h"
#include "timing_event_internal.h"
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/bms_program_edit.h>

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
    return (allow_empty || !label.empty()) &&
           text_util::is_valid_bounded_text(label, limits.maximum_label_bytes);
}

auto checked_add(std::size_t& total, std::size_t value, std::size_t maximum) -> bool {
    return checked_math::account_size(total, value, maximum);
}

auto is_marker_text_within_limit(const chart_document_t& chart, const marker_edit_limits_t& limits)
    -> bool {
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

struct marker_edit_context_t : chart_edit_context_t<marker_edit_limits_t> {
    static constexpr std::string_view invalid_limits_message =
        "marker and event caps must be positive";
    static auto has_valid_limits(const marker_edit_limits_t& limits) -> bool {
        return limits.maximum_bookmarks != 0 && limits.maximum_sections != 0 &&
               limits.maximum_kiai_events != 0 && limits.maximum_chart_objects != 0;
    }

    static constexpr std::string_view unknown_chart_message =
        "marker edit refers to an unknown or ambiguous chart";
    static constexpr bool requires_unique_chart = true;
    std::optional<measure_index_t> index{};
    template <class Edit> auto event_caps(const chart_document_t& chart, const Edit&) const {
        constexpr std::string_view message = "selected chart exceeds a marker or kiai event cap";
        return std::array{
            chart_event_cap_t{chart.bookmarks.size(), limits.maximum_bookmarks, message},
            chart_event_cap_t{chart.sections.size(), limits.maximum_sections, message},
            chart_event_cap_t{chart.kiai.size(), limits.maximum_kiai_events, message}};
    }
    auto prepare(const chart_document_t& chart) -> bool {
        auto built = measure_index_t::from(chart);
        if (!built) {
            result.diagnostics = std::move(built.error());
            return false;
        }
        index = std::move(*built);
        return true;
    }
    auto postvalidate(const chart_document_t& chart, song_document_t&) -> bool {
        return postvalidate_native(chart);
    }
    auto postvalidate_native(const chart_document_t& chart) -> bool {
        if (is_marker_text_within_limit(chart, limits))
            return true;
        reject(result, diagnostic_code_t::unsupported_feature,
               "marker labels exceed the configured UTF-8 text limits");
        return false;
    }
    void operator()(chart_document_t& chart,
                    const wormedit::delete_edit_t<wormedit::bookmark_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::delete_edit_t<wormedit::section_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::delete_edit_t<wormedit::kiai_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::insert_edit_t<wormedit::bookmark_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::update_edit_t<wormedit::bookmark_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::insert_edit_t<wormedit::section_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::update_edit_t<wormedit::section_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::insert_edit_t<wormedit::kiai_edit_value_t>& edit);
    void operator()(chart_document_t& chart,
                    const wormedit::update_edit_t<wormedit::kiai_edit_value_t>& edit);
    void operator()(chart_document_t& chart, const edit_kiai_span_action_t& edit);
};

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
           std::string(marker) + " label is invalid UTF-8, contains NUL, or exceeds its byte cap");
    return false;
}

void marker_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::insert_edit_t<wormedit::bookmark_edit_value_t>& edit) {
    auto& context = *this;
    const auto& index = *this->index;
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

void marker_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::update_edit_t<wormedit::bookmark_edit_value_t>& edit) {
    auto& context = *this;
    const auto& index = *this->index;
    if (!validate_marker_label(context, edit.value.label, "bookmark") ||
        !validate_bookmark_anchor(chart, edit.value.anchor, edit.value.position,
                                  edit.value.audio_milliseconds, index, context.result))
        return;
    auto* found = chart.unique_target(&chart_document_t::bookmarks, edit.id,
                                      context.result.diagnostics, bookmark_target_policy,
                                      "bookmark update refers to an object that is not a bookmark");
    if (found == nullptr)
        return;
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

void marker_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::delete_edit_t<wormedit::bookmark_edit_value_t>& edit) {
    auto& context = *this;
    auto* found = chart.unique_target(
        &chart_document_t::bookmarks, edit.id, context.result.diagnostics, bookmark_target_policy,
        "bookmark deletion refers to an object that is not a bookmark");
    if (found == nullptr)
        return;
    std::erase_if(chart.bookmarks, [&](const auto& value) { return value.id == edit.id; });
    context.command_label = "Delete bookmark";
    context.changed = true;
}

void marker_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::insert_edit_t<wormedit::section_edit_value_t>& edit) {
    auto& context = *this;
    const auto& index = *this->index;
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

void marker_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::update_edit_t<wormedit::section_edit_value_t>& edit) {
    auto& context = *this;
    const auto& index = *this->index;
    if (!validate_marker_label(context, edit.value.label, "section") ||
        !validate_section_anchor(chart, edit.value.anchor, edit.value.beat_span,
                                 edit.value.audio_millisecond_span, index, context.result))
        return;
    auto* found = chart.unique_target(&chart_document_t::sections, edit.id,
                                      context.result.diagnostics, section_target_policy,
                                      "section update refers to an object that is not a section");
    if (found == nullptr)
        return;
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

void marker_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::delete_edit_t<wormedit::section_edit_value_t>& edit) {
    auto& context = *this;
    auto* found = chart.unique_target(&chart_document_t::sections, edit.id,
                                      context.result.diagnostics, section_target_policy,
                                      "section deletion refers to an object that is not a section");
    if (found == nullptr)
        return;
    std::erase_if(chart.sections, [&](const auto& value) { return value.id == edit.id; });
    context.command_label = "Delete section";
    context.changed = true;
}

void marker_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::insert_edit_t<wormedit::kiai_edit_value_t>& edit) {
    auto& context = *this;
    const auto& index = *this->index;
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

void marker_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::update_edit_t<wormedit::kiai_edit_value_t>& edit) {
    auto& context = *this;
    const auto& index = *this->index;
    if (!validate_position(chart, edit.value.position, index, context.result, "kiai event"))
        return;
    auto* found = chart.unique_target(&chart_document_t::kiai, edit.id, context.result.diagnostics,
                                      kiai_target_policy,
                                      "kiai update refers to an object that is not a kiai event");
    if (found == nullptr)
        return;
    context.changed =
        found->position != edit.value.position || found->enabled != edit.value.settings.enabled;
    if (!context.changed)
        return;
    found->position = edit.value.position;
    found->enabled = edit.value.settings.enabled;
    context.command_label = "Update kiai event";
}

void marker_edit_context_t::operator()(
    chart_document_t& chart, const wormedit::delete_edit_t<wormedit::kiai_edit_value_t>& edit) {
    auto& context = *this;
    auto* found = chart.unique_target(&chart_document_t::kiai, edit.id, context.result.diagnostics,
                                      kiai_target_policy,
                                      "kiai deletion refers to an object that is not a kiai event");
    if (found == nullptr)
        return;
    std::erase_if(chart.kiai, [&](const auto& value) { return value.id == edit.id; });
    context.command_label = "Delete kiai event";
    context.changed = true;
}

void marker_edit_context_t::operator()(chart_document_t& chart,
                                       const edit_kiai_span_action_t& edit) {
    auto& context = *this;
    const auto& index = *this->index;
    const auto before = chart.kiai;
    const auto allocate = [&]() { return allocate_marker_id(context, chart); };
    if (const auto error = apply_kiai_span_edit(chart, index, edit.edit, allocate,
                                                context.limits.maximum_kiai_events)) {
        if (context.result.diagnostics.empty())
            reject(context.result, diagnostic_code_t::invalid_resolution, *error);
        return;
    }
    const auto same_event = [](const kiai_event_t& left, const kiai_event_t& right) {
        return left.id == right.id && left.position == right.position &&
               left.enabled == right.enabled;
    };
    context.changed = !std::ranges::equal(before, chart.kiai, same_event);
    context.command_label = "Edit kiai area";
}

auto append_scoped_kiai_change(bms_program_content_edit_batch_t& batch,
    const chart_document_t& geometry, const kiai_event_t& event, bool insert) -> bool {
    const auto value = bms_native_content_t::from(event, geometry);
    if (!value) return false;
    if (insert) batch.edits.push_back(bms_program_content_insert_t{.item = {.id = event.id, .value = *value}});
    else batch.edits.push_back(bms_program_content_update_t{.owner = {
        .scope = batch.target.scope, .chart = batch.target.chart, .item = event.id}, .value = *value});
    return true;
}

auto make_scoped_kiai_batch(document_command_transaction_t& transaction,
    const edit_kiai_span_action_t& edit, const marker_edit_limits_t& limits)
    -> attempt_t<bms_program_content_edit_batch_t> {
    if (!marker_edit_context_t::has_valid_limits(limits)) return {.diagnostics = {error(
        diagnostic_code_t::invalid_resolution, std::string(marker_edit_context_t::invalid_limits_message))}};
    const auto target = transaction.bms_target(edit.chart, edit.edit.head);
    if (!target) return {.diagnostics = {error(diagnostic_code_t::invalid_id, target.error().message)}};
    const auto* original = transaction.read_chart(edit.chart);
    const auto index = original ? measure_index_t::from(*original) : result_t<measure_index_t, diagnostic_list_t>{std::unexpected(diagnostic_list_t{})};
    if (!index) return {.diagnostics = {error(diagnostic_code_t::invalid_measure, "Scoped Kiai requires exact measure geometry.")}};
    auto chart = *original;
    const auto first = transaction.next_bms_program_item_id();
    std::uint64_t allocated{};
    const auto allocate = [&]() -> std::optional<object_id_t> {
        if (!first || allocated > std::numeric_limits<std::uint64_t>::max() - first->value) return std::nullopt;
        return object_id_t{first->value + allocated++};
    };
    if (const auto failure = apply_kiai_span_edit(chart, *index, edit.edit, allocate, limits.maximum_kiai_events))
        return {.diagnostics = {error(diagnostic_code_t::invalid_resolution, *failure)}};
    bms_program_content_edit_batch_t batch{.target = *target, .label = "Edit kiai area"};
    for (const auto& before : original->kiai) {
        const auto after = std::ranges::find(chart.kiai, before.id, &kiai_event_t::id);
        if (after == chart.kiai.end()) batch.edits.push_back(bms_program_content_delete_t{.owner = {
            .scope = target->scope, .chart = target->chart, .item = before.id}});
        else if (*after != before && !append_scoped_kiai_change(batch, chart, *after, false))
            return {.diagnostics = {error(diagnostic_code_t::invalid_measure, "Scoped Kiai boundary is inexact.")}};
    }
    for (const auto& after : chart.kiai)
        if (std::ranges::find(original->kiai, after.id, &kiai_event_t::id) == original->kiai.end() &&
            !append_scoped_kiai_change(batch, chart, after, true))
            return {.diagnostics = {error(diagnostic_code_t::invalid_measure, "Scoped Kiai boundary is inexact.")}};
    return {.value = std::move(batch)};
}

auto stage_marker_edit(document_command_transaction_t& transaction, const marker_edit_action_t& action,
    const marker_edit_limits_t& limits) -> attempt_t<document_edit_effect_t> {
    const auto* span = std::get_if<edit_kiai_span_action_t>(&action);
    if (span && transaction.has_bms_program(span->chart)) {
        const auto batch = make_scoped_kiai_batch(transaction, *span, limits);
        if (!batch.value || !batch.ok()) return {.diagnostics = batch.diagnostics};
        return transaction.stage(*batch.value);
    }
    return transaction.stage<marker_edit_context_t>(action, limits);
}

} // namespace

auto apply_marker_edit(document_history_t& history, const marker_edit_action_t& action,
                       marker_edit_limits_t limits) -> document_command_result_t {
    document_command_transaction_t transaction(history);
    return transaction.finish_execution(stage_marker_edit(transaction, action, limits));
}

auto apply_marker_edit(document_command_transaction_t& transaction,
                       const marker_edit_action_t& action, marker_edit_limits_t limits)
    -> document_command_result_t {
    return transaction.finish_execution(stage_marker_edit(transaction, action, limits));
}

} // namespace wormedit
