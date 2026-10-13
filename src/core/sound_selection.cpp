#include <algorithm>
#include <ranges>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/sound_selection.h>
#include <wormedit/core/bmson_sound_channel_editor.h>

namespace wormedit {
namespace {
auto channel_key(const chart_document_t& chart, object_id_t event)
    -> std::optional<sound_selection_key_t> {
    for (const auto& channel : chart.sound_channels)
        if (std::ranges::find(channel.events, event, &sound_channel_event_t::id) !=
            channel.events.end())
            return sound_selection_key_t{sound_selection_key_kind_t::channel, channel.id.value};
    return std::nullopt;
}
auto resource_key(const resource_entry_t& resource) -> sound_selection_key_t {
    const bool identity_label =
        std::ranges::any_of(resource.target_identities, [](const auto& identity) {
            return identity.family == format_family_t::bms && !identity.identifier.empty();
        });
    return identity_label
               ? sound_selection_key_t{sound_selection_key_kind_t::resource, resource.id.value}
               : sound_selection_key_t{sound_selection_key_kind_t::asset, resource.asset.value};
}
auto object_key(const song_document_t& song, const chart_document_t& chart,
                std::optional<resource_id_t> sound, std::optional<object_id_t> event)
    -> std::optional<sound_selection_key_t> {
    if (chart.source_family == format_family_t::bmson)
        return event ? channel_key(chart, *event) : std::nullopt;
    return sound ? sound_selection_resource_key(song, chart, *sound) : std::nullopt;
}
auto in_range(const measure_index_t& index, local_position_t position,
              std::optional<local_position_t> end, const sound_selection_query_t& query) -> bool {
    if (!query.visible)
        return true;
    const auto begin = absolute_row(index, position);
    const auto tail = end ? absolute_row(index, *end) : begin;
    return begin && tail && static_cast<double>(*begin) <= query.visible->end_row &&
           static_cast<double>(*tail) >= query.visible->begin_row;
}
auto layers_visible(std::span<const layer_id_t> layers, const chart_document_t& chart,
                    const sound_selection_query_t& query) -> bool {
    if (!query.visible)
        return true;
    const auto has_layer = [&](layer_id_t id) {
        return std::ranges::find(layers, id) != layers.end();
    };
    if (query.solo_layer && !has_layer(*query.solo_layer))
        return false;
    if (!std::ranges::all_of(query.required_layers, has_layer))
        return false;
    for (const auto id : layers) {
        if (std::ranges::find(query.hidden_layers, id) != query.hidden_layers.end())
            return false;
        const auto found = std::ranges::find(chart.semantic_layers, id, &semantic_layer_t::id);
        if (found != chart.semantic_layers.end() && !found->visible)
            return false;
    }
    return true;
}
struct sound_match_index_t {
    [[nodiscard]] static auto from(const song_document_t& song, const chart_document_t& chart, const sound_selection_query_t& query) -> sound_match_index_t;

