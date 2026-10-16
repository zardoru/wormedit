#include "compose_scene_cache_fixture.h"
#include "../sys/application_dispatch_fixture.h"
#include "sys/application_defaults.h"
#include "sys/compose_input.h"

#include <catch2/catch_test_macros.hpp>

#include <source_location>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
void expect_fit(bool value, std::source_location where = std::source_location::current()) {
    INFO("Beat fit check at line " << where.line());
    REQUIRE(value);
}

auto fit_song(bool downscroll) -> song_document_t {
    auto song = make_audio_project_document(song_id_t{42}, "fixture.mp3", "7K fit", "", 7,
                                      {44'100, 2, 441'000, true});
    expect_fit(song.has_value());
    auto& chart = song->charts.front();
    chart.beat_zero_audio_offset_seconds = {1, 4};
    if (!downscroll) chart.lane_layouts.front().layout.lanes.front().glyph = lane_glyph_t::arrow_up;
    expect_fit(compose_field_downscroll(chart) == downscroll);
    return *song;
}

struct fit_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    document_history_t history;
    workspace_controller_t workspace;
    editor_interaction_state_t state;
    compose_scene_cache_t cache;
    application_command_state_t application;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    WormUiWorkspaceAction action{};
    bool downscroll;

    explicit fit_fixture_t(bool down) : history(document_history_t::from(fit_song(down))), downscroll(down) {
        const auto bound = workspace_controller_t::from(history);
        expect_fit(bound.value.has_value());
        workspace = *bound.value;
        workspace.active_tab = workspace_tab_t::compose;
        workspace.transport.audio_asset = asset_id_t{6};
        const auto& chart = history.document.charts.front();
        const std::uint64_t snaps[]{4, 8, 16};
        const auto setup = editor_interaction_state_t::from(history, chart.id, lane_id_t{1},
            {chart.measures.front().id, 0}, snaps, 16, 800);
        expect_fit(setup.state.has_value());
        state = *setup.state;
        state.viewport_height_pixels = 400;
        state.pixels_per_standard_measure = 240;
        state.viewport_scroll_pixels = 40;
        const auto index = build_editor_field_scene_index(chart, history.document.revision);
        expect_fit(std::holds_alternative<editor_field_scene_index_t>(index));
        static_cast<void>(cache.scene.index.get_or_build(compose_scene_source_key_t{},
            retained_scene_fixture_t{&std::get<editor_field_scene_index_t>(index)}));
        cache.timing.marker_timing_index = timing_index_t::from(chart).value;
        expect_fit(cache.timing.marker_timing_index.has_value());
        cache.waveform.waveform_overview_sample_rate = 44'100;
        cache.waveform.waveform_overview_total_frames = 441'000;
    }

    void send(WormUiComposeInputKind kind, double world_pixel, bool ctrl = false, bool onset = false) {
        action = {};
        action.compose_input_chart_id = state.chart.value;
        action.compose_input_document_revision = history.document.revision;
        action.compose_input_viewport_width_pixels = 800;
        action.compose_input_count = 1;
        const auto source_y = world_pixel - state.viewport_scroll_pixels;
        action.compose_inputs[0] = {.kind = kind,
            .object_id = history.document.charts.front().divisions_per_standard_measure / 2,
            .y_pixels = downscroll ? state.viewport_height_pixels - source_y : source_y,
            .beat_fit_ctrl_held = ctrl, .beat_fit_onset_snap_held = onset,
            .beat_fit_snap_step_numerator = 10, .beat_fit_snap_step_denominator = 1};
        const auto status = dispatch_compose_input_action(action, history, workspace, state,
            viewport, navigation, application, cache, 0, &sink);
        INFO(status);
        expect_fit(!status.empty());
    }
};

void check_fit_proposal(fit_fixture_t& fixture, double world_pixel,
                       std::uint64_t frame, signed_rational_t bpm) {
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE, world_pixel);
    expect_fit(fixture.cache.timing.beat_fit_drag.has_value());
    expect_fit(fixture.cache.timing.beat_fit_drag->payload.proposed_sample_frame == frame);
    expect_fit(fixture.cache.timing.beat_fit_drag->payload.proposed_bpm == bpm);
    expect_fit(fixture.history.document.charts.front().bpms.front().beats_per_minute ==
               signed_rational_t{120, 1});
    expect_fit(fixture.history.undo.empty());
}

