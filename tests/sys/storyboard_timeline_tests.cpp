#include "sys/storyboard/storyboard_timeline_view.h"
#include "../support/storyboard_timeline_fixture.h"
#include <wormedit/core/grid.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <limits>

using namespace wormedit;
using test::timeline_song;
using test::timeline_root;

namespace {
void check_signed_millisecond_rounding(const sys::storyboard_timeline_timing_t& absent) {
    CHECK(sys::plan_storyboard_timeline_time({.milliseconds = -2, .remainder = 1, .denominator = 2},
        absent, false, false) == -2);
    CHECK(sys::plan_storyboard_timeline_time({.milliseconds = -2, .remainder = 2, .denominator = 3},
        absent, false, false) == -1);
    CHECK(sys::plan_storyboard_timeline_time({.milliseconds = 8, .remainder = 1, .denominator = 2},
        absent, true, true) == 8);
}

void check_shared_beat_grid_rounding(const sys::storyboard_timeline_timing_t& source) {
    CHECK(sys::plan_storyboard_timeline_time({.milliseconds = 249}, source, true, false) == 0);
    CHECK(sys::plan_storyboard_timeline_time({.milliseconds = 250}, source, true, false) == 0);
    CHECK(sys::plan_storyboard_timeline_time({.milliseconds = 251}, source, true, false) == 500);
    CHECK(sys::plan_storyboard_timeline_time({.milliseconds = 251}, source, true, true) == 251);
}

void check_transform_row_order(const ui::storyboard_timeline_view_t& view) {
    REQUIRE(view.rows.size() == 10);
    CHECK(view.rows[0].track.node == "first");
    CHECK(view.rows[0].track.op == "pivot");
    CHECK(view.rows[1].track.op == "rotate");
    CHECK(view.rows[1].depth == 1);
}

void check_property_row_order(const ui::storyboard_timeline_view_t& view) {
    CHECK(view.rows[2].track.kind == osbx_track_kind_t::opacity);
    CHECK(view.rows[3].track.kind == osbx_track_kind_t::color);
    CHECK(view.rows[4].track.kind == osbx_track_kind_t::visibility);
    CHECK(view.rows[5].track.kind == osbx_track_kind_t::blend);
    CHECK(view.rows[6].track.node == "second");
}

void check_transform_key_positions(const ui::storyboard_timeline_view_t& view) {
    REQUIRE(view.rows[1].keys.size() == 2);
    CHECK(view.rows[1].keys[0].id == "key-a");
    CHECK(view.rows[1].keys[0].time_ms == -10);
    CHECK(view.rows[1].keys[0].position == Catch::Approx(-1));
    CHECK(view.rows[1].keys[1].position == Catch::Approx(2));
}

void check_selected_keys_and_easings(const ui::storyboard_timeline_view_t& view) {
    CHECK(view.rows[1].keys[0].selected);
    CHECK_FALSE(view.rows[1].keys[1].selected);
    CHECK(view.rows[1].keys[0].easing_label == "Step");
    CHECK(view.rows[2].keys[0].easing_label == "In Quad");
    CHECK(view.rows[2].keys[1].selected);
}

void check_playhead_and_easing_previews(const ui::storyboard_timeline_view_t& view, storyboard_time_t time) {
    CHECK(view.time == time);
    CHECK(view.playhead_position == Catch::Approx(-0.15));
    REQUIRE(view.easings.size() == 38);
    CHECK(view.easings[1].preview[16] == Catch::Approx(0.5));
    CHECK(view.easings[5].preview[16] == Catch::Approx(0.25));
}

void check_keyless_color_insertion(const song_document_t& song, storyboard_time_t time) {
    const auto color = sys::plan_storyboard_timeline_edit(song, storyboard_id_t{11},
        {.kind = ui::storyboard_timeline_action_kind_t::add_key,
         .track = {.node = "first", .kind = osbx_track_kind_t::color}}, time);
    REQUIRE(color);
    const auto& created = std::get<osbx_add_key_t>(*color);
    CHECK(created.time_ms == -2); // Signed -1.5 ms: the authored key tie goes earlier.
    CHECK(std::get<std::vector<double>>(created.value) == std::vector<double>{1, 1, 1, 1});
}

void check_occupied_move_refusal(document_history_t& history, const song_document_t& song,
                                 const ui::storyboard_timeline_action_t& action) {
    const auto collision = sys::plan_storyboard_timeline_edit(history.document, storyboard_id_t{11}, action, {});
    REQUIRE(collision);
    CHECK(std::get<osbx_move_keys_t>(*collision).delta_ms == 30);
    const auto refused = apply_osbx_edit(history, {.storyboard = storyboard_id_t{11}, .change = *collision});
    CHECK_FALSE(refused.applied);
    CHECK_FALSE(refused.diagnostics.empty());
    CHECK(history.document.storyboards == song.storyboards);
    CHECK(history.undo.empty());
}

void check_signed_pointer_conversion() {
    const auto fractional = sys::storyboard_timeline_pointer_time(-1.5);
    REQUIRE(fractional);
    CHECK(*fractional == storyboard_time_t{.milliseconds = -2, .remainder = 1, .denominator = 2});
    CHECK_FALSE(sys::storyboard_timeline_pointer_time(std::numeric_limits<double>::infinity()));
    CHECK_FALSE(sys::storyboard_timeline_pointer_time(std::numeric_limits<double>::quiet_NaN()));
}
} // namespace

