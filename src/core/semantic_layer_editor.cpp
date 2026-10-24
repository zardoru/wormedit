#include "id_ceiling.h"
#include "../core/text_util.h"
#include "checked_math.h"
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/bms_program_edit.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

using text_util::is_valid_utf8;

auto is_valid_name(std::string_view name, const semantic_layer_edit_limits_t& limits) -> bool {
    return !name.empty() && text_util::is_valid_bounded_text(name, limits.maximum_name_bytes);
}

auto add_size(std::size_t& total, std::size_t value, std::size_t maximum) -> bool {
    return checked_math::account_size(total, value, maximum);
}

auto validate_layer_names(const chart_document_t& chart, const semantic_layer_edit_limits_t& limits,
                          document_command_result_t& result) -> bool {
    std::size_t total{};
    for (const auto& layer : chart.semantic_layers) {
        if (!is_valid_name(layer.name, limits)) {
            reject(result, diagnostic_code_t::unsupported_feature,
                   "semantic layer name is empty, invalid UTF-8, contains NUL, or exceeds its byte "
                   "cap");
            return false;
        }
        if (!add_size(total, layer.name.size(), limits.maximum_total_name_bytes)) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "semantic layer names exceed the configured total byte cap");
            return false;
        }
    }
    return true;
}

auto validate_layer_ids(const chart_document_t& chart, document_command_result_t& result) -> bool {
    std::unordered_set<std::uint64_t> ids;
    for (const auto& layer : chart.semantic_layers) {
        if (!layer.id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "semantic layer uses the reserved zero layer ID");
            return false;
        }
        if (!ids.insert(layer.id.value).second) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "semantic layer ID is duplicated in the selected chart");
            return false;
        }
    }
    return true;
}

auto is_valid_color(semantic_layer_rgba_t color) -> bool {
    const auto is_color_component_in_range = [](double component) {
        return std::isfinite(component) && component >= 0.0 && component <= 1.0;
    };
    return is_color_component_in_range(color.red) && is_color_component_in_range(color.green) && is_color_component_in_range(color.blue) &&
           is_color_component_in_range(color.alpha);
}

auto color_channel(double component) -> std::uint8_t {
    return static_cast<std::uint8_t>(std::lround(component * 255.0));
}

auto stored_color(semantic_layer_rgba_t color) -> color_rgba_t {
    return {.red = color_channel(color.red),
            .green = color_channel(color.green),
            .blue = color_channel(color.blue),
            .alpha = color_channel(color.alpha)};
}

constexpr target_query_policy_t layer_target_policy{
    .messages = {"semantic layer command refers to an unknown layer",
                 "semantic layer ID is ambiguous because it appears more than once",
                 "semantic layer command uses the reserved zero layer ID"}};
constexpr target_query_messages_t note_target_messages{
    "semantic layer selection refers to an unknown note",
    "note ID is ambiguous because it appears more than once in the chart",
    "semantic layer selection uses the reserved zero note ID"};
constexpr std::string_view unknown_bgm_target_message =
    "semantic layer selection refers to an unknown BGM event";
constexpr target_query_messages_t bgm_target_messages{
    unknown_bgm_target_message,
    "BGM event ID is ambiguous because it appears more than once in the chart",
    unknown_bgm_target_message};

struct chart_note_index_t {
    std::unordered_map<std::uint64_t, std::size_t> object_id_counts;
    std::unordered_map<std::uint64_t, note_t*> notes;
    std::unordered_map<std::uint64_t, bgm_event_t*> bgm_events;

    auto unique_note(object_id_t id, document_command_result_t& result) -> note_t*;
    auto unique_bgm_event(object_id_t id, document_command_result_t& result) -> bgm_event_t*;

};

void index_bgm_event_pointers(chart_document_t& chart, chart_note_index_t& index) {
    for (auto& measure : chart.measures)
        for (auto& event : measure.bgm_events) index.bgm_events.try_emplace(event.id.value, &event);
}

auto build_chart_note_index(chart_document_t& chart, std::size_t maximum_chart_objects,
                            std::size_t expected_selection_size, document_command_result_t& result)
    -> std::optional<chart_note_index_t> {
    chart_note_index_t index;
    index.object_id_counts.reserve(expected_selection_size);
    index.notes.reserve(expected_selection_size);
    auto object_count = std::size_t{};
    const auto record = [&](object_id_t id, note_t* note = nullptr) {
        if (object_count >= maximum_chart_objects) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "chart object index exceeds the configured object cap");
            return false;
        }
        ++object_count;
        auto& count = index.object_id_counts[id.value];
        if (count < 2) {
            ++count;
        }
        if (note != nullptr) {
            index.notes.try_emplace(id.value, note);
        }
        return true;
    };
    const auto record_range = [&record](const auto& values) {
        for (const auto& value : values) {
            if (!record(value.id)) {
                return false;
            }
        }
        return true;
    };

    const auto within_limit = record_range(chart.bgm_source_lanes) &&
                              record_range(chart.lane_layouts) && record_range(chart.bpms) &&
                              record_range(chart.pauses) && record_range(chart.warps) &&
                              record_range(chart.scrolls) && record_range(chart.speeds) &&
                              record_range(chart.kiai) && record_range(chart.timing_anchors) &&
                              record_range(chart.bookmarks) && record_range(chart.sections) &&
                              record_range(chart.bga_events) && record_range(chart.sound_channels);
    if (!within_limit) {
        return std::nullopt;
    }
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            if (!record(note.id, &note)) {
                return std::nullopt;
            }
        }
        if (!record_range(measure.bgm_events)) {
            return std::nullopt;
        }
    }
    for (const auto& channel : chart.sound_channels) {
        if (!record_range(channel.events)) {
            return std::nullopt;
        }
    }
    index_bgm_event_pointers(chart, index);
    return index;
}

