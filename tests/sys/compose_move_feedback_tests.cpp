// Tool chip projection and note-move conflict feedback.
#include "frame_timing_fixtures.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
auto chip_source_song() -> song_document_t {
    auto song = compose_test_song();
    song.audio_assets = {{.id = asset_id_t{200}, .stream = {44'100, 2, 441'000, true}}};
    for (std::uint64_t index = 0; index < 80; ++index)
        song.resources.push_back({.id = resource_id_t{100 + index},
            .name = "sample " + std::to_string(index), .asset = asset_id_t{200},
            .roles = {resource_role_t::sample}});
    song.resources.back().name = "音楽.wav";
    song.resources.back().target_identities = {{.family = format_family_t::bms, .identifier = "0A", .namespace_name = "wav"}};
    return song;
}

void project_chip_request(compose_test_fixture_t& fixture, wormedit::ui::keysound_dock_request_t request) {
    wormedit::test::require_workspace_result(rebind_workspace(*fixture.binding.value, fixture.history));
    const auto model = workspace_view_t::from(*fixture.binding.value, fixture.history.document);
    t475_require(model.value.has_value());
    fixture.flattened = frontend_workspace_view_t::from(*model.value, fixture.history.document, "", {}, false, dock_sort_t::natural_name,
        request, {}, {}, &fixture.cache.keysound_list_cache);

    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr);
    t475_require(fixture.flattened.view.compose.scene_available);
}

auto dispatch_chip_input(compose_test_fixture_t& fixture, wormedit::ui::compose_input_action_t input) -> std::string {
    wormedit::ui::workspace_action_t action{};
    action.stamp.chart_id.value = 1;
    action.stamp.project_id.value = fixture.history.document.id.value;
    action.stamp.document_revision = fixture.history.document.revision;
    action.input.compose_inputs.resize(1);
    action.input.compose_inputs[0] = input;
    action.input.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    return dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
}
} // namespace

TEST_CASE("Tool chip projects the actual off-page filtered sound with a retained source cache",
          "[sys][compose][source-chip][frame-arena]") {
    compose_test_fixture_t fixture(chip_source_song());
    fixture.application.project.compose_gesture.draw_source_kind = 1;
    fixture.application.project.compose_gesture.draw_source_id = 179;
    wormedit::ui::keysound_dock_request_t request{};
    request.include_ungrouped = true;
    request.primary_resource_id = wormedit::resource_id_t{100};
    request.automatic_search = "not this source";
    project_chip_request(fixture, request);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "0A 音楽.wav");
    t475_check(!fixture.flattened.view.compose.draw_source_missing);
    t475_check(!fixture.flattened.view.compose.keysound_projected_filters_unrestricted);
    const auto builds = fixture.cache.stems.draw_stem_sources.build_count();
    request.automatic_search = {};
    request.primary_resource_id = wormedit::resource_id_t{179};
    request.automatic_row_limit = 8;
    project_chip_request(fixture, request);
    const auto& view = fixture.flattened.view.compose;
    t475_check(view.keysound_projected_primary_resource_id.value == 179);
    t475_check(view.keysound_projected_filters_unrestricted && view.keysound_primary_display_index_valid);
    t475_check(view.keysound_primary_display_index == 80);
    t475_check(view.keysound_display_total_count == 81);
    t475_check(fixture.cache.stems.draw_stem_sources.build_count() == builds);
}

TEST_CASE("Tool chip names are bounded UTF-8 and survive a moved frame and source rename",
          "[sys][compose][source-chip][frame-arena]") {
    compose_test_fixture_t fixture(chip_source_song());
    fixture.application.project.compose_gesture.draw_source_kind = 1;
    fixture.application.project.compose_gesture.draw_source_id = 179;
    t475_project(fixture);
    auto retained = std::move(fixture.flattened);
    auto proposed = fixture.history.document;
    proposed.resources.back().name.clear();
    for (int index = 0; index < 400; ++index) proposed.resources.back().name += "音";
    const auto renamed = commit_document_command(fixture.history, std::move(proposed), "Rename chip source");
    t475_require(renamed);
    t475_project(fixture);
    const std::string_view label = fixture.flattened.view.compose.draw_source_label;
    t475_check(label.size() <= 512 && label.ends_with("…"));
    t475_check(label.substr(3, label.size() - 6).size() % 3 == 0);
    t475_check(std::string_view(retained.view.compose.draw_source_label) == "0A 音楽.wav");
}

