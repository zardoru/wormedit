#include <wormedit/core/timing_links.h>
#include <wormedit/formats/native_project.h>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <limits>
using namespace wormedit;
namespace {
auto timing_link_song() -> song_document_t {
    auto song = song_document_t{.id = song_id_t{1}, .revision = 7,
        .charts = {{.id = chart_id_t{10}, .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{11}}, {.id = measure_id_t{12}}},
            .bpms = {{.id = object_id_t{13}, .position = {.measure = measure_id_t{12}, .row = 120}, .beats_per_minute = {120, 1}}}},
            {.id = chart_id_t{20}, .divisions_per_standard_measure = 480,
            .measures = {{.id = measure_id_t{21}}, {.id = measure_id_t{22}}},
            .bpms = {{.id = object_id_t{23}, .position = {.measure = measure_id_t{22}, .row = 60}, .beats_per_minute = {120, 1}}}}}};
    song.charts.back().timing_links.value(timing_category_t::bpm) = timing_category_link_t{
        .source = chart_id_t{10}, .objects = {{.source = object_id_t{13}, .target = object_id_t{23}}}};
    return song;
}
void require_valid_links(const song_document_t& song) {
    const auto diagnostics = validate_timing_links(song);
    for (const auto& diagnostic : diagnostics) INFO(diagnostic.message);
    REQUIRE(diagnostics.empty());
}
void require_link_error(const song_document_t& song, diagnostic_code_t code) {
    const auto diagnostics = validate_timing_links(song);
    REQUIRE(std::ranges::any_of(diagnostics, [code](const auto& diagnostic) { return diagnostic.code == code; }));
}
auto native_link_json(const song_document_t& song) -> nlohmann::json {
    const auto output = write_native_project(song);
    for (const auto& diagnostic : output.diagnostics) INFO(diagnostic.message);
    REQUIRE(output.value);
    return nlohmann::json::parse(*output.value);
}
}
TEST_CASE("Timing links map exact beats across unrelated measure boundaries and resolutions", "[core][timing-links]") {
    auto song = timing_link_song();
    song.charts.back().measures = {{.id = measure_id_t{21}, .definition = {.length = {2, 1}, .meter = std::nullopt}}};
    const auto map = timing_position_map_t::from(song.charts.front(), song.charts.back());
    REQUIRE(map);
    CHECK(map->position({.measure = measure_id_t{12}, .row = 120}) == local_position_t{.measure = measure_id_t{21}, .row = 540});
    CHECK_FALSE(map->position({.measure = measure_id_t{11}, .row = 1}));
    CHECK_FALSE(map->position({.measure = measure_id_t{12}, .row = 960}));
    CHECK(map->position({.measure = measure_id_t{12}, .row = 960}, measure_chart_end_policy_t::allow) ==
        local_position_t{.measure = measure_id_t{21}, .row = 960});
    CHECK(map->rows(240) == 120);
}
TEST_CASE("Timing link spans preserve exclusive chart ends and reject inexact or overflowing grids", "[core][timing-links]") {
    auto song = timing_link_song();
    const auto map = timing_position_map_t::from(song.charts.front(), song.charts.back());
    REQUIRE(map);
    const auto span = map->span({.begin = {.measure = measure_id_t{11}, .row = 0},
        .end = {.measure = measure_id_t{12}, .row = 960}});
    REQUIRE(span);
    CHECK(span->end == local_position_t{.measure = measure_id_t{22}, .row = 480});
    const auto overflow = timing_position_map_t{.source_resolution = 1, .target_resolution = 2};
    CHECK_FALSE(overflow.rows(std::numeric_limits<std::uint64_t>::max()));
    song.charts.back().divisions_per_standard_measure = 0;
    CHECK_FALSE(timing_position_map_t::from(song.charts.front(), song.charts.back()));
}
TEST_CASE("Timing source links validate identity mappings and effective snapshots", "[core][timing-links]") {
    auto song = timing_link_song();
    require_valid_links(song);
    SECTION("wrong effective value") { song.charts.back().bpms.front().beats_per_minute = {180, 1}; }
    SECTION("inexact row") { song.charts.front().bpms.front().position.row = 121; }
    SECTION("wrong target identity") { song.charts.back().bpms.front().id = object_id_t{24}; }
    require_link_error(song, diagnostic_code_t::unsupported_feature);
}
TEST_CASE("Timing source graph rejects missing self cyclic and ambiguous mappings", "[core][timing-links]") {
    auto song = timing_link_song();
    auto& link = *song.charts.back().timing_links.value(timing_category_t::bpm);
    SECTION("missing") { link.source = chart_id_t{99}; }
    SECTION("self") { link.source = chart_id_t{20}; }
    SECTION("cycle") {
        song.charts.front().timing_links.value(timing_category_t::bpm) = timing_category_link_t{
            .source = chart_id_t{20}, .objects = {{.source = object_id_t{23}, .target = object_id_t{13}}}};
    }
    SECTION("duplicate mapping") { link.objects.push_back(link.objects.front()); }
    require_link_error(song, diagnostic_code_t::invalid_id);
}
TEST_CASE("Timing local overrides remember a source without following its current snapshot", "[core][timing-links]") {
    auto song = timing_link_song();
    song.charts.back().timing_links.value(timing_category_t::bpm)->is_local_override = true;
    song.charts.back().bpms.front().beats_per_minute = {180, 1};
    require_valid_links(song);
    const auto root = native_link_json(song);
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->charts.back().timing_links == song.charts.back().timing_links);
    CHECK(parsed.value->charts.back().bpms.front() == song.charts.back().bpms.front());
}
TEST_CASE("Native timing links round trip exact source identities and reject inconsistent stored data", "[core][timing-links][native_project]") {
    const auto song = timing_link_song();
    auto root = native_link_json(song);
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->charts.back().timing_links == song.charts.back().timing_links);
    root["document"]["charts"][1]["bpms"][0]["beats_per_minute"]["numerator"] = 180;
    CHECK_FALSE(parse_native_project(root.dump()).ok());
}
TEST_CASE("Native schema twelve migrates every chart to independent timing categories", "[core][timing-links][native_project]") {
    auto song = timing_link_song();
    song.charts.back().timing_links = {};
    auto root = native_link_json(song);
    root["document"]["schema_version"] = 12;
    for (auto& chart : root["document"]["charts"]) chart.erase("timing_links");
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->schema_version == current_document_schema_version);
    for (const auto& chart : parsed.value->charts) CHECK(chart.timing_links == timing_links_t{});
    CHECK(parsed.value->charts.back().bpms.front() == song.charts.back().bpms.front());
}
TEST_CASE("Linked scroll recipes map spans and fixed row steps with stable recipe identities", "[core][timing-links]") {
    auto song = timing_link_song();
    auto& source = song.charts.front();
    auto& target = song.charts.back();
    source.sv_recipes.push_back({.id = recipe_id_t{31}, .name = "Scroll", .range = {
        .begin = {.measure = measure_id_t{11}, .row = 0}, .end = {.measure = measure_id_t{12}, .row = 960}},
        .output = sv_recipe_output_t::scrolls, .sampling = {.fixed_point_count = std::nullopt, .fixed_step_rows = 240}});
    auto recipe = source.sv_recipes.front();
    recipe.id = recipe_id_t{32};
    recipe.range = {.begin = {.measure = measure_id_t{21}, .row = 0}, .end = {.measure = measure_id_t{22}, .row = 480}};
    recipe.sampling.fixed_step_rows = 120;
    target.sv_recipes.push_back(recipe);
    target.timing_links.value(timing_category_t::scroll) = timing_category_link_t{.source = source.id,
        .recipes = {{.source = recipe_id_t{31}, .target = recipe_id_t{32}}}};
    require_valid_links(song);
    target.sv_recipes.front().sampling.fixed_step_rows = 240;
    require_link_error(song, diagnostic_code_t::unsupported_feature);
}
TEST_CASE("Linked measure layouts preserve independent measure identities and row resolutions", "[core][timing-links]") {
    auto song = timing_link_song();
    song.charts.back().timing_links.value(timing_category_t::measures) = timing_category_link_t{.source = chart_id_t{10},
        .measures = {{.source = measure_id_t{11}, .target = measure_id_t{21}},
            {.source = measure_id_t{12}, .target = measure_id_t{22}}}};
    require_valid_links(song);
    const auto root = native_link_json(song);
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->charts.back().divisions_per_standard_measure == 480);
    CHECK(parsed.value->charts.back().timing_links == song.charts.back().timing_links);
    song.charts.back().measures.back().definition.meter = std::nullopt;
    require_link_error(song, diagnostic_code_t::unsupported_feature);
}
TEST_CASE("Stops delays and offsets resolve independently without rescaling pause units", "[core][timing-links]") {
    auto song = timing_link_song();
    auto& source = song.charts.front();
    auto& target = song.charts.back();
    source.pauses.push_back({.id = object_id_t{14}, .position = {.measure = measure_id_t{11}, .row = 0},
        .kind = pause_kind_t::stop, .duration = {1, 2}, .source_unit = pause_unit_t::beats});
    target.pauses = {{.id = object_id_t{24}, .position = {.measure = measure_id_t{21}, .row = 0},
        .kind = pause_kind_t::stop, .duration = {1, 2}, .source_unit = pause_unit_t::beats},
        {.id = object_id_t{25}, .position = {.measure = measure_id_t{21}, .row = 0},
        .kind = pause_kind_t::delay, .duration = {7, 1}}};
    target.timing_links.value(timing_category_t::stop) = timing_category_link_t{.source = source.id,
        .objects = {{.source = object_id_t{14}, .target = object_id_t{24}}}};
    target.timing_links.value(timing_category_t::offset) = timing_category_link_t{.source = source.id};
    require_valid_links(song);
    target.pauses.back().duration = {8, 1};
    require_valid_links(song);
    target.beat_zero_audio_offset_seconds = {-1, 10};
    require_link_error(song, diagnostic_code_t::unsupported_feature);
}