    std::unordered_set<std::uint64_t> resources, assets, channels;
    std::unordered_map<std::uint64_t, std::uint64_t> resource_assets, event_channels;
};
auto sound_match_index_t::from(const song_document_t& song, const chart_document_t& chart,
                               const sound_selection_query_t& query) -> sound_match_index_t {
    sound_match_index_t index;
    for (const auto& key : query.keys) {
        switch (key.kind) {
        case sound_selection_key_kind_t::resource:
            index.resources.insert(key.value);
            break;
        case sound_selection_key_kind_t::asset:
            index.assets.insert(key.value);
            break;
        case sound_selection_key_kind_t::channel:
            index.channels.insert(key.value);
            break;
        }
    }
    for (const auto& resource : song.resources)
        index.resource_assets.emplace(resource.id.value, resource.asset.value);
    for (const auto& channel : chart.sound_channels)
        for (const auto& event : channel.events)
            index.event_channels.emplace(event.id.value, channel.id.value);
    return index;
}

[[maybe_unused]] auto make_sound_match_index(const song_document_t& song,
                                             const chart_document_t& chart,
                                             const sound_selection_query_t& query)
    -> sound_match_index_t {
    return sound_match_index_t::from(song, chart, query);
}
auto key_matches(const chart_document_t& chart, const sound_match_index_t& index,
                 std::optional<resource_id_t> sound, std::optional<object_id_t> event) -> bool {
    if (chart.source_family == format_family_t::bmson) {
        if (!event)
            return false;
        const auto channel = index.event_channels.find(event->value);
        return channel != index.event_channels.end() && index.channels.contains(channel->second);
    }
    if (!sound)
        return false;
    if (index.resources.contains(sound->value))
        return true;
    const auto asset = index.resource_assets.find(sound->value);
    return asset != index.resource_assets.end() && index.assets.contains(asset->second);
}
auto lane_visible(const chart_document_t& chart, const note_t& note,
                  const sound_selection_query_t& query) -> bool {
    if (!query.visible)
        return true;
    if (std::ranges::find(query.hidden_lanes, note.lane) != query.hidden_lanes.end())
        return false;
    const auto* layout = active_lane_layout_segment(chart, note.position);
    if (!layout)
        return false;
    const auto lane = layout->layout.find(note.lane);
    return lane != nullptr && lane->role != lane_role_t::invisible &&
           lane->role != lane_role_t::autoplay && lane->role != lane_role_t::mine;
}
template <typename Object>
auto matches(const chart_document_t& chart, const sound_match_index_t& sounds,
             const measure_index_t& index, const Object& object,
             const sound_selection_query_t& query) -> bool {
    const bool same_sound = key_matches(chart, sounds, object.sound, object.sound_channel_event);
    return same_sound && in_range(index, object.position, object.end, query) &&
           layers_visible(object.semantic_layers, chart, query);
}
auto sound_bindings(const song_document_t& song, const chart_document_t& chart)
    -> std::unordered_map<std::uint64_t, sound_selection_key_t> {
    const bool bmson = chart.source_family == format_family_t::bmson;
    std::unordered_map<std::uint64_t, sound_selection_key_t> bindings;
    if (bmson) {
        for (const auto& channel : chart.sound_channels)
            for (const auto& event : channel.events)
                bindings.emplace(
                    event.id.value,
                    sound_selection_key_t{sound_selection_key_kind_t::channel, channel.id.value});
    } else {
        for (const auto& resource : song.resources)
            bindings.emplace(resource.id.value, resource_key(resource));
    }
    return bindings;
}
} // namespace

auto sound_selection_resource_key(const song_document_t& song, const chart_document_t& chart,
                                  resource_id_t resource) -> std::optional<sound_selection_key_t> {
    const auto found = std::ranges::find(song.resources, resource, &resource_entry_t::id);
    if (found == song.resources.end())
        return std::nullopt;
    if (chart.source_family == format_family_t::bmson) {
        const auto bindings = bmson_channel_resource_bindings(song, chart.id);
        const auto binding = std::ranges::find(bindings, resource, &bmson_channel_resource_binding_t::resource);
        if (binding == bindings.end() || binding->ambiguous || !binding->channel) return std::nullopt;
        return sound_selection_key_t{sound_selection_key_kind_t::channel, binding->channel->value};
    }
    return resource_key(*found);
}
auto sound_selection_object_key(const song_document_t& song, const chart_document_t& chart,
                                object_id_t object) -> std::optional<sound_selection_key_t> {
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes)
            if (note.id == object)
                return object_key(song, chart, note.sound, note.sound_channel_event);
    for (const auto& measure : chart.measures)
        for (const auto& bgm : measure.bgm_events)
            if (bgm.id == object)
                return object_key(song, chart, bgm.sound, bgm.sound_channel_event);
    // A projected stem cut carries the retained channel event ID.
    return chart.source_family == format_family_t::bmson ? channel_key(chart, object)
                                                         : std::nullopt;
}
auto sound_selection_object_keys(const song_document_t& song, const chart_document_t& chart,
                                 std::span<const object_id_t> objects)
    -> std::vector<sound_selection_key_t> {
    if (objects.empty())
        return {};
    std::unordered_set<std::uint64_t> selected;
    for (const auto id : objects)
        selected.insert(id.value);
    const bool bmson = chart.source_family == format_family_t::bmson;
    const auto bindings = sound_bindings(song, chart);
    std::vector<sound_selection_key_t> result;
    std::set<std::pair<sound_selection_key_kind_t, std::uint64_t>> seen;
    const auto add = [&](const auto& object) {
        if (!selected.contains(object.id.value))
            return;
        std::optional<std::uint64_t> binding;
        if (bmson && object.sound_channel_event)
            binding = object.sound_channel_event->value;
        if (!bmson && object.sound)
            binding = object.sound->value;
        if (!binding)
            return;
        const auto found = bindings.find(*binding);
        if (found != bindings.end() && seen.emplace(found->second.kind, found->second.value).second)
            result.push_back(found->second);
    };
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            add(note);
        for (const auto& bgm : measure.bgm_events)
            add(bgm);
    }
    return result;
}
auto select_same_sound(const song_document_t& song, const chart_document_t& chart,
                       const sound_selection_query_t& query)
    -> std::vector<editor_selection_object_t> {
    std::vector<editor_selection_object_t> result;
    const auto built = build_measure_index(chart);
    const auto* index = std::get_if<measure_index_t>(&built);
    if (!index || query.keys.empty())
        return result;
    const auto sounds = sound_match_index_t::from(song, chart, query);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!lane_visible(chart, note, query))
                continue;
            if (matches(chart, sounds, *index, note, query))
                result.push_back({editor_selection_object_kind_t::note, note.id});
        }
    }
    for (const auto& measure : chart.measures)
        for (const auto& bgm : measure.bgm_events)
            if (matches(chart, sounds, *index, bgm, query))
                result.push_back({editor_selection_object_kind_t::bgm_event, bgm.id});
    return result;
}


} // namespace wormedit
