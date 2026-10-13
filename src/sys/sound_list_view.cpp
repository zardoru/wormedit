#include "core/text_util.h"
#include <wormedit/core/document_index.h>
#include "sys/sound_list_view.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <wormedit/core/natural_order.h>
#include <wormedit/core/resource_assignment.h>

namespace wormedit::sys {
namespace {
using wormedit::text_util::ascii_lowercase;

struct sound_resource_metadata_t {
    resource_reference_counts_t counts;
    std::unordered_map<std::uint64_t, std::string> paths;
    auto uses(resource_id_t id) const -> std::uint64_t {
        const auto found = counts.find(id.value);
        return found == counts.end() ? 0 : found->second;
    }
    auto path(asset_id_t id) const -> std::string_view {
        const auto found = paths.find(id.value);
        return found == paths.end() ? std::string_view{} : found->second;
    }
};
auto sound_resource_metadata(const song_document_t& document) -> sound_resource_metadata_t {
    sound_resource_metadata_t result{.counts = count_resource_references(document)};
    for (const auto& asset : document.audio_assets)
        result.paths.emplace(asset.id.value, asset.source_path.generic_string());
    return result;
}
void append_keysound_metadata(keysound_list_t& list, const song_document_t& document,
                               const sound_resource_metadata_t& metadata) {
    std::unordered_map<std::uint64_t, asset_id_t> assets;
    for (const auto& resource : document.resources) assets.emplace(resource.id.value, resource.asset);
    const auto append = [&](keysound_row_t& row) {
        row.use_count = metadata.uses(row.resource);
        const auto asset = assets.find(row.resource.value);
        if (asset != assets.end()) row.source_path = metadata.path(asset->second);
    };
    for (auto& row : list.rows) append(row);
    for (auto& row : list.selected_rows) append(row);
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
            return text_util::ascii_iequal(candidate, tag);
        }))
        return false;
    if (search.empty()) return true;
    return ascii_lowercase(resource.name).find(search) != std::string::npos ||
           std::ranges::any_of(resource.tags, [&](const auto& candidate) {
               return ascii_lowercase(candidate).find(search) != std::string::npos;
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

auto build_sound_list_view(const song_document_t& document, const sound_list_request_t& request)
    -> sound_list_t {
    sound_list_t list;
    const auto search = ascii_lowercase(request.search);
    const auto tag = ascii_lowercase(request.tag);
    const auto counts = count_resource_references(document);
    std::vector<const resource_entry_t*> matched;
    for (const auto& resource : document.resources) {
        const auto* asset = document.find_audio_asset(resource.asset);
        if (!asset) continue;
        for (const auto& candidate : resource.tags) {
            const auto key = ascii_lowercase(candidate);
            if (std::ranges::none_of(list.all_tags,
                                     [&](const auto& known) { return text_util::ascii_iequal(known, key); }))
                list.all_tags.push_back(candidate);
        }
        if (matches(resource, search, tag)) matched.push_back(&resource);
    }
    std::ranges::sort(list.all_tags, [](const auto& left, const auto& right) {
        return text_util::ascii_iless(left, right);
    });
    list.total_matching = matched.size();
    list.first_row = std::min(request.first_row, matched.size());
    const auto last = std::min(matched.size(), list.first_row + request.row_limit);
    for (auto index = list.first_row; index < last; ++index) {
        const auto& resource = *matched[index];
        const auto* asset = document.find_audio_asset(resource.asset);
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

auto build_sound_properties_view(const song_document_t& document, resource_id_t resource_id)
    -> sound_properties_t {
    const auto resource = std::ranges::find(document.resources, resource_id, &resource_entry_t::id);
    if (!resource_id || resource == document.resources.end()) return {};
    const auto* asset = document.find_audio_asset(resource->asset);
    if (!asset) return {};
    const auto counts = count_resource_references(document);
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
        return item.family == format_family_t::bms && text_util::ascii_iequal(item.namespace_name, "wav");
    });
    return identity == resource.target_identities.end() ? std::string{} : identity->identifier;
}

auto keysound_resource_less(const resource_entry_t& left, const resource_entry_t& right,
                            dock_sort_t sort) -> bool {
    if (sort == dock_sort_t::bms_id) {
        const auto left_slot = ascii_lowercase(keysound_slot(left));
        const auto right_slot = ascii_lowercase(keysound_slot(right));
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

auto is_placeable_keysound(const song_document_t& document, const resource_entry_t& resource,
                           const std::unordered_set<std::uint64_t>* audio_asset_ids = nullptr)
    -> bool {
    return std::ranges::find(resource.roles, resource_role_t::sample) != resource.roles.end() &&
           (audio_asset_ids ? audio_asset_ids->contains(resource.asset.value)
                            : document.find_audio_asset(resource.asset) != nullptr) &&
           effective_sound_kind(resource) != sound_kind_t::stem;
}

auto is_listed_sound(const song_document_t& document, const chart_document_t* chart,
                     const resource_entry_t& resource,
                     const std::unordered_set<std::uint64_t>* audio_asset_ids) -> bool {
    if (is_placeable_keysound(document, resource, audio_asset_ids)) return true;
    if (!chart || chart->source_family != format_family_t::bmson ||
        effective_sound_kind(resource) != sound_kind_t::stem) return false;
    return std::ranges::find(resource.roles, resource_role_t::bgm) != resource.roles.end() &&
           (audio_asset_ids ? audio_asset_ids->contains(resource.asset.value)
                            : document.find_audio_asset(resource.asset) != nullptr);
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

auto keysound_matches_request(const resource_entry_t& resource,
                              const chart_document_t* chart,
                              const keysound_list_request_t& request,
                              std::string_view folded_search) -> bool {
    if (!folded_search.empty() &&
        ascii_lowercase(resource.name).find(folded_search) == std::string::npos &&
        ascii_lowercase(keysound_slot(resource)).find(folded_search) == std::string::npos)
        return false;
    const auto group_id = resource.sound_group ? resource.sound_group->value : 0;
    if (request.group_filter_active) {
        const auto chosen = std::ranges::find(request.group_ids, group_id);
        if ((group_id == 0 && !request.include_ungrouped) ||
            (group_id != 0 && chosen == request.group_ids.end()))
            return false;
    }
    if (request.layer_filter == keysound_layer_filter_kind_t::any) return true;
    if (!chart) return request.layer_filter == keysound_layer_filter_kind_t::no_layer;
    const auto defaults = std::ranges::find(chart->sound_layer_defaults, resource.id,
        &sound_layer_default_t::resource);
    const bool has_layers = defaults != chart->sound_layer_defaults.end() &&
                            !defaults->layers.empty();
    if (request.layer_filter == keysound_layer_filter_kind_t::no_layer) return !has_layers;
    return defaults != chart->sound_layer_defaults.end() &&
           std::ranges::find(defaults->layers, layer_id_t{request.layer_id}) !=
                              defaults->layers.end();
}

auto group_is_collapsed(const keysound_list_request_t& request, std::uint64_t group_id) -> bool {
    return group_id == 0 ? request.ungrouped_collapsed
                         : std::ranges::find(request.collapsed_group_ids, group_id) !=
                               request.collapsed_group_ids.end();
}

auto keysound_row_from_resource(const chart_document_t* chart,
                                const keysound_list_t& list,
                                const resource_entry_t& resource) -> keysound_row_t {
    return {.resource = resource.id,
            .slot = keysound_slot(resource),
            .name = resource.name.empty() ? "Resource " + std::to_string(resource.id.value)
                                          : resource.name,
            .missing = resource.missing,
            .layer_mask = chart ? keysound_layer_mask(*chart, list, resource.id) : 0,
            .sound_group_id = resource.sound_group ? resource.sound_group->value : 0};
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
    for (auto& row : list.selected_rows) rows.emplace(row.resource.value, &row);
    std::unordered_map<std::uint64_t, keysound_selection_row_t*> selection_rows;
    for (auto& row : list.selection_rows)
        selection_rows.emplace(row.resource.value, &row);
    std::unordered_map<std::uint64_t, const std::vector<layer_id_t>*> defaults;
    for (const auto& entry : chart.sound_layer_defaults)
        defaults.emplace(entry.resource.value, &entry.layers);
    const std::vector<layer_id_t> empty;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!note.sound || note.semantic_layers.empty()) continue;
            const auto layers = defaults.find(note.sound->value);
            const auto conflicts = note_conflicts(
                note, layers == defaults.end() ? empty : *layers->second, list);
            if (const auto row = rows.find(note.sound->value); row != rows.end())
                row->second->note_layer_conflict_mask |= conflicts;
            if (const auto row = selection_rows.find(note.sound->value);
                row != selection_rows.end())
                row->second->note_layer_conflict_mask |= conflicts;
        }
    }
}

} // namespace

namespace {
auto build_keysound_list_legacy_impl_view(const song_document_t& document,
    const chart_document_t* chart, std::size_t maximum_rows, dock_sort_t sort,
    bool ids_available, const std::unordered_set<std::uint64_t>* audio_asset_ids)
    -> keysound_list_t {
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
        if (is_listed_sound(document, chart, resource, audio_asset_ids)) matching.push_back(&resource);
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
             .layer_mask = chart ? keysound_layer_mask(*chart, list, resource.id) : 0,
             .sound_group_id = resource.sound_group ? resource.sound_group->value : 0});
    }
    if (chart) populate_note_conflicts(*chart, list);
    append_keysound_metadata(list, document, sound_resource_metadata(document));
    return list;
}
} // namespace

