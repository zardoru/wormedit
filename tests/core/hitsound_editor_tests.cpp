#include <wormedit/core/hitsound_editor.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/core/interaction_dispatch.h>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {
using namespace wormedit;
auto hitsound_song() -> song_document_t {
    song_document_t song{.id = song_id_t{1}, .charts = {{.id = chart_id_t{2},
        .source_family = format_family_t::osu_mania,
        .measures = {{.id = measure_id_t{3}, .notes = {
            {.id = object_id_t{10}, .position = {measure_id_t{3}, 120}, .lane = lane_id_t{1}},
            {.id = object_id_t{11}, .position = {measure_id_t{3}, 240}, .lane = lane_id_t{1},
             .osu_hitsample = osu_hitsample_t{.whistle = true, .volume = 60}}}}},
        .lane_layouts = {{.id = object_id_t{20}, .position = {measure_id_t{3}, 0},
            .layout = {.profile_id = "one-key", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
        .bpms = {{.id = object_id_t{21}, .position = {measure_id_t{3}, 0}, .beats_per_minute = {120, 1}}}}}};
    REQUIRE(validate_document(song).empty());
    return song;
}
void check_selected_whistles(const chart_document_t& chart) {
    CHECK(chart.find_note(object_id_t{10})->osu_hitsample->whistle);
    CHECK(chart.find_note(object_id_t{11})->osu_hitsample->whistle);
    CHECK(chart.find_note(object_id_t{11})->osu_hitsample->volume == 60);
}
auto native_hitsound_json() -> nlohmann::json {
    const auto written = write_native_project(hitsound_song());
    REQUIRE(written.value);
    return nlohmann::json::parse(*written.value);
}
void capture_hold_defaults(document_history_t& history, editor_interaction_state_t& state) {
    REQUIRE(dispatch_editor_action(history, state, begin_hold_action_t{{measure_id_t{3}, 0}}));
    state.armed_hitsample = osu_hitsample_t{};
    REQUIRE(dispatch_editor_action(history, state, commit_hold_action_t{{measure_id_t{3}, 480}}));
}
void capture_paint_defaults(document_history_t& history, editor_interaction_state_t& state) {
    REQUIRE(dispatch_editor_action(history, state,
        begin_note_paint_gesture_action_t{.lane = lane_id_t{1}, .position = {measure_id_t{3}, 0}}));
    state.armed_hitsample = osu_hitsample_t{};
    REQUIRE(dispatch_editor_action(history, state,
        end_note_paint_gesture_action_t{.lane = lane_id_t{1}, .position = {measure_id_t{3}, 480}}));
}
void check_captured_defaults(document_history_t& history, const osu_hitsample_t& sample) {
    const auto& notes = history.document.charts.front().measures.front().notes;
    REQUIRE_FALSE(notes.empty());
    for (const auto& note : notes) CHECK(note.osu_hitsample == sample);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures.front().notes.empty());
}
}

TEST_CASE("Hitsound mixed additions become on for the whole selection in one Undo step", "[unit][hitsound]") {
    auto history = document_history_t::from(hitsound_song());
    const std::array ids{object_id_t{10}, object_id_t{11}};
    const auto edit = apply_hitsound_edit(history, chart_id_t{2}, ids, {.field = hitsound_field_t::whistle});
    REQUIRE(edit.applied);
    check_selected_whistles(history.document.charts[0]);
    REQUIRE(undo_document_command(history));
    CHECK_FALSE(history.document.charts[0].find_note(ids[0])->osu_hitsample);
    CHECK_FALSE(undo_document_command(history));
}

TEST_CASE("Hitsound sample sets cycle and accept direct choices", "[unit][hitsound]") {
    auto sample = changed_hitsample({}, {.field = hitsound_field_t::normal_set});
    CHECK(sample.normal_set == osu_sample_set_t::normal);
    sample = changed_hitsample(sample, {.field = hitsound_field_t::addition_set, .value = 3});
    CHECK(sample.addition_set == osu_sample_set_t::drum);
    sample = changed_hitsample(sample, {.field = hitsound_field_t::addition_set});
    CHECK(sample.addition_set == osu_sample_set_t::automatic);
}

TEST_CASE("Next note hitsound defaults are written by the normal tap placement pipeline", "[unit][hitsound]") {
    auto history = document_history_t::from(hitsound_song());
    const std::array<std::uint64_t, 1> snaps{4};
    auto setup = editor_interaction_state_t::from(history, chart_id_t{2}, lane_id_t{1},
        {measure_id_t{3}, 0}, snaps, 4, 600);
    REQUIRE(setup.state);
    setup.state->armed_hitsample = osu_hitsample_t{.clap = true};
    const auto result = dispatch_editor_action(history, *setup.state,
        place_tap_action_t{.position = {measure_id_t{3}, 0}});
    REQUIRE(result.created_note);
    CHECK(history.document.charts[0].find_note(*result.created_note)->osu_hitsample == setup.state->armed_hitsample);
}

TEST_CASE("Hitsound commands reject invalid selections and values atomically", "[unit][hitsound]") {
    auto history = document_history_t::from(hitsound_song());
    std::array ids{object_id_t{10}, object_id_t{11}};
    hitsound_change_t change{.field = hitsound_field_t::clap};
    SECTION("missing") { ids[1] = object_id_t{999}; }
    SECTION("duplicate") { ids[1] = ids[0]; }
    SECTION("invalid value") { change.value = 4; }
    SECTION("invalid field") { change.field = static_cast<hitsound_field_t>(99); }
    const auto revision = history.document.revision;
    CHECK_FALSE(apply_hitsound_edit(history, chart_id_t{2}, ids, change).applied);
    CHECK(history.document.revision == revision);
    CHECK_FALSE(undo_document_command(history));
}

TEST_CASE("Native hold and paint gestures capture hitsound defaults at their start", "[unit][hitsound]") {
    auto song = hitsound_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::native_project;
    chart.measures.front().notes.clear();
    auto history = document_history_t::from(std::move(song));
    const std::array<std::uint64_t, 1> snaps{4};
    auto setup = editor_interaction_state_t::from(history, chart_id_t{2}, lane_id_t{1},
        {measure_id_t{3}, 0}, snaps, 4, 600);
    REQUIRE(setup.state);
    auto& state = *setup.state;
    CHECK_FALSE(state.armed_hitsample);
    const osu_hitsample_t sample{.finish = true, .normal_set = osu_sample_set_t::drum};
    state.armed_hitsample = sample;
    SECTION("hold") { capture_hold_defaults(history, state); }
    SECTION("paint") { capture_paint_defaults(history, state); }
    check_captured_defaults(history, sample);
}

TEST_CASE("Native hitsample storage accepts absent fields and rejects invalid values", "[unit][native_project][hitsound]") {
    auto root = native_hitsound_json();
    auto& note = root["document"]["charts"][0]["measures"][0]["notes"][1];
    bool valid{true};
    std::optional<osu_hitsample_t> expected = hitsound_song().charts[0].find_note(object_id_t{11})->osu_hitsample;
    SECTION("absent") {
        note.erase("osu_hitsample");
        expected.reset();
    }
    SECTION("invalid volume") {
        note["osu_hitsample"]["volume"] = 101;
        valid = false;
    }
    SECTION("all fields") {}
    const auto parsed = parse_native_project(root.dump());
    CHECK(parsed.value.has_value() == valid);
    if (!valid) return;
    REQUIRE(parsed.value);
    CHECK(parsed.value->charts[0].find_note(object_id_t{11})->osu_hitsample == expected);
}