TEST_CASE("Deleted Draw source remains picked and warns until an explicit repick",
          "[sys][compose][source-chip][missing]") {
    compose_test_fixture_t fixture(chip_source_song());
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.application.project.compose_gesture.draw_source_kind = 1;
    fixture.application.project.compose_gesture.draw_source_id = 179;
    t475_project(fixture);
    auto proposed = fixture.history.document;
    proposed.resources.pop_back();
    t475_require(commit_document_command(fixture.history, std::move(proposed), "Delete chip source"));
    t475_project(fixture);
    t475_check(fixture.application.project.compose_gesture.draw_source_kind == 1 && fixture.application.project.compose_gesture.draw_source_id == 179);
    t475_check(fixture.flattened.view.compose.draw_source_missing);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "missing source");
    const auto revision = fixture.history.document.revision;
    const auto undos = fixture.history.undo.size();
    const auto attempted = paint_stroke_inputs(fixture, 179);
    const auto rejected = dispatch_chip_input(fixture, attempted.input.compose_inputs[0]);
    t475_check(rejected.find("paint sound is not an existing sample resource") != std::string::npos);
    t475_check(fixture.history.document.revision == revision && fixture.history.undo.size() == undos);
    t475_check(!fixture.interaction->gesture<wormedit::pending_note_paint_gesture_t>());
    const auto repicked = dispatch_chip_input(fixture, {.kind = WORM_UI_COMPOSE_INPUT_DRAW_SOURCE});
    t475_check(repicked == "Draw source selected.");
    t475_project(fixture);
    t475_check(!fixture.flattened.view.compose.draw_source_missing);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "no sound resource");
}

TEST_CASE("Unassigned Draw source describes resource assignment rather than hitsound audibility",
          "[sys][compose][source-chip][hitsound]") {
    compose_test_fixture_t fixture(chip_source_song());
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "no sound resource");
    fixture.interaction->armed_hitsample = osu_hitsample_t{.whistle = true, .clap = true};
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "no sound resource");
}

TEST_CASE("Tool chip BGA uses resource identity and Layer reveals the off-page target ordinal",
          "[sys][compose][source-chip][bga][semantic-layer]") {
    compose_test_fixture_t fixture(bga_virtual_preview_song());
    fixture.application.project.compose_gesture.draw_source_kind = 2;
    fixture.application.project.compose_gesture.draw_source_id = 501;
    for (std::uint64_t index = 0; index < 100; ++index)
        fixture.history.document.charts.front().semantic_layers.push_back({
            .id = layer_id_t{index + 1}, .name = "Drums " + std::to_string(index)});
    fixture.application.project.semantic_layer.semantic_layer_target = layer_id_t{100};
    t475_project(fixture);
    const auto& view = fixture.flattened.view.compose;
    t475_check(std::string_view(view.draw_source_label) == "501 preview image");
    t475_check(!view.draw_source_missing && view.draw_source_id.value != fixture.history.document.resources.front().asset.value);
    t475_check(view.semantic_layer_target_row == 99);
    t475_check(std::string_view(view.semantic_layer_target.name) == "Drums 99");
}

namespace {
auto move_feedback_dispatch_song() -> song_document_t {
    auto song = compose_test_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 360}},
        {.id = object_id_t{201},
         .position = {measure_id_t{1}, 600},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 660}},
        {.id = object_id_t{202}, .position = {measure_id_t{1}, 720}, .lane = lane_id_t{1}}};
    return song;
}

