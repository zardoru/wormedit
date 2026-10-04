#include "sys/sound_list_projection.h"

#include <algorithm>
#include <unordered_map>
#include <wormedit/core/natural_order.h>
#include <wormedit/core/resource_assignment.h>

namespace wormedit::sys {
namespace {

auto fold(std::string_view text) -> std::string {
    std::string result{text};
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return result;
}

auto use_counts(const song_document_t& document) -> std::unordered_map<std::uint64_t, std::uint32_t> {
    std::unordered_map<std::uint64_t, std::uint32_t> counts;
    for (const auto& chart : document.charts) {
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                if (note.sound) ++counts[note.sound->value];
            }
            for (const auto& event : measure.bgm_events) {
                if (event.sound) ++counts[event.sound->value];
            }
        }
    }
    return counts;
}

auto find_audio_asset(const song_document_t& document, asset_id_t asset) -> const audio_asset_t* {
    const auto found = std::ranges::find(document.audio_assets, asset, &audio_asset_t::id);
    return found == document.audio_assets.end() ? nullptr : &*found;
}

auto joined(const std::vector<std::string>& tags) -> std::string {
    std::string text;
    for (const auto& tag : tags) {
        if (!text.empty()) text += ", ";
        text += tag;
    }
    return text;
}

auto matches(const resource_entry_t& resource, const std::string& search, const std::string& tag)
    -> bool {
    if (!tag.empty() && std::ranges::none_of(resource.tags, [&](const auto& candidate) {
            return fold(candidate) == tag;
        }))
        return false;
    if (search.empty()) return true;
    return fold(resource.name).find(search) != std::string::npos ||
           std::ranges::any_of(resource.tags, [&](const auto& candidate) {
               return fold(candidate).find(search) != std::string::npos;
           });
}

} // namespace

auto effective_sound_kind(const resource_entry_t& resource) -> sound_kind_t {
    if (resource.sound_kind != sound_kind_t::unspecified) return resource.sound_kind;
    const auto has = [&](resource_role_t role) {
        return std::ranges::find(resource.roles, role) != resource.roles.end();
    };
    const bool long_form = has(resource_role_t::music) || has(resource_role_t::bgm);
    return long_form && !has(resource_role_t::sample) ? sound_kind_t::stem
                                                      : sound_kind_t::keysound;
}

auto project_sound_list(const song_document_t& document, const sound_list_request_t& request)
    -> sound_list_t {
    sound_list_t list;
    const auto search = fold(request.search);
    const auto tag = fold(request.tag);
    const auto counts = use_counts(document);
    std::vector<const resource_entry_t*> matched;
    for (const auto& resource : document.resources) {
        const auto* asset = find_audio_asset(document, resource.asset);
        if (!asset) continue;
        for (const auto& candidate : resource.tags) {
            const auto key = fold(candidate);
            if (std::ranges::none_of(list.all_tags,
                                     [&](const auto& known) { return fold(known) == key; }))
                list.all_tags.push_back(candidate);
        }
        if (matches(resource, search, tag)) matched.push_back(&resource);
    }
    std::ranges::sort(list.all_tags, [](const auto& left, const auto& right) {
        return fold(left) < fold(right);
    });
    list.total_matching = matched.size();
    list.first_row = std::min(request.first_row, matched.size());
    const auto last = std::min(matched.size(), list.first_row + request.row_limit);
    for (auto index = list.first_row; index < last; ++index) {
        const auto& resource = *matched[index];
        const auto* asset = find_audio_asset(document, resource.asset);
        const auto count = counts.find(resource.id.value);
        list.rows.push_back({.resource = resource.id,
                             .name = resource.name,
                             .kind = effective_sound_kind(resource),
                             .tag_count = static_cast<std::uint32_t>(resource.tags.size()),
                             .tags_text = joined(resource.tags),
                             .duration_frames = asset->stream.frame_count.value_or(0),
                             .sample_rate = asset->stream.sample_rate,
                             .use_count = count == counts.end() ? 0U : count->second,
                             .missing = resource.missing});
    }
    return list;
}

