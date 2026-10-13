#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/lane_presentation.h>
#include <wormedit/formats/native_project.h>

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
    return document_history_t::from(std::move(song));
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
        auto history = document_history_t::from(song_document_t{.id = song_id_t{1}, .revision = 4,
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
    auto history = document_history_t::from(std::move(song));
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
    auto history = document_history_t::from(std::move(song));
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
    auto history = document_history_t::from(song_document_t{.id = song_id_t{1}, .revision = 4,
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
    auto history = document_history_t::from(std::move(song));
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
    auto history = document_history_t::from(std::move(song));
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
    auto history = document_history_t::from(switch_song(10, "osu"));
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
    auto history = document_history_t::from(switch_song(10, "osu"));
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

namespace {

auto visible_10k_colors(const lane_layout_t& layout) -> std::vector<lane_key_color_t> {
    std::vector<const lane_definition_t*> lanes;
    for (const auto& lane : layout.lanes) lanes.push_back(&lane);
    std::ranges::sort(lanes, [&layout](const auto* left, const auto* right) {
        if (layout.split_stage && left->stage != right->stage) return left->stage < right->stage;
        return std::pair{left->display_order, left->id.value} <
               std::pair{right->display_order, right->id.value};
    });
    std::vector<lane_key_color_t> colors;
    for (const auto* lane : lanes) colors.push_back(lane->key_color);
    return colors;
}

auto lane_identity_matches(const lane_layout_t& actual, const lane_layout_t& expected) -> bool {
    if (actual.split_stage != expected.split_stage ||
        actual.split_after_column != expected.split_after_column ||
        actual.lanes.size() != expected.lanes.size()) return false;
    for (std::size_t index = 0; index < actual.lanes.size(); ++index) {
        if (actual.lanes[index].id != expected.lanes[index].id ||
            actual.lanes[index].stage != expected.lanes[index].stage ||
            actual.lanes[index].display_order != expected.lanes[index].display_order) return false;
    }
    return true;
}

auto input_bindings_match(const input_profile_t& actual, const input_profile_t& expected) -> bool {
    if (actual.bindings.size() != expected.bindings.size()) return false;
    for (std::size_t index = 0; index < actual.bindings.size(); ++index) {
        if (actual.bindings[index].key != expected.bindings[index].key ||
            actual.bindings[index].lane != expected.bindings[index].lane ||
            actual.bindings[index].focused_stage != expected.bindings[index].focused_stage)
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("switching between BMS scratchless and osu 10K follows visible split-stage order",
          "[unit][core][lane-profile-switch][key-color]") {
    const auto bms = make_bms_10k_scratchless_profile();
    const auto osu = make_osu_mania_profile(10);
    REQUIRE(osu);
    auto history = document_history_t::from(song_document_t{
        .id = song_id_t{1}, .revision = 4,
        .charts = {{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3}}},
                    .lane_layouts = {{.id = object_id_t{21},
                                      .position = {measure_id_t{3}, 0}, .layout = bms.layout}},
                    .input_profile = bms.input}}});
    auto& initial_layout = history.document.charts.front().lane_layouts.front().layout;
    for (auto& lane : initial_layout.lanes) lane.key_color = lane_key_color_t::generic;
    const std::vector expected{lane_key_color_t::white, lane_key_color_t::black,
        lane_key_color_t::white, lane_key_color_t::black, lane_key_color_t::pms_green,
        lane_key_color_t::pms_yellow, lane_key_color_t::black, lane_key_color_t::white,
        lane_key_color_t::black, lane_key_color_t::white};

    require_profile(static_cast<bool>(apply_lane_profile_switch(
        history, history.document.revision, chart_id_t{2}, {measure_id_t{3}, 0}, bms.layout)));
    const auto after_reapply = history.document.charts.front().lane_layouts.front().layout;
    check_profile(visible_10k_colors(after_reapply) == expected);
    check_profile(lane_identity_matches(after_reapply, bms.layout));
    require_profile(static_cast<bool>(apply_lane_profile_switch(
        history, history.document.revision, chart_id_t{2}, {measure_id_t{3}, 0}, osu->layout)));
    const auto& chart = history.document.charts.front();
    const auto& after_switch = chart.lane_layouts.front().layout;
    check_profile(visible_10k_colors(after_switch) == expected);
    check_profile(lane_identity_matches(after_switch, bms.layout));
    check_profile(input_bindings_match(chart.input_profile, bms.input));
}

TEST_CASE("StepMania chart switching to osu layout receives selected profile skin identity and key classes",
          "[core][lane-profile-switch][field-skin-layout]") {
    auto history = document_history_t::from(switch_song(5, "pump-single"));
    auto& chart = history.document.charts.front();
    chart.source_family = format_family_t::stepmania;
    const auto note_lane = chart.measures.front().notes.front().lane;
    const auto target = make_osu_mania_profile(5);
    require_profile(target.has_value());
    require_profile(static_cast<bool>(apply_lane_profile_switch(history, 4, chart_id_t{2},
        {measure_id_t{3}, 0}, target->layout)));
    const auto& switched = history.document.charts.front();
    const auto& layout = switched.lane_layouts.front().layout;
    check_profile(field_skin_layout_key(layout) == "osu-mania-5k");
    check_profile(switched.source_family == format_family_t::stepmania);
    check_profile(switched.measures.front().notes.front().lane == note_lane);
    std::string classes;
    for (const auto& lane : layout.lanes) classes += field_skin_key_class(lane.key_color).front();
    check_profile(classes == "wbybw");
    require_profile(static_cast<bool>(undo_document_command(history)));
    check_profile(field_skin_layout_key(history.document.charts.front().lane_layouts.front().layout) ==
                  "stepmania:pump-single");
}

namespace {
auto generic_osu_roundtrip(bool explicit_class) -> song_document_t {
    auto song = switch_song(4, "osu");
    auto& layout = song.charts.front().lane_layouts.front().layout;
    for (auto& lane : layout.lanes) lane.key_color = lane_key_color_t::generic;
    if (explicit_class) layout.lanes.front().key_color = lane_key_color_t::pms_green;
    const auto written = write_native_project(song);
    require_profile(written.bytes.has_value());
    const auto loaded = parse_native_project(*written.bytes);
    require_profile(loaded.song.has_value());
    return *loaded.song;
}
}

TEST_CASE("Native osu layouts repair old generic classes while retaining explicit lane colors",
          "[core][lane-profile-switch][field-skin-layout]") {
    const auto repaired = generic_osu_roundtrip(false);
    const auto& layout = repaired.charts.front().lane_layouts.front().layout;
    std::string classes;
    for (const auto& lane : layout.lanes) classes += field_skin_key_class(lane.key_color).front();
    check_profile(classes == "wbbw");
    check_profile(field_skin_layout_key(layout) == "osu-mania-4k");
    const auto retained = generic_osu_roundtrip(true);
    const auto& custom = retained.charts.front().lane_layouts.front().layout;
    check_profile(custom.lanes.front().key_color == lane_key_color_t::pms_green);
    check_profile(custom.lanes[1].key_color == lane_key_color_t::generic);
    check_profile(custom.lanes.front().role == lane_role_t::playable);
}

TEST_CASE("Unknown seven-key profiles switch by compatible lanes with one undo and round trip",
          "[core][lane-profile-switch][compatible-layout]") {
    auto song = switch_song(7, "osu");
    auto& original = song.charts.front().lane_layouts.front().layout;
    original.profile_id = "created-7k";
    original.display_name = "Created 7K";
    const auto initial_bindings = song.charts.front().input_profile;
    auto history = document_history_t::from(song);
    const auto target = make_osu_mania_profile(7);
    require_profile(target.has_value());
    check_profile(lane_layout_supports_key_profiles(original));
    require_profile(static_cast<bool>(apply_lane_profile_switch(history, 4, chart_id_t{2},
        {measure_id_t{3}, 0}, target->layout)));
    const auto& chart = history.document.charts.front();
    check_profile(chart.lane_layouts.front().layout.profile_id == "osu-mania-7k");
    check_profile(lane_identity_matches(chart.lane_layouts.front().layout, original));
    check_profile(input_bindings_match(chart.input_profile, initial_bindings));
    check_profile(chart.measures.front().notes.front().lane == lane_id_t{3});
    check_profile(history.undo.size() == 1);
    const auto bytes = write_native_project(history.document);
    require_profile(bytes.bytes.has_value());
    const auto loaded = parse_native_project(*bytes.bytes);
    require_profile(loaded.song.has_value());
    check_profile(loaded.song->charts.front().lane_layouts.front().layout.profile_id == "osu-mania-7k");
    require_profile(static_cast<bool>(undo_document_command(history)));
    check_profile(history.document.charts.front().lane_layouts.front().layout.profile_id == "created-7k");
    check_profile(lane_identity_matches(history.document.charts.front().lane_layouts.front().layout, original));
}

TEST_CASE("Equal counts cannot relabel scratch or auxiliary lanes as ordinary keys",
          "[core][lane-profile-switch][compatible-layout]") {
    auto current = make_bms_7k_profile().layout;
    const auto target = make_osu_mania_profile(static_cast<std::uint32_t>(current.lanes.size()));
    require_profile(target.has_value());
    check_profile(!lane_layout_supports_key_profiles(current));
    check_profile(!retarget_lane_layout(current, target->layout));
    check_profile(lane_layout_supports_key_profiles(make_bms_10k_scratchless_profile().layout));
    auto song = switch_song(8, "osu");
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout = current;
    chart.input_profile = make_bms_7k_profile().input;
    chart.measures.front().notes.front().lane = current.lanes.front().id;
    require_profile(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));
    const auto rejected = apply_lane_profile_switch(history, 4, chart_id_t{2},
        {measure_id_t{3}, 0}, target->layout);
    check_profile(!rejected.applied);
    check_profile(history.document.revision == 4);
    check_profile(history.undo.empty());
}

TEST_CASE("SpecialStyle switches expose both stages and preserve playable column identities",
          "[core][lane-profile-switch][scratchpad-specialstyle]") {
    for (const auto keys : {12U, 16U}) {
        auto song = switch_song(keys, "osu");
        const auto before = song.charts.front();
        auto history = document_history_t::from(song);
        const auto special = make_osu_mania_profile(keys, true);
        const auto choices = lane_profile_choices(keys);
        check_profile(std::ranges::any_of(choices, [&](const auto& value) {
            return value.layout.profile_id == special->layout.profile_id;
        }));
        require_profile(static_cast<bool>(apply_lane_profile_switch(history, 4, before.id,
            {measure_id_t{3}, 0}, special->layout)));
        const auto& chart = history.document.charts.front();
        const auto& layout = chart.lane_layouts.front().layout;
        check_profile(layout.split_stage && layout.split_after_column == keys / 2);
        check_profile(layout.lanes.back().stage == stage_group_t::right);
        check_profile(layout.lanes.front().key_color == lane_key_color_t::scratch);
        check_profile(layout.lanes.back().key_color == lane_key_color_t::scratch);
        check_profile(chart.measures.front().notes.front().lane == before.measures.front().notes.front().lane);
        check_profile(input_bindings_match(chart.input_profile, before.input_profile));
        check_profile(validate_document(history.document).empty());
        const editor_scratch_presentation_t both_left{false, false};
        const auto presented = editor_presented_lanes(layout, both_left);
        check_profile(presented.front()->id == lane_id_t{1});
        check_profile(presented[keys / 2]->id == lane_id_t{keys});
        check_profile(presented[keys / 2]->role == lane_role_t::playable);
        require_profile(static_cast<bool>(undo_document_command(history)));
        check_profile(lane_identity_matches(history.document.charts.front().lane_layouts.front().layout,
                                            before.lane_layouts.front().layout));
    }
}

TEST_CASE("Leaving SpecialStyle restores an ordinary field without replacing input bindings",
          "[core][lane-profile-switch][scratchpad-specialstyle]") {
    auto song = switch_song(16, "osu");
    song.charts.front().lane_layouts.front().layout = make_osu_mania_profile(16, true)->layout;
    song.charts.front().input_profile = make_osu_mania_profile(16, true)->input;
    auto history = document_history_t::from(song);
    require_profile(static_cast<bool>(apply_lane_profile_switch(history, 4, chart_id_t{2},
        {measure_id_t{3}, 0}, make_osu_mania_profile(16)->layout)));
    const auto& chart = history.document.charts.front();
    check_profile(!chart.lane_layouts.front().layout.split_stage);
    check_profile(!chart.lane_layouts.front().layout.split_after_column);
    check_profile(chart.lane_layouts.front().layout.lanes.back().stage == stage_group_t::left);
    check_profile(input_bindings_match(chart.input_profile, song.charts.front().input_profile));
    check_profile(validate_document(history.document).empty());
}
