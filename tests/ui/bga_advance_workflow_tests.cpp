#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace {
void require_harness_ready(bool value) { REQUIRE(value); }
void require_bga_list_valid(const worm_ui_nuklear_compose_state_t& state) {
    REQUIRE(state.bga_catalog.bga_list_valid);
}
void require_bga_row_armed(const worm_ui_nuklear_compose_state_t& state) {
    REQUIRE(state.resource_picker.armed_resource_id == 1400);
    REQUIRE(state.bga_catalog.armed_from_bga_dock);
}
void require_bga_draw_source(const worm_ui_nuklear_compose_state_t& state) {
    REQUIRE(state.resource_picker.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
    REQUIRE(state.pointer_capture.draw_source_kind == 2);
    REQUIRE(state.pointer_capture.draw_source_id == 1400);
}
void require_bga_auto_advance(const worm_ui_nuklear_compose_state_t& state) {
    REQUIRE(state.bga_catalog.bga_auto_increment_on_place);
}
void check_resource_place(const wormedit::ui::compose_input_action_t* input) {
    REQUIRE(input != nullptr);
    CHECK(input->resource_id.value == 1400);
    CHECK(input->paint_intent == 1);
    CHECK(input->resource_role == wormedit::resource_role_t::bga);
}
void check_no_advance_before_dispatch(const worm_ui_nuklear_compose_state_t& state) {
    CHECK(state.resource_picker.armed_resource_id == 1400);
    CHECK(state.resource_picker.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
}
void check_successor_armed(const worm_ui_nuklear_compose_state_t& state) {
    CHECK(state.resource_picker.armed_resource_id == 1700);
    CHECK(state.bga_catalog.bga_selection.primary == 1700);
    CHECK(state.bga_catalog.bga_reveal_primary);
    CHECK(state.resource_picker.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
    CHECK(state.bga_catalog.bga_auto_increment_on_place);
}
void check_order_exhausted(const worm_ui_nuklear_compose_state_t& state) {
    CHECK(state.resource_picker.armed_resource_id == 1700);
    CHECK(!state.bga_catalog.bga_auto_increment_on_place);
    CHECK(state.resource_picker.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::off);
}
void check_sound_toggle_preserved(bool current, bool initial) { CHECK(current == initial); }
void check_bga_request(const WormUiBgaDockRequest& request) {
    CHECK(request.visible);
    CHECK(request.sort_by_id);
    CHECK(std::strcmp(request.search, "a") == 0);
}
auto find_resource_place(const std::vector<workspace_action_snapshot_t>& frames)
    -> const wormedit::ui::compose_input_action_t* {
    for (const auto& frame : frames)
        for (std::size_t index = 0; index < frame.get().compose_inputs.size(); ++index) {
            const auto& input = frame.get().compose_inputs[index];
            if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE) return &input;
        }
    return nullptr;
}
auto motion(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}
auto button(bool down, float x, float y, bool twice = false) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = twice ? 2 : 1;
    event.button.x = x;
    event.button.y = y;
    return event;
}
auto click(nuklear_event_harness_t& harness, float x, float y, bool twice = false)
    -> std::vector<workspace_action_snapshot_t> {
    const std::array down{motion(x, y), button(true, x, y, twice)};
    const std::array up{motion(x, y), button(false, x, y, twice)};
    const auto pressed = harness.frame(down);
    const auto released = harness.frame(up);
    return {pressed, released};
}
auto canvas_point(nuklear_event_harness_t& harness, double x, double y)
    -> std::pair<float, float> {
    wormedit::ui::testing_rect_view_t viewport{};
    REQUIRE(worm_ui_frontend_compose_viewport_for_testing(&viewport));
    const auto& compose = harness.view().compose;
    return {viewport.x + static_cast<float>(x / compose.viewport_width_pixels) * viewport.width,
            viewport.y + static_cast<float>(y / compose.viewport_height_pixels) * viewport.height};
}
}

