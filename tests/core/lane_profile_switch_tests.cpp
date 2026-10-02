#include <wormedit/core/lane_profile_switch.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace wormedit;

namespace {

auto switch_song(std::uint32_t keys, const char* profile) -> song_document_t {
    auto profile_value = std::string_view(profile) == "osu" ? make_osu_mania_profile(keys)
                                                           : make_stepmania_profile_for(profile);
    REQUIRE(profile_value);
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{
            .id = chart_id_t{2},
            .measures = {{.id = measure_id_t{3}}},
            .lane_layouts = {{.id = object_id_t{21},
                              .position = {measure_id_t{3}, 0},
                              .layout = profile_value->layout}},
            .input_profile = profile_value->input,
            .bpms = {{.id = object_id_t{22},
                      .position = {measure_id_t{3}, 0},
                      .beats_per_minute = {120, 1}}},
        }},
    };
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{30}, .position = {measure_id_t{3}, 0}, .lane = lane_id_t{3}}};
    REQUIRE(validate_document(song).empty());
    return song;
}

} // namespace

TEST_CASE("a ten-lane layout offers osu!mania and pump-double", "[unit][core][lane-profile-switch]") {
    const auto choices = lane_profile_choices(10);
    REQUIRE(choices.size() == 2);
    CHECK(choices[0].layout.profile_id == "osu-mania-10k");
    CHECK(choices[1].layout.profile_id == "stepmania:pump-double");
    CHECK(choices[1].layout.lanes.front().glyph == lane_glyph_t::arrow_down_left);
    CHECK(lane_profile_choices(7).size() == 1);
}

TEST_CASE("switching profile keeps lane ids and notes and changes glyphs",
          "[unit][core][lane-profile-switch]") {
    auto history = make_document_history(switch_song(10, "osu"));
    const auto target = make_stepmania_profile_for("pump-double");
    REQUIRE(target);
    const auto applied = apply_lane_profile_switch(history, 4, chart_id_t{2},
                                                   {measure_id_t{3}, 0}, target->layout);
    REQUIRE(applied);
    const auto& layout = history.document.charts.front().lane_layouts.front().layout;
    CHECK(layout.profile_id == "stepmania:pump-double");
    CHECK(layout.lanes[2].id == lane_id_t{3});
    CHECK(layout.lanes[2].glyph == lane_glyph_t::arrow_center);
    CHECK(history.document.charts.front().measures.front().notes.front().lane == lane_id_t{3});

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().lane_layouts.front().layout.profile_id ==
          "osu-mania-10k");
}

TEST_CASE("switching to a profile with another lane count is rejected",
          "[unit][core][lane-profile-switch]") {
    auto history = make_document_history(switch_song(10, "osu"));
    const auto target = make_stepmania_profile_for("pump-single");
    REQUIRE(target);
    const auto rejected = apply_lane_profile_switch(history, 4, chart_id_t{2},
                                                    {measure_id_t{3}, 0}, target->layout);
    CHECK_FALSE(rejected);
    CHECK(history.document.charts.front().lane_layouts.front().layout.profile_id ==
          "osu-mania-10k");
}

TEST_CASE("osu!mania 10K profile carries the canonical key colors",
          "[unit][core][lane-profile-switch]") {
    const auto profile = make_osu_mania_profile(10);
    REQUIRE(profile);
    CHECK(profile->layout.lanes[4].key_color == lane_key_color_t::pms_green);
    CHECK(profile->layout.lanes[5].key_color == lane_key_color_t::pms_yellow);
    CHECK(make_osu_mania_profile(14));
    CHECK_FALSE(make_osu_mania_profile(19));
}