auto project_sound_properties(const song_document_t& document, resource_id_t resource_id)
    -> sound_properties_t {
    const auto resource = std::ranges::find(document.resources, resource_id, &resource_entry_t::id);
    if (!resource_id || resource == document.resources.end()) return {};
    const auto* asset = find_audio_asset(document, resource->asset);
    if (!asset) return {};
    const auto counts = use_counts(document);
    const auto count = counts.find(resource->id.value);
    return {.valid = true,
            .resource = resource->id,
            .asset = asset->id,
            .name = resource->name,
            .source_path = asset->source_path.generic_string(),
            .kind = effective_sound_kind(*resource),
            .tags = resource->tags,
            .duration_frames = asset->stream.frame_count.value_or(0),
            .sample_rate = asset->stream.sample_rate,
            .channels = asset->stream.channels,
            .use_count = count == counts.end() ? 0U : count->second,
            .missing = resource->missing};
}

namespace {

auto keysound_slot(const resource_entry_t& resource) -> std::string {
    const auto identity = std::ranges::find_if(resource.target_identities, [](const auto& item) {
        return item.family == format_family_t::bms && fold(item.namespace_name) == "wav";
    });
    return identity == resource.target_identities.end() ? std::string{} : identity->identifier;
}

auto keysound_resource_less(const resource_entry_t& left, const resource_entry_t& right,
                            dock_sort_t sort) -> bool {
    if (sort == dock_sort_t::bms_id) {
        const auto left_slot = fold(keysound_slot(left));
        const auto right_slot = fold(keysound_slot(right));
        if (left_slot != right_slot) {
            if (left_slot.empty()) return false;
            if (right_slot.empty()) return true;
            return left_slot < right_slot;
        }
    } else if (left.name != right.name) {
        if (natural_name_less(left.name, right.name)) return true;
        if (natural_name_less(right.name, left.name)) return false;
    }
    return left.id.value < right.id.value;
}

auto is_placeable_keysound(const song_document_t& document, const resource_entry_t& resource)
    -> bool {
    return std::ranges::find(resource.roles, resource_role_t::sample) != resource.roles.end() &&
           find_audio_asset(document, resource.asset) != nullptr &&
           effective_sound_kind(resource) != sound_kind_t::stem;
}

auto keysound_layer_mask(const chart_document_t& chart, const keysound_list_t& list,
                         resource_id_t resource) -> std::uint64_t {
    const auto entry = std::ranges::find(chart.sound_layer_defaults, resource,
                                         &sound_layer_default_t::resource);
    if (entry == chart.sound_layer_defaults.end()) return 0;
    std::uint64_t mask{};
    for (const auto id : entry->layers) {
        const auto layer = std::ranges::find(list.layers, id, &keysound_layer_t::id);
        if (layer != list.layers.end())
            mask |= std::uint64_t{1} << static_cast<unsigned>(layer - list.layers.begin());
    }
    return mask;
}

auto note_conflicts(const note_t& note, const std::vector<layer_id_t>& defaults,
                   const keysound_list_t& list) -> std::uint64_t {
    const auto all = list.layers.size() == 64 ? ~std::uint64_t{0}
        : (std::uint64_t{1} << list.layers.size()) - 1;
    const auto contains_defaults = std::ranges::all_of(defaults, [&](auto id) {
        return std::ranges::find(note.semantic_layers, id) != note.semantic_layers.end();
    });
    if (!contains_defaults || note.semantic_layers.size() > defaults.size() + 1) return all;
    const auto same = note.semantic_layers.size() == defaults.size();
    std::uint64_t matching{};
    for (const auto id : note.semantic_layers) {
        if (!same && std::ranges::find(defaults, id) != defaults.end()) continue;
        const auto layer = std::ranges::find(list.layers, id, &keysound_layer_t::id);
        if (layer != list.layers.end())
            matching |= std::uint64_t{1} << (layer - list.layers.begin());
    }
    return all & ~matching;
}

void populate_note_conflicts(const chart_document_t& chart, keysound_list_t& list) {
    std::unordered_map<std::uint64_t, keysound_row_t*> rows;
    for (auto& row : list.rows) rows.emplace(row.resource.value, &row);
    std::unordered_map<std::uint64_t, const std::vector<layer_id_t>*> defaults;
    for (const auto& entry : chart.sound_layer_defaults)
        defaults.emplace(entry.resource.value, &entry.layers);
    const std::vector<layer_id_t> empty;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!note.sound || note.semantic_layers.empty()) continue;
            const auto row = rows.find(note.sound->value);
            if (row == rows.end()) continue;
            const auto layers = defaults.find(note.sound->value);
            row->second->note_layer_conflict_mask |= note_conflicts(
                note, layers == defaults.end() ? empty : *layers->second, list);
        }
    }
}

} // namespace

