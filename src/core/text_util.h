#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
namespace wormedit::text_util {
inline constexpr std::string_view ascii_whitespace = " \t\r\n\f\v";
inline constexpr auto is_utf8_continuation(char byte) noexcept -> bool {
    return (static_cast<unsigned char>(byte) & 0xc0U) == 0x80U;
}
inline auto truncate_utf8(std::string_view text, std::size_t maximum_bytes) noexcept
    -> std::string_view {
    auto length = std::min(text.size(), maximum_bytes);
    while (length > 0 && length < text.size() && is_utf8_continuation(text[length])) --length;
    return text.substr(0, length);
}
inline constexpr auto ascii_upper(char byte) noexcept -> char {
    return byte >= 'a' && byte <= 'z' ? static_cast<char>(byte - ('a' - 'A')) : byte;
}
inline auto ascii_upper(std::string text) -> std::string {
    for (auto& byte : text) byte = ascii_upper(byte);
    return text;
}
inline auto ascii_upper(std::string_view text) -> std::string {
    return ascii_upper(std::string(text));
}
inline auto invalid_utf8_sequence(std::string_view source, std::size_t at, std::size_t count,
                                  std::uint32_t point) noexcept -> std::optional<std::size_t> {
    for (std::size_t i = 1; i < count; ++i) {
        const auto next = static_cast<unsigned char>(source[at + i]);
        if ((next & 0xc0U) != 0x80U)
            return at + i;
        point = (point << 6U) | (next & 0x3fU);
    }
    if ((count == 3 && point < 0x800U) || (count == 4 && point < 0x10000U) ||
        (point >= 0xd800U && point <= 0xdfffU) || point > 0x10ffffU)
        return at;
    return std::nullopt;
}
inline auto invalid_utf8_offset(std::string_view source) -> std::optional<std::size_t> {
    std::size_t at{};
    while (at < source.size()) {
        const auto first = static_cast<unsigned char>(source[at]);
        if (first <= 0x7fU) {
            ++at;
            continue;
        }
        std::size_t count{};
        std::uint32_t point{};
        if (first >= 0xc2U && first <= 0xdfU) {
            count = 2;
            point = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            count = 3;
            point = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            count = 4;
            point = first & 0x07U;
        } else
            return at;
        if (count > source.size() - at)
            return at;
        if (const auto invalid = invalid_utf8_sequence(source, at, count, point))
            return invalid;
        at += count;
    }
    return std::nullopt;
}
inline auto is_valid_utf8(std::string_view text) noexcept -> bool {
    return !invalid_utf8_offset(text);
}

inline auto is_valid_bounded_text(std::string_view text, std::size_t maximum_bytes) noexcept -> bool {
    return text.size() <= maximum_bytes && text.find('\0') == std::string_view::npos && is_valid_utf8(text);
}
// Compatibility for the coordinated sys/UI migration.
inline auto valid_bounded_text(std::string_view text, std::size_t maximum_bytes) noexcept -> bool { return is_valid_bounded_text(text, maximum_bytes); }
inline auto path_from_utf8(std::string_view value) -> std::filesystem::path {
    std::u8string text;
    text.reserve(value.size());
    for (const auto character : value)
        text.push_back(static_cast<char8_t>(character));
    return std::filesystem::path(std::move(text));
}
inline auto path_utf8(const std::filesystem::path& path) -> std::string {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
inline constexpr auto ascii_fold(char byte) noexcept -> char {
    return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte + ('a' - 'A')) : byte;
}
inline constexpr auto ascii_iequal(char left, char right) noexcept -> bool {
    return ascii_fold(left) == ascii_fold(right);
}
inline auto ascii_fold(std::string text) -> std::string {
    for (auto& character : text) character = ascii_fold(character);
    return text;
}
inline constexpr auto trim(std::string_view text, std::string_view whitespace) noexcept
    -> std::string_view {
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
        return text.substr(text.size());
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}
} // namespace wormedit::text_util
