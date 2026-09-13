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

} // namespace

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
                       "semantic layer edit refers to an unknown or ambiguous chart");
                return;
            }
            if (chart->semantic_layers.size() > limits.maximum_layers) {
                reject(result, diagnostic_code_t::invalid_resolution,
                       "selected chart exceeds the semantic layer cap");
                return;
            }
            if (!validate_layer_ids(*chart, result) ||
                !validate_layer_names(*chart, limits, result)) {
                return;
            }

            if constexpr (std::is_same_v<edit_t, insert_semantic_layer_action_t>) {
                if (chart->semantic_layers.size() >= limits.maximum_layers) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "semantic layer insertion would exceed the configured layer cap");
                    return;
                }
                if (!valid_name(edit.name, limits)) {
                    reject(
                        result, diagnostic_code_t::unsupported_feature,
                        "semantic layer name is empty, invalid UTF-8, contains NUL, or exceeds its "
                        "byte cap");
                    return;
                }
                if (!valid_color(edit.color)) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "semantic layer RGBA components must be finite values between zero and "
                           "one");
                    return;
                }
                std::size_t total_names{};
                for (const auto& layer : chart->semantic_layers) {
                    if (!add_size(total_names, layer.name.size(),
                                  limits.maximum_total_name_bytes)) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               "semantic layer names exceed the configured total byte cap");
                        return;
                    }
                }
                if (!add_size(total_names, edit.name.size(), limits.maximum_total_name_bytes)) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "semantic layer names exceed the configured total byte cap");
                    return;
                }
                const auto id = next_layer_id(*chart);
                if (!id.has_value()) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "semantic layer ID sequence is exhausted");
                    return;
                }
                chart->semantic_layers.push_back({.id = *id,
                                                  .name = edit.name,
                                                  .color = stored_color(edit.color),
                                                  .visible = edit.visible,
                                                  .locked = edit.locked});
                command_label = "Insert semantic layer";
                changed = true;
            } else if constexpr (std::is_same_v<edit_t, update_semantic_layer_action_t>) {
                auto* layer = unique_layer(*chart, edit.layer, result);
                if (layer == nullptr) {
                    return;
                }
                if (!valid_name(edit.name, limits)) {
                    reject(
                        result, diagnostic_code_t::unsupported_feature,
                        "semantic layer name is empty, invalid UTF-8, contains NUL, or exceeds its "
                        "byte cap");
                    return;
                }
                if (!valid_color(edit.color)) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "semantic layer RGBA components must be finite values between zero and "
                           "one");
                    return;
                }
                const auto replacement_color = stored_color(edit.color);
                std::size_t total_names{};
                for (const auto& item : chart->semantic_layers) {
                    const auto& name = item.id == edit.layer ? edit.name : item.name;
                    if (!add_size(total_names, name.size(), limits.maximum_total_name_bytes)) {
                        reject(result, diagnostic_code_t::invalid_resolution,
                               "semantic layer names exceed the configured total byte cap");
                        return;
                    }
                }
                changed = layer->name != edit.name || layer->color != replacement_color ||
                          layer->visible != edit.visible || layer->locked != edit.locked;
                if (!changed) {
                    return;
                }
                layer->name = edit.name;
                layer->color = replacement_color;
                layer->visible = edit.visible;
                layer->locked = edit.locked;
                command_label = "Update semantic layer";
            } else if constexpr (std::is_same_v<edit_t, delete_semantic_layer_action_t>) {
                auto* layer = unique_layer(*chart, edit.layer, result);
                if (layer == nullptr) {
                    return;
                }
                if (layer->locked) {
                    reject(result, diagnostic_code_t::unsupported_feature,
                           "locked semantic layers cannot be deleted");
                    return;
                }
                const auto assigned_note_count = layer_membership_note_count(*chart, edit.layer);
                if (assigned_note_count != 0 && !edit.remove_from_notes) {
                    reject(
                        result, diagnostic_code_t::unsupported_feature,
                        "semantic layer is assigned to notes; explicit removal was not requested");
                    return;
                }
                if (assigned_note_count > limits.maximum_note_selection) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "semantic layer deletion exceeds the configured note removal cap");
                    return;
                }
                if (edit.remove_from_notes) {
                    for (auto& measure : chart->measures) {
                        for (auto& note : measure.notes) {
                            std::erase(note.semantic_layers, edit.layer);
                        }
                    }
                }
                std::erase_if(chart->semantic_layers,
                              [&edit](const auto& item) { return item.id == edit.layer; });
                command_label = "Delete semantic layer";
                changed = true;
            } else if constexpr (std::is_same_v<edit_t, reorder_semantic_layers_action_t>) {
                if (edit.ordered_layers.size() > limits.maximum_layers ||
                    edit.ordered_layers.size() != chart->semantic_layers.size()) {
                    reject(result, diagnostic_code_t::invalid_resolution,
                           "semantic layer order must contain every chart layer exactly once");
                    return;
                }
                std::unordered_set<std::uint64_t> ordered_ids;
                std::unordered_map<std::uint64_t, const semantic_layer_t*> layers_by_id;
                layers_by_id.reserve(chart->semantic_layers.size());
                for (const auto& layer : chart->semantic_layers) {
                    layers_by_id.emplace(layer.id.value, &layer);
                }
                std::vector<semantic_layer_t> reordered;
                reordered.reserve(edit.ordered_layers.size());
                for (const auto id : edit.ordered_layers) {
                    if (!id) {
                        reject(result, diagnostic_code_t::invalid_id,
                               "semantic layer order uses the reserved zero layer ID");
                        return;
                    }
                    if (!ordered_ids.insert(id.value).second) {
                        reject(result, diagnostic_code_t::duplicate_id,
                               "semantic layer order contains a duplicate layer ID");
                        return;
                    }
                    const auto layer = layers_by_id.find(id.value);
                    if (layer == layers_by_id.end()) {
                        reject(result, diagnostic_code_t::invalid_id,
                               "semantic layer order refers to an unknown layer");
                        return;
                    }
                    reordered.push_back(*layer->second);
                }
                changed = false;
                for (std::size_t index = 0; index < reordered.size(); ++index) {
                    if (reordered[index].id != chart->semantic_layers[index].id) {
                        changed = true;
                        break;
                    }
                }
                if (!changed) {
                    return;
                }
                chart->semantic_layers = std::move(reordered);
                command_label = "Reorder semantic layers";
            } else {
                const auto layer_id = edit.layer;
                const auto operation = [&]() {
                    if constexpr (std::is_same_v<edit_t, assign_notes_to_semantic_layer_action_t>) {
                        return membership_operation_t::assign;
                    } else if constexpr (std::is_same_v<
                                             edit_t, remove_notes_from_semantic_layer_action_t>) {
                        return membership_operation_t::remove;
                    } else {
                        return membership_operation_t::toggle;
                    }
                }();
                changed = apply_membership(*chart, layer_id, edit.notes, operation, limits, result);
                if (std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
                        return diagnostic.severity == diagnostic_severity_t::error;
                    })) {
                    changed = false;
                    return;
                }
                switch (operation) {
                case membership_operation_t::assign:
                    command_label = "Assign notes to semantic layer";
                    break;
                case membership_operation_t::remove:
                    command_label = "Remove notes from semantic layer";
                    break;
                case membership_operation_t::toggle:
                    command_label = "Toggle semantic layer membership";
                    break;
                }
            }
        },
        action);

    if (!result.diagnostics.empty() || !changed) {
        return result;
    }
    return commit_document_command(history, std::move(proposed), std::move(command_label));
}

} // namespace wormedit
