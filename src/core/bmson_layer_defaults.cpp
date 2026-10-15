#include <wormedit/core/bmson_layer_defaults.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wormedit {
namespace {

constexpr std::size_t maximum_layer_name_bytes = 4U * 1024U;

// Sixteen hues spaced 22.5 degrees apart at 72% saturation and 62% lightness.
constexpr std::array layer_colors{
    color_rgba_t{228, 88, 88, 255},   color_rgba_t{228, 141, 88, 255},
    color_rgba_t{228, 193, 88, 255},  color_rgba_t{210, 228, 88, 255},
    color_rgba_t{158, 228, 88, 255},  color_rgba_t{105, 228, 88, 255},
    color_rgba_t{88, 228, 123, 255},  color_rgba_t{88, 228, 176, 255},
    color_rgba_t{88, 228, 228, 255},  color_rgba_t{88, 176, 228, 255},
    color_rgba_t{88, 123, 228, 255},  color_rgba_t{105, 88, 228, 255},
    color_rgba_t{158, 88, 228, 255},  color_rgba_t{210, 88, 228, 255},
    color_rgba_t{228, 88, 193, 255},  color_rgba_t{228, 88, 141, 255},
};

using event_layer_map_t = std::unordered_map<std::uint64_t, layer_id_t>;

struct note_membership_update_t {
    note_t* note{};
    std::vector<layer_id_t> layers;
};

struct bgm_membership_update_t {
    bgm_event_t* event{};
    std::vector<layer_id_t> layers;
};

auto file_stem(std::string_view path) -> std::optional<std::string> {
    const auto separator = path.find_last_of("/\\");
    auto filename = path.substr(separator == std::string_view::npos ? 0 : separator + 1);
    const auto extension = filename.find_last_of('.');
    if (extension != std::string_view::npos && extension != 0) filename = filename.substr(0, extension);
    if (filename.empty() || filename.size() > maximum_layer_name_bytes ||
        filename.find('\0') != std::string_view::npos)
        return std::nullopt;
    return std::string{filename};
}

auto build_channel_layers(const chart_document_t& chart, std::vector<semantic_layer_t>& layers,
                          event_layer_map_t& event_layers, std::string& error) -> bool {
    if (chart.sound_channels.size() > std::numeric_limits<std::uint64_t>::max()) {
        error = "BMSON sound-channel count exceeds the semantic layer ID range.";
        return false;
    }
    layers.reserve(chart.sound_channels.size());
    for (std::size_t index = 0; index < chart.sound_channels.size(); ++index) {
        const auto& channel = chart.sound_channels[index];
        const auto name = file_stem(channel.name);
        if (!name) {
            error = "Could not create a semantic layer name from BMSON sound channel " +
                    std::to_string(index + 1) + ".";
            return false;
        }
        const auto layer = layer_id_t{static_cast<std::uint64_t>(index) + 1};
        layers.push_back({.id = layer, .name = *name, .color = layer_colors[index % layer_colors.size()]});
        for (const auto& event : channel.events) {
            if (!event.id || !event_layers.emplace(event.id.value, layer).second) {
                error = "BMSON sound-channel events do not have unique nonzero IDs.";
                return false;
            }
        }
    }
    return true;
}

auto build_membership_updates(chart_document_t& chart, const event_layer_map_t& event_layers,
                              std::vector<note_membership_update_t>& notes,
                              std::vector<bgm_membership_update_t>& bgm,
                              std::string& error) -> bool {
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            if (!note.sound_channel_event) continue;
            const auto layer = event_layers.find(note.sound_channel_event->value);
            if (layer == event_layers.end()) {
                error = "A BMSON note refers to a missing sound-channel event.";
                return false;
            }
            if (!note.semantic_layers.empty()) {
                error = "A BMSON note already has semantic-layer membership without chart layers.";
                return false;
            }
            notes.push_back({.note = &note, .layers = {layer->second}});
        }
        for (auto& event : measure.bgm_events) {
            if (!event.sound_channel_event) continue;
            const auto layer = event_layers.find(event.sound_channel_event->value);
            if (layer == event_layers.end()) {
                error = "A BMSON BGM event refers to a missing sound-channel event.";
                return false;
            }
            if (!event.semantic_layers.empty()) {
                error = "A BMSON BGM event already has semantic-layer membership without chart layers.";
                return false;
            }
            bgm.push_back({.event = &event, .layers = {layer->second}});
        }
    }
    return true;
}

} // namespace

auto create_bmson_sound_channel_layers(chart_document_t& chart)
    -> bmson_layer_defaults_outcome_t {
    if (!chart.semantic_layers.empty() || chart.sound_channels.empty()) return false;

    std::vector<semantic_layer_t> layers;
    event_layer_map_t event_layers;
    std::vector<note_membership_update_t> note_updates;
    std::vector<bgm_membership_update_t> bgm_updates;
    std::string error;
    if (!build_channel_layers(chart, layers, event_layers, error) ||
        !build_membership_updates(chart, event_layers, note_updates, bgm_updates, error))
        return std::unexpected(std::move(error));

    chart.semantic_layers.swap(layers);
    for (auto& update : note_updates) update.note->semantic_layers.swap(update.layers);
    for (auto& update : bgm_updates) update.event->semantic_layers.swap(update.layers);
    return true;
}

} // namespace wormedit