auto build_keysound_list_view(const song_document_t& document, const chart_document_t* chart,
                           std::size_t maximum_rows, dock_sort_t sort,
                           bool ids_available) -> keysound_list_t {
    return build_keysound_list_legacy_impl_view(document, chart, maximum_rows, sort,
                                              ids_available, nullptr);
}

namespace {
struct keysound_section_projection_t {
    std::uint64_t id{};
    std::string name;
    std::vector<const resource_entry_t*> members;
    bool collapsed{};
};

auto section_requested(const keysound_list_request_t& request, std::uint64_t id) -> bool {
    return !request.group_filter_active ||
           std::ranges::find(request.group_ids, id) != request.group_ids.end();
}

void append_section_projection(keysound_list_t& list,
                               const keysound_section_projection_t& section,
                               std::size_t first, std::size_t last,
                               std::uint64_t primary_resource_id,
                               const chart_document_t* chart) {
    if (section.members.empty()) return;
    auto display_index = list.display_total++;
    if (display_index >= first && display_index < last)
        list.display_rows.push_back({.kind = keysound_list_t::display_row_t::kind_t::group_header,
            .group_id = section.id, .group_name = section.name,
            .group_sound_count = section.members.size(), .expanded = !section.collapsed});
    for (const auto* member : section.members) {
        list.advance_rows.push_back({.resource = member->id, .missing = member->missing});
        if (section.collapsed) continue;
        if (member->id.value == primary_resource_id)
            list.primary_display_index = list.display_total;
        list.listed_resources.push_back(member->id);
        display_index = list.display_total++;
        if (display_index >= first && display_index < last) {
            list.display_rows.push_back({.kind = keysound_list_t::display_row_t::kind_t::sound,
                .group_id = section.id, .resource = member->id});
            list.rows.push_back(keysound_row_from_resource(chart, list, *member));
        }
    }
}

auto make_keysound_sections(const song_document_t& document,
                           const keysound_list_request_t& request,
                           std::unordered_map<std::uint64_t, std::size_t>& indices,
                           std::size_t& ungrouped_index)
    -> std::vector<keysound_section_projection_t> {
    std::vector<keysound_section_projection_t> sections;
    sections.reserve(document.sound_groups.size() + 1);
    for (const auto& group : document.sound_groups) {
        if (!section_requested(request, group.id.value)) continue;
        indices.emplace(group.id.value, sections.size());
        sections.push_back({.id = group.id.value, .name = group.name,
                            .collapsed = group_is_collapsed(request, group.id.value)});
    }
    ungrouped_index = sections.size();
    if (!request.group_filter_active || request.include_ungrouped)
        sections.push_back({.id = 0, .name = "Ungrouped",
                            .collapsed = request.ungrouped_collapsed});
    return sections;
}

auto collect_keysound_resources(const song_document_t& document,
                                const chart_document_t* chart,
                                const keysound_list_request_t& request,
                                std::string_view search, keysound_list_t& list,
                                std::vector<keysound_section_projection_t>& sections,
                                const std::unordered_map<std::uint64_t, std::size_t>& indices,
                                std::size_t ungrouped_index,
                                const std::unordered_set<std::uint64_t>* audio_asset_ids,
                                const sound_resource_metadata_t& metadata)
    -> document_id_index_t<resource_entry_t> {
    auto resources = document_id_index_t<resource_entry_t>::from({});
    for (const auto& resource : document.resources) {
        if (!is_listed_sound(document, chart, resource, audio_asset_ids)) continue;
        list.all_resources.push_back(resource.id);
        resources.add(resource);
        if (!matches_resource_use(request.use_filter, metadata.uses(resource.id), resource.missing)) continue;
        const auto row_search = ascii_lowercase(std::string{metadata.path(resource.asset)}).find(search) !=
            std::string::npos ? std::string_view{} : search;
        if (!keysound_matches_request(resource, chart, request, row_search)) continue;
        const auto group_id = resource.sound_group ? resource.sound_group->value : 0;
        const auto found = indices.find(group_id);
        if (group_id == 0) {
            if (ungrouped_index < sections.size())
                sections[ungrouped_index].members.push_back(&resource);
        } else if (found != indices.end()) {
            sections[found->second].members.push_back(&resource);
        }
    }
    return resources;
}

void sort_keysound_sections(std::vector<keysound_section_projection_t>& sections,
                            dock_sort_t sort) {
    for (auto& section : sections)
        std::ranges::sort(section.members, [&](const auto* left, const auto* right) {
            return keysound_resource_less(*left, *right, sort);
        });
    std::ranges::sort(sections, [](const auto& left, const auto& right) {
        if (left.id == 0) return false;
        if (right.id == 0) return true;
        if (natural_name_less(left.name, right.name)) return true;
        if (natural_name_less(right.name, left.name)) return false;
        return left.id < right.id;
    });
}

void append_selected_keysounds(keysound_list_t& list, const chart_document_t* chart,
                               const keysound_list_request_t& request,
                               const document_id_index_t<resource_entry_t>& resources) {
    for (const auto id : request.selected_resource_ids) {
        const auto* found = resources.find(resource_id_t{id});
        if (!found) continue;
        const auto row = keysound_row_from_resource(chart, list, *found);
        list.selection_rows.push_back({.resource = row.resource,
            .group_id = row.sound_group_id, .layer_mask = row.layer_mask,
            .slot = row.slot, .missing = row.missing});
        const auto visible = std::ranges::find(list.rows, row.resource,
                                                &keysound_row_t::resource) != list.rows.end();
        if (request.include_selected_metadata && !visible) list.selected_rows.push_back(row);
    }
}
} // namespace

