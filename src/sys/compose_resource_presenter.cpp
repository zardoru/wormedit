#include "sys/compose_resource_presenter.h"

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <wormedit/core/resource_assignment.h>

namespace wormedit::sys {

// The one place that picks the short on-note label. It is keyed by format family:
// BMS shows the object ID (for example "0A"). osu!mania would show its sample
// name here later; until then every other family falls back to the file stem.
static auto short_sound_label(const resource_entry_t& resource, const std::string& name)
    -> std::string {
    const auto bms_identity = std::ranges::find_if(
        resource.target_identities, [](const auto& identity) {
            return identity.family == format_family_t::bms;
        });
    if (bms_identity != resource.target_identities.end() && !bms_identity->identifier.empty())
        return bms_identity->identifier;
    return std::filesystem::path{name}.stem().string();
}

auto compose_resource_sound_label(const resource_entry_t& resource)
    -> std::pair<std::string, std::string> {
    const auto name = resource.name.empty()
                          ? "Resource " + std::to_string(resource.id.value)
                          : resource.name;
    auto label = short_sound_label(resource, name);
    return {label.empty() ? std::to_string(resource.id.value) : std::move(label), name};
}

auto compose_note_sound_text(std::optional<resource_id_t> sound,
                             const compose_sound_labels_t& labels) -> compose_sound_text_t {
    if (!sound) return {"", ""};
    const auto found = labels.find(sound->value);
    if (found == labels.end()) return {"?", "Unknown sound resource"};
    return {found->second.first.c_str(), found->second.second.c_str()};
}

namespace {
auto make_resource_entry(const song_document_t& document, const resource_entry_t& resource,
                          std::optional<asset_id_t> selected_audio_asset,
                          std::optional<asset_id_t> selected_visual_asset,
                          const resource_reference_counts_t& counts) -> compose_resource_entry_t {
    const auto has_role = [&](resource_role_t role) {
        return std::ranges::find(resource.roles, role) != resource.roles.end();
    };
    const auto audio =
        document.find_audio_asset(resource.asset);
    const auto visual =
        std::ranges::find(document.visual_assets, resource.asset, &visual_asset_t::id);
    const auto bms_identity =
        std::ranges::find_if(resource.target_identities, [](const auto& identity) {
            return identity.family == format_family_t::bms;
        });
    const auto target = bms_identity != resource.target_identities.end()
                            ? bms_identity
                            : resource.target_identities.begin();
    compose_resource_entry_t entry;
    entry.resource = resource.id;
    const auto count = counts.find(resource.id.value);
    entry.use_count = count == counts.end() ? 0 : count->second;
    entry.asset = resource.asset;
    entry.sample = has_role(resource_role_t::sample);
    entry.bgm = has_role(resource_role_t::bgm);
    entry.bga = has_role(resource_role_t::bga);
    entry.audio_asset = audio != nullptr;
    entry.selected_audio_asset = entry.audio_asset && selected_audio_asset == resource.asset;
    entry.image_asset = visual != document.visual_assets.end() &&
                        visual->kind == asset_kind_t::image;
    entry.selected_visual_asset = entry.image_asset &&
                                  selected_visual_asset == resource.asset;
    entry.missing = resource.missing || (audio == nullptr &&
                                         visual == document.visual_assets.end());
    entry.target_index = target == resource.target_identities.end()
                             ? "#" + std::to_string(resource.id.value)
                             : "#" + target->namespace_name + target->identifier;
    entry.name =
        resource.name.empty() ? "Resource " + std::to_string(resource.id.value) : resource.name;
    if (audio != nullptr) {
        entry.source_name = audio->source_path.filename().generic_string();
        entry.source_path = audio->source_path.generic_string();
    } else if (visual != document.visual_assets.end()) {
        entry.source_name = visual->source_path.filename().generic_string();
        entry.source_path = visual->source_path.generic_string();
    }
    return entry;
}
} // namespace

auto compose_resource_catalog_t::from(const song_document_t& document,
                                   std::optional<asset_id_t> selected_audio_asset,
                                   std::size_t maximum_entries,
                                   std::optional<asset_id_t> selected_visual_asset)
    -> compose_resource_catalog_t {
    compose_resource_catalog_t result;
    const auto counts = count_resource_references(document);
    result.entries.reserve(std::min(maximum_entries, document.resources.size()));
    for (const auto& resource : document.resources) {
        const auto has_role = [&](resource_role_t role) {
            return std::ranges::find(resource.roles, role) != resource.roles.end();
        };
        const auto sample = has_role(resource_role_t::sample);
        const auto bgm = has_role(resource_role_t::bgm);
        const auto bga = has_role(resource_role_t::bga);
        if (!sample && !bgm && !bga) {
            continue;
        }
        ++result.total_count;
        if (result.entries.size() == maximum_entries) {
            result.truncated = true;
            continue;
        }

        result.entries.push_back(make_resource_entry(document, resource, selected_audio_asset,
                                                       selected_visual_asset, counts));
    }
    return result;
}

auto find_compose_resource_uses(const chart_document_t& chart, resource_id_t resource)
    -> std::vector<editor_selection_object_t> {
    std::vector<editor_selection_object_t> uses;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.sound == resource) {
                uses.push_back({editor_selection_object_kind_t::note, note.id});
            }
        }
        for (const auto& event : measure.bgm_events) {
            if (event.sound == resource) {
                uses.push_back({editor_selection_object_kind_t::bgm_event, event.id});
            }
        }
    }
    for (const auto& event : chart.bga_events) {
        if (event.resource == resource) {
            uses.push_back({editor_selection_object_kind_t::bga_event, event.id});
        }
    }
    return uses;
}

} // namespace wormedit::sys