auto chart_note_index_t::unique_note(object_id_t id, document_command_result_t& result)
    -> note_t* {
    const auto count = object_id_counts.find(id.value);
    if (!target_identity_query_t::from(id,
            count == object_id_counts.end() ? 0 : count->second)
             .validate(result.diagnostics, note_target_messages)) return nullptr;
    const auto note = notes.find(id.value);
    if (note == notes.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "semantic layer selection ID does not identify a note");
        return nullptr;
    }
    return note->second;
}

auto layer_membership_note_count(const chart_document_t& chart, layer_id_t layer) -> std::size_t {
    auto count = std::size_t{};
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (std::ranges::find(note.semantic_layers, layer) != note.semantic_layers.end()) {
                ++count;
            }
        }
        for (const auto& event : measure.bgm_events)
            if (std::ranges::find(event.semantic_layers, layer) != event.semantic_layers.end()) ++count;
    }
    return count;
}

auto apply_membership(chart_document_t& chart, layer_id_t layer_id,
                      const std::vector<object_id_t>& note_ids, membership_operation_t operation,
                      const semantic_layer_edit_limits_t& limits, document_command_result_t& result)
    -> bool {
    if (note_ids.size() > limits.maximum_note_selection) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer note selection exceeds the configured cap");
        return false;
    }
    auto* layer = chart.unique_target(&chart_document_t::semantic_layers, layer_id,
                                      result.diagnostics, layer_target_policy);
    if (layer == nullptr) {
        return false;
    }
    if (layer->locked) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "note memberships cannot be changed on a locked semantic layer");
        return false;
    }

    std::unordered_set<std::uint64_t> selected_ids;
    std::vector<note_t*> selected_notes;
    selected_ids.reserve(note_ids.size());
    selected_notes.reserve(note_ids.size());
    for (const auto note_id : note_ids) {
        if (!selected_ids.insert(note_id.value).second) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "semantic layer note selection contains a duplicate note ID");
            return false;
        }
    }
    auto note_index =
        build_chart_note_index(chart, limits.maximum_chart_objects, note_ids.size(), result);
    if (!note_index.has_value()) {
        return false;
    }
    for (const auto note_id : note_ids) {
        auto* note = note_index->unique_note(note_id, result);
        if (note == nullptr) {
            return false;
        }
        if (std::ranges::count(note->semantic_layers, layer_id) > 1) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "note contains a duplicated semantic layer membership");
            return false;
        }
        selected_notes.push_back(note);
    }

    auto changed = false;
    for (auto* note : selected_notes) {
        const auto membership = std::ranges::find(note->semantic_layers, layer_id);
        const auto has_layer = membership != note->semantic_layers.end();
        const auto should_have_layer = operation == membership_operation_t::assign   ? true
                                       : operation == membership_operation_t::remove ? false
                                                                                     : !has_layer;
        if (should_have_layer == has_layer) {
            continue;
        }
        if (should_have_layer) {
            note->semantic_layers.push_back(layer_id);
        } else {
            note->semantic_layers.erase(membership);
        }
        changed = true;
    }
    return changed;
}

using membership_layer_index_t = std::unordered_map<std::uint64_t, const semantic_layer_t*>;

auto membership_layer_index(const chart_document_t& chart) -> membership_layer_index_t {
    membership_layer_index_t layers;
    layers.reserve(chart.semantic_layers.size());
    for (const auto& layer : chart.semantic_layers) {
        const auto [entry, inserted] = layers.emplace(layer.id.value, &layer);
        if (!inserted) entry->second = nullptr;
    }
    return layers;
}

auto is_editable_membership_layer(const membership_layer_index_t& layers, layer_id_t id,
                                document_command_result_t& result) -> bool {
    const auto found = layers.find(id.value);
    if (!id || found == layers.end()) {
        reject(result, diagnostic_code_t::invalid_id, "note membership refers to an unknown layer");
        return false;
    }
    if (!found->second) {
        reject(result, diagnostic_code_t::duplicate_id, "note membership refers to a duplicated layer ID");
        return false;
    }
    if (found->second->locked) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "note memberships cannot be changed on a locked semantic layer");
        return false;
    }
    return true;
}

auto validate_clear_note_layers(const membership_layer_index_t& layers, const note_t& note,
                                document_command_result_t& result) -> bool {
    std::unordered_set<std::uint64_t> memberships;
    for (const auto layer_id : note.semantic_layers) {
        if (!memberships.insert(layer_id.value).second) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "note contains a duplicated semantic layer membership");
            return false;
        }
        if (!is_editable_membership_layer(layers, layer_id, result)) return false;
    }
    return true;
}

auto validate_bgm_layers(const membership_layer_index_t& layers, const bgm_event_t& event,
                         document_command_result_t& result) -> bool {
    if (event.semantic_layers.size() > 1) {
        reject(result, diagnostic_code_t::invalid_id,
               "BGM event has multiple semantic layer memberships");
        return false;
    }
    for (const auto layer : event.semantic_layers)
        if (!is_editable_membership_layer(layers, layer, result)) return false;
    return true;
}