namespace {
auto build_keysound_list_request_impl_view(const song_document_t& document,
    const chart_document_t* chart, const keysound_list_request_t& request,
    const std::unordered_set<std::uint64_t>* audio_asset_ids) -> keysound_list_t {
    keysound_list_t list;
    list.bms_capacity = bms_identifier_capacity(document, "WAV");
    if (chart) {
        for (const auto& layer : chart->semantic_layers) {
            if (list.layers.size() == keysound_layer_limit) break;
            list.layers.push_back(
                {.id = layer.id, .name = layer.name, .color = layer.color, .locked = layer.locked});
        }
        list.filter_layer_total = chart->semantic_layers.size();
        list.filter_layer_first = std::min(request.filter_layer_first,
                                           list.filter_layer_total);
        const auto choice_count = std::min(request.filter_layer_limit,
                                           list.filter_layer_total - list.filter_layer_first);
        list.filter_layers.reserve(choice_count);
        for (std::size_t index = 0; index < choice_count; ++index) {
            const auto& layer = chart->semantic_layers[list.filter_layer_first + index];
            list.filter_layers.push_back({.id = layer.id, .name = layer.name,
                .color = layer.color, .locked = layer.locked});
        }
        const auto selected = std::ranges::find(chart->semantic_layers,
            layer_id_t{request.layer_id}, &semantic_layer_t::id);
        if (request.layer_id != 0 && selected != chart->semantic_layers.end()) {
            list.selected_filter_layer_valid = true;
            list.selected_filter_layer_name = selected->name;
        }
    }
    std::unordered_map<std::uint64_t, std::size_t> section_indices;
    std::size_t ungrouped_index{};
    auto sections = make_keysound_sections(document, request, section_indices, ungrouped_index);
    const auto metadata = sound_resource_metadata(document);
    const auto resources_by_id = collect_keysound_resources(document, chart, request,
        ascii_lowercase(request.search), list, sections, section_indices, ungrouped_index, audio_asset_ids, metadata);
    list.total = list.all_resources.size();
    sort_keysound_sections(sections, request.ids_available ? request.sort
                                                          : dock_sort_t::natural_name);
    const auto first = request.first_row;
    const auto last = request.row_limit > std::numeric_limits<std::size_t>::max() - first
        ? std::numeric_limits<std::size_t>::max() : first + request.row_limit;
    for (const auto& section : sections)
        append_section_projection(list, section, first, last,
                                  request.primary_resource_id, chart);
    append_selected_keysounds(list, chart, request, resources_by_id);
    append_keysound_metadata(list, document, metadata);
    list.display_first_row = std::min(first, list.display_total);
    list.display_truncated = list.display_first_row != 0 ||
                             list.display_rows.size() < list.display_total;
    list.truncated = list.display_truncated;
    if (chart) populate_note_conflicts(*chart, list);
    return list;
}
} // namespace

