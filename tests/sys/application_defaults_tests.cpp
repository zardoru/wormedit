#include "sys/application_defaults.h"

#include <wormedit/core/document.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <vector>

namespace {

auto valid_initial_layout(const wormedit::song_document_t& document, std::uint32_t lanes)
    -> bool {
    if (document.charts.size() != 1 || !wormedit::validate_document(document).empty())
        return false;
    const auto& chart = document.charts.front();
    if (chart.lane_layouts.size() != 1 || chart.bpms.size() != 1)
        return false;
    return chart.lane_layouts.front().layout.lanes.size() == lanes &&
           chart.bpms.front().beats_per_minute == wormedit::signed_rational_t{120, 1};
}

auto supported_exports(const wormedit::song_document_t& document, std::uint32_t lanes) -> bool {
    if (!wormedit::write_osu_mania(document).value) return false;
    if (lanes == 9 || lanes == 14) return true;
    return wormedit::write_stepmania(document).value.has_value();
}

auto valid_audio_project(const wormedit::song_document_t& document,
                         const std::filesystem::path& path) -> bool {
    if (document.metadata.title != "Title from tag" ||
        document.metadata.artist != "Artist from tag" ||
        document.audio_assets.size() != 1 || document.resources.size() != 1 ||
        document.charts.size() != 1 || !wormedit::validate_document(document).empty())
        return false;
    const auto& layout = document.charts.front().lane_layouts.front().layout;
    const auto& roles = document.resources.front().roles;
    return layout.lanes.size() == 7 &&
           std::ranges::all_of(layout.lanes, [](const auto& lane) {
               return lane.role == wormedit::lane_role_t::playable;
           }) &&
           document.audio_assets.front().source_path == path &&
           document.resources.front().asset == document.audio_assets.front().id &&
           std::ranges::find(roles, wormedit::resource_role_t::music) != roles.end() &&
           std::ranges::find(roles, wormedit::resource_role_t::bgm) != roles.end();
}

void check_lane_count_case(std::uint32_t lanes) {
    const auto document = wormedit::sys::make_initial_document(wormedit::song_id_t{123}, lanes);
    REQUIRE(document);
    CHECK(valid_initial_layout(*document, lanes));
    CHECK(supported_exports(*document, lanes));
    const auto expected_groups = lanes == 9 || lanes == 14 ? 1U : 2U;
    REQUIRE(document->output_groups.size() == expected_groups);
    CHECK(document->output_groups.front().family == wormedit::format_family_t::osu_mania);
}

void check_default_stepmania_group(const wormedit::output_group_t& group) {
    CHECK(group.family == wormedit::format_family_t::stepmania);
    CHECK(group.relative_output_path == "ssc");
    REQUIRE(group.stepmania);
    CHECK(group.stepmania->file_profile == wormedit::stepmania_profile_t::ssc);
}

} // namespace

TEST_CASE("new project layouts create valid charts with the requested playable columns",
          "[unit][sys][application-defaults]") {
    constexpr std::array<std::uint32_t, 8> lane_counts{4, 5, 6, 7, 8, 9, 10, 14};
    for (const auto lanes : lane_counts) {
        DYNAMIC_SECTION(lanes << "K") { check_lane_count_case(lanes); }
    }
}

TEST_CASE("new project rejects unsupported lane counts and retains legacy BMS template",
          "[unit][sys][application-defaults]") {
    CHECK_FALSE(wormedit::sys::make_initial_document(wormedit::song_id_t{123}, 0));
    CHECK_FALSE(wormedit::sys::make_initial_document(wormedit::song_id_t{123}, 19));
    const auto legacy = wormedit::sys::make_initial_document(wormedit::song_id_t{123});
    CHECK(legacy.charts.front().source_family == wormedit::format_family_t::bms);
    CHECK(legacy.charts.front().lane_layouts.front().layout.lanes.size() == 8);
}

TEST_CASE("audio project starts with playable lanes and a linked music resource",
          "[unit][sys][application-defaults]") {
    const auto audio_path = std::filesystem::path{"/tmp/example-track.ogg"};
    const auto document = wormedit::sys::make_audio_project_document(
        wormedit::song_id_t{123}, audio_path, "Title from tag", "Artist from tag", 7);
    REQUIRE(document);
    CHECK(valid_audio_project(*document, audio_path));
    REQUIRE(document->output_groups.size() == 2);
    CHECK(document->output_groups[0].family == wormedit::format_family_t::osu_mania);
    CHECK(document->output_groups[0].relative_output_path == "osu");
    check_default_stepmania_group(document->output_groups[1]);
    for (const auto& group : document->output_groups)
        CHECK(group.charts == std::vector{document->charts.front().id});
    const auto nine_lanes = wormedit::sys::make_audio_project_document(
        wormedit::song_id_t{124}, audio_path, "Title", "Artist", 9);
    REQUIRE(nine_lanes);
    REQUIRE(nine_lanes->output_groups.size() == 1);
    CHECK(nine_lanes->output_groups.front().family == wormedit::format_family_t::osu_mania);
    CHECK_FALSE(wormedit::sys::make_audio_project_document(wormedit::song_id_t{123}, {}, "", "", 7));
}

