#pragma once

#include <wormedit/formats/conversion.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace wormedit {

struct conversion_publication_file_t {
    std::size_t output_index{};
    std::filesystem::path project_relative_path;
};

struct conversion_publication_asset_t {
    asset_id_t asset;
    std::filesystem::path project_relative_path;
};

struct conversion_publication_plan_t {
    std::vector<conversion_publication_file_t> files;
    std::vector<conversion_publication_asset_t> assets;
    std::vector<std::string> errors;

    [[nodiscard]] explicit operator bool() const noexcept {
        return errors.empty();
    }
};

// Resolves the complete converted output set against one selected output group.
// This is a read-only preflight: it checks the already-staged asset files and
// destination paths, but it does not reserve names or publish chart bytes.
[[nodiscard]] auto plan_conversion_publication(const std::filesystem::path& project_directory,
                                               const output_group_t& group,
                                               const conversion_request_t& request,
                                               const conversion_result_t& converted)
    -> conversion_publication_plan_t;

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
};

struct conversion_publication_result_t {
    bool published{};
    std::size_t pruned_files{};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

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