auto build_keysound_list_view(const song_document_t& document, const chart_document_t* chart,
                           const keysound_list_request_t& request) -> keysound_list_t {
    return build_keysound_list_request_impl_view(document, chart, request, nullptr);
}

namespace {
auto projection_identity(const song_document_t& document, const chart_document_t* chart)
    -> keysound_list_cache_t::identity_t {
    return {.project_id = document.id.value,
            .project_address = reinterpret_cast<std::uintptr_t>(&document),
            .revision = document.revision,
            .chart_id = chart ? chart->id.value : 0,
            .chart_address = reinterpret_cast<std::uintptr_t>(chart)};
}

auto same_request(const keysound_list_cache_t::request_key_t& key,
                  const keysound_list_request_t& request) -> bool {
    return key.search == request.search && key.use_filter == request.use_filter &&
        std::ranges::equal(key.selected_resource_ids, request.selected_resource_ids) &&
        key.include_selected_metadata == request.include_selected_metadata &&
        std::ranges::equal(key.group_ids, request.group_ids) &&
        key.group_filter_active == request.group_filter_active &&
        key.include_ungrouped == request.include_ungrouped &&
        key.layer_filter == request.layer_filter && key.layer_id == request.layer_id &&
        std::ranges::equal(key.collapsed_group_ids, request.collapsed_group_ids) &&
        key.ungrouped_collapsed == request.ungrouped_collapsed &&
        key.first_row == request.first_row && key.row_limit == request.row_limit &&
        key.primary_resource_id == request.primary_resource_id &&
        key.filter_layer_first == request.filter_layer_first &&
        key.filter_layer_limit == request.filter_layer_limit && key.sort == request.sort &&
        key.ids_available == request.ids_available;
}

auto copy_request_key(const keysound_list_cache_t::identity_t& identity,
                      const keysound_list_request_t& request)
    -> keysound_list_cache_t::request_key_t {
    return {.identity = identity, .search = std::string{request.search},
        .use_filter = request.use_filter,
        .selected_resource_ids = std::vector<std::uint64_t>(request.selected_resource_ids.begin(),
                                                            request.selected_resource_ids.end()),
        .include_selected_metadata = request.include_selected_metadata,
        .group_ids = std::vector<std::uint64_t>(request.group_ids.begin(), request.group_ids.end()),
        .group_filter_active = request.group_filter_active,
        .include_ungrouped = request.include_ungrouped, .layer_filter = request.layer_filter,
        .layer_id = request.layer_id,
        .collapsed_group_ids = std::vector<std::uint64_t>(request.collapsed_group_ids.begin(),
                                                           request.collapsed_group_ids.end()),
        .ungrouped_collapsed = request.ungrouped_collapsed,
        .first_row = request.first_row, .row_limit = request.row_limit,
        .primary_resource_id = request.primary_resource_id,
        .filter_layer_first = request.filter_layer_first,
        .filter_layer_limit = request.filter_layer_limit,
        .sort = request.sort, .ids_available = request.ids_available};
}
} // namespace

