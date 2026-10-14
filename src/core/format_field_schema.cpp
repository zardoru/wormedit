#include "text_util.h"
#include <wormedit/core/format_field_schema.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace wormedit {
namespace {
using wormedit::text_util::ascii_iequal;

constexpr std::array<std::string_view, 6> stepmania_difficulties{
    "Beginner", "Easy", "Medium", "Hard", "Challenge", "Edit"};

using family_t = format_family_t;
using type_t = format_field_type_t;

const std::array bms_schema{
    format_field_schema_t{family_t::bms, "PLAYER", "Player", type_t::integer, 1, 4, {}},
    format_field_schema_t{family_t::bms, "RANK", "Judge rank", type_t::integer, 0, 4, {}},
    format_field_schema_t{family_t::bms, "TOTAL", "Total", type_t::decimal, 0, std::nullopt, {}},
    format_field_schema_t{family_t::bms, "PLAYLEVEL", "Play level", type_t::integer, 0, 99, {}},
    format_field_schema_t{family_t::bms, "DIFFICULTY", "Difficulty", type_t::integer, 1, 5, {}},
};

const std::array stepmania_schema{
    format_field_schema_t{family_t::stepmania, "CREDIT", "Credit", type_t::text, std::nullopt,
                          std::nullopt, {}},
    format_field_schema_t{family_t::stepmania, "DIFFICULTY", "Difficulty", type_t::choice,
                          std::nullopt, std::nullopt, stepmania_difficulties},
    format_field_schema_t{family_t::stepmania, "METER", "Meter", type_t::integer, 0, 999, {}},
};

const std::array osu_schema{
    format_field_schema_t{family_t::osu_mania, "Difficulty.HPDrainRate", "HP drain",
                          type_t::decimal, 0, 10, {}},
    format_field_schema_t{family_t::osu_mania, "Difficulty.OverallDifficulty",
                          "Overall difficulty", type_t::decimal, 0, 10, {}},
    format_field_schema_t{family_t::osu_mania, "Metadata.Version", "Version", type_t::text,
                          std::nullopt, std::nullopt, {}},
};

const std::array bmson_schema{
    format_field_schema_t{family_t::bmson, "info.level", "Level", type_t::integer, 0,
                          std::nullopt, {}},
    format_field_schema_t{family_t::bmson, "info.judge_rank", "Judge rank", type_t::decimal, 0,
                          std::nullopt, {}},
    format_field_schema_t{family_t::bmson, "info.total", "Total", type_t::decimal, 0,
                          std::nullopt, {}},
};


auto is_digit(char value) -> bool {
    return value >= '0' && value <= '9';
}

// Parses `digits[.digits]` with an optional leading '-' without using the
// process locale. Returns nullopt for anything else, including an exponent.
auto parse_plain_number(std::string_view text, bool allow_fraction) -> std::optional<long double> {
    auto cursor = text;
    const auto negative = !cursor.empty() && cursor.front() == '-';
    if (negative) cursor.remove_prefix(1);
    if (cursor.empty() || !is_digit(cursor.front())) return std::nullopt;
    long double value{};
    while (!cursor.empty() && is_digit(cursor.front())) {
        value = value * 10 + (cursor.front() - '0');
        cursor.remove_prefix(1);
    }
    if (!cursor.empty()) {
        if (!allow_fraction || cursor.front() != '.' || cursor.size() == 1) return std::nullopt;
        cursor.remove_prefix(1);
        long double scale = 0.1L;
        while (!cursor.empty() && is_digit(cursor.front())) {
            value += (cursor.front() - '0') * scale;
            scale /= 10;
            cursor.remove_prefix(1);
        }
        if (!cursor.empty()) return std::nullopt;
    }
    return negative ? -value : value;
}

auto range_error(const format_field_schema_t& field, long double value)
    -> std::optional<std::string> {
    if (field.minimum && value < *field.minimum)
        return std::string(field.label) + " is below the minimum";
    if (field.maximum && value > *field.maximum)
        return std::string(field.label) + " is above the maximum";
    return std::nullopt;
}

auto validate_number(const format_field_schema_t& field, std::string_view value)
    -> std::optional<std::string> {
    const auto parsed = parse_plain_number(value, field.type == type_t::decimal);
    if (!parsed) {
        return std::string(field.label) + (field.type == type_t::integer
                                               ? " must be a whole number"
                                               : " must be a plain decimal number");
    }
    return range_error(field, *parsed);
}

} // namespace

auto format_field_schemas(format_family_t family) -> std::span<const format_field_schema_t> {
    switch (family) {
    case format_family_t::bms:
        return bms_schema;
    case format_family_t::stepmania:
        return stepmania_schema;
    case format_family_t::osu_mania:
        return osu_schema;
    case format_family_t::bmson:
        return bmson_schema;
    default:
        return {};
    }
}

auto find_format_field_schema(format_family_t family, std::string_view name)
    -> const format_field_schema_t* {
    const auto schemas = format_field_schemas(family);
    const auto found = std::ranges::find(schemas, name, &format_field_schema_t::name);
    return found == schemas.end() ? nullptr : &*found;
}

auto validate_format_field_value(const format_field_schema_t& field, std::string_view value)
    -> std::optional<std::string> {
    switch (field.type) {
    case type_t::integer:
    case type_t::decimal:
        return validate_number(field, value);
    case type_t::choice: {
        const auto match = std::ranges::any_of(field.choices, [&](std::string_view choice) {
            return ascii_iequal(choice, value);
        });
        return match ? std::nullopt
                     : std::optional<std::string>(std::string(field.label) +
                                                  " is not one of the allowed values");
    }
    case type_t::text:
        if (value.find_first_of("\r\n") != std::string_view::npos)
            return std::string(field.label) + " must be a single line";
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace wormedit
