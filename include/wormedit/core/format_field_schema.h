#pragma once

#include <wormedit/core/document.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace wormedit {

enum class format_field_type_t {
    integer,
    decimal,
    text,
    // One of `format_field_schema_t::choices`, matched case-insensitively.
    choice,
};

// One named, typed metadata field of a source format. `name` is the exact
// spelling stored in `format_metadata_t::ordered_fields`: BMS tokens without the
// `#`, StepMania tags, osu! `Section.Key` names and BMSON dotted paths.
struct format_field_schema_t {
    format_family_t family{format_family_t::native_project};
    std::string_view name;
    std::string_view label;
    format_field_type_t type{format_field_type_t::text};
    std::optional<long double> minimum;
    std::optional<long double> maximum;
    std::span<const std::string_view> choices;
};

// The typed fields offered for a format; empty for formats without a schema.
[[nodiscard]] auto format_field_schemas(format_family_t family)
    -> std::span<const format_field_schema_t>;

// Exact, case-sensitive lookup by stored field name.
[[nodiscard]] auto find_format_field_schema(format_family_t family, std::string_view name)
    -> const format_field_schema_t*;

// Checks one raw value against its schema: integers and decimals must be plain
// ASCII numbers (no exponent, no separators, no surrounding space) inside the
// range, choices must match one entry, and text must not contain a line break.
// Returns an inline error message, or nullopt when the value is acceptable.
[[nodiscard]] auto validate_format_field_value(const format_field_schema_t& field,
                                               std::string_view value)
    -> std::optional<std::string>;

} // namespace wormedit