auto keysound_list_cache_t::asset_ids_for(const song_document_t& document)
    -> const std::unordered_set<std::uint64_t>& {
    const identity_t identity{.project_id = document.id.value,
        .project_address = reinterpret_cast<std::uintptr_t>(&document),
        .revision = document.revision, .chart_id = 0, .chart_address = 0};
    if (asset_index_valid_ && asset_index_identity_ == identity) return audio_asset_ids_;
    asset_index_identity_ = identity;
    audio_asset_ids_.clear();
    audio_asset_ids_.reserve(document.audio_assets.size());
    for (const auto& asset : document.audio_assets) audio_asset_ids_.insert(asset.id.value);
    asset_index_valid_ = true;
    ++asset_index_builds_;
    return audio_asset_ids_;
}

auto build_keysound_list_cached_view(keysound_list_cache_t& cache,
    const song_document_t& document, const chart_document_t* chart, std::size_t maximum_rows,
    dock_sort_t sort, bool ids_available) -> const keysound_list_t& {
    const keysound_list_cache_t::legacy_key_t key{.identity = projection_identity(document, chart),
        .maximum_rows = maximum_rows, .sort = sort, .ids_available = ids_available};
    if (cache.legacy_valid_ && cache.legacy_key_ == key) return cache.legacy_result_;
    const auto& asset_ids = cache.asset_ids_for(document);
    cache.legacy_result_ = build_keysound_list_legacy_impl_view(document, chart, maximum_rows,
        sort, ids_available, &asset_ids);
    cache.legacy_key_ = key;
    cache.legacy_valid_ = true;
    ++cache.projection_builds_;
    return cache.legacy_result_;
}

