#pragma once
#include "text_util.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// Field rules for osu! legacy section lines (.osu sections and .osb storyboards). osu! splits every
// line on bare commas: there is no CSV quoting or "" escaping. Filename fields in [Events] may be
// wrapped in quotes, which osu! trims after splitting (osu!lazer LegacyDecoder.CleanFilename), so a
// filename can never contain a comma. Hit-object hitSample filenames are used as written.
namespace wormedit::osu_line {
inline auto split_fields(std::string_view line) -> std::vector<std::string> {
    std::vector<std::string> fields;
    while (true) {
        const auto comma = line.find(',');
        fields.emplace_back(text_util::trim(line.substr(0, comma), " \t\r"));
        if (comma == std::string_view::npos) return fields;
        line.remove_prefix(comma + 1);
    }
}

inline auto clean_filename(std::string_view field) -> std::string {
    const auto first = field.find_first_not_of('"');
    if (first == std::string_view::npos) return {};
    return std::string(field.substr(first, field.find_last_not_of('"') - first + 1));
}

// A filename osu! reads back unchanged once quoted: no comma, quote or control character.
inline auto is_writable_filename(std::string_view name) -> bool {
    return !name.empty() && std::ranges::none_of(name, [](unsigned char character) {
        return character < 32 || character == ',' || character == '"';
    });
}

inline auto quote_filename(std::string_view name) -> std::string {
    return "\"" + std::string(name) + "\"";
}
}