auto chart_note_index_t::unique_bgm_event(object_id_t id, document_command_result_t& result) -> bgm_event_t* {
    const auto count = object_id_counts.find(id.value);
    if (!target_identity_query_t::from(id,
            count == object_id_counts.end() ? 0 : count->second)
             .validate(result.diagnostics, bgm_target_messages)) return nullptr;
    const auto event = bgm_events.find(id.value);
    if (event == bgm_events.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "semantic layer selection ID does not identify a BGM event");
        return nullptr;
    }
    return event->second;
}

auto collect_replacement_notes(chart_document_t& chart, const std::vector<object_id_t>& notes,
                               const membership_layer_index_t& layers,
                               const semantic_layer_edit_limits_t& limits,
                               document_command_result_t& result)
    -> std::optional<std::vector<note_t*>> {
    if (notes.size() > limits.maximum_note_selection) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer note selection exceeds the configured cap");
        return std::nullopt;
    }
    auto note_index = build_chart_note_index(chart, limits.maximum_chart_objects, notes.size(), result);
    if (!note_index) return std::nullopt;
    std::unordered_set<std::uint64_t> selected_ids;
    std::vector<note_t*> selected_notes;
    selected_ids.reserve(notes.size());
    selected_notes.reserve(notes.size());
    for (const auto note_id : notes) {
        if (!selected_ids.insert(note_id.value).second) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "semantic layer note selection contains a duplicate note ID");
            return std::nullopt;
        }
        auto* note = note_index->unique_note(note_id, result);
        if (!note || !validate_clear_note_layers(layers, *note, result)) return std::nullopt;
        selected_notes.push_back(note);
    }
    return selected_notes;
}

auto replace_note_memberships(chart_document_t& chart, const std::vector<object_id_t>& notes,
                               std::optional<layer_id_t> target, bool toggle,
                               const semantic_layer_edit_limits_t& limits,
                               document_command_result_t& result) -> bool {
    const auto layers = membership_layer_index(chart);
    if (target && !is_editable_membership_layer(layers, *target, result)) return false;
    const auto selected = collect_replacement_notes(chart, notes, layers, limits, result);
    if (!selected) return false;
    bool changed = false;
    for (auto* note : *selected) {
        const std::vector<layer_id_t> replacement = target ? std::vector{*target} : std::vector<layer_id_t>{};
        const auto clear = toggle && note->semantic_layers == replacement;
        const auto desired = clear ? std::vector<layer_id_t>{} : replacement;
        changed = changed || note->semantic_layers != desired;
        note->semantic_layers = desired;
    }
    return changed;
}

auto collect_bgm_memberships(chart_document_t& chart, const std::vector<object_id_t>& ids,
                             const membership_layer_index_t& layers,
                             const semantic_layer_edit_limits_t& limits,
                             document_command_result_t& result)
    -> std::optional<std::vector<bgm_event_t*>> {
    auto indexed = build_chart_note_index(chart, limits.maximum_chart_objects, ids.size(), result);
    if (!indexed) return std::nullopt;
    std::unordered_set<std::uint64_t> selected_ids;
    std::vector<bgm_event_t*> selected;
    selected.reserve(ids.size());
    for (const auto id : ids) {
        if (!selected_ids.insert(id.value).second) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "semantic layer BGM selection contains a duplicate event ID");
            return std::nullopt;
        }
        auto* match = indexed->unique_bgm_event(id, result);
        if (!match || !validate_bgm_layers(layers, *match, result)) return std::nullopt;
        selected.push_back(match);
    }
    return selected;
}

auto changed_bgm_membership(const bgm_event_t& event, std::optional<layer_id_t> target,
                           bool toggle, membership_operation_t operation, bool replace_mode)
    -> std::vector<layer_id_t> {
    if (!target) return replace_mode ? std::vector<layer_id_t>{} : event.semantic_layers;
    if (replace_mode || operation != membership_operation_t::remove) {
        const auto has_layer = event.semantic_layers.size() == 1 &&
                               event.semantic_layers.front() == *target;
        const auto clear = (toggle || operation == membership_operation_t::toggle) && has_layer;
        return clear ? std::vector<layer_id_t>{} : std::vector{*target};
    }
    auto desired = event.semantic_layers;
    const auto membership = std::ranges::find(desired, *target);
    const auto present = membership != desired.end();
    const auto should_have = operation == membership_operation_t::assign ? true
                             : operation == membership_operation_t::remove ? false
                                                                           : !present;
    if (should_have && !present) desired.push_back(*target);
    if (!should_have && present) desired.erase(membership);
    return desired;
}

auto apply_bgm_memberships(chart_document_t& chart, const std::vector<object_id_t>& ids,
                           std::optional<layer_id_t> target, bool toggle,
                           membership_operation_t operation, bool replace_mode,
                           const semantic_layer_edit_limits_t& limits,
                           document_command_result_t& result) -> bool {
    const auto layers = membership_layer_index(chart);
    if (target && !is_editable_membership_layer(layers, *target, result)) return false;
    auto selected = collect_bgm_memberships(chart, ids, layers, limits, result);
    if (!selected) return false;
    bool changed = false;
    for (auto* event : *selected) {
        auto desired = changed_bgm_membership(*event, target, toggle, operation, replace_mode);
        changed = changed || desired != event->semantic_layers;
        event->semantic_layers = std::move(desired);
    }
    return changed;
}

