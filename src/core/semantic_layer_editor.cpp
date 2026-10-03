#include <wormedit/core/semantic_layer_editor.h>

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

auto valid_name(std::string_view name, const semantic_layer_edit_limits_t& limits) -> bool {
    return !name.empty() && name.size() <= limits.maximum_name_bytes &&
           name.find('\0') == std::string_view::npos && valid_utf8(name);
}

auto add_size(std::size_t& total, std::size_t amount, std::size_t maximum) -> bool {
    if (total > maximum || amount > maximum - total) {
        return false;
    }
    total += amount;
    return true;
}

auto validate_layer_names(const chart_document_t& chart, const semantic_layer_edit_limits_t& limits,
                          document_command_result_t& result) -> bool {
    std::size_t total{};
    for (const auto& layer : chart.semantic_layers) {
        if (!valid_name(layer.name, limits)) {
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

auto valid_color(semantic_layer_rgba_t color) -> bool {
    const auto in_range = [](double component) {
        return std::isfinite(component) && component >= 0.0 && component <= 1.0;
    };
    return in_range(color.red) && in_range(color.green) && in_range(color.blue) &&
           in_range(color.alpha);
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

auto unique_layer(chart_document_t& chart, layer_id_t id, document_command_result_t& result)
    -> semantic_layer_t* {
    if (!id) {
        reject(result, diagnostic_code_t::invalid_id,
               "semantic layer command uses the reserved zero layer ID");
        return nullptr;
    }
    const auto found = std::ranges::find(chart.semantic_layers, id, &semantic_layer_t::id);
    if (found == chart.semantic_layers.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "semantic layer command refers to an unknown layer");
        return nullptr;
    }
    if (std::ranges::count(chart.semantic_layers, id, &semantic_layer_t::id) != 1) {
        reject(result, diagnostic_code_t::duplicate_id,
               "semantic layer ID is ambiguous because it appears more than once");
        return nullptr;
    }
    return &*found;
}

struct chart_note_index_t {
    std::unordered_map<std::uint64_t, std::size_t> object_id_counts;
    std::unordered_map<std::uint64_t, note_t*> notes;
};

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
    return index;
}

auto unique_note(chart_note_index_t& index, object_id_t id, document_command_result_t& result)
    -> note_t* {
    if (!id) {
        reject(result, diagnostic_code_t::invalid_id,
               "semantic layer selection uses the reserved zero note ID");
        return nullptr;
    }
    const auto count = index.object_id_counts.find(id.value);
    if (count == index.object_id_counts.end() || count->second == 0) {
        reject(result, diagnostic_code_t::invalid_id,
               "semantic layer selection refers to an unknown note");
        return nullptr;
    }
    if (count->second != 1) {
        reject(result, diagnostic_code_t::duplicate_id,
               "note ID is ambiguous because it appears more than once in the chart");
        return nullptr;
    }
    const auto note = index.notes.find(id.value);
    if (note == index.notes.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "semantic layer selection ID does not identify a note");
        return nullptr;
    }
    return note->second;
}

auto next_layer_id(const chart_document_t& chart) -> std::optional<layer_id_t> {
    auto maximum = std::uint64_t{};
    for (const auto& layer : chart.semantic_layers) {
        maximum = std::max(maximum, layer.id.value);
    }
    if (maximum == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return layer_id_t{maximum + 1};
}

auto layer_membership_note_count(const chart_document_t& chart, layer_id_t layer) -> std::size_t {
    auto count = std::size_t{};
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (std::ranges::find(note.semantic_layers, layer) != note.semantic_layers.end()) {
                ++count;
            }
        }
    }
    return count;
}

enum class membership_operation_t { assign, remove, toggle };

auto apply_membership(chart_document_t& chart, layer_id_t layer_id,
                      const std::vector<object_id_t>& note_ids, membership_operation_t operation,
                      const semantic_layer_edit_limits_t& limits, document_command_result_t& result)
    -> bool {
    if (note_ids.size() > limits.maximum_note_selection) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer note selection exceeds the configured cap");
        return false;
    }
    auto* layer = unique_layer(chart, layer_id, result);
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
        auto* note = unique_note(*note_index, note_id, result);
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

auto editable_membership_layer(const membership_layer_index_t& layers, layer_id_t id,
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
        if (!editable_membership_layer(layers, layer_id, result)) return false;
    }
    return true;
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
        auto* note = unique_note(*note_index, note_id, result);
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
    if (target && !editable_membership_layer(layers, *target, result)) return false;
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