struct move_feedback_dispatch_fixture_t {
    compose_test_fixture_t compose;
    notification_center_t center;
    notification_sink_t sink{center};
    double x{}, pixels_per_row{};
    explicit move_feedback_dispatch_fixture_t(song_document_t song = move_feedback_dispatch_song())
        : compose(std::move(song)) {
        compose.application.project.compose_view.edit_snap_enabled = false;
        compose.project();
        expect_compose_test(static_cast<bool>(
            dispatch_editor_action(compose.history, *compose.interaction,
                                   select_editor_notes_action_t{{object_id_t{101}}})));
        compose.project();
        const auto& view = compose.flattened.view.compose;
        const auto endpoints = std::span(view.note_endpoints);
        const auto source = std::ranges::find_if(
            endpoints, [](const auto& note) { return note.note_id == object_id_t{101} && !note.tail; });
        expect_compose_test(source != endpoints.end());
        x = (source->bounds.left + source->bounds.right) * .5;
        pixels_per_row = view.pixels_per_standard_measure / 960;
        (void)dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 240);
        expect_compose_test(compose.interaction->gesture<wormedit::pending_note_move_gesture_t>() != nullptr);
    }
    auto dispatch(WormUiComposeInputKind kind, double row, bool to_bgm = false) -> std::string {
        wormedit::ui::workspace_action_t action{};
        action.stamp.project_id.value = compose.history.document.id.value;
        action.stamp.chart_id.value = 1;
        action.stamp.document_revision = compose.history.document.revision;
        action.input.compose_inputs.resize(1);
        const auto& view = compose.flattened.view.compose;
        const auto downscroll = compose.cache.frame.preview_scene_request.scroll_direction ==
            editor_field_scroll_direction_t::down;
        action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{1}));
        action.input.compose_inputs[0] = {
            .kind = kind,
            .note_move_to_bgm = to_bgm,
            .x_pixels = to_bgm ? view.playable_field_right_pixels + 10 : x,
            .y_pixels = compose_project_y(row + .000001, pixels_per_row,
                compose.interaction->viewport_scroll_pixels, view.viewport_height_pixels,
                downscroll)};
        action.input.compose_input_viewport_width_pixels =
            compose.flattened.view.compose.viewport_width_pixels;
        viewport_input_controller_t viewport;
        editor_navigation_input_controller_t navigation;
        return dispatch_compose_input_action(action, compose.history, *compose.binding.value,
                                             *compose.interaction, viewport, navigation,
                                             compose.application, compose.cache, 0, &sink);
    }
};

void check_projected_move_conflicts(move_feedback_dispatch_fixture_t& fixture) {
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.move_conflict_count == 1);
    const auto endpoints = std::span(view.note_endpoints);
    expect_compose_test(std::ranges::count_if(endpoints, [](const auto& note) {
                            return note.proposal_ghost && note.move_conflict != note_move_conflict_t::none;
                        }) == 2);
    expect_compose_test(std::ranges::count_if(endpoints, [](const auto& note) {
                            return note.move_conflict_occupant;
                        }) == 3);
    expect_compose_test(std::ranges::any_of(
        std::span(view.hold_bodies),
        [](const auto& hold) { return hold.proposal_ghost && hold.move_conflict != note_move_conflict_t::none; }));
    expect_compose_test(std::ranges::any_of(
        std::span(view.hold_bodies),
        [](const auto& hold) { return hold.note_id == object_id_t{201} && hold.move_conflict_occupant; }));
}

void check_move_release_warning(move_feedback_dispatch_fixture_t& fixture) {
    fixture.center.drain_inbox(notification_time_t{0});
    const auto messages = fixture.center.snapshot(notification_capacity);
    expect_compose_test(messages.size() == 1);
    expect_compose_test(messages.front().severity == notification_severity_t::warning);
    expect_compose_test(messages.front().message == "1 notes would overlap");
    expect_compose_test(!fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
    expect_compose_test(fixture.compose.history.document.revision == 1);
    expect_compose_test(fixture.compose.history.undo.empty());
    expect_compose_test(fixture.compose.history.redo.empty());
}
} // namespace