auto apply_mixed_membership(chart_document_t& chart, const std::vector<object_id_t>& notes,
                            const std::vector<object_id_t>& bgm_events,
                            std::optional<layer_id_t> target, bool toggle,
                            membership_operation_t operation,
                            bool replace_mode,
                            const semantic_layer_edit_limits_t& limits,
                            document_command_result_t& result) -> bool {
    if (notes.size() > limits.maximum_note_selection ||
        bgm_events.size() > limits.maximum_note_selection - notes.size()) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer selection exceeds the configured cap");
        return false;
    }
    auto changed = false;
    if (!notes.empty() || bgm_events.empty())
        changed = replace_mode
                      ? replace_note_memberships(chart, notes, target, toggle, limits, result)
                      : target ? apply_membership(chart, *target, notes, operation, limits, result)
                               : false;
    if (!result.diagnostics.empty()) return false;
    if (!bgm_events.empty())
        changed = apply_bgm_memberships(chart, bgm_events, target, toggle, operation,
                                        replace_mode, limits, result) || changed;
    return changed;
}

auto insert_semantic_layer(
    chart_document_t& chart,
    const wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>& edit,
    const semantic_layer_edit_limits_t& limits, document_command_result_t& result) -> bool {
    if (chart.semantic_layers.size() >= limits.maximum_layers) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer insertion would exceed the configured layer cap");
        return false;
    }
    if (!is_valid_name(edit.value.name, limits)) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "semantic layer name is empty, invalid UTF-8, contains NUL, or exceeds its byte cap");
        return false;
    }
    if (!is_valid_color(edit.value.color)) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer RGBA components must be finite values between zero and one");
        return false;
    }
    std::size_t total_names{};
    for (const auto& layer : chart.semantic_layers) {
        if (!add_size(total_names, layer.name.size(), limits.maximum_total_name_bytes)) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "semantic layer names exceed the configured total byte cap");
            return false;
        }
    }
    if (!add_size(total_names, edit.value.name.size(), limits.maximum_total_name_bytes)) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer names exceed the configured total byte cap");
        return false;
    }
    const auto id = id_ceiling_t<layer_id_t>::from(chart).next();
    if (!id.has_value()) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer ID sequence is exhausted");
        return false;
    }
    chart.semantic_layers.push_back({.id = *id,
                                     .name = edit.value.name,
                                     .color = stored_color(edit.value.color),
                                     .visible = edit.value.visible,
                                     .locked = edit.value.locked});
    return true;
}

auto update_semantic_layer(
    chart_document_t& chart,
    const wormedit::update_edit_t<wormedit::semantic_layer_edit_value_t>& edit,
    const semantic_layer_edit_limits_t& limits, document_command_result_t& result) -> bool {
    auto* layer = chart.unique_target(&chart_document_t::semantic_layers, edit.id,
                                      result.diagnostics, layer_target_policy);
    if (layer == nullptr) {
        return false;
    }
    if (!is_valid_name(edit.value.name, limits)) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "semantic layer name is empty, invalid UTF-8, contains NUL, or exceeds its byte cap");
        return false;
    }
    if (!is_valid_color(edit.value.color)) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer RGBA components must be finite values between zero and one");
        return false;
    }
    const auto replacement_color = stored_color(edit.value.color);
    std::size_t total_names{};
    for (const auto& item : chart.semantic_layers) {
        const auto& name = item.id == edit.id ? edit.value.name : item.name;
        if (!add_size(total_names, name.size(), limits.maximum_total_name_bytes)) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "semantic layer names exceed the configured total byte cap");
            return false;
        }
    }
    const auto changed = layer->name != edit.value.name || layer->color != replacement_color ||
                         layer->visible != edit.value.visible || layer->locked != edit.value.locked;
    if (changed) {
        layer->name = edit.value.name;
        layer->color = replacement_color;
        layer->visible = edit.value.visible;
        layer->locked = edit.value.locked;
    }
    return changed;
}

auto delete_semantic_layer(
    chart_document_t& chart,
    const wormedit::delete_edit_t<wormedit::semantic_layer_edit_value_t>& edit,
    const semantic_layer_edit_limits_t& limits, document_command_result_t& result) -> bool {
    auto* layer = chart.unique_target(&chart_document_t::semantic_layers, edit.id,
                                      result.diagnostics, layer_target_policy);
    if (layer == nullptr) {
        return false;
    }
    if (layer->locked) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "locked semantic layers cannot be deleted");
        return false;
    }
    const auto assigned_count = layer_membership_note_count(chart, edit.id);
    if (assigned_count != 0 && !edit.options.remove_from_notes) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "semantic layer is assigned to notes; explicit removal was not requested");
        return false;
    }
    if (assigned_count > limits.maximum_note_selection) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer deletion exceeds the configured note removal cap");
        return false;
    }
    if (edit.options.remove_from_notes) {
        for (auto& measure : chart.measures) {
            for (auto& note : measure.notes) {
                std::erase(note.semantic_layers, edit.id);
            }
            for (auto& event : measure.bgm_events)
                std::erase(event.semantic_layers, edit.id);
        }
    }
    for (auto& entry : chart.sound_layer_defaults)
        std::erase(entry.layers, edit.id);
    std::erase_if(chart.sound_layer_defaults, [](const auto& entry) { return entry.layers.empty(); });
    std::erase_if(chart.semantic_layers, [&edit](const auto& item) { return item.id == edit.id; });
    return true;
}

