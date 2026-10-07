#include <wormedit/core/player_preview.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

using namespace wormedit;

namespace {

auto lane(lane_id_t id, std::uint32_t order, lane_glyph_t glyph,
          stage_group_t stage = stage_group_t::none) -> lane_definition_t {
    return {.id = id,
            .name = "Lane " + std::to_string(id.value),
            .role = lane_role_t::playable,
            .stage = stage,
            .display_order = order,
            .glyph = glyph};
}

auto preview_chart() -> chart_document_t {
    chart_document_t chart{
        .id = chart_id_t{7},
        .divisions_per_standard_measure = 960,
        .beat_zero_audio_offset_seconds = {},
    };
    chart.measures = {
        {.id = measure_id_t{1},
         .definition = {{1, 1}, meter_t{4, 4}},
         .notes = {{.id = object_id_t{10},
                    .position = {measure_id_t{1}, 240},
                    .lane = lane_id_t{1}},
                   {.id = object_id_t{11},
                    .position = {measure_id_t{1}, 480},
                    .lane = lane_id_t{2},
                    .kind = note_kind_t::hold,
                    .end = local_position_t{measure_id_t{2}, 0}}}},
        {.id = measure_id_t{2}, .definition = {{1, 1}, meter_t{4, 4}}},
    };
    chart.bpms = {
        {.id = object_id_t{20}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}}};
    chart.lane_layouts = {
        {.id = object_id_t{30},
         .position = {measure_id_t{1}, 0},
         .layout = {.profile_id = "dance-single",
                    .display_name = "Dance Single",
                    .lanes = {lane(lane_id_t{1}, 0, lane_glyph_t::arrow_left, stage_group_t::left),
                              lane(lane_id_t{2}, 1, lane_glyph_t::arrow_down, stage_group_t::left),
                              lane(lane_id_t{3}, 0, lane_glyph_t::arrow_up, stage_group_t::right),
                              lane(lane_id_t{4}, 1, lane_glyph_t::arrow_right,
                                   stage_group_t::right)},
                    .split_stage = true,
                    .split_after_column = 2}},
    };
    return chart;
}

auto frame_at(const player_preview_index_t& index, std::uint64_t row,
              std::uint32_t sample_rate = 48'000) -> std::uint64_t {
    const auto time = timing_at(index.timing, row);
    REQUIRE(time);
    const auto frame = sample_frame_at(*time.seconds, sample_rate);
    REQUIRE(frame);
    return *frame.frame;
}

auto request(std::uint64_t sample_frame = 0) -> player_preview_scene_request_t {
    return {.sample_frame = sample_frame,
            .sample_rate = 48'000,
            .viewport_width_pixels = 412.0,
            .viewport_height_pixels = 520.0,
            .receptor_y_pixels = 64.0,
            .pixels_per_beat = 96.0,
            .split_stage_gutter_pixels = 12.0,
            .skin = player_preview_skin_t::arrows};
}

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

void check_preview_stage_order(const player_preview_scene_t& scene) {
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(scene.lanes[i].lane == lane_id_t{i + 1});
        CHECK(scene.lanes[i].stage == (i < 2 ? stage_group_t::left : stage_group_t::right));
    }
}

} // namespace

TEST_CASE("player preview composes lanes receptors notes and holds from one transport frame",
          "[unit][core][player-preview]") {
    const auto chart = preview_chart();
    const auto built = build_player_preview_index(chart, 9);
    REQUIRE(built);
    const auto scene = make_player_preview_scene(*built.index, request());
    REQUIRE(scene);
    CHECK(scene.scene->chart == chart_id_t{7});
    CHECK(scene.scene->document_revision == 9);
    CHECK(scene.scene->absolute_row == signed_rational_t{0, 1});
    CHECK(scene.scene->split_stage);
    REQUIRE(scene.scene->lanes.size() == 4);
    REQUIRE(scene.scene->receptors.size() == 4);
    check_preview_stage_order(*scene.scene);
    CHECK(scene.scene->lanes[0].bounds.left == 0.0);
    CHECK(scene.scene->lanes[1].bounds.right == 200.0);
    CHECK(scene.scene->lanes[2].bounds.left == 212.0);
    CHECK(scene.scene->lanes[3].bounds.right == 412.0);
    CHECK(scene.scene->receptors[0].glyph == lane_glyph_t::arrow_left);

    const auto tap = std::ranges::find(scene.scene->note_endpoints, object_id_t{10},
                                       &player_preview_note_geometry_t::note);
    REQUIRE(tap != scene.scene->note_endpoints.end());
    CHECK(tap->bounds.top == Catch::Approx(153.0));
    CHECK(tap->bounds.bottom == Catch::Approx(167.0));
    REQUIRE(scene.scene->hold_bodies.size() == 1);
    CHECK(scene.scene->hold_bodies[0].bounds.top == Catch::Approx(256.0));
    CHECK(scene.scene->hold_bodies[0].bounds.bottom == Catch::Approx(448.0));
    CHECK(scene.scene->visible_note_count == 2);
}

