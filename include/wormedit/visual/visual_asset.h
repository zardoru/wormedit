#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::visual {

enum class visual_asset_status_t : std::uint8_t {
    ready,
    unknown_resource,
    missing_resource,
    unknown_asset,
    empty_path,
    relative_path_without_base,
};

// Decoder-neutral result of resolving a visual resource through a document.
// Static images and video adapters share this value without depending on one
// another's codec implementation.
struct resolved_visual_asset_t {
    [[nodiscard]] static auto from(const song_document_t& document, resource_id_t resource,
                                   const std::filesystem::path& project_directory)
        -> result_t<resolved_visual_asset_t, error_t<visual_asset_status_t>>;

    resource_id_t resource;
    asset_id_t asset;
    asset_kind_t kind{asset_kind_t::image};
    std::filesystem::path path;
    std::optional<std::uint32_t> declared_width;
    std::optional<std::uint32_t> declared_height;
};

using visual_asset_resolution_outcome_t =
    result_t<resolved_visual_asset_t, error_t<visual_asset_status_t>>;

// Compatibility boundary for existing sys/UI callers; migrate them before removing this adapter.
struct visual_asset_resolution_result_t {
    [[nodiscard]] static auto from(visual_asset_resolution_outcome_t&& outcome)
        -> visual_asset_resolution_result_t;

    visual_asset_status_t status{visual_asset_status_t::unknown_resource};
    std::optional<resolved_visual_asset_t> source;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == visual_asset_status_t::ready && source.has_value();
    }
};

// Resolves one resource through the immutable document inventory. Relative
// paths require an explicit project directory; process state is never used.
[[nodiscard]] auto resolve_visual_asset(const song_document_t& document, resource_id_t resource,
                                        const std::filesystem::path& project_directory)
    -> visual_asset_resolution_result_t;

} // namespace wormedit::visual