TEST_CASE("Storyboard timeline millisecond snapping keeps exact negative ties and refuses overflow",
          "[sys][storyboard-timeline]") {
    const sys::storyboard_timeline_timing_t absent;
    check_signed_millisecond_rounding(absent);
    const auto unavailable = sys::plan_storyboard_timeline_time({}, absent, true, false);
    REQUIRE_FALSE(unavailable);
    CHECK(unavailable.error() == failure_code_t::unavailable);
    const auto invalid = sys::plan_storyboard_timeline_time({.denominator = 0}, absent, false, false);
    REQUIRE_FALSE(invalid);
    const auto overflow = sys::plan_storyboard_timeline_time({.milliseconds = std::numeric_limits<std::int64_t>::max(),
        .remainder = 2, .denominator = 3}, absent, false, false);
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error() == failure_code_t::arithmetic_overflow);
}

TEST_CASE("Storyboard timeline beat snap maps through selected chart timing and the shared grid",
          "[sys][storyboard-timeline]") {
    chart_document_t chart;
    chart.divisions_per_standard_measure = 192;
    chart.measures = {{.id = measure_id_t{1}, .definition = {.length = {1, 1}}}};
    chart.bpms = {{.id = object_id_t{1}, .position = {.measure = measure_id_t{1}},
        .beats_per_minute = {120, 1}}};
    const auto measures = measure_index_t::from(chart);
    const auto timing = timing_index_t::from(chart);
    REQUIRE(measures);
    REQUIRE(timing.value);
    const sys::storyboard_timeline_timing_t source{.chart = &chart, .measures = &*measures,
        .timing = &*timing.value, .grid = {.denominator = 4}};
    REQUIRE(source.is_available());
    check_shared_beat_grid_rounding(source);
}

TEST_CASE("Storyboard timeline rows follow document op depth and property order with stable selected keys",
          "[sys][storyboard-timeline]") {
    const auto song = timeline_song();
    auto root = timeline_root();
    ui::storyboard_timeline_view_t view;
    sys::storyboard_timeline_projection_t projection;
    const storyboard_time_t time{.milliseconds = -2, .remainder = 1, .denominator = 2};
    projection.refill(view, song, root, time);
    check_transform_row_order(view);
    check_property_row_order(view);
    check_transform_key_positions(view);
    check_selected_keys_and_easings(view);
    check_playhead_and_easing_previews(view, time);
    root.state.viewport.timeline_begin_ms = -20;
    root.state.viewport.timeline_pixels_per_ms = 0.5;
    projection.refill(view, song, root, time);
    CHECK(view.rows[1].keys[0].position == Catch::Approx(5));
    CHECK(view.playhead_position == Catch::Approx(9.25));
}

TEST_CASE("Storyboard timeline cache invalidates on revision board selection session and prepared generation",
          "[sys][storyboard-timeline]") {
    auto song = timeline_song(); auto root = timeline_root();
    ui::storyboard_timeline_view_t view;
    sys::storyboard_timeline_projection_t projection;
    projection.refill(view, song, root, {});
    song.storyboards[0].osbx->nodes[0].tracks.opacity.keys[0].time_ms = 5;
    ++song.revision;
    projection.refill(view, song, root, {});
    CHECK(view.rows[2].keys[0].time_ms == 5);
    root.state.selection.nodes = {"second"};
    projection.refill(view, song, root, {});
    REQUIRE(view.rows.size() == 4);
    CHECK(view.rows[0].track.node == "second");
    root.state.selection.storyboard = storyboard_id_t{999};
    projection.refill(view, song, root, {});
    CHECK(view.rows.empty());
}

