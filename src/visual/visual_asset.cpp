#include <wormedit/visual/visual_asset.h>

#include <ranges>

namespace wormedit::visual {

auto resolve_visual_asset(const song_document_t& document, resource_id_t resource,
                          const std::filesystem::path& project_directory)
    -> visual_asset_resolution_result_t {
    const auto resource_entry =
        std::ranges::find(document.resources, resource, &resource_entry_t::id);
    if (resource_entry == document.resources.end()) {
        return {.status = visual_asset_status_t::unknown_resource,
                .source = std::nullopt,
                .message = "visual event refers to an unknown resource"};
    }
    if (resource_entry->missing) {
        return {.status = visual_asset_status_t::missing_resource,
                .source = std::nullopt,
                .message = "visual resource is marked missing and must be relinked"};
    }
    const auto asset =
        std::ranges::find(document.visual_assets, resource_entry->asset, &visual_asset_t::id);
    if (asset == document.visual_assets.end()) {
        return {.status = visual_asset_status_t::unknown_asset,
                .source = std::nullopt,
                .message = "visual resource does not resolve to a visual asset"};
    }
    if (asset->source_path.empty()) {
        return {.status = visual_asset_status_t::empty_path,
                .source = std::nullopt,
                .message = "visual asset has no source path"};
    }
    if (!asset->source_path.is_absolute() && project_directory.empty()) {
        return {.status = visual_asset_status_t::relative_path_without_base,
                .source = std::nullopt,
                .message = "relative visual asset path requires a project directory"};
    }
    const auto path = asset->source_path.is_absolute()
                          ? asset->source_path.lexically_normal()
                          : (project_directory / asset->source_path).lexically_normal();
    return {.status = visual_asset_status_t::ready,
            .source = resolved_visual_asset_t{.resource = resource_entry->id,
                                              .asset = asset->id,
                                              .kind = asset->kind,
                                              .path = path,
                                              .declared_width = asset->width,
                                              .declared_height = asset->height},
            .message = {}};
}

} // namespace wormedit::visual
