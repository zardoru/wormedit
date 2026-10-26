#include <wormedit/core/marker_editor.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <limits>

namespace {
using namespace wormedit;
using json = nlohmann::json;

auto export_cut_song() -> song_document_t {
    return {.id = song_id_t{1},
        .charts = {{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3}}},
            .bpms = {{.id = object_id_t{10}, .position = {measure_id_t{3}, 0}, .beats_per_minute = {120, 1}}}}},
        .export_cuts = {{.id = object_id_t{11}, .label = "Intro", .begin_milliseconds = 1000, .end_milliseconds = 2000},
            {.id = object_id_t{12}, .begin_milliseconds = 1500, .end_milliseconds = 3000}}};
}

auto encoded_song() -> json {
    const auto written = write_native_project(export_cut_song());
    REQUIRE(written.value);
    return json::parse(*written.value);
}
} // namespace

TEST_CASE("Native schema22 retains authored export cuts separately from their union", "[format][native-project][export-cut]") {
    const auto encoded = encoded_song();
    CHECK(encoded["document"]["schema_version"] == current_document_schema_version);
    REQUIRE(encoded["document"]["export_cuts"].size() == 2);
    const auto parsed = parse_native_project(encoded.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->export_cuts == export_cut_song().export_cuts);
    CHECK_FALSE(parsed.value->export_cuts.back().label);
}

TEST_CASE("Native schema21 migration starts with no export cuts", "[format][native-project][export-cut][migration]") {
    auto encoded = encoded_song();
    encoded["document"]["schema_version"] = 21;
    encoded["document"].erase("export_cuts");
    const auto parsed = parse_native_project(encoded.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->schema_version == current_document_schema_version);
    CHECK(parsed.value->export_cuts.empty());
}

TEST_CASE("Native export-cut timing rejects fractional and out-of-range numbers", "[format][native-project][export-cut]") {
    auto encoded = encoded_song();
    const auto field = GENERATE("begin_milliseconds", "end_milliseconds");
    const auto invalid = GENERATE(json(1.5), json(std::numeric_limits<std::uint64_t>::max()), json("1500"));
    encoded["document"]["export_cuts"][0][field] = invalid;
    const auto parsed = parse_native_project(encoded.dump());
    CHECK_FALSE(parsed.value);
    CHECK_FALSE(parsed.diagnostics.empty());
}

TEST_CASE("Native export-cut semantics reject reversed bounds and duplicate identities", "[format][native-project][export-cut]") {
    auto encoded = encoded_song();
    SECTION("negative start") { encoded["document"]["export_cuts"][0]["begin_milliseconds"] = -1; }
    SECTION("empty span") { encoded["document"]["export_cuts"][0]["end_milliseconds"] = 1000; }
    SECTION("reversed span") { encoded["document"]["export_cuts"][0]["end_milliseconds"] = 999; }
    SECTION("duplicate identity") { encoded["document"]["export_cuts"][1]["id"] = 11; }
    CHECK_FALSE(parse_native_project(encoded.dump()).value);
}

TEST_CASE("Authoring cuts leaves serialized chart content and timing unchanged", "[format][native-project][export-cut]") {
    const auto before = encoded_song();
    auto history = document_history_t::from(export_cut_song());
    REQUIRE(apply_export_cut_edit(history, insert_edit_t<export_cut_edit_value_t>{
        .value = {.label = "Break", .begin_milliseconds = 5000, .end_milliseconds = 6000}}).applied);
    const auto written = write_native_project(history.document);
    REQUIRE(written.value);
    const auto after = json::parse(*written.value);
    CHECK(after["document"]["charts"] == before["document"]["charts"]);
    CHECK(after["document"]["export_cuts"].size() == 3);
}