TEST_CASE("audio project creates editable measures through the imported music duration",
          "[sys][application-defaults][audio-extent]") {
    const auto document = wormedit::sys::make_audio_project_document(
        wormedit::song_id_t{123}, "/tmp/track.mp3", "Track", "Artist", 7,
        {44'100, 2, 44'100 * 13, true});
    REQUIRE(document);
    REQUIRE(document->charts.front().measures.size() == 7);
    CHECK(document->charts.front().measures.front().id == wormedit::measure_id_t{3});
    CHECK(document->audio_assets.front().stream.frame_count == 44'100 * 13);
    CHECK(wormedit::validate_document(*document).empty());
}

namespace {

void check_mania_catalog_document(std::uint32_t keys) {
    const auto document = wormedit::sys::make_initial_document(wormedit::song_id_t{123}, keys);
    REQUIRE(document);
    CHECK(valid_initial_layout(*document, keys));
    const auto bindings = document->charts.front().input_profile.bindings.size();
    // 12K, 16K and 18K have owner-chosen keys (2026-10-18); other wide counts start unbound.
    const bool keyed = keys == 12 || keys == 16 || keys == 18;
    CHECK((keys <= 10 || bindings == (keyed ? keys : 0)));
}

auto is_same_binding(const wormedit::lane_binding_t& actual,
                     const wormedit::lane_binding_t& expected) -> bool {
    return actual.key == expected.key && actual.lane == expected.lane &&
           actual.focused_stage == expected.focused_stage;
}

auto is_same_input(const wormedit::input_profile_t& actual,
                   const wormedit::input_profile_t& expected) -> bool {
    return actual.profile_id == expected.profile_id && actual.stage_count == expected.stage_count &&
           actual.requires_focused_stage == expected.requires_focused_stage &&
           std::ranges::equal(actual.bindings, expected.bindings, is_same_binding);
}

auto is_catalog_output(const wormedit::output_group_t& actual,
                       const wormedit::new_project_output_group_descriptor_t& expected) -> bool {
    return actual.family == expected.family && actual.name == expected.label &&
           actual.relative_output_path == expected.relative_output_path &&
           actual.stepmania == expected.stepmania &&
           actual.charts == std::vector{wormedit::chart_id_t{2}};
}

void check_catalog_document(const wormedit::song_document_t& document,
                            const wormedit::new_project_layout_descriptor_t& layout) {
    const auto expected = layout.profile();
    const auto& chart = document.charts.front();
    CHECK(chart.lane_layouts.front().layout == expected.layout);
    CHECK(is_same_input(chart.input_profile, expected.input));
    CHECK(std::ranges::equal(document.output_groups, layout.default_output_groups, is_catalog_output));
}

void check_initial_family(const wormedit::song_document_t& document) {
    const auto family = document.charts.front().source_family;
    const auto& groups = document.output_groups;
    const bool shared_family = !groups.empty() && std::ranges::all_of(groups, [&](const auto& group) {
        return group.family == groups.front().family;
    });
    CHECK(family == (shared_family ? groups.front().family : wormedit::format_family_t::native_project));
}

void check_catalog_document_valid(const wormedit::song_document_t& document) {
    const auto& chart = document.charts.front();
    const auto lanes = chart.lane_layouts.front().layout.lanes.size();
    const auto bindings = chart.input_profile.bindings.size();
    CHECK((lanes <= 10 || bindings == 0 || bindings == lanes));
    CHECK(wormedit::validate_document(document).empty());
}

void check_catalog_layout(const wormedit::new_project_layout_descriptor_t& layout) {
    const auto blank = wormedit::sys::make_initial_document(wormedit::song_id_t{123}, layout.id);
    REQUIRE(blank);
    const auto audio = wormedit::sys::make_audio_project_document(
        wormedit::song_id_t{123}, "/tmp/track.ogg", "Track", "Artist", layout.id);
    REQUIRE(audio);
    check_catalog_document(*blank, layout);
    check_catalog_document(*audio, layout);
    check_initial_family(*blank);
    check_initial_family(*audio);
    check_catalog_document_valid(*blank);
    check_catalog_document_valid(*audio);
}

} // namespace

TEST_CASE("D7 new projects accept every osu mania key count", "[sys][application-defaults][catalog]") {
    for (std::uint32_t keys = 1; keys <= 18; ++keys) {
        DYNAMIC_SECTION(keys << "K") { check_mania_catalog_document(keys); }
    }
}

TEST_CASE("D7 catalog layouts survive blank and audio document construction", "[sys][application-defaults][catalog]") {
    for (const auto& layout : wormedit::new_project_layouts()) {
        DYNAMIC_SECTION(layout.id) { check_catalog_layout(layout); }
    }
}

TEST_CASE("D7 catalog document construction rejects invalid identities", "[sys][application-defaults][catalog]") {
    CHECK_FALSE(wormedit::sys::make_initial_document(wormedit::song_id_t{123}, "unknown"));
    CHECK_FALSE(wormedit::sys::make_initial_document(wormedit::song_id_t{}, "osu-mania-7k"));
    CHECK_FALSE(wormedit::sys::make_audio_project_document(wormedit::song_id_t{123}, {}, "", "", "pms-standard-9button"));
}

TEST_CASE("new project layouts without output groups keep the native family",
          "[sys][application-defaults][catalog]") {
    auto layout = wormedit::new_project_layouts().front();
    layout.default_output_groups = {};
    CHECK(layout.source_family() == wormedit::format_family_t::native_project);
}