auto project_keysound_list(const song_document_t& document, const chart_document_t* chart,
                           std::size_t maximum_rows, dock_sort_t sort,
                           bool ids_available) -> keysound_list_t {
    keysound_list_t list;
    list.bms_capacity = bms_identifier_capacity(document, "WAV");
    if (chart) {
        for (const auto& layer : chart->semantic_layers) {
            if (list.layers.size() == keysound_layer_limit) break;
            list.layers.push_back(
                {.id = layer.id, .name = layer.name, .color = layer.color, .locked = layer.locked});
        }
    }
    std::vector<const resource_entry_t*> matching;
    for (const auto& resource : document.resources)
        if (is_placeable_keysound(document, resource)) matching.push_back(&resource);
    const auto effective_sort = ids_available ? sort : dock_sort_t::natural_name;
    std::ranges::sort(matching, [effective_sort](const auto* left, const auto* right) {
        return keysound_resource_less(*left, *right, effective_sort);
    });
    list.total = matching.size();
    const auto count = std::min(maximum_rows, matching.size());
    list.truncated = count < matching.size();
    for (std::size_t index = 0; index < count; ++index) {
        const auto& resource = *matching[index];
        list.rows.push_back(
            {.resource = resource.id,
             .slot = keysound_slot(resource),
             .name = resource.name.empty() ? "Resource " + std::to_string(resource.id.value)
                                           : resource.name,
             .missing = resource.missing,
             .layer_mask = chart ? keysound_layer_mask(*chart, list, resource.id) : 0});
    }
    if (chart) populate_note_conflicts(*chart, list);
    return list;
}

namespace {
constexpr std::string_view slot_digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
auto slot_number(const target_resource_identity_t& id) -> std::size_t {
    if (id.family != format_family_t::bms || fold(id.namespace_name) != "wav" ||
        id.identifier.size() != 2) return 0;
    const auto text = fold(id.identifier);
    constexpr std::string_view digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    const auto high = digits.find(text[0]), low = digits.find(text[1]);
    if (high == digits.npos || low == digits.npos) return 0;
    return high * 36 + low;
}
auto slot_identifier(std::size_t number) -> std::string {
    return {slot_digits[number / 36], slot_digits[number % 36]};
}
} // namespace

auto project_sound_slots(const song_document_t& document, const sound_slot_request_t& request)
    -> sound_slot_list_t {
    std::array<const resource_entry_t*, 1296> owners{};
    for (const auto& resource : document.resources)
        for (const auto& identity : resource.target_identities)
            if (const auto number = slot_number(identity); number != 0) owners[number] = &resource;
    const auto counts = use_counts(document);
    const auto query = fold(request.search);
    std::vector<std::size_t> matching;
    for (std::size_t number = 1; number < owners.size(); ++number) {
        const auto* resource = owners[number];
        const auto name = resource ? resource->name : "(empty)";
        if (query.empty() || fold(slot_identifier(number)).find(query) != std::string::npos ||
            fold(name).find(query) != std::string::npos) matching.push_back(number);
    }
    sound_slot_list_t list;
    list.total_matching = matching.size();
    const auto selected = std::ranges::find(matching, request.selected_slot);
    if (selected != matching.end())
        list.selected_row = static_cast<std::size_t>(selected - matching.begin());
    list.first_row = std::min(request.first_row, matching.size());
    const auto count = std::min({request.row_limit, std::size_t{128},
                                 matching.size() - list.first_row});
    for (std::size_t index = 0; index < count; ++index) {
        const auto number = matching[list.first_row + index];
        const auto* resource = owners[number];
        const auto uses = resource ? counts.find(resource->id.value) : counts.end();
        list.rows.push_back({.number = static_cast<std::uint16_t>(number),
            .identifier = slot_identifier(number), .resource = resource ? resource->id : resource_id_t{},
            .name = resource ? resource->name : "(empty)",
            .use_count = uses == counts.end() ? 0U : uses->second,
            .missing = resource && resource->missing,
            .placeable = resource && is_placeable_keysound(document, *resource)});
    }
    return list;
}

} // namespace wormedit::sys