TEST_CASE("Storyboard timeline explicit K creates keyless tracks and samples fractional keyed tracks through shared policy",
          "[sys][storyboard-timeline]") {
    auto song = timeline_song();
    const storyboard_time_t time{.milliseconds = -2, .remainder = 1, .denominator = 2};
    check_keyless_color_insertion(song, time);
    auto& opacity = song.storyboards[0].osbx->nodes[0].tracks.opacity;
    const auto keyed = sys::plan_storyboard_timeline_edit(song, storyboard_id_t{11},
        {.kind = ui::storyboard_timeline_action_kind_t::add_key,
         .track = {.node = "first", .kind = osbx_track_kind_t::opacity}},
        {.milliseconds = 50, .remainder = 1, .denominator = 2});
    REQUIRE(keyed);
    const auto& inserted = std::get<osbx_add_key_t>(*keyed);
    CHECK(inserted.time_ms == 50);
    CHECK(std::get<std::vector<double>>(inserted.value)[0] == Catch::Approx(0.505 * 0.505));
    const auto occupied = sys::plan_storyboard_timeline_edit(song, storyboard_id_t{11},
        {.kind = ui::storyboard_timeline_action_kind_t::add_key,
         .track = {.node = "first", .kind = osbx_track_kind_t::opacity}}, {});
    REQUIRE(occupied);
    CHECK(std::get<osbx_set_key_value_t>(*occupied).key == opacity.keys[0].id);
}

TEST_CASE("Storyboard timeline move plan uses original signed anchor and refuses occupied times atomically",
          "[sys][storyboard-timeline]") {
    auto song = timeline_song();
    song.storyboards[0].osbx->nodes[0].lifetime = osbx_lifetime_t{.begin_ms = -100, .end_ms = 100};
    auto history = document_history_t::from(song);
    ui::storyboard_timeline_action_t action{.kind = ui::storyboard_timeline_action_kind_t::move_keys,
        .keys = {{.track = {.node = "first", .kind = osbx_track_kind_t::transform, .op = "rotate"}, .key = "key-a"}},
        .pointer_delta_ms = 30, .anchor_time_ms = -10};
    check_occupied_move_refusal(history, song, action);
    action.pointer_delta_ms = 10;
    const auto move = sys::plan_storyboard_timeline_edit(history.document, storyboard_id_t{11}, action, {});
    REQUIRE(move);
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard_id_t{11}, .change = *move}).applied);
    CHECK(history.undo.size() == 1);
    const auto& node = history.document.storyboards[0].osbx->nodes[0];
    CHECK(node.ops[0].children[0].track.keys[0].time_ms == 0);
    CHECK(node.ops[0].children[0].track.keys[0].id == "key-a");
    CHECK(node.lifetime == song.storyboards[0].osbx->nodes[0].lifetime);
}

TEST_CASE("Storyboard timeline prepared Lua and generated-only rows retain authored editability",
          "[sys][storyboard-timeline]") {
    auto song = timeline_song(); auto root = timeline_root();
    song.storyboards[0].osbx->nodes[0].tracks.opacity.lua = osbx_lua_track_link_t{};
    auto prepared = *song.storyboards[0].osbx;
    prepared.nodes[0].tracks.opacity.lua.reset();
    prepared.nodes.push_back({.id = "generated"});
    root.state.selection.nodes.push_back("generated");
    ui::storyboard_timeline_view_t view;
    sys::storyboard_timeline_projection_t projection;
    projection.refill(view, song, root, {}, &prepared);
    CHECK(view.rows[2].editable);
    CHECK(view.rows[2].keys.size() == 2);
    CHECK_FALSE(view.rows.back().editable);
    CHECK_FALSE(view.rows.back().reason.empty());
    const auto add = sys::plan_storyboard_timeline_edit(song, storyboard_id_t{11},
        {.kind = ui::storyboard_timeline_action_kind_t::add_key,
         .track = {.node = "first", .kind = osbx_track_kind_t::opacity}}, {}, &prepared);
    REQUIRE_FALSE(add);
    CHECK(add.error() == failure_code_t::unavailable);
}

TEST_CASE("Storyboard timeline pointer conversion and relative drag preserve signed clock bounds",
          "[sys][storyboard-timeline]") {
    check_signed_pointer_conversion();
    auto song = timeline_song();
    song.storyboards[0].osbx->nodes[0].tracks.opacity.keys = {{.id = "fade-a",
        .time_ms = std::numeric_limits<std::int64_t>::max() - 1, .value = {0}}};
    const osbx_key_target_t key{.track = {.node = "first", .kind = osbx_track_kind_t::opacity}, .key = "fade-a"};
    ui::storyboard_timeline_action_t action{.kind = ui::storyboard_timeline_action_kind_t::move_keys,
        .keys = {key}, .pointer_delta_ms = 1, .anchor_time_ms = std::numeric_limits<std::int64_t>::max() - 1};
    const auto move = sys::plan_storyboard_timeline_edit(song, storyboard_id_t{11}, action, {});
    REQUIRE(move);
    CHECK(std::get<osbx_move_keys_t>(*move).delta_ms == 1);
    action.pointer_delta_ms = 2;
    const auto overflow = sys::plan_storyboard_timeline_edit(song, storyboard_id_t{11}, action, {});
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error() == failure_code_t::arithmetic_overflow);
}
