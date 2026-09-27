#pragma once

#include <wormedit/core/document.h>

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>

namespace wormedit {

enum class project_asset_copy_status_t {
    copied,
    reused,
    invalid_request,
    source_unavailable,
    destination_collision,
    io_error,
    cancelled,
};

struct project_asset_copy_request_t {
    std::filesystem::path project_directory;
    std::filesystem::path source_path;
    asset_id_t asset;
    std::uintmax_t maximum_bytes{64ULL * 1024ULL * 1024ULL * 1024ULL};
};

struct project_asset_copy_result_t {
    project_asset_copy_status_t status{project_asset_copy_status_t::invalid_request};
    std::filesystem::path relative_path;
    std::filesystem::path absolute_path;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == project_asset_copy_status_t::copied ||
               status == project_asset_copy_status_t::reused;
    }
};

// Publishes a byte-identical copy under project_directory/assets with a
// deterministic asset-ID-prefixed filename. A same-directory temporary is
// flushed and atomically installed without replacing an existing destination.
// Existing identical files are reused; differing files reject. The caller
// commits the returned relative path through apply_resource_asset_relink.
[[nodiscard]] auto copy_resource_file_into_project(
    const project_asset_copy_request_t& request, std::stop_token stop = {})
    -> project_asset_copy_result_t;

} // namespace wormedit