auto insert_semantic_layer(chart_document_t& chart,
                           const insert_semantic_layer_action_t& edit,
                           const semantic_layer_edit_limits_t& limits,
                           document_command_result_t& result) -> bool {
    if (chart.semantic_layers.size() >= limits.maximum_layers) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer insertion would exceed the configured layer cap");
        return false;
    }
    if (!valid_name(edit.name, limits)) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "semantic layer name is empty, invalid UTF-8, contains NUL, or exceeds its byte cap");
        return false;
    }
    if (!valid_color(edit.color)) {
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
    if (!add_size(total_names, edit.name.size(), limits.maximum_total_name_bytes)) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer names exceed the configured total byte cap");
        return false;
    }
    const auto id = next_layer_id(chart);
    if (!id.has_value()) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer ID sequence is exhausted");
        return false;
    }
    chart.semantic_layers.push_back({.id = *id,
                                     .name = edit.name,
                                     .color = stored_color(edit.color),
                                     .visible = edit.visible,
                                     .locked = edit.locked});
    return true;
}

auto update_semantic_layer(chart_document_t& chart,
                           const update_semantic_layer_action_t& edit,
                           const semantic_layer_edit_limits_t& limits,
                           document_command_result_t& result) -> bool {
    auto* layer = unique_layer(chart, edit.layer, result);
    if (layer == nullptr) {
        return false;
    }
    if (!valid_name(edit.name, limits)) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "semantic layer name is empty, invalid UTF-8, contains NUL, or exceeds its byte cap");
        return false;
    }
    if (!valid_color(edit.color)) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer RGBA components must be finite values between zero and one");
        return false;
    }
    const auto replacement_color = stored_color(edit.color);
    std::size_t total_names{};
    for (const auto& item : chart.semantic_layers) {
        const auto& name = item.id == edit.layer ? edit.name : item.name;
        if (!add_size(total_names, name.size(), limits.maximum_total_name_bytes)) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "semantic layer names exceed the configured total byte cap");
            return false;
        }
    }
    const auto changed = layer->name != edit.name || layer->color != replacement_color ||
                         layer->visible != edit.visible || layer->locked != edit.locked;
    if (changed) {
        layer->name = edit.name;
        layer->color = replacement_color;
        layer->visible = edit.visible;
        layer->locked = edit.locked;
    }
    return changed;
}