TEST_CASE("Compose direct drag moves an unselected note without moving the previous selection",
          "[sys][compose][move][direct-drag]") {
    const bool other_selected = GENERATE(false, true);
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE, 240);
    const std::vector<object_id_t> selection = other_selected
        ? std::vector<object_id_t>{object_id_t{201}} : std::vector<object_id_t>{};
    expect_compose_test(dispatch_editor_action(fixture.compose.history, *fixture.compose.interaction,
        select_editor_notes_action_t{selection}).accepted);
    fixture.compose.project();
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 240);
    const auto* pending = fixture.compose.interaction->gesture<pending_note_move_gesture_t>();
    expect_compose_test(pending != nullptr);
    expect_compose_test(pending->notes == std::vector<object_id_t>{object_id_t{101}});
    expect_compose_test(fixture.compose.interaction->selected_notes == selection);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 120);
    expect_compose_test(fixture.compose.history.undo.empty());
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 120);
    const auto* chart = fixture.compose.history.read_chart(chart_id_t{1});
    expect_compose_test(chart->find_note(object_id_t{101})->position == local_position_t{measure_id_t{1}, 120});
    expect_compose_test(chart->find_note(object_id_t{101})->end == local_position_t{measure_id_t{1}, 240});
    expect_compose_test(chart->find_note(object_id_t{201})->position == local_position_t{measure_id_t{1}, 600});
    expect_compose_test(fixture.compose.interaction->selected_notes == std::vector<object_id_t>{object_id_t{101}});
    expect_compose_test(fixture.compose.history.undo.size() == 1);
}

TEST_CASE("Compose rejected note drags ignore trailing motion and release without repeated notifications",
          "[sys][compose][move][direct-drag]") {
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE, 240);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 480);
    expect_compose_test(!fixture.compose.interaction->is_pending());
    for (int motion = 0; motion < 3; ++motion)
        (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 500);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 500);
    fixture.center.drain_inbox(notification_time_t{0});
    const auto messages = fixture.center.snapshot(notification_capacity);
    expect_compose_test(messages.size() == 1);
    expect_compose_test(messages.front().severity == notification_severity_t::warning);
    expect_compose_test(!fixture.compose.interaction->is_pending());
    expect_compose_test(fixture.compose.history.undo.empty());
}

TEST_CASE("Compose move motion projects all conflicts without notifications and release warns once",
          "[sys][compose][move][t420]") {
    move_feedback_dispatch_fixture_t fixture;
    for (int motion = 0; motion < 3; ++motion)
        (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 600);
    fixture.center.drain_inbox(notification_time_t{0});
    expect_compose_test(fixture.center.snapshot(notification_capacity).empty());
    INFO("Actual move target " << fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>()->preview_position.measure.value
         << ":" << fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>()->preview_position.row
         << " snap=" << fixture.compose.interaction->snap_enabled
         << " scroll=" << fixture.compose.interaction->viewport_scroll_pixels);
    expect_compose_test(fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>()->preview_position ==
                        local_position_t{measure_id_t{1}, 600});
    check_projected_move_conflicts(fixture);
    const auto status = fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 600);
    expect_compose_test(status == "1 notes would overlap");
    check_move_release_warning(fixture);
}

TEST_CASE("Compose move conflict recovery commits one undo and clears projected flags",
          "[sys][compose][move][t420]") {
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 600);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 120);
    fixture.compose.project();
    expect_compose_test(fixture.compose.flattened.view.compose.move_conflict_count == 0);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 120);
    expect_compose_test(fixture.compose.history.undo.size() == 1);
    expect_compose_test(fixture.compose.history.document.revision == 2);
    const auto& notes = fixture.compose.history.document.charts.front().measures.front().notes;
    const auto moved = std::ranges::find(notes, object_id_t{101}, [](const auto& note) { return note.id; });
    expect_compose_test(moved != notes.end());
    INFO("Actual recovered note " << moved->position.measure.value << ":" << moved->position.row);
    expect_compose_test(moved->position == local_position_t{measure_id_t{1}, 120});
    expect_compose_test(moved->end == local_position_t{measure_id_t{1}, 240});
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.move_conflict_count == 0);
    expect_compose_test(std::ranges::none_of(
        std::span(view.note_endpoints),
        [](const auto& note) { return note.move_conflict != note_move_conflict_t::none || note.move_conflict_occupant; }));
}

TEST_CASE("Compose move release outside actual chart geometry cancels without editing",
          "[sys][compose][move][t420]") {
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 120);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 100000);
    expect_compose_test(!fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
    expect_compose_test(fixture.compose.history.document.revision == 1);
    expect_compose_test(fixture.compose.history.undo.empty());
    fixture.center.drain_inbox(notification_time_t{0});
    expect_compose_test(fixture.center.snapshot(notification_capacity).empty());
}

