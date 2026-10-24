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
inline auto is_sha256_digest(std::string_view value) noexcept -> bool {
    return value.size() == 64 && std::ranges::all_of(value, [](char byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
            (byte >= 'A' && byte <= 'F');
    });
}
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
// ASCII only: preserve every non-ASCII byte; Unicode folding is a separate operation.
inline constexpr auto ascii_lowercase(char byte) noexcept -> char {
    return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte + ('a' - 'A')) : byte;
}
inline constexpr auto ascii_iequal(char left, char right) noexcept -> bool {
    return ascii_lowercase(left) == ascii_lowercase(right);
}
inline auto ascii_lowercase(std::string text) -> std::string {
    for (auto& character : text) character = ascii_lowercase(character);
    return text;
}
inline auto ascii_lowercase(std::string_view text) -> std::string {
    return ascii_lowercase(std::string{text});
}
inline auto ascii_lowercase(const char* text) -> std::string {
    return ascii_lowercase(std::string_view{text});
}
inline constexpr auto ascii_iequal(std::string_view left, std::string_view right) noexcept -> bool {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (!ascii_iequal(left[index], right[index])) return false;
    return true;
}
inline constexpr auto ascii_iless(std::string_view left, std::string_view right) noexcept -> bool {
    const auto count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto lbyte = static_cast<unsigned char>(ascii_lowercase(left[index]));
        const auto rbyte = static_cast<unsigned char>(ascii_lowercase(right[index]));
        if (lbyte != rbyte) return lbyte < rbyte;
    }
    return left.size() < right.size();
}
inline auto lowercase_extension(const std::filesystem::path& path) -> std::string {
    return ascii_lowercase(path.extension().string());
}
inline constexpr auto trim(std::string_view text, std::string_view whitespace) noexcept
    -> std::string_view {
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
        return text.substr(text.size());
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}
} // namespace wormedit::text_util
