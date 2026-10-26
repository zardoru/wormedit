#pragma once

#include "core/text_util.h"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace wormedit::sys {

// The importer and startup/drop admission share one format and size policy.
inline auto supported_source_limit(std::string_view extension) -> std::size_t {
    constexpr std::size_t chart_source_limit = 16U * 1024U * 1024U;
    constexpr std::size_t native_source_limit = 64U * 1024U * 1024U;
    if (extension == ".wormedit") return native_source_limit;
    if (extension == ".bms" || extension == ".bme" || extension == ".bml" ||
        extension == ".pms" || extension == ".bmson" || extension == ".sm" ||
        extension == ".ssc" || extension == ".osu")
        return chart_source_limit;
    return 0;
}

[[nodiscard]] inline auto is_supported_project_file_extension(const std::filesystem::path& path)
    -> bool {
    return supported_source_limit(text_util::lowercase_extension(path)) != 0;
}

} // namespace wormedit::sys
