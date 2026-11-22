#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit {

// A semantic version such as "1.0.0-rc4". Precedence follows SemVer 2.0 except that an
// alphanumeric prerelease identifier ending in digits compares by its letters, then by
// that number as a number ("rc9" < "rc10"). Build metadata after '+' is dropped.
struct release_version_t {
    std::uint32_t major{}, minor{}, patch{};
    std::string prerelease; // Empty for a final release.

    [[nodiscard]] static auto from(std::string_view text) -> std::optional<release_version_t>;
    [[nodiscard]] auto is_prerelease() const -> bool { return !prerelease.empty(); }
    // A nightly build is a snapshot of the main branch: never offered an update.
    [[nodiscard]] auto is_nightly() const -> bool;
    [[nodiscard]] auto to_string() const -> std::string;
    [[nodiscard]] auto compare(const release_version_t&) const -> std::strong_ordering;
    auto operator==(const release_version_t&) const -> bool = default;
};

} // namespace wormedit