void check_beat_drag(bool downscroll) {
    fit_fixture_t fixture(downscroll);
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN, 120);
    check_fit_proposal(fixture, 120, 55'125, {120, 1});
    check_fit_proposal(fixture, 90, 44'100, {160, 1});
    check_fit_proposal(fixture, 180, 77'175, {80, 1});
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE, 180);
    expect_fit(!fixture.cache.timing.beat_fit_drag);
    expect_fit(fixture.history.undo.size() == 1);
    expect_fit(fixture.history.document.charts.front().bpms.front().beats_per_minute ==
               signed_rational_t{80, 1});
    expect_fit(undo_document_command(fixture.history));
    expect_fit(fixture.history.document.charts.front().bpms.front().beats_per_minute ==
               signed_rational_t{120, 1});
}

void check_offset_proposal(fit_fixture_t& fixture, WormUiComposeInputKind kind,
                           double world_pixel, std::int64_t delta) {
    fixture.send(kind, world_pixel);
    expect_fit(fixture.action.audio_offset_drag_delta_frames == delta);
    expect_fit(fixture.action.audio_offset_drag_sample_rate == 44'100);
    expect_fit(fixture.history.document.charts.front().beat_zero_audio_offset_seconds ==
               signed_rational_t{1, 4});
    expect_fit(fixture.history.undo.empty());
}

void check_offset_drag(bool downscroll) {
    fit_fixture_t fixture(downscroll);
    fixture.send(WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN, 120);
    expect_fit(fixture.cache.timing.offset_fit_drag.has_value());
    check_offset_proposal(fixture, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE, 120, 0);
    check_offset_proposal(fixture, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE, 90, -11'025);
    check_offset_proposal(fixture, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE, 180, 22'050);
    check_offset_proposal(fixture, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE, 180, 22'050);
    expect_fit(fixture.action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_COMMIT);
    expect_fit(!fixture.cache.timing.offset_fit_drag);
}
} // namespace

TEST_CASE("Compose beat BPM drag follows upscroll and downscroll with exact commit and undo",
          "[sys][beat-fit][downscroll]") {
    check_beat_drag(false);
    check_beat_drag(true);
}

TEST_CASE("Compose Shift beat drag follows upscroll and downscroll when fitting audio offset",
          "[sys][beat-fit][downscroll]") {
    check_offset_drag(false);
    check_offset_drag(true);
}

TEST_CASE("Compose onset fit preserves exact frame while plain Ctrl rounds BPM",
          "[sys][beat-fit][onset-snap]") {
    for (const bool down : {false, true}) {
        fit_fixture_t fixture(down);
        fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN, 120);
        fixture.cache.timing.beat_fit_onsets.frames = {44'000};
        fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE, 90, true, true);
        expect_fit(fixture.cache.timing.beat_fit_drag.has_value());
        expect_fit(fixture.cache.timing.beat_fit_drag->payload.proposed_sample_frame == 44'000);
        expect_fit(fixture.cache.timing.beat_fit_onset_snapped);
        expect_fit(!(fixture.cache.timing.beat_fit_drag->payload.snap_active));
        expect_fit(fixture.history.undo.empty());
        fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE, 90, true, false);
        expect_fit(!(fixture.cache.timing.beat_fit_onset_snapped));
        expect_fit(fixture.cache.timing.beat_fit_drag->payload.snap_active);
        expect_fit(fixture.cache.timing.beat_fit_drag->payload.proposed_bpm == signed_rational_t{160, 1});
        fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE, 90, true, true);
        expect_fit(fixture.history.undo.size() == 1);
        expect_fit(undo_document_command(fixture.history));
        expect_fit(fixture.history.document.charts.front().bpms.front().beats_per_minute ==
              signed_rational_t{120, 1});
    }
}

TEST_CASE("Compose onset fit missing candidates remains unsnapped and source change cancels",
          "[sys][beat-fit][onset-snap]") {
    fit_fixture_t fixture(false);
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN, 120);
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE, 90, true, true);
    expect_fit(fixture.cache.timing.beat_fit_drag->payload.proposed_sample_frame == 44'100);
    expect_fit(!(fixture.cache.timing.beat_fit_onset_snapped));
    expect_fit(!(fixture.cache.timing.beat_fit_drag->payload.snap_active));
    fixture.workspace.transport.audio_asset = asset_id_t{99};
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE, 90, true, true);
    expect_fit(!(fixture.cache.timing.beat_fit_drag));
    expect_fit(fixture.history.undo.empty());
}