TEST_CASE("Compose vertical move through an empty target layout keeps red proposals until release",
          "[sys][compose][move][t420]") {
    auto song = move_feedback_dispatch_song();
    auto& chart = song.charts.front();
    chart.measures.push_back({.id = measure_id_t{2}});
    chart.lane_layouts.push_back({.id = object_id_t{300},
                                  .position = {measure_id_t{2}, 0},
                                  .layout = {.profile_id = "empty-target"}});
    move_feedback_dispatch_fixture_t fixture{std::move(song)};
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 1000);
    expect_compose_test(fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>() != nullptr);
    const auto& pending = *fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>();
    INFO("Actual empty-layout target " << pending.preview_position.measure.value << ":" << pending.preview_position.row);
    expect_compose_test(pending.preview_position == local_position_t{measure_id_t{2}, 40});
    expect_compose_test(pending.preview.front().conflict == note_move_conflict_t::out_of_lane);
    fixture.compose.project();
    expect_compose_test(fixture.compose.flattened.view.compose.move_conflict_count == 1);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 1000);
    check_move_release_warning(fixture);
}

TEST_CASE("Compose no-hit move outside the horizontal field cancels without editing",
          "[sys][compose][move][t420]") {
    move_feedback_dispatch_fixture_t fixture;
    fixture.x = -10000;
    for (int motion = 0; motion < 3; ++motion)
        (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 600);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 600);
    expect_compose_test(!fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
    expect_compose_test(fixture.compose.history.document.revision == 1);
    expect_compose_test(fixture.compose.history.undo.empty());
    fixture.center.drain_inbox(notification_time_t{0});
    expect_compose_test(fixture.center.snapshot(notification_capacity).empty());
}

TEST_CASE("Compose routes a shifted BGM drop and projects transient amber hold ghosts", "[sys][compose][bgm-move]") {
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 480, true);
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.scene_available && view.move_conflict_count == 0);
    expect_compose_test(view.move_to_bgm_count == 1);
    const auto events = std::span(view.bgm_events);
    expect_compose_test(std::ranges::any_of(events, [](const auto& event) {
        return event.proposal_ghost && event.has_tail && !event.move_conflict;
    }));
    expect_compose_test(fixture.compose.history.undo.empty());
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 480, true);
    expect_compose_test(fixture.compose.history.undo.size() == 1);
    expect_compose_test(!fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
    const auto& measures = fixture.compose.history.document.charts.front().measures;
    const auto event = std::ranges::find(measures.front().bgm_events, object_id_t{101}, &bgm_event_t::id);
    expect_compose_test(event != measures.front().bgm_events.end());
    expect_compose_test(event->position == local_position_t{measure_id_t{1}, 480});
    expect_compose_test(event->end == local_position_t{measure_id_t{1}, 600});
}

TEST_CASE("Compose captures the collision preference and previews every displaced occupant", "[sys][compose][bgm-move]") {
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE, 240);
    fixture.compose.application.preferences.editor_preferences.move_collision_policy = move_collision_policy_t::displace_to_bgm;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 240);
    // The option is captured at begin; changing it mid-drag cannot change the transaction.
    fixture.compose.application.preferences.editor_preferences.move_collision_policy = move_collision_policy_t::reject;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 600);
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.scene_available && view.move_conflict_count == 0 && view.move_to_bgm_count == 2);
    const auto events = std::span(view.bgm_events);
    expect_compose_test(std::ranges::count_if(events, [](const auto& event) {
        return event.proposal_ghost && !event.move_conflict;
    }) == 2);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 600);
    expect_compose_test(fixture.compose.history.undo.size() == 1);
    expect_compose_test(!fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
}