auto delete_semantic_layer(chart_document_t& chart,
                           const delete_semantic_layer_action_t& edit,
                           const semantic_layer_edit_limits_t& limits,
                           document_command_result_t& result) -> bool {
    auto* layer = unique_layer(chart, edit.layer, result);
    if (layer == nullptr) {
        return false;
    }
    if (layer->locked) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "locked semantic layers cannot be deleted");
        return false;
    }
    const auto assigned_count = layer_membership_note_count(chart, edit.layer);
    if (assigned_count != 0 && !edit.remove_from_notes) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "semantic layer is assigned to notes; explicit removal was not requested");
        return false;
    }
    if (assigned_count > limits.maximum_note_selection) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer deletion exceeds the configured note removal cap");
        return false;
    }
    if (edit.remove_from_notes) {
        for (auto& measure : chart.measures) {
            for (auto& note : measure.notes) {
                std::erase(note.semantic_layers, edit.layer);
            }
        }
    }
    for (auto& entry : chart.sound_layer_defaults) std::erase(entry.layers, edit.layer);
    std::erase_if(chart.sound_layer_defaults, [](const auto& entry) { return entry.layers.empty(); });
    std::erase_if(chart.semantic_layers,
                  [&edit](const auto& item) { return item.id == edit.layer; });
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
    if constexpr (std::is_same_v<edit_t, insert_semantic_layer_action_t>) {
        return "Insert semantic layer";
    } else if constexpr (std::is_same_v<edit_t, update_semantic_layer_action_t>) {
        return "Update semantic layer";
    } else if constexpr (std::is_same_v<edit_t, delete_semantic_layer_action_t>) {
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

template <typename edit_t>
auto membership_operation(const edit_t&) -> membership_operation_t {
    if constexpr (std::is_same_v<edit_t, assign_notes_to_semantic_layer_action_t>) {
        return membership_operation_t::assign;
    } else if constexpr (std::is_same_v<edit_t, remove_notes_from_semantic_layer_action_t>) {
        return membership_operation_t::remove;
    } else {
        return membership_operation_t::toggle;
    }
}

template <typename edit_t>
auto apply_typed_semantic_edit(chart_document_t& chart, const edit_t& edit,
                               const semantic_layer_edit_limits_t& limits,
                               document_command_result_t& result)
    -> semantic_layer_edit_outcome_t {
    if constexpr (std::is_same_v<edit_t, insert_semantic_layer_action_t>) {
        return {.label = semantic_edit_label<edit_t>(),
                .changed = insert_semantic_layer(chart, edit, limits, result)};
    } else if constexpr (std::is_same_v<edit_t, update_semantic_layer_action_t>) {
        return {.label = semantic_edit_label<edit_t>(),
                .changed = update_semantic_layer(chart, edit, limits, result)};
    } else if constexpr (std::is_same_v<edit_t, delete_semantic_layer_action_t>) {
        return {.label = semantic_edit_label<edit_t>(),
                .changed = delete_semantic_layer(chart, edit, limits, result)};
    } else if constexpr (std::is_same_v<edit_t, reorder_semantic_layers_action_t>) {
        return {.label = semantic_edit_label<edit_t>(),
                .changed = reorder_semantic_layers(chart, edit, limits, result)};
    } else if constexpr (std::is_same_v<edit_t, clear_semantic_layer_membership_action_t>) {
        return {.label = "Clear semantic layer membership",
                .changed = replace_note_memberships(chart, edit.notes, std::nullopt, false, limits, result)};
    } else if constexpr (std::is_same_v<edit_t, replace_notes_semantic_layer_action_t>) {
        return {.label = "Replace semantic layer membership",
                .changed = replace_note_memberships(chart, edit.notes, edit.layer, edit.toggle, limits, result)};
    } else {
        const auto operation = membership_operation(edit);
        return {.label = semantic_edit_label<edit_t>(operation),
                .changed = apply_membership(chart, edit.layer, edit.notes, operation, limits, result)};
    }
}

auto apply_semantic_layer_action(song_document_t& proposed,
                                 const semantic_layer_edit_action_t& action,
                                 const semantic_layer_edit_limits_t& limits,
                                 document_command_result_t& result)
    -> semantic_layer_edit_outcome_t {
    return std::visit(
        [&](const auto& edit) -> semantic_layer_edit_outcome_t {
            const auto chart = std::ranges::find(proposed.charts, edit.chart, &chart_document_t::id);
            if (!edit.chart || chart == proposed.charts.end() ||
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

} // namespace

auto make_semantic_layer_palette_window(const chart_document_t& chart,
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

auto make_semantic_layer_palette_window(const chart_document_t& chart,
                                        std::size_t first_row, std::size_t row_count,
                                        std::size_t maximum_layers)
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

auto apply_semantic_layer_edit(document_history_t& history, std::uint64_t expected_revision,
                               const semantic_layer_edit_action_t& action,
                               semantic_layer_edit_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "semantic layer editor document revision is stale");
        return result;
    }
    if (limits.maximum_layers == 0 || limits.maximum_note_selection == 0 ||
        limits.maximum_chart_objects == 0 || limits.maximum_name_bytes == 0 ||
        limits.maximum_total_name_bytes == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "semantic layer, object, note selection, and name byte caps must be positive");
        return result;
    }
    auto proposed = history.document;
    auto outcome = apply_semantic_layer_action(proposed, action, limits, result);
    if (!result.diagnostics.empty() || !outcome.changed) {
        return result;
    }
    return commit_document_command(history, std::move(proposed), std::move(outcome.label));
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

auto apply_sound_layer_defaults(document_history_t& history, std::uint64_t expected_revision,
                                const sound_layer_defaults_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "sound layer defaults document revision is stale");
        return result;
    }
    auto proposed = history.document;
    const auto chart = std::ranges::find(proposed.charts, action.chart, &chart_document_t::id);
    if (!action.chart || chart == proposed.charts.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "sound layer defaults refer to an unknown chart");
        return result;
    }
    if (!validate_sound_layer_defaults_request(proposed, *chart, action, result) ||
        !write_sound_layer_defaults(*chart, action)) {
        return result;
    }
    return commit_document_command(history, std::move(proposed),
                                   action.layers.empty() ? "Clear sound layers"
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

auto apply_sound_layer_membership(document_history_t& history, std::uint64_t expected_revision,
                                  const sound_layer_membership_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "sound layer membership document revision is stale");
        return result;
    }
    auto proposed = history.document;
    const auto chart = std::ranges::find(proposed.charts, action.chart, &chart_document_t::id);
    if (!action.chart || chart == proposed.charts.end()) {
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
    if (!write_sound_layer_membership(*chart, action)) return result;
    return commit_document_command(history, std::move(proposed),
                                   action.member ? "Add layer to sounds" : "Remove layer from sounds");
}

} // namespace wormedit
