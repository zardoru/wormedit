#include "sys/sound_list_projection.h"

#include <algorithm>
#include <unordered_map>

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

} // namespace wormedit::sys
