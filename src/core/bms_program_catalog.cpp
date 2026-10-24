#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/metadata_inheritance.h>
#include "bms_native_content_internal.h"
#include <algorithm>
#include <set>

namespace wormedit {
namespace {
template<class Value>
void refresh_catalog(std::vector<Value>& cached, const std::vector<Value>& canonical) {
    for (auto& value : cached) {
        const auto found = std::ranges::find(canonical, value.id, &Value::id);
        if (found != canonical.end()) value = *found;
    }
}
template<class Value>
void remove_retired_catalog(std::vector<Value>& cached, const std::vector<Value>& before,
    const std::vector<Value>& canonical) {
    std::erase_if(cached, [&](const auto& value) {
        return std::ranges::find(before, value.id, &Value::id) != before.end() &&
            std::ranges::find(canonical, value.id, &Value::id) == canonical.end();
    });
}
template<class Value, class Id>
void include_catalog(std::vector<Value>& cached, const std::vector<Value>& canonical, Id id) {
    if (!id || std::ranges::find(cached, id, &Value::id) != cached.end()) return;
    const auto found = std::ranges::find(canonical, id, &Value::id);
    if (found != canonical.end()) cached.push_back(*found);
}
void include_resource(bms_prepared_program_t& prepared, const song_document_t& song, resource_id_t id) {
    include_catalog(prepared.resources, song.resources, id);
}
void include_native(bms_prepared_program_t& prepared, const song_document_t& song,
    const bms_native_content_t& content) {
    if (const auto id = event_resource(content.payload())) include_resource(prepared, song, *id);
    if (const auto* channel = std::get_if<sound_channel_t>(&content.payload()))
        include_catalog(prepared.audio_assets, song.audio_assets, channel->source_clip.source);
}
}

auto bms_prepared_program_t::with_catalog(const song_document_t& song) const -> bms_prepared_program_t {
    auto result = *this;
    refresh_catalog(result.resources, song.resources);
    refresh_catalog(result.audio_assets, song.audio_assets);
    refresh_catalog(result.visual_assets, song.visual_assets);
    for (const auto& node : program.nodes()) {
        const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
        if (!chart) continue;
        for (const auto& item : chart->items) {
            if (const auto* declaration = std::get_if<bms_resource_declaration_t>(&item.value))
                include_resource(result, song, declaration->resource);
            if (const auto* native = std::get_if<bms_native_content_t>(&item.value))
                include_native(result, song, *native);
        }
    }
    const auto references = metadata_audio_references_t::from(song, settings.overrides);
    for (const auto id : references.value.assets) include_catalog(result.audio_assets, song.audio_assets, id);
    for (const auto& defaults : sound_layer_defaults) include_resource(result, song, defaults.resource);
    for (const auto& resource : result.resources) {
        include_catalog(result.audio_assets, song.audio_assets, resource.asset);
        include_catalog(result.visual_assets, song.visual_assets, resource.asset);
    }
    return result;
}

namespace {
template<class Asset>
void record_asset_resolutions(std::vector<bms_asset_resolution_t>& records,
    const std::vector<Asset>& before, const std::vector<Asset>& after) {
    for (const auto& asset : before) {
        const auto found = std::ranges::find(after, asset.id, &Asset::id);
        if (found == after.end() || found->source_path == asset.source_path) continue;
        const auto record = bms_asset_resolution_t{asset.source_path, found->source_path};
        if (std::ranges::find(records, record) == records.end()) records.push_back(record);
    }
}
}
auto bms_prepared_program_t::with_resolved_catalog(const song_document_t& song) const -> bms_prepared_program_t {
    auto result = with_catalog(song);
    record_asset_resolutions(result.resolved_asset_paths, audio_assets, result.audio_assets);
    record_asset_resolutions(result.resolved_asset_paths, visual_assets, result.visual_assets);
    return result;
}

auto reconcile_bms_program_catalogs(const song_document_t& before_document, song_document_t& document) -> diagnostic_list_t {
    for (auto& owner : document.prepared_bms_programs) {
        if (!owner) continue;
        auto prepared = owner->prepared().with_catalog(document);
        remove_retired_catalog(prepared.resources, before_document.resources, document.resources);
        remove_retired_catalog(prepared.audio_assets, before_document.audio_assets, document.audio_assets);
        remove_retired_catalog(prepared.visual_assets, before_document.visual_assets, document.visual_assets);
        const auto& before = owner->prepared();
        if (prepared.resources == before.resources && prepared.audio_assets == before.audio_assets &&
            prepared.visual_assets == before.visual_assets) continue;
        auto replacement = bms_program_document_t::from(owner->difficulty(), std::move(prepared), owner->evaluation());
        if (!replacement.value || !replacement.ok()) {
            diagnostic_list_t diagnostics;
            for (const auto& item : replacement.diagnostics)
                diagnostics.push_back(error(diagnostic_code_t::invalid_binding, item.message));
            return diagnostics;
        }
        owner = std::move(*replacement.value);
    }
    return {};
}
} // namespace wormedit