auto build_keysound_list_cached_view(keysound_list_cache_t& cache,
    const song_document_t& document, const chart_document_t* chart,
    const keysound_list_request_t& request) -> const keysound_list_t& {
    const auto identity = projection_identity(document, chart);
    if (cache.request_valid_ && cache.request_key_.identity == identity &&
        same_request(cache.request_key_, request)) return cache.request_result_;
    const auto owned_key = copy_request_key(identity, request);
    const auto& asset_ids = cache.asset_ids_for(document);
    cache.request_result_ = build_keysound_list_request_impl_view(document, chart, request,
                                                                &asset_ids);
    cache.request_key_ = std::move(owned_key);
    cache.request_valid_ = true;
    ++cache.projection_builds_;
    return cache.request_result_;
}

namespace {
constexpr std::string_view slot_digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
auto slot_number(const target_resource_identity_t& id) -> std::size_t {
    if (id.family != format_family_t::bms || !text_util::ascii_iequal(id.namespace_name, "wav") ||
        id.identifier.size() != 2) return 0;
    const auto text = ascii_lowercase(id.identifier);
    constexpr std::string_view digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    const auto high = digits.find(text[0]), low = digits.find(text[1]);
    if (high == digits.npos || low == digits.npos) return 0;
    return high * 36 + low;
}
auto slot_identifier(std::size_t number) -> std::string {
    return {slot_digits[number / 36], slot_digits[number % 36]};
}
auto sound_slot_matches(const resource_entry_t* resource, std::size_t number,
                        const sound_resource_metadata_t& metadata,
                        resource_use_filter_t filter, std::string_view query) -> bool {
    const auto uses = resource ? metadata.uses(resource->id) : 0;
    if (!matches_resource_use(filter, uses, resource && resource->missing)) return false;
    const auto path = resource ? metadata.path(resource->asset) : std::string_view{};
    const auto name = resource ? resource->name : "(empty)";
    return ascii_lowercase(slot_identifier(number)).find(query) != std::string::npos ||
        ascii_lowercase(name).find(query) != std::string::npos ||
        ascii_lowercase(path).find(query) != std::string::npos;
}
} // namespace