TEST_CASE("Compose fit captures visible cached onsets once and rejects a replaced source",
          "[sys][beat-fit][onset-snap]") {
    fit_fixture_t fixture(false);
    auto analysis = std::make_shared<audio::waveform_analysis_result_t>();
    analysis->waveform.emplace();
    auto& wave = *analysis->waveform;
    wave.sample_rate = 44'100; wave.total_frames = 441'000;
    wave.key.frames_per_bin = 441;
    wave.bins.resize(1000);
    for (auto& bin : wave.bins) bin.frame_count = 441;
    wave.bins[100].rms[1] = 0.5f;
    fixture.cache.timing.fit_waveform = analysis;
    fixture.cache.timing.fit_waveform_asset = fixture.workspace.transport.audio_asset;
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN, 120);
    expect_fit(fixture.cache.timing.beat_fit_onsets.frames == std::vector<std::uint64_t>{44'100});
    auto refreshed = std::make_shared<audio::waveform_analysis_result_t>(*analysis);
    refreshed->waveform->bins[100].rms[1] = 0;
    fixture.cache.timing.fit_waveform = refreshed;
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE, 90.2, true, true);
    expect_fit(fixture.cache.timing.beat_fit_drag->payload.proposed_sample_frame == 44'100);
    expect_fit(fixture.cache.timing.beat_fit_onset_snapped);
    refreshed->waveform->key.begin_frame = 441;
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE, 90.2, true, true);
    expect_fit(!(fixture.cache.timing.beat_fit_onset_snapped));
    expect_fit(fixture.cache.timing.beat_fit_drag->payload.proposed_sample_frame != 44'100);
    expect_fit(fixture.history.undo.empty());
}

TEST_CASE("Compose visible onset query clips negative audio time and outside chart viewports",
          "[sys][beat-fit][onset-snap]") {
    for (const bool down : {false, true}) {
        fit_fixture_t fixture(down);
        auto& chart = fixture.history.document.charts.front();
        chart.beat_zero_audio_offset_seconds = {-1, 4};
        fixture.cache.timing.marker_timing_index = timing_index_t::from(chart).value;
        fixture.state.viewport_scroll_pixels = -40;
        auto analysis = std::make_shared<audio::waveform_analysis_result_t>();
        analysis->waveform.emplace();
        auto& wave = *analysis->waveform;
        wave.sample_rate = 44'100; wave.total_frames = 441'000;
        wave.key.frames_per_bin = 441;
        wave.bins.resize(1000);
        for (auto& bin : wave.bins) bin.frame_count = 441;
        wave.bins[0].rms[1] = 0.5f;
        wave.bins[900].rms[1] = 0.5f;
        fixture.cache.timing.fit_waveform = analysis;
        fixture.cache.timing.fit_waveform_asset = fixture.workspace.transport.audio_asset;
        fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN, 120);
        expect_fit(fixture.cache.timing.beat_fit_onsets.frames == std::vector<std::uint64_t>{0});
        fixture.cache.clear_beat_fit_capture();
        expect_fit(fixture.cache.timing.beat_fit_onsets.frames.empty());
        fixture.state.viewport_scroll_pixels = 1'000'000;
        fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN, 120);
        expect_fit(fixture.cache.timing.beat_fit_onsets.frames.empty());
        fixture.cache.clear_beat_fit_capture();
        fixture.state.viewport_scroll_pixels = -1'000'000;
        fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN, 120);
        expect_fit(fixture.cache.timing.beat_fit_onsets.frames.empty());
    }
}

TEST_CASE("Beat fit notifications distinguish previews commits and unchanged release", "[unit][sys][compose-notification]") {
    fit_fixture_t fixture(false);
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN, 120);
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE, 180);
    fixture.center.drain_inbox(notification_time_t{0});
    auto entries = fixture.center.snapshot(notification_capacity);
    expect_fit(entries.empty());
    expect_fit(fixture.center.current_line() != nullptr);
    expect_fit(fixture.center.current_line()->severity == notification_severity_t::hint);
    expect_fit(fixture.center.current_line()->source == notification_source_t::input);
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE, 180);
    fixture.center.drain_inbox(notification_time_t{1});
    entries = fixture.center.snapshot(notification_capacity);
    expect_fit(entries[0].severity == notification_severity_t::success);
    expect_fit(entries[0].source == notification_source_t::edit);
    expect_fit(entries[0].message == "Beat interval fitted to the selected audio frame.");
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN, 180);
    fixture.send(WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE, 180);
    fixture.center.drain_inbox(notification_time_t{2});
    entries = fixture.center.snapshot(notification_capacity);
    expect_fit(entries[0].severity == notification_severity_t::info);
    expect_fit(entries[0].source == notification_source_t::edit);
    expect_fit(entries[0].message == "Beat fit was unchanged.");
}

TEST_CASE("Offset fit stages hint feedback and cancel preserves timing", "[unit][sys][compose-notification]") {
    fit_fixture_t fixture(false);
    fixture.send(WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN, 120);
    fixture.send(WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE, 180);
    fixture.send(WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE, 180);
    fixture.center.drain_inbox(notification_time_t{0});
    const auto entries = fixture.center.snapshot(notification_capacity);
    expect_fit(entries.empty());
    expect_fit(fixture.center.current_line() != nullptr);
    expect_fit(fixture.center.current_line()->severity == notification_severity_t::hint);
    expect_fit(fixture.history.undo.empty());
    expect_fit(fixture.action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_COMMIT);
}
