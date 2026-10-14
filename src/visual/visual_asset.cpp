#include <wormedit/visual/visual_asset.h>

#include <ranges>
#include <utility>

namespace wormedit::visual {

auto resolved_visual_asset_t::from(const song_document_t& document, resource_id_t resource,
                                   const std::filesystem::path& project_directory)
    -> visual_asset_resolution_outcome_t {
    const auto resource_entry =
        std::ranges::find(document.resources, resource, &resource_entry_t::id);
    if (resource_entry == document.resources.end()) {
        return std::unexpected(error_t<visual_asset_status_t>{
            visual_asset_status_t::unknown_resource, "visual event refers to an unknown resource"});
    }
    if (resource_entry->missing) {
        return std::unexpected(error_t<visual_asset_status_t>{
            visual_asset_status_t::missing_resource,
            "visual resource is marked missing and must be relinked"});
    }
    const auto asset =
        std::ranges::find(document.visual_assets, resource_entry->asset, &visual_asset_t::id);
    if (asset == document.visual_assets.end()) {
        return std::unexpected(
            error_t<visual_asset_status_t>{visual_asset_status_t::unknown_asset,
                                           "visual resource does not resolve to a visual asset"});
    }
    if (asset->source_path.empty()) {
        return std::unexpected(error_t<visual_asset_status_t>{visual_asset_status_t::empty_path,
                                                              "visual asset has no source path"});
    }
    if (!asset->source_path.is_absolute() && project_directory.empty()) {
        return std::unexpected(error_t<visual_asset_status_t>{
            visual_asset_status_t::relative_path_without_base,
            "relative visual asset path requires a project directory"});
    }
    const auto path = asset->source_path.is_absolute()
                          ? asset->source_path.lexically_normal()
                          : (project_directory / asset->source_path).lexically_normal();
    return resolved_visual_asset_t{.resource = resource_entry->id,
                                   .asset = asset->id,
                                   .kind = asset->kind,
                                   .path = path,
                                   .declared_width = asset->width,
                                   .declared_height = asset->height};
}

auto visual_asset_resolution_result_t::from(visual_asset_resolution_outcome_t&& outcome)
    -> visual_asset_resolution_result_t {
    if (!outcome)
        return {.status = outcome.error().code,
                .source = std::nullopt,
                .message = std::move(outcome.error().message)};
    return {.status = visual_asset_status_t::ready, .source = std::move(*outcome), .message = {}};
}

auto resolve_visual_asset(const song_document_t& document, resource_id_t resource,
                          const std::filesystem::path& project_directory)
    -> visual_asset_resolution_result_t {
    return visual_asset_resolution_result_t::from(
        resolved_visual_asset_t::from(document, resource, project_directory));
}

} // namespace wormedit::visual
