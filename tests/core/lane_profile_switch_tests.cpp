#include <wormedit/core/lane_profile_switch.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace wormedit;

namespace {
void check_profile(bool value) { CHECK(value); }
void require_profile(bool value) { REQUIRE(value); }


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

auto auxiliary_bms_history() -> document_history_t {
    auto profile = make_bms_5k_free_zone_profile();
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{
            .id = chart_id_t{2},
            .measures = {{.id = measure_id_t{3}}},
            .lane_layouts = {{.id = object_id_t{21},
                              .position = {measure_id_t{3}, 0},
                              .layout = profile.layout}},
            .input_profile = profile.input,
        }},
    };
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{30}, .position = {measure_id_t{3}, 0}, .lane = lane_id_t{107}}};
    REQUIRE(validate_document(song).empty());
    return make_document_history(std::move(song));
}

auto auxiliary_bms_data_unchanged(const document_history_t& history,
                                  const song_document_t& before) -> bool {
    const auto& after_chart = history.document.charts.front();
    const auto& before_chart = before.charts.front();
    const auto& after_note = after_chart.measures.front().notes.front();
    const auto& before_note = before_chart.measures.front().notes.front();
    return history.document.revision == before.revision &&
           after_chart.lane_layouts.front().layout.profile_id ==
               before_chart.lane_layouts.front().layout.profile_id &&
           after_chart.measures.front().notes.size() == before_chart.measures.front().notes.size() &&
           after_note.id == before_note.id && after_note.lane == before_note.lane &&
           after_note.position == before_note.position;
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

TEST_CASE("initial profile choices include every built-in family",
          "[unit][core][lane-profile-switch][bms]") {
    const auto choices = lane_profile_choices();
    const auto bms = make_bms_7k_profile();
    const auto step = make_stepmania_profile_for("pump-double");
    const auto osu = make_osu_mania_profile(18);
    CHECK(choices.size() == 41);
    CHECK(std::ranges::find(choices, bms.layout.profile_id, [](const auto& profile) {
        return std::string_view{profile.layout.profile_id};
    }) != choices.end());
    REQUIRE(step);
    CHECK(std::ranges::find(choices, step->layout.profile_id, [](const auto& profile) {
        return std::string_view{profile.layout.profile_id};
    }) != choices.end());
    REQUIRE(osu);
    CHECK(std::ranges::find(choices, osu->layout.profile_id, [](const auto& profile) {
        return std::string_view{profile.layout.profile_id};
    }) != choices.end());
}

TEST_CASE("first profile choices include and create both osu SpecialStyle variants",
          "[unit][core][lane-profile-switch][osu]") {
    const auto choices = lane_profile_choices();
    for (const auto keys : {12u, 16u}) {
        const auto target = make_osu_mania_profile(keys, true);
        require_profile(target.has_value());
        const auto choice = std::ranges::find(choices, target->layout.profile_id,
            [](const auto& profile) { return std::string_view{profile.layout.profile_id}; });
        require_profile(choice != choices.end());
        auto history = make_document_history(song_document_t{.id = song_id_t{1}, .revision = 4,
            .charts = {{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3}}}}}});
        const auto applied = apply_lane_profile_switch(history, 4, chart_id_t{2},
                                                       {measure_id_t{3}, 0}, choice->layout);
        require_profile(applied.applied);
        const auto& chart = history.document.charts.front();
        check_profile(chart.lane_layouts.front().layout.profile_id == target->layout.profile_id);
        check_profile(chart.lane_layouts.front().layout.lanes.size() == keys);
        std::vector<lane_id_t> lanes;
        std::vector<lane_id_t> bindings;
        for (const auto& lane : chart.lane_layouts.front().layout.lanes) lanes.push_back(lane.id);
        for (const auto& binding : chart.input_profile.bindings) bindings.push_back(binding.lane);
        std::ranges::sort(lanes);
        std::ranges::sort(bindings);
        check_profile(bindings == lanes);
        check_profile(history.undo.size() == 1);
        require_profile(undo_document_command(history));
        check_profile(history.document.charts.front().lane_layouts.empty());
    }
}

TEST_CASE("a chart with no layout can insert its first built-in profile",
          "[unit][core][lane-profile-switch][bms]") {
    const auto target = make_bms_7k_profile();
    song_document_t song{.id = song_id_t{1}, .revision = 4,
        .charts = {{.id = chart_id_t{2},
                    .measures = {{.id = measure_id_t{3}}, {.id = measure_id_t{4}}}}}};
    auto history = make_document_history(std::move(song));
    const auto applied = apply_lane_profile_switch(history, 4, chart_id_t{2},
                                                   {measure_id_t{4}, 120}, target.layout);
    REQUIRE(applied);
    REQUIRE(history.document.charts.front().lane_layouts.size() == 1);
    CHECK(history.document.charts.front().lane_layouts.front().layout.profile_id ==
          target.layout.profile_id);
    CHECK(history.document.charts.front().lane_layouts.front().position ==
          (local_position_t{measure_id_t{3}, 0}));
    CHECK_FALSE(history.document.charts.front().input_profile.bindings.empty());
}

