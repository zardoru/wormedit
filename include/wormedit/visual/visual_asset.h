#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::visual {

// Decoder-neutral result of resolving a visual resource through a document.
// Static images and video adapters share this value without depending on one
// another's codec implementation.
struct resolved_visual_asset_t {
    [[nodiscard]] static auto from(const song_document_t& document, resource_id_t resource,
                                   const std::filesystem::path& project_directory)
        -> result_t<resolved_visual_asset_t, error_t<failure_code_t>>;

    resource_id_t resource;
    asset_id_t asset;
    asset_kind_t kind{asset_kind_t::image};
    std::filesystem::path path;
    std::optional<std::uint32_t> declared_width;
    std::optional<std::uint32_t> declared_height;
};

} // namespace wormedit::visual