TEST_CASE("BGA dock auto advance follows eligible filtered order after placement notification",
          "[frontend-state][event-harness][bga][advance]") {
    nuklear_event_harness_t harness{1600, 900};
    require_harness_ready(harness.ready());
    auto& view = harness.view().compose;
    const std::array<std::uint64_t, 3> matching{1700, 1450, 1400};
    const std::array<std::uint64_t, 2> eligible{1700, 1400};
    const std::array<WormUiComposeBgaRowView, 2> rows{{
        {.resource_id = 1700, .slot = "02", .name = "alpha.png"},
        {.resource_id = 1450, .slot = "03", .name = "missing.png", .missing = true}}};
    const std::array<WormUiComposeResourceView, 3> resources{{
        {.resource_id = 1400, .asset_id = 14000, .roles = WORM_UI_COMPOSE_RESOURCE_ROLE_BGA,
         .image_asset = true, .target_index = "01", .name = "zeta.png", .source_name = "zeta.png"},
        {.resource_id = 1450, .asset_id = 14500, .roles = WORM_UI_COMPOSE_RESOURCE_ROLE_BGA,
         .image_asset = true, .missing = true, .target_index = "03", .name = "missing.png",
         .source_name = "missing.png"},
        {.resource_id = 1700, .asset_id = 17000, .roles = WORM_UI_COMPOSE_RESOURCE_ROLE_BGA,
         .image_asset = true, .target_index = "02", .name = "alpha.png", .source_name = "alpha.png"}}};
    view.bga_rows = rows.data();
    view.bga_row_count = rows.size();
    view.bga_total_matching = matching.size();
    view.bga_matching_ids = matching.data();
    view.bga_advance_ids = eligible.data();
    view.bga_advance_count = eligible.size();
    view.resources = resources.data();
    view.resource_count = resources.size();
    view.resource_total_count = resources.size();
    view.bga_ids_available = true;
    view.bga_search = "";
    (void)harness.frame();

    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.bga_panel_visible = true;
    (void)harness.frame();
    // Match chart-model dimensions to the actual first-frame editor viewport.
    // Keep the lane in the visible editor area, left of the floating dock.
    wormedit::ui::testing_rect_view_t viewport{};
    REQUIRE(worm_ui_frontend_compose_viewport_for_testing(&viewport));
    const auto lane_left = std::min(430.0, static_cast<double>(viewport.width) - 64.0);
    REQUIRE(lane_left > 300.0);
    view.viewport_width_pixels = viewport.width;
    view.viewport_height_pixels = viewport.height;
    view.playable_field_left_pixels = 50;
    view.playable_field_right_pixels = lane_left - 20;
    view.bga_lane_visible = true;
    view.bga_lane_bounds = {lane_left, 0, lane_left + 48, viewport.height};
    (void)harness.frame();
    require_bga_list_valid(state);

    // Sort by BMP ID through the real toolbar, then enter a search in the
    // actual field. The projection consumer receives the resulting order.
    click(harness, state.bga_catalog.bga_list_x + 207,
          state.bga_catalog.bga_list_y + 43);
    click(harness, state.bga_catalog.bga_list_x + 90, state.bga_catalog.bga_list_y + 155);
    SDL_Event search_text{};
    search_text.type = SDL_EVENT_TEXT_INPUT;
    search_text.text.type = SDL_EVENT_TEXT_INPUT;
    search_text.text.text = "a";
    static_cast<void>(harness.frame(std::span{&search_text, 1}));
    WormUiBgaDockRequest request{};
    worm_ui_frontend_bga_dock_request(&request);
    check_bga_request(request);

    const std::array<std::uint64_t, 2> filtered_matching{1400, 1700};
    const std::array<WormUiComposeBgaRowView, 2> filtered_rows{{
        {.resource_id = 1400, .slot = "01", .name = "zeta.png"},
        {.resource_id = 1700, .slot = "02", .name = "alpha.png"}}};
    const std::array<std::uint64_t, 2> filtered_eligible{1400, 1700};
    view.bga_search = request.search;
    view.bga_matching_ids = filtered_matching.data();
    view.bga_total_matching = filtered_matching.size();
    view.bga_rows = filtered_rows.data();
    view.bga_row_count = filtered_rows.size();
    view.bga_advance_ids = filtered_eligible.data();
    view.bga_advance_count = filtered_eligible.size();
    (void)harness.frame();

    // Double-clicking a row selects the Draw source; Draw stays an independent toolbar tool.
    const auto row_center = harness.drawn_text_center("zeta.png");
    REQUIRE(row_center);
    click(harness, row_center->first, row_center->second, true);
    require_bga_row_armed(state);
    require_bga_draw_source(state);
    view.note_paint_mode_active = true;
    (void)harness.frame();
    const auto sounds_auto_advance = state.resource_picker.auto_increment_on_place;
    click(harness, state.bga_catalog.bga_list_x + 22, state.bga_catalog.bga_list_y + 43);
    require_bga_auto_advance(state);
    check_sound_toggle_preserved(state.resource_picker.auto_increment_on_place, sounds_auto_advance);

    // Placement comes from the BGA lane. Until the application accepts it and
    // sends its notification, the dock stays armed.
    const auto target = canvas_point(harness, lane_left + 24, 300);
    const auto chart_click = click(harness, target.first, target.second);
    const auto* place = find_resource_place(chart_click);
    check_resource_place(place);
    check_no_advance_before_dispatch(state);

    // The dispatcher calls this only after an application placement succeeds.
    worm_ui_frontend_resource_placed(1400, 0);
    check_successor_armed(state);

    // End-of-order notifications retain the last valid resource and stop mode.
    worm_ui_frontend_resource_placed(1700, 0);
    check_order_exhausted(state);
}
