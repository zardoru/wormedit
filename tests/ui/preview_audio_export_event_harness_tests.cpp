#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstring>

namespace {
void check_preview_request(const wormedit::ui::preview_audio_export_action_t& request) {
    CHECK(request.filename == "short-preview.ogg");
    CHECK(request.duration_seconds == 5);
    CHECK(request.fade_out_ms == 750);
    CHECK(request.replace_existing);
    CHECK(request.begin_row == 0);
    CHECK(request.end_row == 9600);
}
void open_preview(nuklear_event_harness_t& harness) {
    REQUIRE(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.inspector_visible = state.dock_layout.sounds_panel_visible = state.dock_layout.layers_panel_visible = false;
    auto& view = harness.view().compose;
    view.area_range_available = true;
    view.area_rows_per_measure = 960;
    view.area_canvas_rows = 9600;
    view.area_range_begin_row = 0;
    view.area_range_end_row = 9600;
    view.preview_audio_range = wormedit::ui::preview_audio_range_view_t::from(0, 20);
    (void)harness.frame();
    const auto motion = wormedit::test::mouse_motion(900, 400);
    auto button = wormedit::test::mouse_button(true, 900, 400);
    button.button.button = SDL_BUTTON_RIGHT;
    (void)harness.frame(std::array{motion, button});
    button.type = SDL_EVENT_MOUSE_BUTTON_UP; button.button.down = false;
    (void)harness.frame(std::span{&button, 1});
    (void)harness.frame();
    const auto entry = harness.drawn_text_center("Export preview audio…");
    REQUIRE(entry);
    (void)wormedit::test::click_at(harness, entry->first, entry->second);
    (void)harness.frame();
    REQUIRE(state.preview_audio.open);
}
}

TEST_CASE("Area preview export dialog captures exact bounds duration fade filename and replacement", "[frontend-state][event-harness][preview-export]") {
    nuklear_event_harness_t harness{1280, 900};
    open_preview(harness);
    auto& state = worm_ui_nuklear_compose_state().preview_audio;
    std::strcpy(state.filename.data(), "short-preview.ogg");
    state.duration = 5;
    state.fade_out_ms = 750;
    state.replace_existing = true;
    (void)harness.frame();
    const auto button = harness.drawn_text_center("Export");
    REQUIRE(button);
    const auto actions = wormedit::test::click_at(harness, button->first, button->second);
    const auto found = std::ranges::find_if(actions, [](const auto& a) { return a.get().preview_audio_export.has_value(); });
    REQUIRE(found != actions.end());
    check_preview_request(*found->get().preview_audio_export);
    CHECK_FALSE(state.open);
}

TEST_CASE("Area preview export dialog discards stale or cancelled requests", "[frontend-state][event-harness][preview-export]") {
    nuklear_event_harness_t harness{1280, 900};
    open_preview(harness);
    SECTION("revision") { ++harness.view().compose.document_revision; }
    SECTION("Escape") {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_ESCAPE; event.key.scancode = SDL_SCANCODE_ESCAPE; event.key.down = true;
        CHECK_FALSE(harness.frame(std::span{&event, 1}).get().preview_audio_export);
    }
    CHECK_FALSE(harness.frame().get().preview_audio_export);
    CHECK_FALSE(worm_ui_nuklear_compose_state().preview_audio.open);
}

TEST_CASE("Preview dialog occlusion keeps Compose cleanup stamped to the visible chart", "[frontend-state][event-harness][preview-export]") {
    nuklear_event_harness_t harness{1280, 900};
    open_preview(harness);
    const auto frame = harness.frame();
    const auto& view = harness.view().compose;
    CHECK(frame.get().compose_input_project_id.value == view.project_id.value);
    CHECK(frame.get().compose_input_chart_id.value == view.chart_id.value);
    CHECK(frame.get().compose_input_document_revision == view.document_revision);
    CHECK(frame.get().compose_input_viewport_width_pixels == view.viewport_width_pixels);
}