TEST_CASE("player preview skins share geometry and change presentation only",
          "[unit][core][player-preview][skin]") {
    const auto built = build_player_preview_index(preview_chart(), 9);
    REQUIRE(built);
    auto arrows_request = request(frame_at(*built.index, 240));
    const auto arrows = make_player_preview_scene(*built.index, arrows_request);
    REQUIRE(arrows);
    auto neutral_request = arrows_request;
    neutral_request.skin = player_preview_skin_t::neutral;
    const auto neutral = make_player_preview_scene(*built.index, neutral_request);
    REQUIRE(neutral);
    auto mania_request = arrows_request;
    mania_request.skin = player_preview_skin_t::mania;
    const auto mania = make_player_preview_scene(*built.index, mania_request);
    REQUIRE(mania);

    REQUIRE(arrows.scene->note_endpoints.size() == neutral.scene->note_endpoints.size());
    REQUIRE(arrows.scene->note_endpoints.size() == mania.scene->note_endpoints.size());
    for (std::size_t index = 0; index < arrows.scene->note_endpoints.size(); ++index) {
        CHECK(arrows.scene->note_endpoints[index].bounds ==
              neutral.scene->note_endpoints[index].bounds);
        CHECK(arrows.scene->note_endpoints[index].bounds ==
              mania.scene->note_endpoints[index].bounds);
        CHECK(neutral.scene->note_endpoints[index].glyph == lane_glyph_t::square);
        CHECK(mania.scene->note_endpoints[index].glyph == lane_glyph_t::bar);
    }
    const auto tap = std::ranges::find(arrows.scene->note_endpoints, object_id_t{10},
                                       &player_preview_note_geometry_t::note);
    REQUIRE(tap != arrows.scene->note_endpoints.end());
    CHECK((tap->bounds.top + tap->bounds.bottom) / 2.0 == Catch::Approx(64.0));
}

namespace {
void check_layer2_rejected(const player_preview_scene_result_t& result) { CHECK_FALSE(result); }
}

TEST_CASE("player preview validates Layer 2 identities, kind and frame bounds", "[player-preview][bga][layer2]") {
    const auto built = build_player_preview_index(preview_chart(), 9);
    REQUIRE(built);
    auto layer_request = request(100);
    layer_request.visual_layers.layer2 = {.sample_frame = 80, .object = object_id_t{80},
        .resource = resource_id_t{180}, .kind = visual_event_kind_t::layer2};
    const auto valid = make_player_preview_scene(*built.index, layer_request);
    REQUIRE(valid);
    CHECK(valid.scene->visual_layers.layer2 == layer_request.visual_layers.layer2);
    auto invalid = layer_request;
    invalid.visual_layers.layer2->kind = visual_event_kind_t::layer;
    check_layer2_rejected(make_player_preview_scene(*built.index, invalid));
    invalid = layer_request;
    invalid.visual_layers.layer2->object = {};
    check_layer2_rejected(make_player_preview_scene(*built.index, invalid));
    invalid = layer_request;
    invalid.visual_layers.layer2->resource = {};
    check_layer2_rejected(make_player_preview_scene(*built.index, invalid));
    invalid = layer_request;
    invalid.visual_layers.layer2->sample_frame = 101;
    check_layer2_rejected(make_player_preview_scene(*built.index, invalid));
}

