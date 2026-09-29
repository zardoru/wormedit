#include "sys/application_defaults.h"

#include <wormedit/core/document.h>
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
    if (chart.source_family != wormedit::format_family_t::native_project ||
        chart.lane_layouts.size() != 1 || chart.bpms.size() != 1)
        return false;
    return chart.lane_layouts.front().layout.lanes.size() == lanes &&
           chart.bpms.front().beats_per_minute == wormedit::signed_rational_t{120, 1};
}

auto supported_exports(const wormedit::song_document_t& document, std::uint32_t lanes) -> bool {
    if (!wormedit::write_osu_mania(document).text) return false;
    if (lanes == 9 || lanes == 14) return true;
    return wormedit::write_stepmania(document).text.has_value();
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
    const auto document = wormedit::sys::initial_document(wormedit::song_id_t{123}, lanes);
    REQUIRE(document);
    CHECK(valid_initial_layout(*document, lanes));
    CHECK(supported_exports(*document, lanes));
    const auto expected_groups = lanes == 9 || lanes == 14 ? 1U : 3U;
    REQUIRE(document->output_groups.size() == expected_groups);
    CHECK(document->output_groups.front().family == wormedit::format_family_t::osu_mania);
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
    CHECK_FALSE(wormedit::sys::initial_document(wormedit::song_id_t{123}, 3));
    CHECK_FALSE(wormedit::sys::initial_document(wormedit::song_id_t{123}, 18));
    const auto legacy = wormedit::sys::initial_document(wormedit::song_id_t{123});
    CHECK(legacy.charts.front().source_family == wormedit::format_family_t::bms);
    CHECK(legacy.charts.front().lane_layouts.front().layout.lanes.size() == 8);
}

TEST_CASE("audio project starts with playable lanes and a linked music resource",
          "[unit][sys][application-defaults]") {
    const auto audio_path = std::filesystem::path{"/tmp/example-track.ogg"};
    const auto document = wormedit::sys::audio_project_document(
        wormedit::song_id_t{123}, audio_path, "Title from tag", "Artist from tag", 7);
    REQUIRE(document);
    CHECK(valid_audio_project(*document, audio_path));
    REQUIRE(document->output_groups.size() == 3);
    CHECK(document->output_groups[0].family == wormedit::format_family_t::osu_mania);
    CHECK(document->output_groups[0].relative_output_path == "osu");
    CHECK(document->output_groups[1].family == wormedit::format_family_t::stepmania);
    CHECK(document->output_groups[1].relative_output_path == "sm");
    CHECK(document->output_groups[2].relative_output_path == "ssc");
    for (const auto& group : document->output_groups)
        CHECK(group.charts == std::vector{document->charts.front().id});
    const auto nine_lanes = wormedit::sys::audio_project_document(
        wormedit::song_id_t{124}, audio_path, "Title", "Artist", 9);
    REQUIRE(nine_lanes);
    REQUIRE(nine_lanes->output_groups.size() == 1);
    CHECK(nine_lanes->output_groups.front().family == wormedit::format_family_t::osu_mania);
    CHECK_FALSE(wormedit::sys::audio_project_document(wormedit::song_id_t{123}, {}, "", "", 7));
}