auto reorder_semantic_layers(chart_document_t& chart,
                             const reorder_semantic_layers_action_t& edit,
                             const semantic_layer_edit_limits_t& limits,
                             document_command_result_t& result) -> bool {
    if (edit.ordered_layers.size() > limits.maximum_layers ||
        edit.ordered_layers.size() != chart.semantic_layers.size()) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer order must contain every chart layer exactly once");
        return false;
    }
    std::unordered_set<std::uint64_t> ordered_ids;
    std::unordered_map<std::uint64_t, const semantic_layer_t*> by_id;
    by_id.reserve(chart.semantic_layers.size());
    for (const auto& layer : chart.semantic_layers) {
        by_id.emplace(layer.id.value, &layer);
    }
    std::vector<semantic_layer_t> reordered;
    reordered.reserve(edit.ordered_layers.size());
    for (const auto id : edit.ordered_layers) {
        if (!id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "semantic layer order uses the reserved zero layer ID");
            return false;
        }
        if (!ordered_ids.insert(id.value).second) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "semantic layer order contains a duplicate layer ID");
            return false;
        }
        const auto layer = by_id.find(id.value);
        if (layer == by_id.end()) {
            reject(result, diagnostic_code_t::invalid_id,
                   "semantic layer order refers to an unknown layer");
            return false;
        }
        reordered.push_back(*layer->second);
    }
    const auto changed = !std::ranges::equal(reordered, chart.semantic_layers, {},
                                              &semantic_layer_t::id, &semantic_layer_t::id);
    if (changed) {
        chart.semantic_layers = std::move(reordered);
    }
    return changed;
}

struct semantic_layer_edit_outcome_t {
    std::string label;
    bool changed{};
};

template <typename edit_t>
auto semantic_edit_label(membership_operation_t operation = membership_operation_t::assign)
    -> std::string {
    if constexpr (std::is_same_v<edit_t,
                                 wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>>) {
        return "Insert semantic layer";
    } else if constexpr (std::is_same_v<edit_t, wormedit::update_edit_t<
                                                    wormedit::semantic_layer_edit_value_t>>) {
        return "Update semantic layer";
    } else if constexpr (std::is_same_v<edit_t, wormedit::delete_edit_t<
                                                    wormedit::semantic_layer_edit_value_t>>) {
        return "Delete semantic layer";
    } else if constexpr (std::is_same_v<edit_t, reorder_semantic_layers_action_t>) {
        return "Reorder semantic layers";
    } else if (operation == membership_operation_t::assign) {
        return "Assign notes to semantic layer";
    } else if (operation == membership_operation_t::remove) {
        return "Remove notes from semantic layer";
    } else {
        return "Toggle semantic layer membership";
    }
}

auto membership_operation(const semantic_layer_membership_edit_t& edit) -> membership_operation_t {
    return edit.operation;
}

template <typename edit_t>
auto apply_typed_semantic_edit(chart_document_t& chart, const edit_t& edit,
                               const semantic_layer_edit_limits_t& limits,
                               document_command_result_t& result)
    -> semantic_layer_edit_outcome_t {
    if constexpr (std::is_same_v<edit_t,
                                 wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>>) {
        return {.label = semantic_edit_label<edit_t>(),
                .changed = insert_semantic_layer(chart, edit, limits, result)};
    } else if constexpr (std::is_same_v<edit_t, wormedit::update_edit_t<
                                                    wormedit::semantic_layer_edit_value_t>>) {
        return {.label = semantic_edit_label<edit_t>(),
                .changed = update_semantic_layer(chart, edit, limits, result)};
    } else if constexpr (std::is_same_v<edit_t, wormedit::delete_edit_t<
                                                    wormedit::semantic_layer_edit_value_t>>) {
        return {.label = semantic_edit_label<edit_t>(),
                .changed = delete_semantic_layer(chart, edit, limits, result)};
    } else if constexpr (std::is_same_v<edit_t, reorder_semantic_layers_action_t>) {
        return {.label = semantic_edit_label<edit_t>(),
                .changed = reorder_semantic_layers(chart, edit, limits, result)};
    } else if constexpr (std::is_same_v<edit_t, clear_semantic_layer_membership_action_t>) {
        return {.label = "Clear semantic layer membership",
                .changed = apply_mixed_membership(chart, edit.notes, edit.bgm_events,
                                                   std::nullopt, false,
                                                   membership_operation_t::remove, true,
                                                   limits, result)};
    } else if constexpr (std::is_same_v<edit_t, replace_notes_semantic_layer_action_t>) {
        return {.label = "Replace semantic layer membership",
                .changed = apply_mixed_membership(chart, edit.notes, edit.bgm_events,
                                                   edit.layer, edit.toggle,
                                                   membership_operation_t::assign, true,
                                                   limits, result)};
    } else {
        const auto operation = membership_operation(edit);
        return {.label = semantic_edit_label<edit_t>(operation),
                .changed = apply_mixed_membership(chart, edit.notes, edit.bgm_events,
                                                   edit.layer, false, operation, false,
                                                   limits, result)};
    }
}

auto apply_semantic_layer_action(song_document_t& proposed,
                                 const semantic_layer_edit_action_t& action,
                                 const semantic_layer_edit_limits_t& limits,
                                 document_command_result_t& result)
    -> semantic_layer_edit_outcome_t {
    return std::visit(
        [&](const auto& edit) -> semantic_layer_edit_outcome_t {
            const auto chart = proposed.find_chart(edit.chart);
            if (!edit.chart || chart == nullptr ||
                std::ranges::count(proposed.charts, edit.chart, &chart_document_t::id) != 1) {
                reject(result, diagnostic_code_t::invalid_id,
                       "semantic layer edit refers to an unknown or ambiguous chart");
                return {};
            }
            if (chart->semantic_layers.size() > limits.maximum_layers) {
                reject(result, diagnostic_code_t::invalid_resolution,
                       "selected chart exceeds the semantic layer cap");
                return {};
            }
            if (!validate_layer_ids(*chart, result) ||
                !validate_layer_names(*chart, limits, result)) {
                return {};
            }
            return apply_typed_semantic_edit(*chart, edit, limits, result);
        },
        action);
}

