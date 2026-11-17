// BGM mute/solo mixing and registered selection scopes.
#include "frame_timing_fixtures.h"
#include "compose_window_overloads_view.h"
#include "compose_window_overloads_input.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
auto bgm_mix_test_action(const compose_test_fixture_t& fixture, WormUiComposeInputKind kind,
    std::uint64_t lane) -> wormedit::ui::workspace_action_t {
    wormedit::ui::workspace_action_t action{};
    action.stamp.chart_id.value = 1;
    action.stamp.document_revision = fixture.history.document.revision;
    action.input.compose_inputs.resize(1);
    action.input.compose_inputs[0] = {.kind = kind, .lane_id = {lane}};
    return action;
}

auto bgm_mix_test_song() -> song_document_t {
    auto song = compose_test_song();
    song.charts.front().bgm_source_lanes = {{.id = object_id_t{80000}},
        {.id = object_id_t{80001}}, {.id = object_id_t{80002}}};
    return song;
}
} // namespace

TEST_CASE("BGM mute and additive solo are session-only and refresh cached lane headers",
          "[frame-timing][bgm-mix]") {
    compose_test_fixture_t fixture{bgm_mix_test_song()};
    std::vector<std::uint64_t> applied;
    fixture.application.services.transport_set_bgm_mix = [&](auto chart, auto muted) {
        expect_compose_test(chart == chart_id_t{1});
        applied.assign(muted.begin(), muted.end());
        return true;
    };
    fixture.project();
    const auto before = write_native_project(fixture.history.document);
    expect_compose_test(before.value.has_value());
    auto toggle = [&](WormUiComposeInputKind kind, std::uint64_t lane) {
        auto action = bgm_mix_test_action(fixture, kind, lane);
        (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
        expect_compose_test(action.input.compose_inputs.size() == 0);
        fixture.project();
    };
    toggle(WORM_UI_COMPOSE_INPUT_BGM_MUTE, 80000);
    expect_compose_test(applied == std::vector<std::uint64_t>{80000});
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes[0].muted);
    toggle(WORM_UI_COMPOSE_INPUT_BGM_SOLO, 80001);
    expect_compose_test(applied == std::vector<std::uint64_t>{80000, 80002});
    toggle(WORM_UI_COMPOSE_INPUT_BGM_SOLO, 80002);
    expect_compose_test(applied == std::vector<std::uint64_t>{80000});
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes[1].solo);
    expect_compose_test(!fixture.flattened.view.compose.bgm_lanes[1].mix_silent);
    toggle(WORM_UI_COMPOSE_INPUT_BGM_MUTE, 80000);
    expect_compose_test(applied == std::vector<std::uint64_t>{80000});
    toggle(WORM_UI_COMPOSE_INPUT_BGM_SOLO, 80001);
    toggle(WORM_UI_COMPOSE_INPUT_BGM_SOLO, 80002);
    expect_compose_test(applied.empty());
    const auto after = write_native_project(fixture.history.document);
    expect_compose_test(after.value == before.value);
    expect_compose_test(fixture.history.undo.empty());
    fixture.application.project.bgm.bgm_lane_mix[1].solo.insert(99999);
    expect_compose_test(compose_bgm_muted_lanes(fixture.history.document.charts.front(),
        fixture.application.project.bgm.bgm_lane_mix.at(1)).empty());
    reset_application_project_edit_state(fixture.application);
    expect_compose_test(fixture.application.project.bgm.bgm_lane_mix.empty());
    expect_compose_test(static_cast<bool>(fixture.application.services.transport_set_bgm_mix));
}

