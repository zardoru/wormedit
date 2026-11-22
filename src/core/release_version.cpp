#include <wormedit/core/release_version.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <tuple>
#include <vector>

namespace wormedit {
namespace {

auto parse_number(std::string_view text, std::uint32_t& value) -> bool {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

auto is_prerelease_text(std::string_view text) -> bool {
    return std::ranges::all_of(text, [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-'; });
}

auto split_dots(std::string_view text) -> std::vector<std::string_view> {
    std::vector<std::string_view> parts;
    while (true) {
        const auto dot = text.find('.');
        parts.push_back(text.substr(0, dot));
        if (dot == std::string_view::npos) return parts;
        text.remove_prefix(dot + 1);
    }
}

auto all_digits(std::string_view text) -> bool {
    return !text.empty() && std::ranges::all_of(text, [](unsigned char c) { return std::isdigit(c); });
}

auto number_of(std::string_view digits) -> std::uint64_t {
    std::uint64_t value{};
    std::from_chars(digits.data(), digits.data() + digits.size(), value);
    return value;
}

// Splits "rc10" into {"rc", 10}; no trailing digits gives {text, none}.
struct identifier_t {
    std::string_view letters;
    std::uint64_t number{};
    bool has_number{};
};
auto split_identifier(std::string_view text) -> identifier_t {
    auto digits = text.size();
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(text[digits - 1]))) --digits;
    if (digits == text.size() || text.size() - digits > 18) return {text, 0, false};
    return {text.substr(0, digits), number_of(text.substr(digits)), true};
}

auto compare_identifier(std::string_view a, std::string_view b) -> std::strong_ordering {
    const auto a_numeric = all_digits(a), b_numeric = all_digits(b);
    if (a_numeric && b_numeric) return number_of(a) <=> number_of(b);
    if (a_numeric != b_numeric) return a_numeric ? std::strong_ordering::less : std::strong_ordering::greater;
    const auto x = split_identifier(a), y = split_identifier(b);
    if (const auto letters = x.letters <=> y.letters; letters != 0) return letters;
    if (x.has_number != y.has_number)
        return x.has_number ? std::strong_ordering::greater : std::strong_ordering::less;
    return x.number <=> y.number;
}

} // namespace

auto release_version_t::from(std::string_view text) -> std::optional<release_version_t> {
    if (text.starts_with('v')) text.remove_prefix(1);
    if (const auto plus = text.find('+'); plus != std::string_view::npos) text = text.substr(0, plus);
    std::string_view prerelease;
    if (const auto dash = text.find('-'); dash != std::string_view::npos) {
        prerelease = text.substr(dash + 1);
        text = text.substr(0, dash);
        if (prerelease.empty() || !is_prerelease_text(prerelease)) return std::nullopt;
    }
    const auto parts = split_dots(text);
    if (parts.size() != 3) return std::nullopt;
    release_version_t version;
    if (!parse_number(parts[0], version.major) || !parse_number(parts[1], version.minor) ||
        !parse_number(parts[2], version.patch))
        return std::nullopt;
    version.prerelease = std::string{prerelease};
    return version;
}

auto release_version_t::is_nightly() const -> bool {
    return prerelease == "nightly" || prerelease.starts_with("nightly.");
}

auto release_version_t::to_string() const -> std::string {
    auto text = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!prerelease.empty()) text += "-" + prerelease;
    return text;
}

auto release_version_t::compare(const release_version_t& other) const -> std::strong_ordering {
    if (const auto core = std::tie(major, minor, patch) <=> std::tie(other.major, other.minor, other.patch);
        core != 0)
        return core;
    if (prerelease.empty() != other.prerelease.empty())
        return prerelease.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
    const auto mine = split_dots(prerelease), theirs = split_dots(other.prerelease);
    for (std::size_t i = 0; i < std::min(mine.size(), theirs.size()); ++i)
        if (const auto order = compare_identifier(mine[i], theirs[i]); order != 0) return order;
    return mine.size() <=> theirs.size();
}

} // namespace wormedit