auto build_sound_slots_view(const song_document_t& document, const sound_slot_request_t& request)
    -> sound_slot_list_t {
    std::array<const resource_entry_t*, 1296> owners{};
    for (const auto& resource : document.resources)
        for (const auto& identity : resource.target_identities)
            if (const auto number = slot_number(identity); number != 0) owners[number] = &resource;
    const auto metadata = sound_resource_metadata(document);
    const auto query = ascii_lowercase(request.search);
    std::vector<std::size_t> matching;
    for (std::size_t number = 1; number < owners.size(); ++number) {
        const auto* resource = owners[number];
        if (sound_slot_matches(resource, number, metadata, request.use_filter, query))
            matching.push_back(number);
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
        const auto uses = resource ? metadata.uses(resource->id) : 0;
        list.rows.push_back({.number = static_cast<std::uint16_t>(number),
            .identifier = slot_identifier(number), .resource = resource ? resource->id : resource_id_t{},
            .name = resource ? resource->name : "(empty)",
            .use_count = uses,
            .missing = resource && resource->missing,
            .placeable = resource && is_placeable_keysound(document, *resource),
            .source_path = resource ? std::string{metadata.path(resource->asset)} : std::string{}});
    }
    return list;
}

} // namespace wormedit::sys

namespace wormedit::sys {

namespace {
struct fallback_cache_t {
    std::uint64_t song{};
    std::uint64_t revision{};
    std::filesystem::path base;
    std::vector<keysound_fallback_t> entries;
    bool valid{};
    std::vector<resource_id_t> missing_resources;
};
} // namespace

auto build_keysound_fallbacks_view(const song_document_t& document,
                                const std::filesystem::path& asset_base, std::size_t limit)
    -> std::vector<keysound_fallback_t> {
    static thread_local fallback_cache_t cache;
    std::vector<resource_id_t> missing_resources;
    for (const auto& resource : document.resources)
        if (resource.missing) missing_resources.push_back(resource.id);
    if (!cache.valid || cache.missing_resources != missing_resources || cache.song != document.id.value || cache.revision != document.revision ||
        cache.base != asset_base) {
        cache = {document.id.value, document.revision, asset_base, {}, true, std::move(missing_resources)};
        audio_directory_cache_t listings;
        for (const auto& resource : document.resources) {
            const auto* asset = document.find_audio_asset(resource.asset);
            if (!asset || !resource.missing) continue;
            auto result = resolve_audio_fallback(asset->source_path, asset_base, &listings);
            if (result.outcome == audio_fallback_outcome_t::fallback ||
                result.outcome == audio_fallback_outcome_t::ambiguous)
                cache.entries.push_back({resource.id, std::move(result)});
        }
    }
    auto entries = cache.entries;
    if (entries.size() > limit) entries.resize(limit);
    return entries;
}

} // namespace wormedit::sys
