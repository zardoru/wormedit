#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace wormedit {

enum class project_asset_copy_phase_t { copied, reused };

struct project_asset_copy_request_t {
    [[nodiscard]] auto destination_filename() const -> std::filesystem::path {
        return planned_filename.empty() ? source_path.filename() : planned_filename;
    }
    std::filesystem::path project_directory;
    std::filesystem::path source_path;
    asset_id_t asset;
    std::uintmax_t maximum_bytes{64ULL * 1024ULL * 1024ULL * 1024ULL};
    // Set by the read-only copy planner and carried unchanged through inspection/publication.
    std::filesystem::path planned_filename;
    // Runs on the private, byte-verified temporary before publication. A
    // failed validator removes that temporary and leaves the destination
    // untouched. Existing identical destinations do not need publication.
    std::function<bool(const std::filesystem::path&)> validate_temporary{};
    // Automatic saves may reuse any byte-identical regular file in assets/.
    bool reuse_identical_files{};
};

// Selects stable filenames without writing. Every request must target the same
// relative asset directory; returned requests carry their selected filename.
[[nodiscard]] auto plan_project_asset_copies(
    std::span<const project_asset_copy_request_t> requests,
    const std::filesystem::path& relative_directory, std::stop_token stop = {})
    -> result_t<std::vector<project_asset_copy_request_t>, error_t<failure_code_t>>;

struct export_asset_copy_request_t {
    std::filesystem::path project_directory;
    std::filesystem::path source_path;
    // The selected output group's typed project-relative file or directory path.
    std::filesystem::path relative_output_path;
    std::optional<output_destination_kind_t> destination_kind;
    asset_id_t asset;
    std::uintmax_t maximum_bytes{64ULL * 1024ULL * 1024ULL * 1024ULL};
    std::function<bool(const std::filesystem::path&)> validate_temporary{};
};

[[nodiscard]] auto export_asset_directory(const std::filesystem::path& relative_output_path)
    -> std::optional<std::filesystem::path>;
[[nodiscard]] auto export_asset_directory(const std::filesystem::path& relative_output_path,
                                          output_destination_kind_t destination_kind)
    -> std::optional<std::filesystem::path>;

// Converts the copy result's project-relative path to the reference a chart
// file writes from its output directory. Rejects a path outside that folder.
[[nodiscard]] auto
export_asset_reference_path(const std::filesystem::path& relative_output_path,
                            const std::filesystem::path& copied_project_relative_path,
                            std::optional<output_destination_kind_t> destination_kind = std::nullopt)
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

// Publishes a byte-identical copy under project_directory/assets using the
// planned source basename or collision fallback. A same-directory temporary
// is flushed and atomically installed without replacing an existing destination.
// Existing identical files are reused. The caller commits the returned relative
// path through apply_resource_asset_relink.
[[nodiscard]] auto create_project_asset_copy(const project_asset_copy_request_t& request,
                                             std::stop_token stop = {})
    -> project_asset_copy_outcome_t;

// Read-only check for the selected project asset destination. A missing directory
// or destination returns not_found; identical bytes return reused; unsafe path
// components or conflicts at both candidate names reject without creating files.
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
