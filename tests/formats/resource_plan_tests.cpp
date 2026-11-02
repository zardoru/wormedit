#include <wormedit/formats/resource_plan.h>
#include <wormedit/core/bms36.h>
#include <wormedit/core/lane_profiles.h>

#include <catch2/catch_test_macros.hpp>
#include <array>

using namespace wormedit;
namespace {
auto resource_song() -> song_document_t {
    const auto profile = make_osu_mania_profile(4);
    REQUIRE(profile);
    return {.id = song_id_t{1}, .revision = 7,
        .audio_assets = {{.id = asset_id_t{10}, .source_path = "keysounds/one.wav"},
                         {.id = asset_id_t{20}, .source_path = "keysounds/two.wav"}},
        .resources = {{.id = resource_id_t{11}, .name = "one", .asset = asset_id_t{10},
                        .roles = {resource_role_t::sample}},
                      {.id = resource_id_t{21}, .name = "two", .asset = asset_id_t{20},
                        .roles = {resource_role_t::bgm}}},
        .charts = {{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{30},
            .bgm_events = {{.id = object_id_t{1}, .position = {measure_id_t{30}, 0}, .source_lane = object_id_t{40}, .sound = resource_id_t{11}},
                           {.id = object_id_t{2}, .position = {measure_id_t{30}, 120}, .source_lane = object_id_t{40}, .sound = resource_id_t{11}}}}},
            .lane_layouts = {{.id = object_id_t{41}, .position = {measure_id_t{30}, 0}, .layout = profile->layout}},
            .input_profile = profile->input, .bgm_source_lanes = {{.id = object_id_t{40}, .name = "BGM"}},
            .bpms = {{.id = object_id_t{42}, .position = {measure_id_t{30}, 0}, .beats_per_minute = {120, 1}}}}}};
}
auto request(resource_id_t resource, std::uint64_t event = 1) -> resource_plan_request_t {
    return {.chart = chart_id_t{2}, .event = object_id_t{event}, .resource = resource};
}
}

namespace {
void check_resource_plan_source_unchanged(const song_document_t& song) {
    CHECK(song.resources == resource_song().resources);
    CHECK(song.audio_assets == resource_song().audio_assets);
    CHECK(song.revision == 7);
}
void check_conflicting_wav_plan(const song_document_t& song, std::span<const resource_plan_request_t> requests) {
    const auto failed = resource_plan_t::from(song, format_family_t::bms, requests);
    CHECK_FALSE(failed.value);
    REQUIRE(failed.diagnostics.size() == 1);
    CHECK(failed.diagnostics[0].code == failure_code_t::conflict);
    CHECK(failed.diagnostics[0].location.event == requests.back().event);
}
void check_compatible_sample_plan(const song_document_t& song, format_family_t target,
    std::span<const resource_plan_request_t> requests) {
    const auto planned = resource_plan_t::from(song, target, requests);
    REQUIRE(planned.value);
    REQUIRE(planned.value->bindings.size() == 2);
    CHECK(planned.value->bindings[0].identifier == planned.value->bindings[1].identifier);
    CHECK(planned.value->bindings[1].decision == resource_plan_decision_t::reused);
    CHECK(resource_plan_t::from(song, target, requests).value->bindings == planned.value->bindings);
}
}

TEST_CASE("resource_plan preserves explicit WAV references and rejects conflicting audio", "[resource_plan]") {
    const auto song = resource_song();
    auto first = request(resource_id_t{11});
    first.explicit_bms_index = "0a";
    auto second = request(resource_id_t{21}, 2);
    second.explicit_bms_index = "0A";
    const std::array requests{first, second};
    check_conflicting_wav_plan(song, requests);
    check_resource_plan_source_unchanged(song);
    const auto retained = resource_plan_t::from(song, format_family_t::bms, std::span(requests).first(1));
    REQUIRE(retained.value);
    CHECK(retained.value->bindings[0].identifier == "0A");
    CHECK(retained.value->bindings[0].decision == resource_plan_decision_t::retained);
}

TEST_CASE("resource_plan allocates through shared WAV occupancy and reports exhaustion", "[resource_plan]") {
    auto song = resource_song();
    song.resources[0].target_identities = {{.family = format_family_t::bms,
        .identifier = "01", .namespace_name = "WAV"}};
    const std::array requests{request(resource_id_t{21})};
    const auto allocated = resource_plan_t::from(song, format_family_t::bms, requests);
    REQUIRE(allocated.value);
    CHECK(allocated.value->bindings[0].identifier == "02");
    for (std::size_t slot = 2; slot < 36 * 36; ++slot)
        song.resources[0].target_identities.push_back({.family = format_family_t::bms,
            .identifier = std::string{bms36_digits[slot / 36], bms36_digits[slot % 36]},
            .namespace_name = "WAV"});
    const auto failed = resource_plan_t::from(song, format_family_t::bms, requests);
    CHECK_FALSE(failed.value);
    REQUIRE(failed.diagnostics.size() == 1);
    CHECK(failed.diagnostics[0].code == failure_code_t::capacity_exceeded);
}

TEST_CASE("resource_plan reuses compatible BMSON channels and osu filenames deterministically", "[resource_plan]") {
    const auto song = resource_song();
    const std::array requests{request(resource_id_t{11}), request(resource_id_t{11}, 2)};
    for (const auto target : {format_family_t::bmson, format_family_t::osu_mania})
        check_compatible_sample_plan(song, target, requests);
}

TEST_CASE("resource_plan rejects filename collisions unsafe paths and unresolved sources before output", "[resource_plan]") {
    const auto song = resource_song();
    auto first = request(resource_id_t{11});
    auto second = request(resource_id_t{21}, 2);
    SECTION("case-insensitive filename collision") {
        first.filename = "same.WAV";
        second.filename = "SAME.wav";
    }
    SECTION("path traversal") { second.filename = "../outside.wav"; }
    SECTION("missing source") { second.resource = resource_id_t{999}; }
    const std::array requests{first, second};
    const auto failed = resource_plan_t::from(song, format_family_t::osu_mania, requests);
    CHECK_FALSE(failed.value);
    REQUIRE_FALSE(failed.diagnostics.empty());
    CHECK(song.resources == resource_song().resources);
    CHECK(song.audio_assets == resource_song().audio_assets);
    CHECK(song.revision == 7);
}


TEST_CASE("resource_plan revalidates explicit source-chart promises instead of trusting resource IDs", "[resource_plan]") {
    auto source = resource_song();
    source.resources.front().target_identities = {{.family = format_family_t::bms, .identifier = "0A", .namespace_name = "WAV"}};
    auto prepared = source;
    auto sample = request(resource_id_t{11});
    sample.explicit_bms_index = "0A"; sample.source_chart = chart_id_t{2};
    const std::array requests{sample};
    REQUIRE(resource_plan_t::from(prepared, format_family_t::osu_mania, requests, &source).value);
    SECTION("deleted source chart") { source.charts.clear(); }
    SECTION("unknown source slot") { source.resources.front().target_identities.clear(); }
    SECTION("ambiguous source slot") { source.resources.back().target_identities = source.resources.front().target_identities; }
    SECTION("rebound slot") { source.resources.front().asset = asset_id_t{20}; }
    SECTION("scoped ID coincidence with changed source audio") { source.audio_assets.front().source_path = "replacement.wav"; }
    const auto failed = resource_plan_t::from(prepared, format_family_t::osu_mania, requests, &source);
    CHECK_FALSE(failed.value);
    CHECK_FALSE(failed.diagnostics.empty());
    CHECK(prepared.audio_assets.front().source_path == "keysounds/one.wav");
}
