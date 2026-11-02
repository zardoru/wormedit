#pragma once

#include <wormedit/core/lua_vocabulary.h>
#include <wormedit/core/vocabulary.h>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {
// Frozen source and invocation shared by SV recipes and storyboard links.
struct lua_source_descriptor_t {
    static constexpr std::size_t maximum_frozen_source_bytes = 16 * 1024;
    static constexpr std::uint32_t current_api_version = 1;
    [[nodiscard]] static auto is_valid_identifier(std::string_view) -> bool;
    std::string frozen_source;
    std::uint32_t api_version{current_api_version};
    std::string entry_point;
    std::optional<std::filesystem::path> source_reference;
    std::string source_hash;
    std::vector<lua_parameter_t> parameters;
    std::uint64_t seed{};
    auto operator==(const lua_source_descriptor_t&) const -> bool = default;
};
} // namespace wormedit