TEST_CASE("inserting a first lane profile records one undo edit",
          "[unit][core][lane-profile-switch][bms]") {
    song_document_t song{.id = song_id_t{1}, .revision = 4,
        .charts = {{.id = chart_id_t{2},
                    .measures = {{.id = measure_id_t{3}}}}}};
    auto history = make_document_history(std::move(song));
    const auto target = make_bms_7k_profile();
    REQUIRE(apply_lane_profile_switch(history, 4, chart_id_t{2},
                                      {measure_id_t{3}, 120}, target.layout));
    REQUIRE(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().lane_layouts.empty());
    CHECK(history.document.charts.front().input_profile.bindings.empty());
}

TEST_CASE("first profile insertion rejects a stale document revision",
          "[unit][core][lane-profile-switch][bms]") {
    auto history = make_document_history(song_document_t{.id = song_id_t{1}, .revision = 4,
        .charts = {{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3}}}}}});
    const auto target = make_bms_7k_profile();
    const auto stale = apply_lane_profile_switch(history, 3, chart_id_t{2},
                                                 {measure_id_t{3}, 0}, target.layout);
    CHECK_FALSE(stale);
    CHECK(history.document.charts.front().lane_layouts.empty());
    CHECK(history.undo.empty());
}

TEST_CASE("first profile remaps every number-row binding without lane ID aliasing",
          "[unit][core][lane-profile-switch][bms]") {
    song_document_t song{.id = song_id_t{1}, .revision = 4,
        .charts = {{.id = chart_id_t{2},
                    .measures = {{.id = measure_id_t{3}}, {.id = measure_id_t{4}}},
                    .lane_layouts = {{.id = object_id_t{30},
                        .position = {measure_id_t{4}, 0},
                        .layout = {.profile_id = "future", .lanes = {
                            {.id = lane_id_t{101}, .name = "Future"}}}}}}}};
    auto history = make_document_history(std::move(song));
    const auto target = make_bms_7k_profile();
    const auto applied = apply_lane_profile_switch(history, 4, chart_id_t{2},
                                                   {measure_id_t{3}, 240}, target.layout);
    require_profile(applied.applied);
    const auto& chart = history.document.charts.front();
    require_profile(chart.lane_layouts.size() == 2);
    const auto& layout = chart.lane_layouts.front().layout;
    std::vector<lane_id_t> lane_ids;
    std::vector<lane_id_t> binding_ids;
    for (const auto& lane : layout.lanes) lane_ids.push_back(lane.id);
    for (const auto& binding : chart.input_profile.bindings) binding_ids.push_back(binding.lane);
    std::ranges::sort(lane_ids);
    std::ranges::sort(binding_ids);
    check_profile(binding_ids == lane_ids);
}

TEST_CASE("first BMS profile materializes measure one and remains one undo command",
          "[unit][core][lane-profile-switch][bms]") {
    song_document_t song{.id = song_id_t{1}, .revision = 4,
        .charts = {{.id = chart_id_t{2}, .source_family = format_family_t::bms}}};
    auto history = make_document_history(std::move(song));
    const auto target = make_bms_7k_profile();
    const auto applied = apply_lane_profile_switch(history, 4, chart_id_t{2}, {}, target.layout);
    require_profile(applied.applied);
    require_profile(history.document.charts.front().measures.size() == 1);
    check_profile(history.document.charts.front().lane_layouts.front().position ==
          (local_position_t{history.document.charts.front().measures.front().id, 0}));
    check_profile(history.undo.size() == 1);
    require_profile(undo_document_command(history));
    check_profile(history.document.charts.front().measures.empty());
    check_profile(history.document.charts.front().lane_layouts.empty());
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

TEST_CASE("switching away from an auxiliary BMS lane rejects without changing chart data",
          "[unit][core][lane-profile-switch][bms]") {
    auto history = auxiliary_bms_history();
    const auto target = make_bms_5k_standard_profile();

    const auto rejected = apply_lane_profile_switch(history, 4, chart_id_t{2},
                                                    {measure_id_t{3}, 0}, target.layout);
    CHECK_FALSE(rejected);
    REQUIRE_FALSE(rejected.diagnostics.empty());
    CHECK(rejected.diagnostics.front().message.find("different number of lanes") !=
          std::string::npos);
}

TEST_CASE("rejected BMS switch leaves auxiliary lane events and profile unchanged",
          "[unit][core][lane-profile-switch][bms]") {
    auto history = auxiliary_bms_history();
    const auto before = history.document;
    const auto target = make_bms_5k_standard_profile();
    const auto rejected = apply_lane_profile_switch(history, 4, chart_id_t{2},
                                                    {measure_id_t{3}, 0}, target.layout);
    REQUIRE_FALSE(rejected);
    CHECK(auxiliary_bms_data_unchanged(history, before));
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
