#pragma once

#include <wormedit/formats/conversion.h>

#include <filesystem>
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

} // namespace wormedit