struct scoped_semantic_editor_t {
    document_command_transaction_t& transaction;
    const semantic_layer_edit_limits_t& limits;

    template<class Edit> auto edit(chart_document_t& chart, const Edit& value) -> attempt_t<document_edit_effect_t> {
        document_command_result_t result;
        if (chart.semantic_layers.size() > limits.maximum_layers || !validate_layer_ids(chart, result) ||
            !validate_layer_names(chart, limits, result)) {
            if (result.diagnostics.empty()) result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                "Selected chart exceeds the semantic layer cap."));
            return {.diagnostics = std::move(result.diagnostics)};
        }
        const auto outcome = apply_typed_semantic_edit(chart, value, limits, result);
        if (!result.diagnostics.empty()) return {.diagnostics = std::move(result.diagnostics)};
        return {.value = document_edit_effect_t{.changed = outcome.changed, .command_label = outcome.label}};
    }
    template<class Edit> auto operator()(const Edit& value) -> attempt_t<document_edit_effect_t> {
        const auto apply = [&](chart_document_t& chart) { return edit(chart, value); };
        if constexpr (requires { value.notes; value.bgm_events; }) {
            auto objects = value.notes;
            objects.insert(objects.end(), value.bgm_events.begin(), value.bgm_events.end());
            return transaction.stage_bms_native_objects(value.chart, objects, bms_existing_edit_kind_t::update, apply);
        } else {
            if constexpr (std::same_as<Edit, delete_edit_t<semantic_layer_edit_value_t>>) {
                if (value.options.remove_from_notes) return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                    "Scoped layer deletion cannot implicitly remove memberships across semantic owners.")}};
            }
            return transaction.stage_bms_layer_settings(value.chart, apply);
        }
    }
};

} // namespace

auto semantic_layer_palette_window_t::from(const chart_document_t& chart,
                                           std::optional<layer_id_t> selected_layer,
                                           std::size_t requested_page, bool reveal_selected,
                                           std::size_t page_size, std::size_t maximum_layers)
    -> std::optional<semantic_layer_palette_window_t> {
    if (page_size == 0 || maximum_layers == 0 || chart.semantic_layers.size() > maximum_layers) {
        return std::nullopt;
    }
    semantic_layer_palette_window_t result;
    std::optional<std::size_t> selected_index;
    for (const auto& layer : chart.semantic_layers) {
        const auto selected = selected_layer == layer.id;
        if (!layer.visible && !selected) {
            continue;
        }
        if (selected) {
            selected_index = result.total_count;
        }
        ++result.total_count;
    }
    result.page_count = result.total_count == 0 ? 0 : 1 + (result.total_count - 1) / page_size;
    if (result.page_count != 0) {
        result.page = reveal_selected && selected_index
                          ? *selected_index / page_size
                          : std::min(requested_page, result.page_count - 1);
    }
    const auto begin = result.page * page_size;
    result.first_row = begin;
    const auto end = std::min(result.total_count, begin + page_size);
    result.layer_indices.reserve(end - begin);
    std::size_t eligible_index{};
    for (std::size_t index = 0; index < chart.semantic_layers.size(); ++index) {
        const auto& layer = chart.semantic_layers[index];
        if (!layer.visible && selected_layer != layer.id) {
            continue;
        }
        if (eligible_index >= begin && eligible_index < end) {
            result.layer_indices.push_back(index);
        }
        ++eligible_index;
        if (eligible_index >= end) {
            break;
        }
    }
    return result;
}

auto semantic_layer_palette_window_t::from(const chart_document_t& chart, std::size_t first_row,
                                           std::size_t row_count, std::size_t maximum_layers)
    -> std::optional<semantic_layer_palette_window_t> {
    const auto count = chart.semantic_layers.size();
    if (row_count == 0 || maximum_layers == 0 || count > maximum_layers) return std::nullopt;
    semantic_layer_palette_window_t result;
    result.total_count = count;
    result.first_row = count == 0 ? 0 : std::min(first_row, count - 1);
    const auto window_count = std::min(row_count, count - result.first_row);
    result.layer_indices.reserve(window_count);
    for (std::size_t offset = 0; offset < window_count; ++offset)
        result.layer_indices.push_back(result.first_row + offset);
    return result;
}

auto apply_semantic_layer_edit(document_history_t& history,
                               const semantic_layer_edit_action_t& action,
                               semantic_layer_edit_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (limits.maximum_layers == 0 || limits.maximum_note_selection == 0 ||
        limits.maximum_chart_objects == 0 || limits.maximum_name_bytes == 0 ||
        limits.maximum_total_name_bytes == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer, object, note selection, and name byte caps must be positive");
        return result;
    }
    auto transaction = document_command_transaction_t(history);
    const auto chart = std::visit([](const auto& edit) { return edit.chart; }, action);
    if (transaction.has_bms_program(chart))
        return transaction.finish_execution(std::visit(scoped_semantic_editor_t{transaction, limits}, action));
    auto& proposed = transaction.document();
    auto outcome = apply_semantic_layer_action(proposed, action, limits, result);
    if (!result.diagnostics.empty() || !outcome.changed) {
        return result;
    }
    return transaction.commit(std::move(outcome.label));
}

