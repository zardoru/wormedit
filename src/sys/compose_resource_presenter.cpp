#include "sys/compose_resource_presenter.h"

#include <algorithm>
#include <ranges>

namespace wormedit::sys {

auto make_compose_resource_catalog(const song_document_t& document,
                                   std::optional<asset_id_t> selected_audio_asset,
                                   std::size_t maximum_entries) -> compose_resource_catalog_t {
    compose_resource_catalog_t result;
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

        const auto audio =
            std::ranges::find(document.audio_assets, resource.asset, &audio_asset_t::id);
        const auto visual =
            std::ranges::find(document.visual_assets, resource.asset, &visual_asset_t::id);
        const auto bms_identity =
            std::ranges::find_if(resource.target_identities, [](const auto& identity) {
                return identity.family == format_family_t::bms;
            });
        const auto target = bms_identity != resource.target_identities.end()
                                ? bms_identity
                                : resource.target_identities.begin();
        auto& entry = result.entries.emplace_back();
        entry.resource = resource.id;
        entry.asset = resource.asset;
        entry.sample = sample;
        entry.bgm = bgm;
        entry.bga = bga;
        entry.audio_asset = audio != document.audio_assets.end();
        entry.selected_audio_asset = entry.audio_asset && selected_audio_asset == resource.asset;
        entry.missing = resource.missing || (audio == document.audio_assets.end() &&
                                             visual == document.visual_assets.end());
        entry.target_index = target == resource.target_identities.end()
                                 ? "#" + std::to_string(resource.id.value)
                                 : "#" + target->namespace_name + target->identifier;
        entry.name =
            resource.name.empty() ? "Resource " + std::to_string(resource.id.value) : resource.name;
        if (audio != document.audio_assets.end()) {
            entry.source_name = audio->source_path.filename().generic_string();
        } else if (visual != document.visual_assets.end()) {
            entry.source_name = visual->source_path.filename().generic_string();
        }
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
