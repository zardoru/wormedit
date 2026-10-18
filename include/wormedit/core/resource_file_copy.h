#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>

namespace wormedit {

enum class project_asset_copy_phase_t { copied, reused };

struct project_asset_copy_request_t {
    [[nodiscard]] auto destination_filename() const -> std::filesystem::path {
        return std::to_string(asset.value) + "-" + source_path.filename().string();
    }
    std::filesystem::path project_directory;
    std::filesystem::path source_path;
    asset_id_t asset;
    std::uintmax_t maximum_bytes{64ULL * 1024ULL * 1024ULL * 1024ULL};
    // Runs on the private, byte-verified temporary before publication. A
    // failed validator removes that temporary and leaves the destination
    // untouched. Existing identical destinations do not need publication.
    std::function<bool(const std::filesystem::path&)> validate_temporary{};
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
    std::function<bool(const std::filesystem::path&)> validate_temporary{};
};

[[nodiscard]] auto export_asset_directory(const std::filesystem::path& relative_output_path)
    -> std::optional<std::filesystem::path>;

// Converts the copy result's project-relative path to the reference a chart
// file writes from its output directory. Rejects a path outside that folder.
[[nodiscard]] auto
export_asset_reference_path(const std::filesystem::path& relative_output_path,
                            const std::filesystem::path& copied_project_relative_path)
    -> std::optional<std::filesystem::path>;

struct project_asset_copy_receipt_t {
    [[nodiscard]] static auto from(const std::filesystem::path& relative_path,
                                   const std::filesystem::path& absolute_path,
                                   project_asset_copy_phase_t status)
        -> project_asset_copy_receipt_t;

    struct published_identity_t {
        std::uint64_t volume{};
        std::uint64_t file{};
        std::uintmax_t size{};
        std::filesystem::file_time_type modified{};
        auto operator==(const published_identity_t&) const -> bool = default;
    };
    project_asset_copy_phase_t status{project_asset_copy_phase_t::copied};
    std::filesystem::path relative_path;
    std::filesystem::path absolute_path;
    std::optional<published_identity_t> published_identity;
    auto operator==(const project_asset_copy_receipt_t&) const -> bool = default;
};
using project_asset_copy_outcome_t =
    result_t<project_asset_copy_receipt_t, error_t<failure_code_t>>;

// Publishes a byte-identical copy under project_directory/assets with a
// deterministic asset-ID-prefixed filename. A same-directory temporary is
// flushed and atomically installed without replacing an existing destination.
// Existing identical files are reused; differing files reject. The caller
// commits the returned relative path through apply_resource_asset_relink.
[[nodiscard]] auto create_project_asset_copy(const project_asset_copy_request_t& request,
                                             std::stop_token stop = {})
    -> project_asset_copy_outcome_t;

// Read-only check for a project asset destination. A missing directory or
// destination returns not_found; identical bytes return reused; differing
// bytes or unsafe path components reject without creating files.
[[nodiscard]] auto inspect_project_asset_file(const project_asset_copy_request_t& request,
                                              std::stop_token stop = {})
    -> project_asset_copy_outcome_t;

// Stages a resource beside one output group's chart under its assets folder.
// The result's relative path is project-relative. No document reference is
// changed; the exporter may use the path relative to the output directory.
[[nodiscard]] auto create_export_asset_copy(const export_asset_copy_request_t& request,
                                            std::stop_token stop = {})
    -> project_asset_copy_outcome_t;

// Read-only check for the deterministic export copy. Returns reused with its
// path only when source and staged bytes still match; missing destinations
// return not_found and no directories or files are created.
[[nodiscard]] auto inspect_export_asset_file(const export_asset_copy_request_t& request,
                                             std::stop_token stop = {})
    -> project_asset_copy_outcome_t;

// Discards a newly published, still byte-identical copy when an import cannot
// be linked. Returns false and leaves the file alone if ownership is uncertain.
[[nodiscard]] auto discard_unlinked_project_asset_copy(const project_asset_copy_request_t& request,
                                                       const project_asset_copy_receipt_t& copy)
    -> bool;
[[nodiscard]] auto discard_unlinked_export_asset_copy(const export_asset_copy_request_t& request,
                                                      const project_asset_copy_receipt_t& copy)
    -> bool;

} // namespace wormedit