TEST_CASE("BGM mixing rejects stale missing and runtime-failed controls atomically",
          "[frame-timing][bgm-mix]") {
    compose_test_fixture_t fixture{bgm_mix_test_song()};
    int calls{};
    fixture.application.services.transport_set_bgm_mix = [&](auto, auto) { ++calls; return false; };
    auto action = bgm_mix_test_action(fixture, WORM_UI_COMPOSE_INPUT_BGM_MUTE, 80000);
    ++action.stamp.document_revision;
    (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
    expect_compose_test(calls == 0);
    action = bgm_mix_test_action(fixture, WORM_UI_COMPOSE_INPUT_BGM_MUTE, 99999);
    (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
    expect_compose_test(calls == 0);
    action = bgm_mix_test_action(fixture, WORM_UI_COMPOSE_INPUT_BGM_MUTE, 80000);
    (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
    expect_compose_test(calls == 1);
    expect_compose_test(fixture.application.project.bgm.bgm_lane_mix.empty());
    fixture.application.services.transport_set_bgm_mix = {};
    action = bgm_mix_test_action(fixture, WORM_UI_COMPOSE_INPUT_BGM_SOLO, 80001);
    (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
    expect_compose_test(fixture.application.project.bgm.bgm_lane_mix.empty());
}

TEST_CASE("BGM-only charts receive canvas width and mix controls without note interaction state",
          "[frame-timing][bgm-mix]") {
    auto song = bgm_mix_test_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.clear();
    chart.input_profile = {};
    chart.measures.front().notes.clear();
    compose_test_fixture_t fixture{std::move(song)};
    fixture.application.services.transport_set_bgm_mix = [](auto, auto) { return true; };
    auto action = bgm_mix_test_action(fixture, WORM_UI_COMPOSE_INPUT_BGM_MUTE, 80000);
    action.input.compose_input_viewport_width_pixels = 1232;
    action.input.compose_input_editor_right_pixels = 800;
    refresh_compose_editor_width(action, fixture.history, *fixture.binding.value, fixture.application);
    expect_compose_test(fixture.application.project.compose_view.compose_editor_right_pixels == 800);
    (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
    fixture.project();
    expect_compose_test(!fixture.interaction);
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes.size() == 3);
    expect_compose_test(fixture.flattened.view.compose.bgm_mix_available);
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes[0].muted);
    for (const auto& lane : std::span(fixture.flattened.view.compose.bgm_lanes)) expect_compose_test(lane.bounds.right <= 800);
}

TEST_CASE("Compose classifies armable BGM stem heads after projection and cache reuse",
          "[sys][compose][bmson][scratchpad-stem-projection]") {
    const auto bgm_only = GENERATE(false, true);
    const auto missing = GENERATE(false, true);
    auto song = t463_shared_asset_song();
    song.resources.front().missing = missing;
    if (bgm_only) song.charts.front().lane_layouts.clear();
    compose_test_fixture_t fixture{std::move(song)};
    for (int pass = 0; pass < 2; ++pass) {
        t475_project(fixture);
        const auto& view = fixture.flattened.view.compose;
        const auto events = std::span(view.bgm_events);
        const auto stem = std::ranges::find(events, object_id_t{500}, &wormedit::ui::compose_bgm_event_view_t::event_id);
        const auto packed = std::ranges::find(events, object_id_t{502}, &wormedit::ui::compose_bgm_event_view_t::event_id);
        t475_require(stem != events.end());
        t475_require(packed != events.end());
        t475_check(stem->armable_stem == !missing);
        t475_check(!packed->armable_stem);
    }
}

TEST_CASE("Mouse stem entry receives the same decoded source geometry as lane keys",
          "[sys][compose][bmson][stem-mouse-entry]") {
    const auto alt = GENERATE(false, true);
    const auto short_source = GENERATE(false, true);
    auto song = t463_shared_asset_song();
    song.audio_assets.front().stream = {};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.project();
    seed_stem_geometry_fixture(fixture.cache.stems.stem_index, chart_stamp_t::from(fixture.history.document, fixture.history.document.charts.front().id),
        {{.asset = asset_id_t{200}, .source_sample_rate = 48000,
        .source_frame_count = short_source ? 1000U : 480000U}});
    auto action = paint_stroke_inputs(fixture, 100);
    action.input.compose_inputs[0].paint_intent = 1;
    if (alt) {
        action.input.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN;
        action.input.compose_inputs[1].kind = WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE;
    }
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto status = dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    INFO(status);
    const auto& chart = fixture.history.document.charts.front();
    expect_compose_test(chart.measures.front().notes.size() == 1);
    expect_compose_test(chart.measures.front().notes.front().sound_channel_event.has_value());
    expect_compose_test(chart.sound_channels.front().events.back().continuation == !short_source);
    expect_compose_test(chart.sound_channels.front().events.back().playable);
    expect_compose_test(!fixture.interaction->hold() && !fixture.interaction->gesture<wormedit::pending_note_paint_gesture_t>());
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(fixture.history.document.audio_assets.front().stream.sample_rate == 0);
}

TEST_CASE("Registered selection scopes preserve mixed objects and expose Select All Objects",
          "[frame-timing][selection-consolidation]") {
    auto song = bga_virtual_preview_song();
    auto& chart = song.charts.front();
    chart.bookmarks.push_back({.id = object_id_t{9001}, .label = "Marker",
        .position = local_position_t{measure_id_t{1}, 240}});
    chart.sections.push_back({.id = object_id_t{9002}, .label = "Section",
        .beat_span = position_span_t{{measure_id_t{1}, 240}, {measure_id_t{1}, 480}}});
    chart.bgm_source_lanes.push_back({.id = object_id_t{700}, .name = "BGM"});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{9003},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{700}});
    compose_test_fixture_t f{std::move(song)};
    f.project();
    const auto revision = f.history.document.revision;
    expect_compose_test(invoke_compose_registered_command(f, "selection.select_all_objects").value.is_invoked());
    const auto all = f.interaction->selected_objects;
    const auto notes = f.interaction->selected_notes.size();
    for (const auto kind : {editor_selection_object_kind_t::note, editor_selection_object_kind_t::bga_event,
                           editor_selection_object_kind_t::bgm_event, editor_selection_object_kind_t::bookmark,
                           editor_selection_object_kind_t::section, editor_selection_object_kind_t::bpm_event})
        expect_compose_test(std::ranges::any_of(all, [=](auto object) { return object.kind == kind; }));
    expect_compose_test(invoke_compose_registered_command(f, "selection.invert").value.is_invoked());
    expect_compose_test(f.interaction->selected_notes.empty());
    expect_compose_test(f.interaction->selected_objects.size() == all.size() - notes);
    expect_compose_test(invoke_compose_registered_command(f, "selection.select_all").value.is_invoked());
    expect_compose_test(std::ranges::is_permutation(f.interaction->selected_objects, all));
    expect_compose_test(f.history.document.revision == revision && f.history.undo.empty());
    // From the cursor onward: the BGM event at row 240 lies before it.
    f.interaction->selected_objects.clear();
    f.interaction->selected_notes.clear();
    place_cursor_at(*f.interaction, {measure_id_t{1}, 241});
    for (const auto* command : {"selection.select_all_after_cursor", "selection.select_all_objects_after_cursor"}) {
        expect_compose_test(invoke_compose_registered_command(f, command).value.is_invoked());
        expect_compose_test(std::ranges::none_of(f.interaction->selected_objects, [](auto object) {
            return object.kind == editor_selection_object_kind_t::bgm_event && object.id == object_id_t{9003};
        }));
    }
}

TEST_CASE("Solo toggle and target actions affect the next selection in the same frame",
          "[frame-timing][selection-consolidation][semantic-layer]") {
    compose_test_fixture_t f{static_style_song()};
    f.project();
    f.application.project.semantic_layer.semantic_layer_target = layer_id_t{101};
    auto sink = notification_sink_t::muted();
    const compose_action_inputs_t environment{
        {f.history, *f.binding.value, sink}, {f.application, f.interaction}, std::nullopt};
    const std::array actions{
        WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO,
        WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER,
        WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO};
    const std::array expected{std::size_t{4}, std::size_t{4}, std::size_t{8}};
    for (std::size_t index = 0; index < actions.size(); ++index) {
        wormedit::ui::workspace_action_t action{};
        action.compose.kind = actions[index];
        action.stamp.project_id = f.history.document.id;
        action.stamp.chart_id = f.interaction->chart;
        action.stamp.document_revision = f.history.document.revision;
        action.compose.layer.id = layer_id_t{index == 0 ? 101u : 102u};
        const auto status = dispatch_compose_view_action(action, environment, *f.interaction,
            f.interaction->viewport_height_pixels, false);
        expect_compose_test(!status.empty());
        expect_compose_test(invoke_compose_registered_command(f, "selection.select_all").value.is_invoked());
        expect_compose_test(f.interaction->selected_notes.size() == expected[index]);
        expect_compose_test(f.interaction->solo_semantic_layer == (index == 2 ? std::nullopt
            : std::optional{layer_id_t{index == 0 ? 101u : 102u}}));
    }
    expect_compose_test(f.history.document.revision == 1);
    expect_compose_test(f.history.undo.empty());
}

TEST_CASE("Solo selection commands use current target without requiring projection",
          "[frame-timing][selection-consolidation][semantic-layer]") {
    const auto project_between = GENERATE(false, true);
    compose_test_fixture_t f{static_style_song()};
    f.project();
    auto& policy = f.application.project.semantic_layer;
    policy.semantic_layer_solo_active = true;
    const auto revision = f.history.document.revision;
    for (const auto target : {101u, 102u, 999u}) {
        policy.semantic_layer_target = layer_id_t{target};
        if (project_between) f.project();
        expect_compose_test(invoke_compose_registered_command(f, "selection.select_all").value.is_invoked());
        const auto& selected = f.interaction->selected_notes;
        expect_compose_test(selected.size() == (target == 999 ? 8 : 4));
        if (target != 999) {
            const auto& notes = f.history.document.charts.front().measures.front().notes;
            for (const auto id : selected) {
                const auto note = std::ranges::find(notes, id, &note_t::id);
                expect_compose_test(note != notes.end());
                expect_compose_test(note->semantic_layers == std::vector{layer_id_t{target}});
            }
        }
    }
    policy.semantic_layer_target = layer_id_t{101};
    policy.semantic_layer_solo_active = false;
    if (project_between) f.project();
    expect_compose_test(invoke_compose_registered_command(f, "selection.select_all").value.is_invoked());
    expect_compose_test(f.interaction->selected_notes.size() == 8);
    // Switch to a chart without the old target, with no projection to clear it.
    auto next = f.history.document.charts.front();
    next.id = chart_id_t{2};
    next.semantic_layers.clear();
    for (auto& note : next.measures.front().notes) note.semantic_layers.clear();
    f.history.document.charts.push_back(std::move(next));
    f.binding.value->selected_chart = chart_id_t{2};
    f.interaction->chart = chart_id_t{2};
    policy.semantic_layer_solo_active = true;
    if (project_between) f.project();
    expect_compose_test(invoke_compose_registered_command(f, "selection.select_all").value.is_invoked());
    expect_compose_test(f.interaction->selected_notes.size() == 8);
    expect_compose_test(!f.interaction->solo_semantic_layer);
    expect_compose_test(f.history.document.revision == revision);
    expect_compose_test(f.history.undo.empty());
}

TEST_CASE("Registered note selection respects active editable lanes",
          "[frame-timing][selection-consolidation]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{2}, .name = "Hidden", .role = lane_role_t::invisible});
    chart.measures.front().notes.push_back(
        {.id = object_id_t{9001}, .position = {measure_id_t{1}, 18}, .lane = lane_id_t{2}});
    auto later = chart.lane_layouts.front();
    later.id = object_id_t{9002};
    later.position = {measure_id_t{1}, 60};
    later.layout.lanes.front().role = lane_role_t::autoplay;
    chart.lane_layouts.push_back(std::move(later));
    compose_test_fixture_t f{std::move(song)};
    // Imported charts can contain non-editable lane endpoints. Exercise the
    // registered verb without requiring that malformed field to render.
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = editor_interaction_state_t::from(f.history, chart_id_t{1}, lane_id_t{1},
                                         {measure_id_t{1}, 0}, snaps, 4);
    expect_compose_test(static_cast<bool>(setup));
    f.interaction = std::move(setup.state);
    expect_compose_test(invoke_compose_registered_command(f, "selection.select_all").value.is_invoked());
    expect_compose_test(f.interaction->selected_notes == std::vector<object_id_t>{
        object_id_t{3}, object_id_t{4}, object_id_t{5}, object_id_t{6}});
    expect_compose_test(invoke_compose_registered_command(f, "selection.invert").value.is_invoked());
    expect_compose_test(f.interaction->selected_notes.empty());
    expect_compose_test(f.history.undo.empty());
}

TEST_CASE("Compose scene keys invalidate only on document or chart changes", "[unit][sys][commands]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{51};
    document.charts.push_back(compose_test_song().charts.front());
    document.charts.front().id = wormedit::chart_id_t{7};
    auto& chart = document.charts.front();
    compose_scene_cache_t cache;
    expect_compose_test(!cache.is_scene_key_current(document, chart));
    expect_compose_test(cache.ensure_scene_key(document, chart));
    cache.frame.projection_error = "retained";
    expect_compose_test(!cache.ensure_scene_key(document, chart));
    expect_compose_test(cache.frame.projection_error == "retained");
    ++document.revision;
    expect_compose_test(!cache.is_scene_key_current(document, chart));
    expect_compose_test(cache.ensure_scene_key(document, chart));
    expect_compose_test(cache.frame.projection_error.empty());
    auto replacement = document;
    expect_compose_test(!cache.is_scene_key_current(replacement, replacement.charts.front()));
    cache.invalidate_scene_cache();
    expect_compose_test(!cache.is_scene_key_current(document, chart));
    expect_compose_test(cache.is_scene_key_current(document, chart, compose_scene_key_policy_t::transport));
    expect_compose_test(cache.is_scene_key_current(document, chart, compose_scene_key_policy_t::measure_geometry));
    seed_stem_geometry_fixture(cache.stems.stem_index, chart_stamp_t::from(document, chart.id), {});
    expect_compose_test(cache.is_scene_key_current(document, chart, compose_scene_key_policy_t::stem_geometry));
    auto stale_stem_stamp = cache.stems.stem_index.key()->stamp;
    ++stale_stem_stamp.document.revision;
    seed_stem_geometry_fixture(cache.stems.stem_index, stale_stem_stamp,
                               cache.stems.stem_index.key()->bindings);
    expect_compose_test(!cache.is_scene_key_current(document, chart, compose_scene_key_policy_t::stem_geometry));
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

TEST_CASE("stem display owner keeps channel and packed event cache keys separate", "[sys][stem][ownership]") {
    wormedit::sys::compose_scene_cache_t cache;
    cache.stems.stem_waveforms.reserve(8);
    cache.stems.packed_waveforms.reserve(8);
    auto& stem = cache.stems.stem_for(object_id_t{7});
    auto& packed = cache.stems.packed_for(object_id_t{7});
    CHECK(&stem != &packed);
    CHECK(&cache.stems.stem_for(object_id_t{7}) == &stem);
    CHECK(&cache.stems.packed_for(object_id_t{7}) == &packed);
    CHECK(cache.stems.stem_waveforms.size() == 1);
    CHECK(cache.stems.packed_waveforms.size() == 1);
}
#endif