TEST_CASE("player preview carries validated visual layers in the deterministic scene",
          "[unit][core][player-preview][bga]") {
    const auto built = build_player_preview_index(preview_chart(), 9);
    REQUIRE(built);
    auto visual_request = request(100);
    visual_request.visual_layers.background = {
        .sample_frame = 80,
        .object = object_id_t{80},
        .resource = resource_id_t{180},
        .kind = visual_event_kind_t::background,
    };
    visual_request.visual_layers.video = {
        .sample_frame = 100,
        .object = object_id_t{81},
        .resource = resource_id_t{181},
        .kind = visual_event_kind_t::video,
    };
    const auto scene = make_player_preview_scene(*built.index, visual_request);
    REQUIRE(scene);
    CHECK(scene.scene->visual_layers == visual_request.visual_layers);
    REQUIRE(scene.scene->video_clock.has_value());
    CHECK(scene.scene->video_clock->event == object_id_t{81});
    CHECK(scene.scene->video_clock->elapsed_sample_frames == 0);
    CHECK(scene.scene->video_clock->media_time_numerator == 0);
    CHECK(scene.scene->video_clock->media_time_denominator == 1);
    CHECK(scene.scene->visual_fit == visual_fit_policy_t::contain);

    auto covered = visual_request;
    covered.visual_fit = visual_fit_policy_t::cover;
    const auto cover_scene = make_player_preview_scene(*built.index, covered);
    REQUIRE(cover_scene);
    CHECK(cover_scene.scene->visual_fit == visual_fit_policy_t::cover);

    auto future = visual_request;
    future.visual_layers.background->sample_frame = 101;
    CHECK_FALSE(make_player_preview_scene(*built.index, future));

    auto wrong_slot = visual_request;
    wrong_slot.visual_layers.background->kind = visual_event_kind_t::layer;
    CHECK_FALSE(make_player_preview_scene(*built.index, wrong_slot));

    auto invalid_fit = visual_request;
    invalid_fit.visual_fit = static_cast<visual_fit_policy_t>(255);
    CHECK_FALSE(make_player_preview_scene(*built.index, invalid_fit));
}

TEST_CASE("player preview carries bounded evaluated storyboard instances",
          "[unit][core][player-preview][storyboard]") {
    const auto built = build_player_preview_index(preview_chart(), 9);
    REQUIRE(built);
    const std::array sprites{
        storyboard_draw_instance_t{.stable_id = 2,
                                   .source_order = 3,
                                   .layer = "Background",
                                   .origin = "Centre",
                                   .path = "back.png",
                                   .x = 320.0,
                                   .y = 240.0},
        storyboard_draw_instance_t{.stable_id = 4,
                                   .source_order = 5,
                                   .layer = "Foreground",
                                   .origin = "TopLeft",
                                   .path = "front.png",
                                   .x = 10.0,
                                   .y = 20.0,
                                   .rotation_radians = 0.25,
                                   .opacity = 0.75},
    };
    auto scene_request = request(100);
    scene_request.storyboard_instances = sprites;
    const auto scene = make_player_preview_scene(*built.index, scene_request);
    REQUIRE(scene);
    CHECK(scene.scene->storyboard_instances ==
          std::vector<storyboard_draw_instance_t>{sprites.begin(), sprites.end()});

    scene_request.maximum_storyboard_instances = 1;
    CHECK_FALSE(make_player_preview_scene(*built.index, scene_request));
    scene_request.maximum_storyboard_instances = 2;
    auto invalid = sprites;
    invalid.front().stable_id = 0;
    scene_request.storyboard_instances = invalid;
    CHECK_FALSE(make_player_preview_scene(*built.index, scene_request));
    invalid.front() = sprites.front();
    invalid.front().layer_order = 5;
    scene_request.storyboard_instances = invalid;
    CHECK_FALSE(make_player_preview_scene(*built.index, scene_request));
}