TEST_CASE("Compose shows rejected BGM displacement destinations in red and commits nothing", "[sys][compose][bgm-move]") {
    auto song = move_feedback_dispatch_song();
    song.charts.front().source_family = format_family_t::stepmania;
    move_feedback_dispatch_fixture_t fixture{std::move(song)};
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE, 240);
    fixture.compose.application.preferences.editor_preferences.move_collision_policy = move_collision_policy_t::displace_to_bgm;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 240);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 600);
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.scene_available && view.move_conflict_count == 1);
    expect_compose_test(std::ranges::any_of(std::span(view.bgm_events), [](const auto& event) {
        return event.proposal_ghost && event.move_conflict;
    }));
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 600);
    expect_compose_test(fixture.compose.history.undo.empty() && !fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
}

TEST_CASE("Same sound registered command uses hover selection and dock adds without Undo", "[sys][compose][select-same-sound]") {
    compose_test_fixture_t fixture;
    auto& song = fixture.history.document;
    song.audio_assets = {{.id = asset_id_t{1}, .source_path = "kick.wav"},
                         {.id = asset_id_t{2}, .source_path = "snare.wav"}};
    song.resources = {{.id = resource_id_t{1}, .name = "kick", .asset = asset_id_t{1}, .roles = {resource_role_t::sample}},
                      {.id = resource_id_t{2}, .name = "snare", .asset = asset_id_t{2}, .roles = {resource_role_t::sample}}};
    auto& notes = song.charts[0].measures[0].notes;
    notes[0].sound = notes[1].sound = resource_id_t{1};
    notes[2].sound = resource_id_t{2};
    fixture.project();
    auto& application = fixture.application;
    application.project.sound_selection.sound_selection_chart = chart_id_t{1};
    application.project.sound_selection.sound_selection_revision = song.revision;
    application.project.sound_selection.sound_selection_dock = {resource_id_t{2}};
    application.project.sound_selection.sound_selection_hover = notes[0].id;
    notification_center_t center;
    notification_sink_t sink{center};
    std::optional<timing_lane_pseudonote_outcome_t> timing;
    const auto command = application_command_t{.kind = application_command_kind_t::selection_select_same_sound_chart};
    auto invoke = [&] {
        return invoke_application_command(command, fixture.history, *fixture.binding.value,
            fixture.interaction, timing, application, sink);
    };
    const auto revision = song.revision;
    expect_compose_test(invoke().value);
    expect_compose_test(fixture.interaction->selected_notes == std::vector<object_id_t>{notes[0].id, notes[1].id});
    // Chart-wide selection ignores visibility; visible input reads the current
    // session target even when no projection follows it.
    auto& policy = application.project.semantic_layer;
    song.charts[0].semantic_layers = {{.id = layer_id_t{101}, .name = "First"},
                                     {.id = layer_id_t{102}, .name = "Second"}};
    notes[0].semantic_layers = {layer_id_t{101}};
    notes[1].semantic_layers = {layer_id_t{102}};
    policy.semantic_layer_solo_active = true;
    for (const auto target : {101u, 102u}) {
        fixture.interaction->selected_notes.clear();
        fixture.interaction->selected_objects.clear();
        policy.semantic_layer_target = layer_id_t{target};
        expect_compose_test(invoke().value);
        CHECK(fixture.interaction->selected_notes == std::vector<object_id_t>{notes[0].id, notes[1].id});
        const auto status = dispatch_chip_input(fixture,
            {.kind = WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_VISIBLE,
             .object_id = notes[0].id, .selection_mode = WORM_UI_COMPOSE_SELECTION_REPLACE});
        expect_compose_test(!status.empty());
        CHECK(fixture.interaction->selected_notes == std::vector{notes[target - 101].id});
    }
    policy.semantic_layer_solo_active = false;
    expect_compose_test(invoke().value);
    application.project.sound_selection.sound_selection_hover.reset();
    expect_compose_test(invoke().value); // Selection takes precedence over the dock.
    expect_compose_test(fixture.interaction->selected_notes.size() == 2);
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects.clear();
    expect_compose_test(invoke().value);
    expect_compose_test(fixture.interaction->selected_notes == std::vector<object_id_t>{notes[2].id});
    application.project.sound_selection.sound_selection_dock.clear();
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects.clear();
    expect_compose_test(!invoke().value);
    expect_compose_test(song.revision == revision && fixture.history.undo.empty());
}