auto sound_default_layers_for_placement(const chart_document_t& chart, resource_id_t resource)
    -> std::vector<layer_id_t> {
    std::vector<layer_id_t> layers;
    const auto entry = std::ranges::find(chart.sound_layer_defaults, resource,
                                         &sound_layer_default_t::resource);
    if (entry == chart.sound_layer_defaults.end()) return layers;
    for (const auto id : entry->layers) {
        const auto layer = std::ranges::find(chart.semantic_layers, id, &semantic_layer_t::id);
        if (layer != chart.semantic_layers.end() && !layer->locked) layers.push_back(id);
    }
    return layers;
}

auto resolve_placement_layers(const chart_document_t& chart,
                              const std::optional<std::vector<layer_id_t>>& armed_layers,
                              std::optional<resource_id_t> sound) -> std::vector<layer_id_t> {
    if (armed_layers) return *armed_layers;
    return sound ? sound_default_layers_for_placement(chart, *sound) : std::vector<layer_id_t>{};
}

namespace {

auto validate_sound_layer_defaults_request(const song_document_t& document,
                                           const chart_document_t& chart,
                                           const sound_layer_defaults_action_t& action,
                                           document_command_result_t& result) -> bool {
    if (action.resources.empty() ||
        action.resources.size() > maximum_sound_layer_default_resources) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "sound layer defaults need a bounded nonempty sound selection");
        return false;
    }
    std::unordered_set<std::uint64_t> seen;
    for (const auto id : action.resources) {
        if (!id || std::ranges::find(document.resources, id, &resource_entry_t::id) ==
                       document.resources.end() ||
            !seen.insert(id.value).second) {
            reject(result, diagnostic_code_t::invalid_id,
                   "sound layer defaults refer to an unknown or repeated sound");
            return false;
        }
    }
    if (action.layers.size() > chart.semantic_layers.size()) {
        reject(result, diagnostic_code_t::invalid_id,
               "sound layer defaults list more layers than the chart has");
        return false;
    }
    seen.clear();
    for (const auto id : action.layers) {
        const auto layer = std::ranges::find(chart.semantic_layers, id, &semantic_layer_t::id);
        if (!id || layer == chart.semantic_layers.end() || !seen.insert(id.value).second) {
            reject(result, diagnostic_code_t::invalid_id,
                   "sound layer defaults refer to an unknown or repeated layer");
            return false;
        }
        if (layer->locked && !action.layers.empty()) {
            reject(result, diagnostic_code_t::unsupported_feature,
                   "locked semantic layers cannot become sound defaults");
            return false;
        }
    }
    return true;
}

auto write_sound_layer_defaults(chart_document_t& chart,
                                const sound_layer_defaults_action_t& action) -> bool {
    bool changed{};
    for (const auto resource : action.resources) {
        const auto entry = std::ranges::find(chart.sound_layer_defaults, resource,
                                             &sound_layer_default_t::resource);
        if (action.layers.empty()) {
            if (entry == chart.sound_layer_defaults.end()) continue;
            chart.sound_layer_defaults.erase(entry);
        } else if (entry == chart.sound_layer_defaults.end()) {
            chart.sound_layer_defaults.push_back({.resource = resource, .layers = action.layers});
        } else if (entry->layers != action.layers) {
            entry->layers = action.layers;
        } else {
            continue;
        }
        changed = true;
    }
    return changed;
}

} // namespace

auto apply_sound_layer_defaults(document_history_t& history,
                                const sound_layer_defaults_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    auto transaction = document_command_transaction_t(history);
    if (transaction.has_bms_program(action.chart)) {
        const auto projection = history.document.find_bms_program(action.chart)->build_projection();
        if (!projection.value) return transaction.reject(diagnostic_code_t::invalid_id, "Scoped sound layers have no source catalog.");
        return transaction.finish_execution(transaction.stage_bms_layer_settings(action.chart,
            [&](chart_document_t& chart) -> attempt_t<document_edit_effect_t> {
                if (!validate_sound_layer_defaults_request(projection.value->song(), chart, action, result))
                    return {.diagnostics = result.diagnostics};
                const auto changed = write_sound_layer_defaults(chart, action);
                return {.value = document_edit_effect_t{.changed = changed,
                    .command_label = action.layers.empty() ? "Clear sound layers" : "Set sound layers"}};
            }));
    }
    auto& proposed = transaction.document();
    const auto chart = proposed.find_chart(action.chart);
    if (!action.chart || chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id,
               "sound layer defaults refer to an unknown chart");
        return result;
    }
    if (!validate_sound_layer_defaults_request(proposed, *chart, action, result) ||
        !write_sound_layer_defaults(*chart, action)) {
        return result;
    }
    return transaction.commit(action.layers.empty() ? "Clear sound layers"
                                                         : "Set sound layers");
}

namespace {

auto write_sound_layer_membership(chart_document_t& chart,
                                  const sound_layer_membership_action_t& action) -> bool {
    bool changed{};
    for (const auto resource : action.resources) {
        auto entry = std::ranges::find(chart.sound_layer_defaults, resource,
                                       &sound_layer_default_t::resource);
        if (action.member) {
            if (entry == chart.sound_layer_defaults.end()) {
                chart.sound_layer_defaults.push_back({.resource = resource, .layers = {action.layer}});
                changed = true;
            } else if (std::ranges::find(entry->layers, action.layer) == entry->layers.end()) {
                entry->layers.push_back(action.layer);
                changed = true;
            }
        } else if (entry != chart.sound_layer_defaults.end() &&
                   std::erase(entry->layers, action.layer) != 0) {
            if (entry->layers.empty()) chart.sound_layer_defaults.erase(entry);
            changed = true;
        }
    }
    return changed;
}

} // namespace

