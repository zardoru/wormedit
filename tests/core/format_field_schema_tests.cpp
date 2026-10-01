#include <wormedit/core/format_field_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using namespace wormedit;

namespace {

auto check(format_family_t family, std::string_view name, std::string_view value)
    -> std::optional<std::string> {
    const auto* field = find_format_field_schema(family, name);
    REQUIRE(field != nullptr);
    return validate_format_field_value(*field, value);
}

} // namespace

TEST_CASE("every listed format has unique typed fields and the others have none",
          "[unit][core][format-field-schema]") {
    CHECK(format_field_schemas(format_family_t::bms).size() == 5);
    CHECK(format_field_schemas(format_family_t::stepmania).size() == 3);
    CHECK(format_field_schemas(format_family_t::osu_mania).size() == 3);
    CHECK(format_field_schemas(format_family_t::bmson).size() == 3);
    CHECK(format_field_schemas(format_family_t::native_project).empty());
    CHECK(format_field_schemas(format_family_t::midi).empty());

    for (const auto family : {format_family_t::bms, format_family_t::stepmania,
                              format_family_t::osu_mania, format_family_t::bmson}) {
        std::set<std::string_view> names;
        for (const auto& field : format_field_schemas(family)) {
            CHECK(field.family == family);
            CHECK(names.insert(field.name).second);
            CHECK_FALSE(field.label.empty());
        }
    }
    CHECK(find_format_field_schema(format_family_t::bms, "PLAYLEVEL") != nullptr);
    CHECK(find_format_field_schema(format_family_t::bms, "playlevel") == nullptr);
    CHECK(find_format_field_schema(format_family_t::bms, "GENRE") == nullptr);
    CHECK(find_format_field_schema(format_family_t::osu_mania, "PLAYER") == nullptr);
}

TEST_CASE("integer fields accept plain whole numbers inside their range",
          "[unit][core][format-field-schema]") {
    CHECK_FALSE(check(format_family_t::bms, "PLAYER", "1").has_value());
    CHECK_FALSE(check(format_family_t::bms, "PLAYER", "4").has_value());
    CHECK(check(format_family_t::bms, "PLAYER", "5").has_value());
    CHECK(check(format_family_t::bms, "PLAYER", "0").has_value());
    CHECK(check(format_family_t::bms, "PLAYER", "").has_value());
    CHECK(check(format_family_t::bms, "PLAYER", "1.5").has_value());
    CHECK(check(format_family_t::bms, "PLAYER", " 2").has_value());
    CHECK(check(format_family_t::bms, "PLAYER", "1e0").has_value());
    CHECK(check(format_family_t::bms, "PLAYER", "-1").has_value());
    CHECK_FALSE(check(format_family_t::bms, "DIFFICULTY", "5").has_value());
    CHECK(check(format_family_t::bms, "DIFFICULTY", "6").has_value());
    CHECK_FALSE(check(format_family_t::stepmania, "METER", "12").has_value());
    CHECK(check(format_family_t::stepmania, "METER", "1000").has_value());
    CHECK_FALSE(check(format_family_t::bmson, "info.level", "9999").has_value());
    CHECK(check(format_family_t::bmson, "info.level", "-3").has_value());
}

TEST_CASE("decimal fields reject exponents, separators and out-of-range values",
          "[unit][core][format-field-schema]") {
    CHECK_FALSE(check(format_family_t::osu_mania, "Difficulty.HPDrainRate", "8.5").has_value());
    CHECK_FALSE(check(format_family_t::osu_mania, "Difficulty.HPDrainRate", "10").has_value());
    CHECK(check(format_family_t::osu_mania, "Difficulty.HPDrainRate", "10.1").has_value());
    CHECK(check(format_family_t::osu_mania, "Difficulty.HPDrainRate", "8,5").has_value());
    CHECK(check(format_family_t::osu_mania, "Difficulty.HPDrainRate", "8.").has_value());
    CHECK(check(format_family_t::osu_mania, "Difficulty.HPDrainRate", ".5").has_value());
    CHECK(check(format_family_t::osu_mania, "Difficulty.HPDrainRate", "1e1").has_value());
    CHECK_FALSE(check(format_family_t::bms, "TOTAL", "260.5").has_value());
    CHECK(check(format_family_t::bms, "TOTAL", "-1").has_value());
    CHECK_FALSE(check(format_family_t::bmson, "info.judge_rank", "100").has_value());
}

TEST_CASE("choice fields match case-insensitively and text must stay on one line",
          "[unit][core][format-field-schema]") {
    CHECK_FALSE(check(format_family_t::stepmania, "DIFFICULTY", "Challenge").has_value());
    CHECK_FALSE(check(format_family_t::stepmania, "DIFFICULTY", "hard").has_value());
    CHECK(check(format_family_t::stepmania, "DIFFICULTY", "Expert").has_value());
    CHECK_FALSE(check(format_family_t::stepmania, "CREDIT", "Someone & friends").has_value());
    CHECK(check(format_family_t::stepmania, "CREDIT", "two\nlines").has_value());
    CHECK_FALSE(check(format_family_t::osu_mania, "Metadata.Version", "[7K] Hard").has_value());
}
