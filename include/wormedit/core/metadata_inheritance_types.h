#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit {
enum class inherited_metadata_field_t { title, subtitle, artist, genre, music, preview, count };
inline constexpr auto inherited_metadata_field_count = static_cast<std::size_t>(inherited_metadata_field_t::count);

struct metadata_overrides_t {
    // nullopt inherits; an authored empty string is an explicit empty value.
    std::array<std::optional<std::string>, inherited_metadata_field_count> values;
    [[nodiscard]] auto value(inherited_metadata_field_t field) const -> const std::optional<std::string>& {
        return values.at(static_cast<std::size_t>(field));
    }
    [[nodiscard]] auto value(inherited_metadata_field_t field) -> std::optional<std::string>& {
        return values.at(static_cast<std::size_t>(field));
    }
    [[nodiscard]] auto resolve(inherited_metadata_field_t field, std::string_view inherited) const -> std::string_view {
        const auto& authored = value(field);
        return authored ? std::string_view{*authored} : inherited;
    }
    auto transform_audio_paths(const std::function<void(std::filesystem::path&)>&) -> bool;
    [[nodiscard]] auto has_values() const -> bool {
        return std::ranges::any_of(values, [](const auto& value) { return value.has_value(); });
    }
    auto operator==(const metadata_overrides_t&) const -> bool = default;
};

} // namespace wormedit
