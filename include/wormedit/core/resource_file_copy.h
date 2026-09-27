#pragma once

#include <wormedit/core/document.h>

#include <cstdint>
#include <filesystem>
#include <optional>
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

struct export_asset_copy_request_t {
    std::filesystem::path project_directory;
    std::filesystem::path source_path;
    // The selected output group's project-relative file or directory path.
    // A path with a filename extension names a chart file; otherwise it names
    // the output directory.
    std::filesystem::path relative_output_path;
    asset_id_t asset;
    std::uintmax_t maximum_bytes{64ULL * 1024ULL * 1024ULL * 1024ULL};
};

[[nodiscard]] auto export_asset_directory(const std::filesystem::path& relative_output_path)
    -> std::optional<std::filesystem::path>;

// Converts the copy result's project-relative path to the reference a chart
// file writes from its output directory. Rejects a path outside that folder.
[[nodiscard]] auto
export_asset_reference_path(const std::filesystem::path& relative_output_path,
                            const std::filesystem::path& copied_project_relative_path)
    -> std::optional<std::filesystem::path>;

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

// Stages a resource beside one output group's chart under its assets folder.
// The result's relative path is project-relative. No document reference is
// changed; the exporter may use the path relative to the output directory.
[[nodiscard]] auto copy_resource_file_into_export(const export_asset_copy_request_t& request,
                                                  std::stop_token stop = {})
    -> project_asset_copy_result_t;

} // namespace wormedit
