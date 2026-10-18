#include <wormedit/visual/visual_asset.h>

#include <ranges>
#include <utility>

namespace wormedit::visual {

auto resolved_visual_asset_t::from(const song_document_t& document, resource_id_t resource,
                                   const std::filesystem::path& project_directory)
    -> result_t<resolved_visual_asset_t, error_t<failure_code_t>> {
    const auto resource_entry =
        std::ranges::find(document.resources, resource, &resource_entry_t::id);
    if (resource_entry == document.resources.end()) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::not_found, "visual event refers to an unknown resource"});
    }
    if (resource_entry->missing) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::not_found,
            "visual resource is marked missing and must be relinked"});
    }
    const auto asset =
        std::ranges::find(document.visual_assets, resource_entry->asset, &visual_asset_t::id);
    if (asset == document.visual_assets.end()) {
        return std::unexpected(
            error_t<failure_code_t>{failure_code_t::not_found,
                                           "visual resource does not resolve to a visual asset"});
    }
    if (asset->source_path.empty()) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
                                                              "visual asset has no source path"});
    }
    if (!asset->source_path.is_absolute() && project_directory.empty()) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::invalid_request,
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

} // namespace wormedit::visual
