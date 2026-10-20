#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_note_menu.h"
#include "ui/nuklear/bmson_lane_mode_ui.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <algorithm>
#include <source_location>

namespace {
void require_ui(bool value, std::source_location at = std::source_location::current()) {
    INFO("Lane mode UI requirement at " << at.line()); REQUIRE(value);
}
void check_ui(bool value, std::source_location at = std::source_location::current()) {
    INFO("Lane mode UI check at " << at.line()); CHECK(value);
}
auto click(nuklear_event_harness_t& harness, float x, float y, uint8_t button = SDL_BUTTON_LEFT)
    -> std::array<workspace_action_snapshot_t, 2> {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION; motion.motion.x = x; motion.motion.y = y;
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN; down.button.button = button;
    down.button.down = true; down.button.clicks = 1; down.button.x = x; down.button.y = y;
    auto up = down; up.type = SDL_EVENT_MOUSE_BUTTON_UP; up.button.down = false;
    const std::array press{motion, down};
    const std::array release{motion, up};
    return {harness.frame(press), harness.frame(release)};
}
auto click_label(nuklear_event_harness_t& harness, std::string_view text)
    -> std::array<workspace_action_snapshot_t, 2> {
    const auto center = harness.drawn_text_center(text);
    INFO(harness.drawn_text());
    require_ui(center.has_value());
    return click(harness, center->first, center->second);
}
void check_emitted(const std::array<workspace_action_snapshot_t, 2>& frames,
                   WormUiComposeActionKind kind, uint64_t channel, uint64_t resource = 0) {
    const auto found = std::ranges::find_if(frames,
        [kind](const auto& frame) { return frame.get().compose_action == kind; });
    require_ui(found != frames.end());
    check_ui(found->get().compose_bmson_channel_id.value == channel);
    check_ui(found->get().compose_resource_id.value == resource);
    check_ui(found->get().compose_inputs.size() == 0);
}
void audio_fixture(nuklear_event_harness_t& harness, wormedit::ui::sound_row_view_t& row) {
    auto& workspace = harness.view();
    workspace.active_tab = WORM_UI_WORKSPACE_TAB_AUDIO_SLICES;
    workspace.compose.visible = false;
    auto& audio = workspace.audio_slices;
    audio.visible = true; audio.has_chart = true; audio.chart_id = wormedit::chart_id_t{7}; audio.document_revision = 91;
    audio.usage_song_id = wormedit::song_id_t{5}; audio.selected_resource_id = row.resource_id;
    audio.has_selected_asset = true; audio.selected_asset_id = row.asset_id;
    audio.sound_rows.assign(&row, &row + 1); audio.offset_denominator = 1;
}
}

TEST_CASE("BMSON lane_mode menus convert packed events and stem headers", "[frontend-state][event-harness][bmson][lane_mode]") {
    const bool stem = GENERATE(false, true);
    const bool continues = GENERATE(false, true);
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    (void)harness.frame();
    wormedit::ui::testing_rect_view_t viewport{};
    require_ui(worm_ui_frontend_compose_viewport_for_testing(&viewport));
    view.viewport_width_pixels = viewport.width;
    view.viewport_height_pixels = viewport.height;
    const auto x = viewport.width * .6f;
    WormUiComposeBgmLaneView lane{.bounds = {x, 0, x + 70, viewport.height},
        .source_lane_id = 90, .stem = stem, .width_pixels = 70, .bmson_channel_id = stem ? 71U : 0U, .bmson_channel_continues = stem && continues};
    WormUiComposeBgmEventView event{.bounds = {x, 90, x + 70, 110}, .event_id = 201,
        .source_lane_id = 90, .sound_id = 42, .has_sound = true, .bmson_slice = true};
    view.bgm_lanes = &lane; view.bgm_lane_count = 1;
    view.bgm_events = &event; view.bgm_event_count = 1;
    (void)harness.frame();
    (void)click(harness, viewport.x + x + 30, viewport.y + (stem ? 8 : 100), SDL_BUTTON_RIGHT);
    require_ui(compose_note_menu_open());
    const auto frames = click_label(harness, stem ? "Pack into columns" : "Make stem");
    if (stem && continues) {
        for (const auto& frame : frames) check_ui(frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
        (void)harness.frame();
        check_ui(harness.drawn_text().find("continuation points need a stem lane") != std::string::npos);
        const auto cancelled = click_label(harness, "Cancel");
        for (const auto& frame : cancelled) {
            check_ui(frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
            check_ui(frame.get().compose_inputs.size() == 0);
        }
        return;
    }
    check_emitted(frames, stem ? WORM_UI_COMPOSE_ACTION_PACK_BMSON_CHANNEL : WORM_UI_COMPOSE_ACTION_MAKE_BMSON_STEM,
        stem ? 71 : 0, stem ? 0 : 42);
}

TEST_CASE("BMSON lane_mode Sounds kind change uses the same action", "[frontend-state][event-harness][bmson][lane_mode]") {
    nuklear_event_harness_t harness{1280, 900};
    wormedit::ui::sound_row_view_t row{.resource_id = wormedit::resource_id_t{3}, .asset_id = wormedit::asset_id_t{42}, .name = "kick.wav", .kind = static_cast<wormedit::sound_kind_t>(2),
        .bmson_channel_id = wormedit::object_id_t{71}};
    audio_fixture(harness, row);
    (void)harness.frame();
    check_emitted(click_label(harness, "Kind: Stem (make stem)"), WORM_UI_COMPOSE_ACTION_MAKE_BMSON_STEM, 71);
}

TEST_CASE("BMSON lane_mode restart conversion waits for confirmation and rejects stale state", "[frontend-state][event-harness][bmson][lane_mode]") {
    const bool stale = GENERATE(false, true);
    nuklear_event_harness_t harness{1280, 900};
    wormedit::ui::sound_row_view_t row{.resource_id = wormedit::resource_id_t{3}, .asset_id = wormedit::asset_id_t{42}, .name = "vox.wav", .kind = static_cast<wormedit::sound_kind_t>(1),
        .bmson_channel_id = wormedit::object_id_t{71}, .bmson_channel_continues = true};
    audio_fixture(harness, row);
    (void)harness.frame();
    const auto requested = click_label(harness, "Kind: Keysound (pack into columns)");
    for (const auto& frame : requested) check_ui(frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    (void)harness.frame();
    check_ui(harness.drawn_text().find("continuation points need a stem lane") != std::string::npos);
    if (stale) {
        ++harness.view().audio_slices.document_revision;
        check_ui(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
        (void)harness.frame();
        check_ui(!bmson_lane_mode_confirmation_owns_input(true));
        return;
    }
    const auto frames = click_label(harness, "Convert continue points to restarts and pack");
    check_emitted(frames, WORM_UI_COMPOSE_ACTION_RESTART_AND_PACK_BMSON_CHANNEL, 71);
}