namespace {
auto paint_sound_layer_notes(chart_document_t& chart,
                            const sound_layer_membership_action_t& action,
                            document_command_result_t& result) -> std::optional<bool> {
    std::unordered_map<std::uint64_t, std::vector<layer_id_t>> defaults;
    for (const auto resource : action.resources)
        defaults.emplace(resource.value, sound_default_layers_for_placement(chart, resource));
    const auto layers = membership_layer_index(chart);
    bool changed{};
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            if (!note.sound) continue;
            const auto desired = defaults.find(note.sound->value);
            if (desired == defaults.end() || note.semantic_layers == desired->second) continue;
            if (!note.semantic_layers.empty() && !action.overwrite_note_layers) continue;
            if (!validate_clear_note_layers(layers, note, result)) return std::nullopt;
            note.semantic_layers = desired->second;
            changed = true;
        }
    }
    return changed;
}
} // namespace

namespace {
auto paint_unassigned_sound_layer_bgm(chart_document_t& chart,
    const sound_layer_membership_action_t& action) -> bool {
    std::unordered_set<std::uint64_t> sounds;
    for (const auto resource : action.resources) sounds.insert(resource.value);
    bool changed{};
    for (auto& measure : chart.measures) {
        for (auto& event : measure.bgm_events) {
            if (!event.sound || !sounds.contains(event.sound->value) || !event.semantic_layers.empty()) continue;
            event.semantic_layers = {action.layer};
            changed = true;
        }
    }
    return changed;
}
} // namespace

auto apply_sound_layer_membership(document_history_t& history,
                                  const sound_layer_membership_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    auto transaction = document_command_transaction_t(history);
    auto& proposed = transaction.document();
    const auto chart = proposed.find_chart(action.chart);
    if (!action.chart || chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id,
               "sound layer membership refers to an unknown chart");
        return result;
    }
    // Reuse the batch validation: one layer, or none when removing from locked layers.
    const sound_layer_defaults_action_t shape{
        .chart = action.chart, .resources = action.resources,
        .layers = action.member ? std::vector<layer_id_t>{action.layer} : std::vector<layer_id_t>{}};
    if (!validate_sound_layer_defaults_request(proposed, *chart, shape, result)) return result;
    if (!action.member &&
        std::ranges::find(chart->semantic_layers, action.layer, &semantic_layer_t::id) ==
            chart->semantic_layers.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "sound layer membership refers to an unknown layer");
        return result;
    }
    auto changed = write_sound_layer_membership(*chart, action);
    if (action.member) {
        const auto painted = paint_sound_layer_notes(*chart, action, result);
        if (!painted) return result;
        changed = changed || *painted;
    }
    if (action.member && action.assign_unlayered_bgm)
        changed = paint_unassigned_sound_layer_bgm(*chart, action) || changed;
    if (!changed) return result;
    return transaction.commit(action.member ? "Add layer to sounds" : "Remove layer from sounds");
}

namespace {
// The existing bounded pointer index borrows a detached chart for this read-only selection query.
auto selected_sound_resources(chart_document_t chart,
    const add_selected_sounds_to_semantic_layer_action_t& action,
    const semantic_layer_edit_limits_t& limits, document_command_result_t& result)
    -> std::optional<std::vector<resource_id_t>> {
    if (action.notes.size() > limits.maximum_note_selection ||
        action.bgm_events.size() > limits.maximum_note_selection - action.notes.size()) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "selected sounds exceed the configured object selection cap");
        return std::nullopt;
    }
    auto index = build_chart_note_index(chart, limits.maximum_chart_objects,
                                       action.notes.size() + action.bgm_events.size(), result);
    if (!index) return std::nullopt;
    std::unordered_set<std::uint64_t> selected;
    std::unordered_set<std::uint64_t> sounds;
    std::vector<resource_id_t> resources;
    const auto append = [&](object_id_t id, bool bgm) {
        if (!selected.insert(id.value).second) {
            reject(result, diagnostic_code_t::duplicate_id, "selected sounds contain a repeated object ID");
            return false;
        }
        std::optional<resource_id_t> sound;
        if (bgm) {
            const auto* event = index->unique_bgm_event(id, result);
            if (!event) return false;
            sound = event->sound;
        } else {
            const auto* note = index->unique_note(id, result);
            if (!note) return false;
            sound = note->sound;
        }
        if (sound && sounds.insert(sound->value).second) resources.push_back(*sound);
        return true;
    };
    for (const auto id : action.notes) if (!append(id, false)) return std::nullopt;
    for (const auto id : action.bgm_events) if (!append(id, true)) return std::nullopt;
    return resources;
}
} // namespace

auto add_selected_sounds_to_semantic_layer(document_history_t& history, const add_selected_sounds_to_semantic_layer_action_t& action,
    semantic_layer_edit_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    const auto chart = history.read_chart(action.chart);
    if (!action.chart || chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id, "selected sound layers refer to an unknown chart");
        return result;
    }
    const auto resources = selected_sound_resources(*chart, action, limits, result);
    if (!resources || resources->empty()) return result;
    return apply_sound_layer_membership(history, {.chart = action.chart,
                                                  .resources = *resources,
                                                  .layer = action.layer,
                                                  .member = true,
                                                  .overwrite_note_layers = false,
                                                  .assign_unlayered_bgm = true});
}


} // namespace wormedit
