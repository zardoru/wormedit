#pragma once

#include <wormedit/formats/conversion.h>

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace wormedit {

struct conversion_publication_file_t {
    std::size_t output_index{};
    std::filesystem::path project_relative_path;
    auto operator==(const conversion_publication_file_t&) const -> bool = default;
};

struct conversion_publication_asset_t {
    asset_id_t asset;
    std::filesystem::path project_relative_path;
    auto operator==(const conversion_publication_asset_t&) const -> bool = default;
};

struct conversion_publication_plan_error_t;

// Resolves the complete converted output set against one selected output group.
// This is a read-only preflight: it checks the already-staged asset files and
// destination paths, but it does not reserve names or publish chart bytes.
struct conversion_publication_plan_payload_t {
    [[nodiscard]] static auto from(const std::filesystem::path& project_directory,
        const output_group_t& group, const conversion_request_t& request,
        const conversion_result_t& converted, bool destination_is_directory = false)
        -> result_t<conversion_publication_plan_payload_t, conversion_publication_plan_error_t>;
    std::vector<conversion_publication_file_t> files;
    std::vector<conversion_publication_asset_t> assets;
    std::vector<std::filesystem::path> generated_files;
    auto operator==(const conversion_publication_plan_payload_t&) const -> bool = default;
};
struct conversion_publication_plan_error_t {
    conversion_publication_plan_payload_t plan;
    std::string message;
    [[nodiscard]] static auto from(conversion_publication_plan_payload_t&& plan,
        std::string message) -> conversion_publication_plan_error_t {
        plan.files.clear();
        plan.assets.clear();
        plan.generated_files.clear();
        return {.plan = std::move(plan), .message = std::move(message)};
    }
    auto operator==(const conversion_publication_plan_error_t&) const -> bool = default;
};
using conversion_publication_plan_outcome_t =
    result_t<conversion_publication_plan_payload_t, conversion_publication_plan_error_t>;

struct staged_asset_discovery_error_t {
    std::vector<conversion_request_t::staged_asset_t> mappings;
    std::string message;
    [[nodiscard]] static auto from(std::vector<conversion_request_t::staged_asset_t>&& mappings,
        std::string message) -> staged_asset_discovery_error_t {
        mappings.clear();
        return {.mappings = std::move(mappings), .message = std::move(message)};
    }
    auto operator==(const staged_asset_discovery_error_t&) const -> bool = default;
};
using staged_asset_discovery_outcome_t =
    result_t<std::vector<conversion_request_t::staged_asset_t>, staged_asset_discovery_error_t>;
struct staged_asset_discovery_t {
    [[nodiscard]] static auto from(const song_document_t& song, const output_group_t& group,
        const std::filesystem::path& project_directory, std::stop_token stop = {}) -> staged_asset_discovery_outcome_t;
    [[nodiscard]] static auto from(staged_asset_discovery_outcome_t&& outcome) -> staged_asset_discovery_t;
    std::vector<conversion_request_t::staged_asset_t> mappings;
    std::vector<std::string> errors;

    [[nodiscard]] explicit operator bool() const noexcept {
        return errors.empty();
    }
};

// Rebuilds chart-relative asset mappings from byte-identical copies already
// present beside the output group. Reads resource and selected sound-channel assets; missing
// or changed source files reject rather than silently exporting stale copies.
[[nodiscard]] auto discover_staged_export_assets(const song_document_t& song,
                                                 const output_group_t& group,
                                                 const std::filesystem::path& project_directory,
                                                 std::stop_token stop = {})
    -> staged_asset_discovery_t;


enum class conversion_publication_stage_t {
    copy_existing,
    write_output,
    validate_staging,
    update_manifest,
    exchange_directory,
};

struct conversion_publication_options_t {
    std::uintmax_t maximum_existing_bytes{64ULL * 1024ULL * 1024ULL * 1024ULL};
    std::size_t maximum_existing_entries{8192};
    std::function<bool(conversion_publication_stage_t)> allow_stage;
    // Called only inside private atomic staging, after all destination preflight.
    std::function<std::optional<std::string>(const std::filesystem::path&)> stage_generated_files;
    // CLI directories may contain dots; default retains app file-path semantics.
    bool destination_is_directory{};
};

struct conversion_publication_diagnostics_t {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    [[nodiscard]] auto has_errors() const -> bool { return !errors.empty(); }
    auto operator==(const conversion_publication_diagnostics_t&) const -> bool = default;
};
struct conversion_publication_payload_t {
    bool published{};
    std::size_t pruned_files{};
    auto operator==(const conversion_publication_payload_t&) const -> bool = default;
};
using conversion_publication_outcome_t = diagnosed_t<conversion_publication_payload_t, conversion_publication_diagnostics_t>;
struct conversion_publication_result_t {
    [[nodiscard]] static auto from(conversion_publication_outcome_t&& outcome) -> conversion_publication_result_t;
    bool published{};
    std::size_t pruned_files{};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

[[nodiscard]] auto create_conversion_group(const std::filesystem::path& project_directory,
    const output_group_t& group, const conversion_request_t& request,
    const conversion_result_t& converted, conversion_publication_options_t options = {})
    -> conversion_publication_outcome_t;

// Replans at execution time, builds a complete sibling directory, then makes
// it visible in one rename/exchange. A bounded in-directory ownership manifest
// tracks generated chart names for safe stale-file pruning on later exports.
// Existing-directory exchange currently
// requires Linux renameat2 or macOS renameatx_np; unsupported platforms fail
// before changing the destination. The output group must have a non-root
// directory with an existing parent.
[[nodiscard]] auto publish_conversion_group_atomic(const std::filesystem::path& project_directory,
                                                   const output_group_t& group,
                                                   const conversion_request_t& request,
                                                   const conversion_result_t& converted,
                                                   conversion_publication_options_t options = {})
    -> conversion_publication_result_t;

} // namespace wormedit