TEST_CASE("player preview uses scroll displacement and remains still through a stop",
          "[unit][core][player-preview][timing]") {
    auto chart = preview_chart();
    chart.scrolls = {
        {.id = object_id_t{40}, .position = {measure_id_t{1}, 0}, .multiplier = {-1, 1}}};
    chart.pauses = {{.id = object_id_t{41},
                     .position = {measure_id_t{1}, 240},
                     .kind = pause_kind_t::stop,
                     .duration = {1, 1},
                     .source_unit = pause_unit_t::seconds}};
    const auto built = build_player_preview_index(chart, 10);
    REQUIRE(built);

    auto reverse_request = request();
    reverse_request.pixels_per_beat = 32.0;
    const auto before = make_player_preview_scene(*built.index, reverse_request);
    REQUIRE(before);
    const auto tap_before = std::ranges::find(before.scene->note_endpoints, object_id_t{10},
                                              &player_preview_note_geometry_t::note);
    REQUIRE(tap_before != before.scene->note_endpoints.end());
    CHECK((tap_before->bounds.top + tap_before->bounds.bottom) / 2.0 == Catch::Approx(32.0));

    const auto event_time = timing_at(built.index->timing, 240);
    const auto departure_time =
        timing_at(built.index->timing, 240, timing_boundary_side_t::after_stops);
    REQUIRE(event_time);
    REQUIRE(departure_time);
    const auto event_frame = sample_frame_at(*event_time.seconds, 48'000);
    REQUIRE(event_frame);
    const auto middle_seconds = signed_rational_t{event_time.seconds->numerator * 2 + 1,
                                                  event_time.seconds->denominator * 2};
    const auto middle_frame = sample_frame_at(middle_seconds, 48'000);
    REQUIRE(middle_frame);
    reverse_request.sample_frame = *event_frame.frame;
    const auto at_event = make_player_preview_scene(*built.index, reverse_request);
    reverse_request.sample_frame = *middle_frame.frame;
    const auto during_stop = make_player_preview_scene(*built.index, reverse_request);
    REQUIRE(at_event);
    REQUIRE(during_stop);
    CHECK(at_event.scene->absolute_row == during_stop.scene->absolute_row);
    CHECK(at_event.scene->note_endpoints == during_stop.scene->note_endpoints);
    CHECK(at_event.scene->hold_bodies == during_stop.scene->hold_bodies);
}

TEST_CASE(
    "player preview extrapolates count-in and rejects bounded composition overflow atomically",
    "[unit][core][player-preview][count-in][negative]") {
    auto chart = preview_chart();
    chart.beat_zero_audio_offset_seconds = {1, 1};
    const auto built = build_player_preview_index(chart, 11);
    REQUIRE(built);
    const auto before_chart = make_player_preview_scene(*built.index, request());
    REQUIRE(before_chart);
    CHECK(before_chart.scene->pre_zero);
    CHECK(before_chart.scene->absolute_row == signed_rational_t{-480, 1});
    const auto tap = std::ranges::find(before_chart.scene->note_endpoints, object_id_t{10},
                                       &player_preview_note_geometry_t::note);
    REQUIRE(tap != before_chart.scene->note_endpoints.end());
    CHECK((tap->bounds.top + tap->bounds.bottom) / 2.0 == Catch::Approx(352.0));

    auto bounded_request = request(frame_at(*built.index, 0));
    bounded_request.maximum_visible_notes = 1;
    const auto over_note_cap = make_player_preview_scene(*built.index, bounded_request);
    CHECK_FALSE(over_note_cap);
    CHECK_FALSE(over_note_cap.scene.has_value());
    CHECK(has_code(over_note_cap.diagnostics, diagnostic_code_t::invalid_resolution));

    bounded_request.maximum_visible_notes = 10;
    bounded_request.maximum_composition_operations = 1;
    const auto over_operation_cap = make_player_preview_scene(*built.index, bounded_request);
    CHECK_FALSE(over_operation_cap);
    CHECK_FALSE(over_operation_cap.scene.has_value());
}

TEST_CASE("player preview interval tree narrows a dense immutable visual schedule",
          "[unit][core][player-preview][performance]") {
    auto chart = preview_chart();
    chart.divisions_per_standard_measure = 60'000;
    chart.measures.resize(1);
    chart.measures[0].notes.clear();
    chart.measures[0].notes.reserve(20'000);
    for (std::uint64_t row = 0; row < 20'000; ++row) {
        chart.measures[0].notes.push_back({.id = object_id_t{1'000 + row},
                                           .position = {measure_id_t{1}, row},
                                           .lane = lane_id_t{1}});
    }
    const auto built = build_player_preview_index(chart, 12, {.maximum_notes = 20'000});
    REQUIRE(built);
    auto narrow = request();
    narrow.viewport_height_pixels = 8.0;
    narrow.receptor_y_pixels = 4.0;
    narrow.pixels_per_beat = 24'000.0;
    narrow.note_head_height_pixels = 1.0;
    narrow.receptor_height_pixels = 2.0;
    const auto scene = make_player_preview_scene(*built.index, narrow);
    REQUIRE(scene);
    CHECK(scene.scene->composition_operations < 256);
    CHECK(scene.scene->visible_note_count < 16);
}