TEST_CASE("Compose mixed move projects BGM tails and collision ghosts without a conversion count",
          "[sys][compose][mixed-move]") {
    const auto collision = GENERATE(false, true);
    auto song = move_feedback_dispatch_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes = {{.id = object_id_t{400}, .name = "BGM"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{401},
        .position = {measure_id_t{1}, 120}, .source_lane = object_id_t{400},
        .end = local_position_t{measure_id_t{1}, 240}}};
    if (collision) chart.measures.front().bgm_events.push_back({.id = object_id_t{402},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{400}});
    move_feedback_dispatch_fixture_t fixture{std::move(song)};
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE, 240);
    fixture.compose.interaction->selected_objects.push_back(
        {editor_selection_object_kind_t::bgm_event, object_id_t{401}});
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 240);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 360);
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.scene_available && view.move_to_bgm_count == 0);
    expect_compose_test((view.move_conflict_count != 0) == collision);
    const auto events = std::span(view.bgm_events);
    expect_compose_test(std::ranges::any_of(events, [collision](const auto& event) {
        return event.event_id == object_id_t{401} && event.proposal_ghost && event.has_tail && event.move_conflict == collision;
    }));
    expect_compose_test(fixture.compose.history.undo.empty());
}

TEST_CASE("Mixed quantize projects BGM hold ghosts and refreshes a BGM-only selection",
          "[sys][frame-timing][mixed-quantize]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes.resize(1);
    chart.measures.front().notes.front().position.row = 130;
    const auto id = chart.measures.front().notes.front().id;
    for (std::uint64_t i = 0; i < 50; ++i)
        chart.bgm_source_lanes.push_back({.id = object_id_t{100000 + i}, .source_order = i});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{90002}, .position = {measure_id_t{1}, 130},
        .source_lane = object_id_t{100000}, .end = local_position_t{measure_id_t{1}, 610}});
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    fixture.interaction->selected_notes = {id};
    fixture.interaction->selected_objects = {{editor_selection_object_kind_t::note, id},
        {editor_selection_object_kind_t::bgm_event, object_id_t{90002}}};
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::earlier,
                                    quantization_hold_t::both_endpoints);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.quantize_preview_moved == 2);
    expect_compose_test(fixture.flattened.view.compose.quantize_ghosts.size() == 2);
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects.erase(fixture.interaction->selected_objects.begin());
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.selected_bgm_event_count == 1 && view.quantize_preview_moved == 1);
    expect_compose_test(view.quantize_preview_rows.size() == 1 && view.quantize_preview_rows[0].bgm);
    expect_compose_test(view.quantize_ghosts.size() == 1 && view.quantize_ghosts[0].has_tail);
    expect_compose_test(view.quantize_ghosts[0].before_head.left >= view.playable_field_right_pixels);
    const auto chart_id = fixture.history.document.charts.front().id.value;
    fixture.application.project.compose_view.bgm_horizontal_scroll_pixels[chart_id] = 12.0;
    fixture.project();
    const auto& clipped = fixture.flattened.view.compose;
    expect_compose_test(clipped.quantize_ghosts.size() == 1 &&
        clipped.quantize_ghosts[0].before_head.left == clipped.playable_field_right_pixels);
    expect_compose_test(clipped.quantize_ghosts[0].before_head.right - clipped.quantize_ghosts[0].before_head.left == 24.0);
    fixture.application.project.compose_view.bgm_horizontal_scroll_pixels[chart_id] = 36.0;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.quantize_ghosts.size() == 0);
    fixture.application.project.compose_view.bgm_horizontal_scroll_pixels[chart_id] = 0.0;
    fixture.project();
    expect_compose_test(fixture.history.undo.empty());
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::earlier,
        quantization_hold_t::both_endpoints, 4, quantization_space_t::standard_measure, true);
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(fixture.application.project.quantize.quantize_applied_report.has_value());
    expect_compose_test(fixture.application.project.quantize.quantize_applied_report->rows.size() == 1 &&
                        fixture.application.project.quantize.quantize_applied_report->rows.front().bgm);
    expect_compose_test(fixture.history.document.charts.front().measures.front().notes.front().position.row == 130);
    expect_compose_test(fixture.history.document.charts.front().measures.front().bgm_events.front().position.row == 240);
}
