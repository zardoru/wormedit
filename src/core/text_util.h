#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
namespace wormedit::text_util {
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
inline auto valid_utf8(std::string_view text) noexcept -> bool {
    return !invalid_utf8_offset(text);
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
inline auto ascii_fold(std::string text) -> std::string {
    for (auto& character : text)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character + ('a' - 'A'));
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
