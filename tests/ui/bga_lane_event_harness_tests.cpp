#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/lane_profile_switch.h>

#include <cmath>
#include <cstdio>

#include <algorithm>
#include <array>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <span>

namespace {
void check_bga(bool condition, const std::source_location location =
                                    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}

auto mouse_button(bool down, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    return event;
}

auto mouse_motion(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

auto mouse_wheel(float amount, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = static_cast<float>(amount);
    event.wheel.mouse_x = x;
    event.wheel.mouse_y = y;
    return event;
}

auto escape_key() -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_ESCAPE;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    event.key.down = true;
    return event;
}

void hide_compose_docks() {
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.inspector_visible = false;
    state.dock_layout.sounds_panel_visible = false;
    state.dock_layout.layers_panel_visible = false;
    state.dock_layout.bga_panel_visible = false;
}

auto click(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    const auto down = std::array{mouse_motion(x, y), mouse_button(true, x, y)};
    const auto up = std::array{mouse_motion(x, y), mouse_button(false, x, y)};
    return {harness.frame(down), harness.frame(up)};
}

auto scroll_to_profile_and_select(nuklear_event_harness_t& harness,
                                  std::string_view profile_name, float x, float y)
    -> std::optional<std::array<workspace_action_snapshot_t, 2>> {
    auto row = harness.drawn_text_center(profile_name);
    for (int step = 0; step < 12 && !row; ++step) {
        const auto scroll = std::array{mouse_motion(x, y), mouse_wheel(-4, x, y)};
        (void)harness.frame(scroll);
        row = harness.drawn_text_center(profile_name);
    }
    if (!row) return std::nullopt;
    const auto settle_scroll = std::array{mouse_motion(x, y), mouse_wheel(-1, x, y)};
    (void)harness.frame(settle_scroll);
    row = harness.drawn_text_center(profile_name);
    if (!row) return std::nullopt;
    return click(harness, row->first, row->second);
}

auto has_input(const workspace_action_snapshot_t& action, WormUiComposeInputKind kind,
               std::uint64_t object_id) -> bool {
    const auto& value = action.get();
    return std::ranges::any_of(std::span(value.compose_inputs, value.compose_input_count),
        [&](const auto& input) { return input.kind == kind && ((kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE ||
            kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART) ? input.lane_id : input.object_id) == object_id; });
}

auto has_input(const std::array<workspace_action_snapshot_t, 2>& actions,
               WormUiComposeInputKind kind, std::uint64_t object_id) -> bool {
    return std::ranges::any_of(actions, [&](const auto& action) {
        return has_input(action, kind, object_id);
    });
}

auto has_resource_place(const std::array<workspace_action_snapshot_t, 2>& actions) -> bool {
    return std::ranges::any_of(actions, [](const auto& action) {
        const auto& value = action.get();
        return std::ranges::any_of(std::span(value.compose_inputs, value.compose_input_count),
            [](const auto& input) {
                return input.kind == WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE;
            });
    });
}

auto canvas_point(nuklear_event_harness_t& harness, double x, double y)
    -> std::pair<float, float> {
    WormUiTestingRect viewport{};
    REQUIRE(worm_ui_frontend_compose_viewport_for_testing(&viewport));
    const auto& compose = harness.view().compose;
    return {viewport.x + static_cast<float>(x / compose.viewport_width_pixels) * viewport.width,
            viewport.y + static_cast<float>(y / compose.viewport_height_pixels) * viewport.height};
}

auto drawn_text_center_with_prefix(const nuklear_event_harness_t& harness,
                                   std::string_view prefix)
    -> std::optional<std::pair<float, float>> {
    const auto& text = harness.drawn_text();
    std::size_t begin{};
    while (begin < text.size()) {
        const auto newline = text.find('\n', begin);
        const auto end = newline == std::string::npos ? text.size() : newline;
        const auto line = std::string_view{text}.substr(begin, end - begin);
        if (line.starts_with(prefix)) return harness.drawn_text_center(line);
        begin = end + 1;
    }
    return std::nullopt;
}

auto rect_center(nuklear_event_harness_t& harness, const WormUiComposeRect& bounds)
    -> std::pair<float, float> {
    return canvas_point(harness, (bounds.left + bounds.right) * 0.5,
                        (bounds.top + bounds.bottom) * 0.5);
}

void open_dense_stack_picker(nuklear_event_harness_t& harness,
                             const WormUiComposeBgaEventView& event) {
    const auto marker = rect_center(harness, event.bounds);
    (void)click(harness, marker.first, marker.second);
    (void)harness.frame();
    REQUIRE(harness.drawn_text_center("Choose the event to select or edit").has_value());
}

void record_visible_stacked_events(nuklear_event_harness_t& harness,
                                   const std::array<WormUiComposeBgaEventView, 50>& events,
                                   std::array<bool, 50>& reachable) {
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto prefix = std::string{"#"} + std::to_string(events[index].event_id) +
                            " · BACKGROUND";
        if (drawn_text_center_with_prefix(harness, prefix).has_value()) reachable[index] = true;
    }
}

auto all_stacked_events_reachable(const std::array<bool, 50>& reachable) -> bool {
    return std::ranges::all_of(reachable, [](bool value) { return value; });
}

auto scroll_picker_through_stacked_events(nuklear_event_harness_t& harness,
                                          float x, float y,
                                          const std::array<WormUiComposeBgaEventView, 50>& events)
    -> bool {
    std::array<bool, 50> reachable{};
    record_visible_stacked_events(harness, events, reachable);
    for (int step = 0; step < 15 && !all_stacked_events_reachable(reachable); ++step) {
        const std::array scroll{mouse_wheel(-5, x, y)};
        (void)harness.frame(scroll);
        record_visible_stacked_events(harness, events, reachable);
    }
    return all_stacked_events_reachable(reachable);
}

void choose_last_stacked_event(nuklear_event_harness_t& harness,
                               std::array<WormUiComposeBgaEventView, 50>& events) {
    const auto first_row = drawn_text_center_with_prefix(harness, "#100 · BACKGROUND");
    REQUIRE(first_row.has_value());
    const auto motion = mouse_motion(first_row->first, first_row->second);
    (void)harness.frame(std::span{&motion, 1});
    CHECK(scroll_picker_through_stacked_events(harness, first_row->first, first_row->second,
                                               events));
    const auto last = drawn_text_center_with_prefix(harness, "#149 · BACKGROUND");
    REQUIRE(last.has_value());
    const auto chosen = click(harness, last->first, last->second);
    CHECK(has_input(chosen, WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK, 149));
    CHECK_FALSE(worm_ui_nuklear_compose_state().bga_interaction.bga_stack_open);
    CHECK_FALSE(harness.drawn_text_center("Choose the event to select or edit").has_value());
    events.back().selected = true;
}

void drag_last_stacked_event(nuklear_event_harness_t& harness,
                             const WormUiComposeBgaEventView& event) {
    (void)harness.frame();
    const auto center = rect_center(harness, event.bounds);
    const auto down = std::array{mouse_motion(center.first, center.second),
                                 mouse_button(true, center.first, center.second)};
    (void)harness.frame(down);
    const auto drag = mouse_motion(center.first, center.second + 30.0f);
    (void)harness.frame(std::span{&drag, 1});
    const auto up = std::array{mouse_motion(center.first, center.second + 30.0f),
                               mouse_button(false, center.first, center.second + 30.0f)};
    CHECK(has_input(harness.frame(up), WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE, 149));
}

void cancel_bga_move_after_drag(nuklear_event_harness_t& harness, float x, float y,
                                std::uint64_t event_id) {
    const auto down = std::array{mouse_motion(x, y), mouse_button(true, x, y)};
    (void)harness.frame(down);
    const auto drag = mouse_motion(x, y + 24.0f);
    (void)harness.frame(std::span{&drag, 1});
    CHECK(worm_ui_nuklear_compose_state().bga_interaction.bga_event_move_active);
    const auto escape = escape_key();
    (void)harness.frame(std::span{&escape, 1});
    const auto release = std::array{mouse_motion(x, y + 24.0f),
                                    mouse_button(false, x, y + 24.0f)};
    CHECK_FALSE(has_input(harness.frame(release), WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE,
                          event_id));
}

void prepare_bga_stack_dismissal_picker(
    nuklear_event_harness_t& harness,
    std::array<WormUiComposeBgaEventView, 3>& events) {
    hide_compose_docks();
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.project_id = 3;
    view.chart_id = 7;
    view.document_revision = 11;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 90;
    view.playable_field_right_pixels = 620;
    view.bga_lane_visible = true;
    view.bga_lane_bounds = {42, 0, 90, 600};
    for (std::size_t index = 0; index < events.size(); ++index) {
        events[index] = {.event_id = 100 + index, .resource_id = 200 + index,
            .canvas_row = 480, .overlap_count = events.size(), .kind = 0,
            .name = "background", .bmp_id = "01", .bounds = {42, 0, 90, 600}};
    }
    view.bga_events = events.data();
    view.bga_event_count = events.size();
    (void)harness.frame();
    open_dense_stack_picker(harness, events.front());
}

void verify_bga_stack_outside_dismissal(nuklear_event_harness_t& harness) {
    const auto outside = canvas_point(harness, 500, 580);
    const auto down = std::array{mouse_motion(outside.first, outside.second),
                                 mouse_button(true, outside.first, outside.second)};
    const auto pressed = harness.frame(down);
    check_bga(!worm_ui_nuklear_compose_state().bga_interaction.bga_stack_open);
    check_bga(worm_ui_nuklear_compose_state().bga_interaction.bga_stack_release_pending);
    check_bga(pressed.get().compose_input_count == 0);
    check_bga(!harness.drawn_text_center("Choose the event to select or edit").has_value());

    const auto up = std::array{mouse_motion(outside.first, outside.second),
                               mouse_button(false, outside.first, outside.second)};
    const auto released = harness.frame(up);
    check_bga(released.get().compose_input_count == 0);
    check_bga(worm_ui_nuklear_compose_state().bga_interaction.bga_stack_release_pending);
    const auto quiet = harness.frame();
    check_bga(quiet.get().compose_input_count == 0);
    check_bga(!worm_ui_nuklear_compose_state().bga_interaction.bga_stack_release_pending);
}

void verify_bga_stack_escape_dismissal(nuklear_event_harness_t& harness) {
    const auto escape = escape_key();
    const auto dismissed = harness.frame(std::span{&escape, 1});
    check_bga(!worm_ui_nuklear_compose_state().bga_interaction.bga_stack_open);
    check_bga(worm_ui_nuklear_compose_state().bga_interaction.bga_stack_release_pending);
    check_bga(dismissed.get().compose_input_count == 0);
    check_bga(!harness.drawn_text_center("Choose the event to select or edit").has_value());

    auto key_up = escape;
    key_up.type = SDL_EVENT_KEY_UP;
    key_up.key.type = SDL_EVENT_KEY_UP;
    key_up.key.down = false;
    const auto released = harness.frame(std::span{&key_up, 1});
    check_bga(released.get().compose_input_count == 0);
    check_bga(worm_ui_nuklear_compose_state().bga_interaction.bga_stack_release_pending);
    const auto quiet = harness.frame();
    check_bga(quiet.get().compose_input_count == 0);
    check_bga(!worm_ui_nuklear_compose_state().bga_interaction.bga_stack_release_pending);
}

using lane_profile_choices_t = decltype(wormedit::lane_profile_choices());

auto prepare_initial_profile_picker(nuklear_event_harness_t& harness)
    -> lane_profile_choices_t {
    auto& view = harness.view().compose;
    view.chart_id = 7;
    view.document_revision = 11;
    auto profiles = wormedit::lane_profile_choices();
    check_bga(profiles.size() <= WORM_UI_COMPOSE_LANE_PROFILE_CAPACITY);
    view.lane_profile_choice_count = static_cast<std::uint8_t>(profiles.size());
    view.lane_profile_current = -1;
    for (std::size_t index = 0; index < profiles.size(); ++index)
        std::snprintf(view.lane_profile_names[index], sizeof(view.lane_profile_names[index]),
                      "%s", profiles[index].layout.display_name.c_str());
    worm_ui_nuklear_compose_state().dock_layout.inspector_visible = true;
    return profiles;
}

auto open_initial_profile_picker(nuklear_event_harness_t& harness)
    -> std::optional<std::pair<float, float>> {
    const auto initial = harness.frame();
    check_bga(initial.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    const auto section = harness.drawn_text_center("Quantize / Snap");
    REQUIRE(section);
    (void)click(harness, section->first, section->second);
    (void)harness.frame();
    const auto placeholder = harness.drawn_text_center("Choose a lane profile...");
    if (!placeholder) return std::nullopt;
    (void)click(harness, placeholder->first, placeholder->second);
    return placeholder;
}

void check_profile_action(const std::array<workspace_action_snapshot_t, 2>& selected,
                          std::uint32_t target_index) {
    const auto& pressed = selected[0].get();
    const auto& released = selected[1].get();
    check_bga(pressed.compose_action == WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE);
    check_bga(pressed.compose_lane_profile_index == target_index);
    check_bga(pressed.compose_input_chart_id == 7);
    check_bga(pressed.compose_input_document_revision == 11);
    check_bga(released.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}
}

TEST_CASE("BGM-only lane profile picker exposes every built-in profile to the frontend",
          "[frontend-state][event-harness][lane-profile]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    const auto profiles = prepare_initial_profile_picker(harness);
    const auto placeholder = open_initial_profile_picker(harness);
    REQUIRE(placeholder);
    const auto target = std::ranges::find(profiles, "bms-10k-scratchless", [](const auto& profile) {
        return std::string_view{profile.layout.profile_id};
    });
    REQUIRE(target != profiles.end());
    const auto target_index = static_cast<std::uint32_t>(target - profiles.begin());
    const auto selected = scroll_to_profile_and_select(
        harness, target->layout.display_name, placeholder->first,
        placeholder->second + 36.0f);
    REQUIRE(selected);
    check_profile_action(*selected, target_index);
}


TEST_CASE("BGA lane click selects an event and drag emits one move on release",
          "[frontend-state][event-harness][bga-lane]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    hide_compose_docks();
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.chart_id = 7;
    view.document_revision = 11;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 90;
    view.playable_field_right_pixels = 620;
    view.bga_lane_visible = true;
    view.bga_lane_bounds = {42, 0, 90, 600};
    WormUiComposeBgaEventView event{.event_id = 81,
                                    .resource_id = 91,
                                    .kind = 0,
                                    .name = "background",
                                    .bmp_id = "01",
                                    .bounds = {42, 0, 90, 600}};
    view.bga_events = &event;
    view.bga_event_count = 1;
    (void)harness.frame();
    const auto center = rect_center(harness, event.bounds);
    const auto selected = click(harness, center.first, center.second);
    CHECK(has_input(selected.back(), WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK, 81));

    event.selected = true;
    (void)harness.frame();
    const auto updated_center = rect_center(harness, event.bounds);
    const auto down = std::array{mouse_motion(updated_center.first, updated_center.second),
                                 mouse_button(true, updated_center.first, updated_center.second)};
    (void)harness.frame(down);
    const auto moved_y = updated_center.second + 24.0f;
    const auto drag = mouse_motion(updated_center.first, moved_y);
    (void)harness.frame(std::span{&drag, 1});
    const auto up = std::array{mouse_motion(updated_center.first, moved_y),
                               mouse_button(false, updated_center.first, moved_y)};
    const auto released = harness.frame(up);
    CHECK(has_input(released, WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE, 81));
    cancel_bga_move_after_drag(harness, updated_center.first, updated_center.second, 81);
}

TEST_CASE("horizontal BGA drag emits the Layer 2 destination kind once", "[event-harness][bga-lane][bga]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    hide_compose_docks();
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.chart_id = 7;
    view.document_revision = 11;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 186;
    view.playable_field_right_pixels = 620;
    view.bga_lane_visible = true;
    view.bga_lane_bounds = {42, 0, 186, 600};
    view.bga_lane_count = 3;
    view.bga_lane_kinds[0] = 0;
    view.bga_lane_kinds[1] = 2;
    view.bga_lane_kinds[2] = 5;
    view.bga_lane_rects[0] = {42, 0, 90, 600};
    view.bga_lane_rects[1] = {90, 0, 138, 600};
    view.bga_lane_rects[2] = {138, 0, 186, 600};
    WormUiComposeBgaEventView event{.event_id = 81, .resource_id = 91, .kind = 2,
        .name = "Layer", .selected = true, .bounds = {90, 100, 138, 120}};
    view.bga_events = &event;
    view.bga_event_count = 1;
    (void)harness.frame();
    const auto start = rect_center(harness, event.bounds);
    const auto finish = canvas_point(harness, 162, 110);
    const auto press = std::array{mouse_motion(start.first, start.second), mouse_button(true, start.first, start.second)};
    (void)harness.frame(press);
    const auto motion = mouse_motion(finish.first, finish.second);
    (void)harness.frame(std::span{&motion, 1});
    const auto release = std::array{mouse_motion(finish.first, finish.second), mouse_button(false, finish.first, finish.second)};
    const auto output = harness.frame(release);
    const auto& action = output.get();
    check_bga(action.compose_input_count == 1);
    check_bga(action.compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE);
    check_bga(action.compose_inputs[0].bga_lane_kind_valid);
    check_bga(action.compose_inputs[0].bga_lane_kind == 5);
    check_bga(action.compose_inputs[0].object_id == 81);
}

TEST_CASE("armed BGA resource cannot place from playable or BGM lane clicks",
          "[frontend-state][event-harness][bga-lane]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    hide_compose_docks();
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.chart_id = 7;
    view.document_revision = 11;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 90;
    view.playable_field_right_pixels = 600;
    view.bga_lane_visible = true;
    view.bga_lane_bounds = {42, 0, 90, 600};
    WormUiComposeBgmLaneView bgm_lane{.bounds = {604, 0, 640, 600},
                                     .source_lane_id = 44};
    view.playable_field_left_pixels = 138;
    view.bga_lane_bounds.right = 138;
    view.bga_lane_count = 2;
    view.bga_lane_kinds[0] = 0;
    view.bga_lane_kinds[1] = 5;
    view.bga_lane_rects[0] = {42, 0, 90, 600};
    view.bga_lane_rects[1] = {90, 0, 138, 600};
    view.bgm_lanes = &bgm_lane;
    view.bgm_lane_count = 1;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.resource_picker.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bga;
    state.resource_picker.armed_resource_id = 91;
    state.resource_picker.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::place;
    const auto field = canvas_point(harness,
        (view.playable_field_left_pixels + view.playable_field_right_pixels) * 0.5,
        view.viewport_height_pixels * 0.5);
    const auto field_click = click(harness, field.first, field.second);
    const auto bgm_center = rect_center(harness, bgm_lane.bounds);
    const auto bgm_click = click(harness, bgm_center.first, bgm_center.second);
    const auto bga_center = rect_center(harness, view.bga_lane_rects[1]);
    const auto bga_click = click(harness, bga_center.first, bga_center.second);
    CHECK_FALSE(has_resource_place(field_click));
    CHECK_FALSE(has_resource_place(bgm_click));
    CHECK(has_resource_place(bga_click));
    for (const auto& output : bga_click)
        for (const auto& input : std::span(output.get().compose_inputs, output.get().compose_input_count))
            if (input.kind == WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE) {
                check_bga(input.bga_lane_kind_valid);
                check_bga(input.bga_lane_kind == 5);
            }
}

TEST_CASE("BGA event hit testing maps through the visible inspector and Sounds docks",
          "[frontend-state][event-harness][bga-lane]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    auto& compose_state = worm_ui_nuklear_compose_state();
    compose_state.dock_layout.inspector_visible = true;
    compose_state.dock_layout.sounds_panel_visible = true;
    compose_state.dock_layout.layers_panel_visible = false;
    compose_state.dock_layout.bga_panel_visible = false;
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.project_id = 3;
    view.chart_id = 7;
    view.document_revision = 11;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 90;
    view.playable_field_right_pixels = 900;
    view.keysounds_available = true;
    view.bga_lane_visible = true;
    view.bga_lane_bounds = {42, 0, 90, 600};
    WormUiComposeBgaEventView event{.event_id = 81, .resource_id = 91, .kind = 0,
        .name = "background", .bmp_id = "01", .bounds = {42, 0, 90, 600}};
    view.bga_events = &event;
    view.bga_event_count = 1;
    (void)harness.frame();
    WormUiTestingRect viewport{};
    REQUIRE(worm_ui_frontend_compose_viewport_for_testing(&viewport));
    view.viewport_width_pixels = viewport.width;
    (void)harness.frame();
    const auto center = rect_center(harness, event.bounds);
    CHECK(event.bounds.right == view.playable_field_left_pixels);
    CHECK(center.first < viewport.x + viewport.width * 0.55f);
    const auto selected = click(harness, center.first, center.second);
    CHECK(has_input(selected.back(), WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK, 81));
}

TEST_CASE("dense BGA overlap marker opens a scrollable picker for every event",
          "[frontend-state][event-harness][bga-lane]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    hide_compose_docks();
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.project_id = 3;
    view.chart_id = 7;
    view.document_revision = 11;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 90;
    view.playable_field_right_pixels = 620;
    view.bga_lane_visible = true;
    view.bga_lane_bounds = {42, 0, 90, 600};
    std::array<WormUiComposeBgaEventView, 50> events{};
    for (std::size_t index = 0; index < events.size(); ++index) {
        events[index] = {.event_id = 100 + index, .resource_id = 200 + index,
            .canvas_row = 480, .overlap_count = events.size(), .kind = 0,
            .name = "background", .bmp_id = "01", .bounds = {42, 0, 90, 600}};
    }
    view.bga_events = events.data();
    view.bga_event_count = events.size();
    (void)harness.frame();
    open_dense_stack_picker(harness, events.front());
    choose_last_stacked_event(harness, events);
    drag_last_stacked_event(harness, events.back());
    (void)harness.frame();
    const auto stack_center = rect_center(harness, events.front().bounds);
    (void)click(harness, stack_center.first, stack_center.second);
    (void)harness.frame();
    INFO("BGA stack open=" << worm_ui_nuklear_compose_state().bga_interaction.bga_stack_open
         << " release pending=" << worm_ui_nuklear_compose_state().bga_interaction.bga_stack_release_pending
         << " text=" << harness.drawn_text());
    REQUIRE(harness.drawn_text_center("Choose the event to select or edit").has_value());
    const auto first_row = drawn_text_center_with_prefix(harness, "#100 · BACKGROUND");
    REQUIRE(first_row.has_value());
    const auto motion = mouse_motion(first_row->first, first_row->second);
    (void)harness.frame(std::span{&motion, 1});
    CHECK(scroll_picker_through_stacked_events(harness, first_row->first, first_row->second,
                                               events));
    const auto previous = drawn_text_center_with_prefix(harness, "#148 · BACKGROUND");
    REQUIRE(previous.has_value());
    const auto another = click(harness, previous->first, previous->second);
    CHECK(has_input(another, WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK, 148));
}

TEST_CASE("BGA overlap picker outside dismissal retains canvas input ownership through release",
          "[frontend-state][event-harness][bga-lane]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    std::array<WormUiComposeBgaEventView, 3> events{};
    prepare_bga_stack_dismissal_picker(harness, events);
    verify_bga_stack_outside_dismissal(harness);
}

TEST_CASE("BGA overlap picker Escape dismisses without reaching canvas input",
          "[frontend-state][event-harness][bga-lane]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    std::array<WormUiComposeBgaEventView, 3> events{};
    prepare_bga_stack_dismissal_picker(harness, events);
    verify_bga_stack_escape_dismissal(harness);
}

namespace {
auto any_input_of(const std::array<workspace_action_snapshot_t, 2>& actions, WormUiComposeInputKind kind) -> bool {
    return std::ranges::any_of(actions, [&](const auto& action) {
        const auto& value = action.get();
        return std::ranges::any_of(std::span(value.compose_inputs, value.compose_input_count),
            [&](const auto& input) { return input.kind == kind; });
    });
}

struct stem_lane_fixture_t {
    nuklear_event_harness_t harness{1280, 800};
    WormUiComposeBgmLaneView lanes[2]{{.bounds = {604, 0, 640, 600}, .source_lane_id = 44, .stem = true},
                                      {.bounds = {644, 0, 680, 600}, .source_lane_id = 45}};
    WormUiComposeBgmEventView marker{.bounds = {604, 20, 640, 36}, .event_id = 501, .source_lane_id = 44,
                                     .bmson_slice = true};
    stem_lane_fixture_t() {
        REQUIRE(harness.ready());
        hide_compose_docks();
        auto& view = harness.view().compose;
        view.scene_available = true;
        view.bmson_tools_available = true;
        view.note_paint_mode_active = true;
        view.chart_id = 7;
        view.document_revision = 11;
        view.viewport_width_pixels = 900;
        view.viewport_height_pixels = 600;
        view.playable_field_left_pixels = 90;
        view.playable_field_right_pixels = 600;
        view.bgm_lanes = lanes;
        view.bgm_lane_count = 2;
        view.bgm_events = &marker;
        view.bgm_event_count = 1;
        (void)harness.frame();
    }
    auto at(double x, double y) { return canvas_point(harness, x, y); }
};
} // namespace

TEST_CASE("clicking empty space in a stem lane adds a continue point, and Shift a restart point",
          "[frontend-state][event-harness][bmson-stem]") {
    stem_lane_fixture_t f;
    const auto empty = f.at(622, 300);
    const auto plain = click(f.harness, empty.first, empty.second);
    CHECK(has_input(plain, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE, 44));

    const auto down = std::array{mouse_motion(empty.first, empty.second), mouse_button(true, empty.first, empty.second)};
    const auto up = std::array{mouse_motion(empty.first, empty.second), mouse_button(false, empty.first, empty.second)};
    const std::array shifted{f.harness.frame(down, SDL_KMOD_LSHIFT), f.harness.frame(up, SDL_KMOD_LSHIFT)};
    CHECK(has_input(shifted, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART, 44));
}

TEST_CASE("stem point clicks ignore lanes without a stem and existing points",
          "[frontend-state][event-harness][bmson-stem]") {
    stem_lane_fixture_t f;
    const auto other_lane = f.at(662, 300);
    CHECK_FALSE(any_input_of(click(f.harness, other_lane.first, other_lane.second),
                             WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE));
    const auto on_point = f.at(622, 28);
    const auto existing = click(f.harness, on_point.first, on_point.second);
    CHECK_FALSE(any_input_of(existing, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE));
    CHECK(has_input(existing, WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK, 501));

}

TEST_CASE("dragging a selected note onto the BGM area moves it to a BGM lane instead of canceling",
          "[frontend-state][event-harness][note-to-bgm]") {
    stem_lane_fixture_t f;
    auto& view = f.harness.view().compose;
    view.note_paint_mode_active = false;
    WormUiComposeNoteView note{.bounds = {200, 296, 260, 304}, .note_id = 77, .lane_id = 3, .selected = true};
    view.note_endpoints = &note;
    view.note_endpoint_count = 1;
    (void)f.harness.frame();
    const auto from = f.at(230, 300);
    const auto to = f.at(662, 300);
    const std::array down{mouse_motion(from.first, from.second), mouse_button(true, from.first, from.second)};
    (void)f.harness.frame(down);
    const std::array drag{mouse_motion(to.first, to.second)};
    const auto dragging = f.harness.frame(drag);
    CHECK(any_input_of({dragging, dragging}, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE));
    const std::array up{mouse_motion(to.first, to.second), mouse_button(false, to.first, to.second)};
    const auto released = f.harness.frame(up);
    const auto& inputs = released.get();
    const auto drop = std::ranges::find_if(std::span(inputs.compose_inputs, inputs.compose_input_count),
        [](const auto& input) { return input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE; });
    REQUIRE(drop != std::span(inputs.compose_inputs, inputs.compose_input_count).end());
    CHECK((drop->lane_id == 45 && drop->note_move_to_bgm));
    CHECK_FALSE(any_input_of({released, released}, WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE));
}

TEST_CASE("dragging a selected BGM marker onto a playfield lane asks to move it into that lane",
          "[frontend-state][event-harness][bgm-to-lane]") {
    stem_lane_fixture_t f;
    auto& view = f.harness.view().compose;
    view.note_paint_mode_active = false;
    WormUiComposeLaneView lane{.bounds = {100, 0, 150, 600}, .lane_id = 3};
    view.lanes = &lane;
    view.lane_count = 1;
    f.marker.selected = true;
    view.selected_bgm_event_id = 501;
    (void)f.harness.frame();
    const auto from = f.at(622, 28);
    const auto to = f.at(125, 300);
    const std::array down{mouse_motion(from.first, from.second), mouse_button(true, from.first, from.second)};
    (void)f.harness.frame(down);
    const std::array drag{mouse_motion(to.first, to.second)};
    (void)f.harness.frame(drag);
    const std::array up{mouse_motion(to.first, to.second), mouse_button(false, to.first, to.second)};
    const auto released = f.harness.frame(up);
    const auto& inputs = released.get();
    const auto drop = std::ranges::find_if(std::span(inputs.compose_inputs, inputs.compose_input_count),
        [](const auto& input) { return input.kind == WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE; });
    REQUIRE(drop != std::span(inputs.compose_inputs, inputs.compose_input_count).end());
    CHECK(drop->lane_id == 3);
    CHECK(drop->object_id == 501);
}

TEST_CASE("Shift lane key routes successive assignment with BGM selected",
          "[frontend-state][event-harness][bgm-to-lane]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const WormUiComposeLaneKeyBinding binding{.scancode = SDL_SCANCODE_1, .lane_id = 3};
    view.lane_key_bindings = &binding;
    view.lane_key_binding_count = 1;
    view.selected_bgm_event_count = 1;
    (void)harness.frame();
    SDL_Event press{};
    press.type = SDL_EVENT_KEY_DOWN;
    press.key.type = SDL_EVENT_KEY_DOWN;
    press.key.key = SDLK_1;
    press.key.scancode = SDL_SCANCODE_1;
    press.key.down = true;
    const auto shifted = harness.frame(std::span{&press, 1}, SDL_KMOD_SHIFT);
    REQUIRE(shifted.get().compose_input_count == 1);
    CHECK(shifted.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    CHECK(shifted.get().compose_inputs[0].successive);
    CHECK(shifted.get().compose_inputs[0].lane_id == 3);
}

TEST_CASE("Y-lock stops a selected BGA event from being dragged to another row",
          "[frontend-state][event-harness][bga-lane][y-lock]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    hide_compose_docks();
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.chart_id = 7;
    view.document_revision = 11;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 90;
    view.playable_field_right_pixels = 620;
    view.bga_lane_visible = true;
    view.bga_lane_bounds = {42, 0, 90, 600};
    WormUiComposeBgaEventView event{.event_id = 81, .resource_id = 91, .kind = 0, .name = "background",
                                    .bmp_id = "01", .bounds = {42, 0, 90, 600}};
    event.selected = true;
    view.bga_events = &event;
    view.bga_event_count = 1;
    view.y_lock = true;
    (void)harness.frame();
    const auto center = rect_center(harness, event.bounds);
    const auto down = std::array{mouse_motion(center.first, center.second),
                                 mouse_button(true, center.first, center.second)};
    (void)harness.frame(down);
    const auto moved_y = center.second + 24.0f;
    const auto drag = mouse_motion(center.first, moved_y);
    (void)harness.frame(std::span{&drag, 1});
    const auto up = std::array{mouse_motion(center.first, moved_y), mouse_button(false, center.first, moved_y)};
    const auto released = harness.frame(up);
    CHECK_FALSE(has_input(released, WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE, 81));
}

TEST_CASE("BGA lane controls reveal every kind without a hidden dropdown", "[event-harness][bga][bga-discovery]") {
    nuklear_event_harness_t harness{1280, 600};
    REQUIRE(harness.ready());
    hide_compose_docks();
    auto& view = harness.view().compose;
    view.bga_lane_count = 0;
    view.bga_ids_available = true;
    (void)harness.frame();
    worm_ui_nuklear_compose_state().dock_layout.bga_panel_visible = true;
    (void)harness.frame();
    REQUIRE(harness.drawn_text_center("Show BGA lanes"));
    constexpr std::array labels{"BGA", "Layer", "Layer 2", "Poor (miss)"};
    constexpr std::array<std::uint8_t, 4> kinds{0, 2, 5, 1};
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const auto control = harness.drawn_text_center(labels[index]);
        REQUIRE(control);
        const auto actions = click(harness, control->first, control->second);
        const auto revealed = std::ranges::any_of(actions, [&](const auto& snapshot) {
            const auto& action = snapshot.get();
            return std::ranges::any_of(std::span(action.compose_inputs, action.compose_input_count),
                [&](const auto& input) {
                    return input.kind == WORM_UI_COMPOSE_INPUT_BGA_LANE_REVEAL &&
                        input.bga_lane_kind_valid && input.bga_lane_kind == kinds[index];
                });
        });
        check_bga(revealed);
    }
}

TEST_CASE("Populated BGA lanes retain discoverable controls", "[event-harness][bga][bga-discovery]") {
    nuklear_event_harness_t harness{1280, 600};
    REQUIRE(harness.ready());
    hide_compose_docks();
    auto& view = harness.view().compose;
    view.bga_lane_count = 4;
    constexpr std::array<std::uint8_t, 4> kinds{0, 2, 5, 1};
    std::ranges::copy(kinds, view.bga_lane_kinds);
    (void)harness.frame();
    worm_ui_nuklear_compose_state().dock_layout.bga_panel_visible = true;
    (void)harness.frame();
    REQUIRE(harness.drawn_text_center("Layer 2"));
    REQUIRE(harness.drawn_text_center("Poor (miss)"));
}

namespace {
struct resizable_stem_lane_fixture_t {
    nuklear_event_harness_t harness{1600, 900};
    WormUiComposeBgmLaneView lane{.bounds = {604, 0, 640, 600}, .source_lane_id = 44,
        .stem = true, .width_pixels = 36, .resize_edge_pixels = 640,
        .filename = "素材/かなり長い stem filename.wav"};
    WormUiComposeBgmEventView marker{.bounds = {604, 20, 640, 36}, .event_id = 501,
        .source_lane_id = 44, .bmson_slice = true};
    resizable_stem_lane_fixture_t() {
        REQUIRE(harness.ready());
        hide_compose_docks();
        harness.view().project_id = 123;
        auto& view = harness.view().compose;
        view.visible = true;
        view.scene_available = true;
        view.project_id = 123;
        view.chart_id = 7;
        view.document_revision = 11;
        view.viewport_width_pixels = 1200;
        view.viewport_height_pixels = 600;
        view.playable_field_left_pixels = 90;
        view.playable_field_right_pixels = 600;
        view.bmson_tools_available = true;
        view.bgm_lanes = &lane;
        view.bgm_lane_count = 1;
        view.bgm_events = &marker;
        view.bgm_event_count = 1;
        (void)harness.frame();
    }
    auto at(double x, double y) { return canvas_point(harness, x, y); }
    auto width() const { return worm_ui_frontend_compose_bgm_lane_width(123, 7, 44); }
    auto edge() { return at(lane.resize_edge_pixels, 6); }
    auto expected_drag_width(double delta) {
        const auto from = edge();
        const auto to = at(lane.resize_edge_pixels + delta, 6);
        WormUiTestingRect viewport{};
        REQUIRE(worm_ui_frontend_compose_viewport_for_testing(&viewport));
        return std::clamp(width() + (to.first - std::trunc(from.first)) *
            harness.view().compose.viewport_width_pixels / viewport.width, 36.0, 480.0);
    }
    void refresh_geometry() {
        lane.width_pixels = width();
        lane.bounds.right = lane.bounds.left + lane.width_pixels;
        lane.resize_edge_pixels = lane.bounds.right;
        marker.bounds.right = lane.bounds.right;
    }
};

auto press_stem_edge(resizable_stem_lane_fixture_t& f) -> workspace_action_snapshot_t {
    const auto p = f.edge();
    const auto events = std::array{mouse_motion(p.first, p.second), mouse_button(true, p.first, p.second)};
    return f.harness.frame(events);
}

auto drag_stem_edge(resizable_stem_lane_fixture_t& f, float delta) -> workspace_action_snapshot_t {
    const auto p = f.at(f.lane.resize_edge_pixels + delta, 6);
    (void)press_stem_edge(f);
    const auto moved = mouse_motion(p.first, p.second);
    return f.harness.frame(std::span{&moved, 1});
}

void release_stem_edge(resizable_stem_lane_fixture_t& f, float delta) {
    const auto p = f.at(f.lane.resize_edge_pixels + delta, 6);
    const auto events = std::array{mouse_motion(p.first, p.second),
                                   mouse_button(false, p.first, p.second)};
    (void)f.harness.frame(events);
    f.refresh_geometry();
}

auto double_click_stem_edge(resizable_stem_lane_fixture_t& f) -> workspace_action_snapshot_t {
    const auto p = f.edge();
    auto down = mouse_button(true, p.first, p.second);
    auto up = mouse_button(false, p.first, p.second);
    down.button.clicks = 2;
    up.button.clicks = 2;
    const auto events = std::array{mouse_motion(p.first, p.second), down, up};
    return f.harness.frame(events);
}

auto no_stem_point_actions(const workspace_action_snapshot_t& action) -> bool {
    const auto& value = action.get();
    return std::ranges::none_of(std::span(value.compose_inputs, value.compose_input_count),
        [](const auto& input) {
            return input.kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE ||
                   input.kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART;
        });
}

void start_resizing(resizable_stem_lane_fixture_t& f) {
    const auto expected = f.expected_drag_width(40);
    (void)drag_stem_edge(f, 40);
    check_bga(std::abs(f.width() - expected) < 1e-6);
    check_bga(worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize.has_value());
    check_bga(worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize_cursor_requested);
}

auto window_focus_lost_event() -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    return event;
}

void release_inactive_stem_pointer(resizable_stem_lane_fixture_t& f) {
    const auto up = mouse_button(false, 0, 0);
    (void)f.harness.frame(std::span{&up, 1});
}
} // namespace

TEST_CASE("stem lane header edge resizes through production input and enables far-side points",
          "[event-harness][bmson-stem][resize]") {
    resizable_stem_lane_fixture_t f;
    const auto expected = f.expected_drag_width(264);
    const auto moved = drag_stem_edge(f, 264);
    check_bga(std::abs(f.width() - expected) < 1e-6);
    check_bga(no_stem_point_actions(moved));
    release_stem_edge(f, 264);
    (void)f.harness.frame();
    f.harness.view().compose.note_paint_mode_active = true;
    const auto p = f.at(850, 300);
    const auto added = click(f.harness, p.first, p.second);
    check_bga(has_input(added, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE, 44));
}

TEST_CASE("stem lane resize clamps and double click restores its default width",
          "[event-harness][bmson-stem][resize]") {
    resizable_stem_lane_fixture_t f;
    (void)drag_stem_edge(f, 1000);
    check_bga(f.width() == 480.0);
    release_stem_edge(f, 1000);
    (void)drag_stem_edge(f, -1000);
    check_bga(f.width() == 36.0);
    release_stem_edge(f, -1000);
    const auto expected = f.expected_drag_width(180);
    (void)drag_stem_edge(f, 180);
    release_stem_edge(f, 180);
    check_bga(std::abs(f.width() - expected) < 1e-6);
    (void)double_click_stem_edge(f);
    check_bga(f.width() == 36.0);
}

TEST_CASE("stem lane resize ignores packed and clipped lane edges",
          "[event-harness][bmson-stem][resize]") {
    resizable_stem_lane_fixture_t f;
    f.lane.stem = false;
    (void)f.harness.frame();
    (void)drag_stem_edge(f, 100);
    check_bga(f.width() == 36.0);
    release_stem_edge(f, 100);
    f.lane.stem = true;
    f.lane.bounds.left = 620;
    f.lane.bounds.right = 640;
    f.lane.resize_edge_pixels = 700;
    (void)f.harness.frame();
    check_bga(no_stem_point_actions(double_click_stem_edge(f)));
    check_bga(f.width() == 36.0);
}

TEST_CASE("stem lane resize cancels on focus loss, hidden Compose, tab switch, and occlusion",
          "[event-harness][bmson-stem][resize]") {
    resizable_stem_lane_fixture_t f;
    const auto expected = f.expected_drag_width(120);
    (void)drag_stem_edge(f, 120);
    release_stem_edge(f, 120);
    check_bga(std::abs(f.width() - expected) < 1e-6);
    const auto anchor = f.width();
    start_resizing(f);
    f.harness.view().compose.visible = false;
    (void)f.harness.frame();
    check_bga(f.width() == anchor);
    check_bga(!worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize);
    check_bga(!worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize_cursor_requested);
    release_inactive_stem_pointer(f);

    f.harness.view().compose.visible = true;
    (void)f.harness.frame();
    start_resizing(f);
    f.harness.view().active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
    (void)f.harness.frame();
    check_bga(!worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize);
    check_bga(f.width() == anchor);
    check_bga(!worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize_cursor_requested);
    release_inactive_stem_pointer(f);

    f.harness.view().active_tab = WORM_UI_WORKSPACE_TAB_COMPOSE;
    (void)f.harness.frame();
    start_resizing(f);
    f.harness.view().compose.input_occluded = true;
    (void)f.harness.frame();
    check_bga(!worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize);
    check_bga(f.width() == anchor);
    check_bga(!worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize_cursor_requested);
    release_inactive_stem_pointer(f);

    f.harness.view().compose.input_occluded = false;
    (void)f.harness.frame();
    start_resizing(f);
    f.harness.view().compose.visible = false;
    const auto focus_lost = window_focus_lost_event();
    (void)f.harness.frame(std::span{&focus_lost, 1});
    check_bga(!worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize);
    check_bga(!worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize_cursor_requested);
    check_bga(f.width() == anchor);
}

TEST_CASE("stem filename retains one full UTF8 tooltip at narrow and wide widths",
          "[event-harness][bmson-stem][resize]") {
    resizable_stem_lane_fixture_t f;
    const std::string filename{"素材/かなり長い stem filename.wav"};
    const auto narrow = f.at(622, 6);
    const auto narrow_move = mouse_motion(narrow.first, narrow.second);
    (void)f.harness.frame(std::span{&narrow_move, 1});
    const auto found = f.harness.drawn_text().find(filename);
    check_bga(found != std::string::npos);
    check_bga(f.harness.drawn_text().find(filename, found + filename.size()) == std::string::npos);
    worm_ui_nuklear_set_stem_lane_width(worm_ui_nuklear_compose_state(), 123, 7, 44, 96);
    f.refresh_geometry();
    (void)f.harness.frame();
    const auto hover = f.at(648, 6);
    const auto move = mouse_motion(hover.first, hover.second);
    (void)f.harness.frame(std::span{&move, 1});
    const auto wide_found = f.harness.drawn_text().find(filename);
    check_bga(wide_found != std::string::npos);
    check_bga(f.harness.drawn_text().find(filename, wide_found + filename.size()) == std::string::npos);
}

TEST_CASE("batched stem edge press and motion uses the original header press",
          "[event-harness][bmson-stem][resize]") {
    resizable_stem_lane_fixture_t f;
    const auto expected = f.expected_drag_width(264);
    const auto from = f.edge();
    const auto to = f.at(f.lane.resize_edge_pixels + 264, 6);
    const auto events = std::array{mouse_motion(from.first, from.second),
        mouse_button(true, from.first, from.second), mouse_motion(to.first, to.second)};
    const auto action = f.harness.frame(events);
    check_bga(worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize.has_value());
    check_bga(std::abs(f.width() - expected) < 1e-6);
    check_bga(no_stem_point_actions(action));
    release_stem_edge(f, 264);
}
TEST_CASE("batched stem body press followed by edge motion does not begin resizing",
          "[event-harness][bmson-stem][resize]") {
    resizable_stem_lane_fixture_t f;
    const auto from = f.at(622, 100);
    const auto to = f.edge();
    const auto events = std::array{mouse_motion(from.first, from.second),
        mouse_button(true, from.first, from.second), mouse_motion(to.first, to.second)};
    (void)f.harness.frame(events);
    check_bga(!worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize);
    check_bga(f.width() == 36);
    const auto up = mouse_button(false, to.first, to.second);
    (void)f.harness.frame(std::span{&up, 1});
}

TEST_CASE("Draw BGA source emits one stroke with the destination kind and no resource pointer placement",
          "[frontend-state][event-harness][bga][draw]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    hide_compose_docks();
    auto& view = harness.view().compose;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 138;
    view.playable_field_right_pixels = 600;
    view.bga_lane_visible = true;
    view.bga_lane_bounds = {42, 0, 138, 600};
    view.bga_lane_count = 2;
    view.bga_lane_kinds[0] = 0;
    view.bga_lane_kinds[1] = 5;
    view.bga_lane_rects[0] = {42, 0, 90, 600};
    view.bga_lane_rects[1] = {90, 0, 138, 600};
    view.note_paint_mode_active = true;
    auto& state = worm_ui_nuklear_compose_state();
    (void)harness.frame();
    state.pointer_capture.draw_source_kind = 2;
    state.pointer_capture.draw_source_id = 91;
    (void)harness.frame();
    const auto center = rect_center(harness, view.bga_lane_rects[1]);
    const auto frames = click(harness, center.first, center.second);
    size_t begins{}, releases{};
    for (const auto& frame : frames) {
        for (size_t i = 0; i < frame.get().compose_input_count; ++i) {
            const auto& input = frame.get().compose_inputs[i];
            if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) {
                ++begins;
                check_bga(input.resource_id == 91 && input.paint_intent == 1);
                check_bga(input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGA);
                check_bga(input.bga_lane_kind == 5 && input.bga_lane_kind_valid);
            }
            releases += input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE;
            check_bga(input.kind != WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE);
        }
    }
    check_bga(begins == 1 && releases == 1);
}

TEST_CASE("Draw accepts empty stem lanes while Select only clears selection",
          "[event-harness][bmson-stem][draw]") {
    stem_lane_fixture_t f;
    f.harness.view().compose.bgm_event_count = 0;
    const auto point = f.at(622, 300);
    const auto draw = click(f.harness, point.first, point.second);
    check_bga(has_input(draw, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE, 44));
    f.harness.view().compose.note_paint_mode_active = false;
    const auto selected = click(f.harness, point.first, point.second);
    check_bga(any_input_of(selected, WORM_UI_COMPOSE_INPUT_BMSON_EMPTY_SELECT));
    check_bga(!any_input_of(selected, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE));
}

TEST_CASE("Packed Draw emits the current keysound and rejects a stem source",
          "[event-harness][bmson-stem][draw]") {
    stem_lane_fixture_t f;
    auto& state = worm_ui_nuklear_compose_state();
    state.pointer_capture.draw_source_kind = 1;
    state.pointer_capture.draw_source_id = 81;
    f.harness.view().compose.draw_source_kind = 1;
    f.harness.view().compose.draw_source_id = 81;
    const auto point = f.at(662, 300);
    const auto placed = click(f.harness, point.first, point.second);
    bool matched{};
    for (const auto& snapshot : placed)
        for (const auto& input : std::span(snapshot.get().compose_inputs, snapshot.get().compose_input_count))
            matched |= input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN &&
                       input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM &&
                       input.lane_id == 45 && input.resource_id == 81;
    check_bga(matched);
    f.harness.view().compose.draw_source_stem_lane_id = 44;
    f.harness.view().compose.draw_source_stem_lane_name = "Voice stem";
    const auto rejected = click(f.harness, point.first, point.second);
    check_bga(!any_input_of(rejected, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN));
}

namespace {

struct dock_stem_fixture_t {
    nuklear_event_harness_t harness{1280, 800};
    WormUiComposeBgmLaneView lanes[3]{
        {.bounds = {604, 0, 640, 600}, .source_lane_id = 44, .stem = true},
        {.bounds = {644, 0, 680, 600}, .source_lane_id = 45},
        {.bounds = {684, 0, 720, 600}, .source_lane_id = 46, .stem = true},
    };
    WormUiComposeBgmEventView marker{.bounds = {604, 20, 640, 36}, .event_id = 501,
        .source_lane_id = 44, .bmson_slice = true};
    WormUiComposeKeysoundRowView source{.resource_id = 81, .slot = "", .name = "Dock stem sample",
        .stem_source_lane_id = 44, .stem_lane_name = "Voice stem"};

    dock_stem_fixture_t() {
        REQUIRE(harness.ready());
        auto& state = worm_ui_nuklear_compose_state();
        hide_compose_docks();
        state.dock_layout.inspector_visible = true;
        state.dock_layout.sounds_panel_visible = true;
        auto& view = harness.view().compose;
        view.scene_available = true;
        view.bmson_tools_available = true;
        view.note_paint_mode_active = true;
        view.project_id = 3;
        view.chart_id = 7;
        view.document_revision = 11;
        view.viewport_width_pixels = 900;
        view.viewport_height_pixels = 600;
        view.playable_field_left_pixels = 90;
        view.playable_field_right_pixels = 600;
        view.bgm_lanes = lanes;
        view.bgm_lane_count = 3;
        view.bgm_events = &marker;
        view.bgm_event_count = 1;
        view.keysounds_available = true;
        view.keysounds = &source;
        view.keysound_count = 1;
        view.keysound_total_count = 1;
        (void)harness.frame();
    }

    auto field_point(std::size_t lane_index) {
        constexpr std::array x{622.0, 662.0, 702.0};
        return canvas_point(harness, x[lane_index], 300.0);
    }

    auto source_point() const { return drawn_text_center_with_prefix(harness, "Dock stem"); }
};

auto stem_input(const workspace_action_snapshot_t& action) -> const WormUiComposeInputAction* {
    const auto& value = action.get();
    const auto inputs = std::span(value.compose_inputs, value.compose_input_count);
    const auto found = std::ranges::find_if(inputs, [](const auto& input) {
        return input.kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART ||
               input.kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE ||
               input.kind == WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW;
    });
    return found == inputs.end() ? nullptr : &*found;
}

auto drop_source_on_lane(dock_stem_fixture_t& fixture, std::size_t lane_index)
    -> workspace_action_snapshot_t {
    const auto source = fixture.source_point();
    REQUIRE(source);
    const auto press = mouse_button(true, source->first, source->second);
    (void)fixture.harness.frame(std::span{&press, 1});
    const auto target = fixture.field_point(lane_index);
    const auto move = mouse_motion(target.first, target.second);
    (void)fixture.harness.frame(std::span{&move, 1});
    const auto release = mouse_button(false, target.first, target.second);
    return fixture.harness.frame(std::span{&release, 1});
}

auto source_drop_cancelled(int transition) -> bool {
    dock_stem_fixture_t fixture;
    const auto source = fixture.source_point();
    if (!source) return false;
    const auto press = mouse_button(true, source->first, source->second);
    (void)fixture.harness.frame(std::span{&press, 1});
    const auto target = fixture.field_point(0);
    auto& view = fixture.harness.view();
    if (transition == 0) {
        SDL_Event lost{};
        lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        lost.window.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        (void)fixture.harness.frame(std::span{&lost, 1});
    } else if (transition == 1) {
        view.active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        view.compose.visible = false;
        view.metadata.visible = true;
        (void)fixture.harness.frame();
    } else {
        ++view.compose.document_revision;
        (void)fixture.harness.frame();
    }
    const auto release = mouse_button(false, target.first, target.second);
    const auto action = fixture.harness.frame(std::span{&release, 1});
    return stem_input(action) == nullptr &&
           !worm_ui_nuklear_compose_state().bmson_input.bmson_point_capture.has_value();
}

auto ui_bmson_song() -> wormedit::song_document_t {
    using namespace wormedit;
    constexpr measure_id_t measure{14};
    constexpr asset_id_t asset{10};
    constexpr object_id_t lane{4};
    constexpr object_id_t channel{5};
    constexpr resource_id_t resource{11};
    song_document_t song{.id = song_id_t{13}};
    song.audio_assets.push_back({.id = asset, .source_path = "stem.wav",
                                 .stream = {48'000, 2, 200'000, true}});
    song.resources.push_back({.id = resource, .name = "Stem source", .asset = asset,
                              .roles = {resource_role_t::bgm}});
    chart_document_t chart{.id = chart_id_t{12}, .source_family = format_family_t::bmson};
    chart.measures.push_back({.id = measure});
    chart.bpms.push_back({.id = object_id_t{9}, .position = {measure, 0},
                          .beats_per_minute = {120, 1}});
    chart.bgm_source_lanes.push_back({.id = lane, .name = "Voice stem",
                                      .stem_channel = channel});
    chart.sound_channels.push_back({.id = channel, .name = "stem.wav",
        .source_clip = {.id = clip_id_t{15}, .source = asset, .begin_frame = 0},
        .events = {{.id = object_id_t{7}, .position = {measure, 240},
                    .continuation = false, .playable = false}}});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{8},
        .position = {measure, 240}, .source_lane = lane, .sound = resource,
        .sound_channel_event = object_id_t{7}});
    song.charts.push_back(std::move(chart));
    return song;
}

auto remap_bmson_stem_song(std::uint64_t lane_id, std::uint64_t resource_id,
                            bool empty_channel = false) -> wormedit::song_document_t {
    auto song = ui_bmson_song();
    auto& chart = song.charts.front();
    const auto channel = chart.sound_channels.front().id;
    const auto lane = std::ranges::find_if(chart.bgm_source_lanes, [&](const auto& item) {
        return item.stem_channel == channel;
    });
    REQUIRE(lane != chart.bgm_source_lanes.end());
    const auto old_lane = lane->id;
    lane->id = wormedit::object_id_t{lane_id};
    const auto asset = chart.sound_channels.front().source_clip.source;
    const auto resource = std::ranges::find_if(song.resources, [&](const auto& item) {
        return item.asset == asset &&
               std::ranges::find(item.roles, wormedit::resource_role_t::bgm) != item.roles.end();
    });
    REQUIRE(resource != song.resources.end());
    const auto old_resource = resource->id;
    resource->id = wormedit::resource_id_t{resource_id};
    resource->missing = false;
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    for (auto& measure : chart.measures) {
        for (auto& bgm : measure.bgm_events) {
            if (bgm.source_lane == old_lane) bgm.source_lane = lane->id;
            if (bgm.sound == old_resource) bgm.sound = resource->id;
        }
        if (empty_channel) {
            std::erase_if(measure.notes, [&](const auto& note) {
                return note.sound_channel_event.has_value() &&
                    std::ranges::any_of(chart.sound_channels.front().events, [&](const auto& event) {
                        return event.id == *note.sound_channel_event;
                    });
            });
            std::erase_if(measure.bgm_events, [&](const auto& bgm) {
                return bgm.sound_channel_event.has_value() &&
                    std::ranges::any_of(chart.sound_channels.front().events, [&](const auto& event) {
                        return event.id == *bgm.sound_channel_event;
                    });
            });
        }
    }
    if (empty_channel) {
        chart.sound_channels.front().events.clear();
        resource->name = chart.sound_channels.front().name;
        resource->sound_kind = wormedit::sound_kind_t::stem;
    }
    return song;
}

auto make_packed_bmson_song(std::uint64_t lane_id, std::uint64_t resource_id)
    -> wormedit::song_document_t {
    auto song = remap_bmson_stem_song(44, 81);
    auto& chart = song.charts.front();
    auto asset = song.audio_assets.front();
    asset.id = wormedit::asset_id_t{resource_id + 1};
    asset.stream = {48'000, 2, 144'000, true};
    song.audio_assets.push_back(asset);
    auto resource = song.resources.front();
    resource.id = wormedit::resource_id_t{resource_id};
    resource.name = "Packed sample";
    resource.asset = asset.id;
    resource.roles = {wormedit::resource_role_t::sample};
    song.resources.push_back(resource);
    chart.bgm_source_lanes.push_back({.id = wormedit::object_id_t{lane_id},
        .name = "Packed lane", .source_order = 1});
    return song;
}

auto core_payload_undoes_once(const WormUiComposeInputAction& input, std::uint64_t row,
                              bool empty_stem = false) -> bool {
    auto song = input.kind == WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW
        ? make_packed_bmson_song(input.lane_id, input.resource_id)
        : remap_bmson_stem_song(input.lane_id, 81, empty_stem);
    auto history = wormedit::make_document_history(std::move(song));
    const auto original_chart = history.document.charts.front();
    const auto revision = history.document.revision;
    wormedit::document_command_result_t result;
    if (input.kind == WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW) {
        result = wormedit::insert_bmson_packed_one_shot(history,
            history.document.charts.front().id, wormedit::object_id_t{input.lane_id},
            wormedit::resource_id_t{input.resource_id},
            {history.document.charts.front().measures.front().id, row});
    } else {
        const auto lane = std::ranges::find(history.document.charts.front().bgm_source_lanes,
            wormedit::object_id_t{input.lane_id}, &wormedit::bgm_source_lane_t::id);
        if (lane == history.document.charts.front().bgm_source_lanes.end() || !lane->stem_channel)
            return false;
        const auto channel = std::ranges::find(history.document.charts.front().sound_channels,
            *lane->stem_channel, &wormedit::sound_channel_t::id);
        if (channel == history.document.charts.front().sound_channels.end()) return false;
        const auto geometry = wormedit::bmson_source_geometry_t{
            .asset = channel->source_clip.source, .sample_rate = 48'000, .frame_count = 200'000};
        result = wormedit::insert_bmson_stem_point(history,
            history.document.charts.front().id, channel->id,
            {history.document.charts.front().measures.front().id, row},
            input.kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART, geometry);
    }
    if (!result || history.undo.size() != 1 || !wormedit::validate_document(history.document).empty())
        return false;
    if (!wormedit::undo_document_command(history)) return false;
    const auto& restored = history.document.charts.front();
    return restored.sound_channels.size() == original_chart.sound_channels.size() &&
           restored.bgm_source_lanes.size() == original_chart.bgm_source_lanes.size() &&
           restored.measures.size() == original_chart.measures.size();
}

auto packed_stroke_undoes_once(const WormUiComposeInputAction& input, std::uint64_t row) -> bool {
    auto history = wormedit::make_document_history(
        make_packed_bmson_song(input.lane_id, input.resource_id));
    const std::array cells{wormedit::local_position_t{
        history.document.charts.front().measures.front().id, row}};
    const auto original = history.document.charts.front().sound_channels.size();
    const auto result = wormedit::apply_bmson_packed_stroke(history,
        history.document.charts.front().id, wormedit::object_id_t{input.lane_id},
        wormedit::resource_id_t{input.resource_id}, cells);
    if (!result || history.undo.size() != 1 || !wormedit::validate_document(history.document).empty())
        return false;
    return wormedit::undo_document_command(history) &&
           history.document.charts.front().sound_channels.size() == original;
}

auto input_payload_undoes_once(const workspace_action_snapshot_t& action,
                               WormUiComposeInputKind kind, std::uint64_t row,
                               bool empty_stem = false) -> bool {
    const auto* input = stem_input(action);
    return input && input->kind == kind && core_payload_undoes_once(*input, row, empty_stem);
}

} // namespace

TEST_CASE("Sounds dock stem source drops only onto its own stem lane and emits restart payload",
          "[frontend-state][event-harness][bmson-stem][draw]") {
    dock_stem_fixture_t fixture;
    const auto action = drop_source_on_lane(fixture, 0);
    const auto* input = stem_input(action);
    REQUIRE(input);
    check_bga(input->kind == WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART);
    check_bga(input->lane_id == 44 && input->resource_id == 81);
    check_bga(input->resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM);
    check_bga(core_payload_undoes_once(*input, 300));
}

TEST_CASE("Sounds dock stem drops to packed or other lanes and stale captures are discarded",
          "[frontend-state][event-harness][bmson-stem][draw]") {
    {
        dock_stem_fixture_t packed;
        check_bga(stem_input(drop_source_on_lane(packed, 1)) == nullptr);
    }
    {
        dock_stem_fixture_t other_stem;
        check_bga(stem_input(drop_source_on_lane(other_stem, 2)) == nullptr);
    }
    for (int transition = 0; transition < 3; ++transition)
        check_bga(source_drop_cancelled(transition));
}

TEST_CASE("Emitted stem and packed draw payloads each commit one undoable BMSON command",
          "[frontend-state][event-harness][bmson-stem][draw]") {
    {
        stem_lane_fixture_t stem;
        const auto point = stem.at(622, 300);
        const auto plain = click(stem.harness, point.first, point.second);
        check_bga(input_payload_undoes_once(plain[1],
            WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE, 300));

        const auto down = std::array{mouse_motion(point.first, point.second),
                                     mouse_button(true, point.first, point.second)};
        const auto up = std::array{mouse_motion(point.first, point.second),
                                   mouse_button(false, point.first, point.second)};
        (void)stem.harness.frame(down, SDL_KMOD_LSHIFT);
        const auto shifted = stem.harness.frame(up, SDL_KMOD_LSHIFT);
        check_bga(input_payload_undoes_once(shifted,
            WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART, 300));
    }
    {
        stem_lane_fixture_t empty;
        empty.harness.view().compose.bgm_event_count = 0;
        const auto empty_point = empty.at(622, 300);
        const auto empty_click = click(empty.harness, empty_point.first, empty_point.second);
        check_bga(input_payload_undoes_once(empty_click[1],
            WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE, 300, true));
    }
    {
        stem_lane_fixture_t packed;
        auto& state = worm_ui_nuklear_compose_state();
        state.pointer_capture.draw_source_kind = 1;
        state.pointer_capture.draw_source_id = 800'001;
        packed.harness.view().compose.draw_source_kind = 1;
        packed.harness.view().compose.draw_source_id = 800'001;
        const auto packed_point = packed.at(662, 300);
        const auto packed_click = click(packed.harness, packed_point.first, packed_point.second);
        const WormUiComposeInputAction* packed_input{};
        for (const auto& snapshot : packed_click)
            for (const auto& input : std::span(snapshot.get().compose_inputs,
                                               snapshot.get().compose_input_count))
                if (input.kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN) packed_input = &input;
        check_bga(packed_input && packed_input->lane_id == 45 &&
                  packed_input->resource_id == 800'001 &&
                  packed_stroke_undoes_once(*packed_input, 90));
    }
}

TEST_CASE("Mixed selection starts a BGM drag without a singular inspector event",
          "[frontend-state][event-harness][mixed-move]") {
    stem_lane_fixture_t f;
    auto& view = f.harness.view().compose;
    view.note_paint_mode_active = false;
    f.marker.selected = true;
    view.selected_bgm_event_id = 0;
    view.selected_bgm_event_count = 2;
    (void)f.harness.frame();
    const auto from = f.at(622, 28);
    const auto to = f.at(622, 128);
    const std::array down{mouse_motion(from.first, from.second), mouse_button(true, from.first, from.second)};
    (void)f.harness.frame(down);
    const std::array drag{mouse_motion(to.first, to.second)};
    const auto moving = f.harness.frame(drag);
    check_bga(has_input({moving, moving}, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN, 501));
    const std::array up{mouse_motion(to.first, to.second), mouse_button(false, to.first, to.second)};
    const auto released = f.harness.frame(up);
    check_bga(has_input({released, released}, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE, 501));
    check_bga(!any_input_of({released, released}, WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK));
}

TEST_CASE("Single ordinary BGM drag sends the destination source lane through release",
          "[frontend-state][event-harness][single-bgm-move]") {
    stem_lane_fixture_t f;
    auto& view = f.harness.view().compose;
    view.note_paint_mode_active = false;
    f.marker.selected = true;
    f.marker.bmson_slice = false;
    f.lanes[0].stem = false;
    view.selected_bgm_event_id = 501;
    view.selected_bgm_event_count = 1;
    (void)f.harness.frame();
    const auto from = f.at(622, 28);
    const auto to = f.at(662, 128);
    const std::array down{mouse_motion(from.first, from.second), mouse_button(true, from.first, from.second)};
    (void)f.harness.frame(down);
    const std::array drag{mouse_motion(to.first, to.second)};
    const auto moving = f.harness.frame(drag);
    check_bga(has_input({moving, moving}, WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN, 501));
    const std::array up{mouse_motion(to.first, to.second), mouse_button(false, to.first, to.second)};
    const auto released = f.harness.frame(up);
    const auto& output = released.get();
    const auto inputs = std::span(output.compose_inputs, output.compose_input_count);
    const auto found = std::ranges::find_if(inputs, [](const auto& input) {
        return input.kind == WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE;
    });
    REQUIRE(found != inputs.end());
    check_bga(found->object_id == 501 && found->lane_id == 45);
    check_bga(!any_input_of({released, released}, WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK));
}

TEST_CASE("Toolbar BGA visibility is actionable while no lane is projected",
          "[frontend-state][event-harness][bga][bga-visibility]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    harness.view().compose.bga_lane_visible = false;
    harness.view().compose.bga_lane_count = 0;
    (void)harness.frame();
    const auto actions = click(harness, 303.0f, 122.0f);
    check_bga(actions.front().get().compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY);
    check_bga(actions.front().get().compose_input_count == 0);
    check_bga(actions.back().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Unrecognized key layout picker waits for a deliberate compatible profile choice",
          "[frontend-state][event-harness][lane-profile][compatible-layout]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    view.chart_id = 7;
    view.document_revision = 11;
    view.lane_profile_choice_count = 1;
    view.lane_profile_current = -1;
    std::snprintf(view.lane_profile_names[0], sizeof(view.lane_profile_names[0]), "osu!mania 7K");
    worm_ui_nuklear_compose_state().dock_layout.inspector_visible = true;
    const auto placeholder = open_initial_profile_picker(harness);
    REQUIRE(placeholder);
    const auto selected = scroll_to_profile_and_select(harness, "osu!mania 7K",
        placeholder->first, placeholder->second + 36.0f);
    REQUIRE(selected);
    check_profile_action(*selected, 0);
}

namespace {
struct timing_controls_fixture_t {
    nuklear_event_harness_t harness{1600, 1000};
    std::array<WormUiComposeWaveformOverviewRowView, 2> rows{{{0, 0.5f}, {1, 0.5f}}};
    timing_controls_fixture_t() {
        REQUIRE(harness.ready());
        auto& view = harness.view().compose;
        view.waveform_overview_rows = rows.data();
        view.waveform_overview_row_count = rows.size();
        view.waveform_overview_total_frames = 96000;
        view.waveform_overview_sample_rate = 48000;
        view.waveform_audio_offset_denominator = 1;
        view.waveform_overview_visible_end_fraction = 1;
        view.audio_offset_editable = true;
        worm_ui_nuklear_compose_state().dock_layout.inspector_visible = true;
        (void)harness.frame();
        const auto section = harness.drawn_text_center("Quantize / Snap");
        REQUIRE(section);
        (void)click(harness, section->first, section->second);
        (void)harness.frame();
    }
    auto reveal(std::string_view text) -> std::pair<float, float> {
        auto point = harness.drawn_text_center(text);
        for (int i = 0; i < 24 && !point; ++i) {
            (void)harness.frame(std::array{mouse_motion(1450, 700), mouse_wheel(-2, 1450, 700)});
            point = harness.drawn_text_center(text);
        }
        REQUIRE(point);
        return *point;
    }
    void edit_offset() {
        (void)harness.frame();
        const auto p = reveal("+0.000 s");
        auto down = mouse_button(true, p.first, p.second);
        auto up = mouse_button(false, p.first, p.second);
        down.button.clicks = 2;
        up.button.clicks = 2;
        (void)harness.frame(std::array{mouse_motion(p.first, p.second), down, up});
        check_bga(worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
        (void)harness.frame();
    }
};
auto timing_control_key(SDL_Keycode key, SDL_Scancode scancode) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    event.key.scancode = scancode;
    event.key.down = true;
    return event;
}
}

TEST_CASE("Timing controls leave the normal toolbar and appear during timing focus",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.inspector_visible = false;
    (void)f.harness.frame();
    check_bga(!f.harness.drawn_text_center("+0.000 s"));
    f.harness.view().compose.timing_focus = true;
    (void)f.harness.frame();
    check_bga(f.harness.drawn_text_center("+0.000 s").has_value());
    f.harness.view().compose.timing_focus = false;
    state.dock_layout.inspector_visible = true;
    const auto snap = f.reveal("BPM snap");
    check_bga(snap.first > 1200);
    check_bga(f.harness.drawn_text_center("Timing Settings...").has_value());
}

TEST_CASE("Offset editor commits one exact stamped action from either timing surface",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    const bool inspector = GENERATE(true, false);
    f.harness.view().compose.timing_focus = !inspector;
    f.edit_offset();
    check_bga(worm_ui_nuklear_compose_state().inline_offset.inline_offset_in_inspector == inspector);
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "-1/8";
    const auto typed = f.harness.frame(std::array{text});
    check_bga(typed.get().compose_input_count == 0);
    const auto committed = f.harness.frame(std::array{timing_control_key(SDLK_RETURN, SDL_SCANCODE_RETURN)});
    check_bga(committed.get().compose_action == WORM_UI_COMPOSE_ACTION_APPLY_AUDIO_OFFSET);
    check_bga(committed.get().compose_audio_offset_numerator == -1);
    check_bga(committed.get().compose_audio_offset_denominator == 8);
    check_bga(committed.get().compose_input_chart_id == 1);
    check_bga(committed.get().compose_input_document_revision == 1);
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
    check_bga(f.harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Hiding an offset editor owner cancels its draft",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    f.edit_offset();
    worm_ui_nuklear_compose_state().dock_layout.inspector_visible = false;
    check_bga(f.harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
    f.harness.view().compose.timing_focus = true;
    f.edit_offset();
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_in_inspector);
    f.harness.view().compose.timing_focus = false;
    check_bga(f.harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
}

TEST_CASE("Inspector offset nudges preserve frame precision and disabled locks",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    (void)f.reveal("BPM snap");
    const auto value = f.reveal("+0.000 s");
    const auto bounds = f.harness.drawn_text_bounds();
    const auto label = std::ranges::find_if(bounds, [](const auto& item) {
        return std::string_view{item.text} == "Audio offset · seconds";
    });
    REQUIRE(label != bounds.end());
    const float left = label->x, width = label->width;
    constexpr std::array<std::int64_t, 4> deltas{-480, -1, 1, 480};
    for (std::size_t index = 0; index < deltas.size(); ++index) {
        const auto actions = click(f.harness, left + width * (static_cast<float>(index) + 0.5f) / 4,
                                   value.second + 31);
        check_bga(actions.front().get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_NUDGE);
        check_bga(actions.front().get().audio_offset_drag_delta_frames == deltas[index]);
        check_bga(actions.front().get().audio_offset_drag_chart_id == 1);
        check_bga(actions.front().get().audio_offset_drag_document_revision == 1);
        check_bga(actions.back().get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
    }
    f.harness.view().compose.audio_offset_locked = true;
    (void)f.harness.frame();
    const auto disabled = click(f.harness, left + width * 0.875f, value.second + 31);
    check_bga(disabled.front().get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
}

TEST_CASE("Collapsing Timing cancels an inspector offset draft",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    f.edit_offset();
    const auto header = f.harness.drawn_text_center("Timing");
    REQUIRE(header);
    const auto collapsed = click(f.harness, header->first, header->second);
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
    check_bga(collapsed.front().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_bga(!f.harness.drawn_text_center("BPM snap"));
}

TEST_CASE("Switching into timing focus cancels the hidden inspector offset draft",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    f.edit_offset();
    f.harness.view().compose.timing_focus = true;
    const auto changed = f.harness.frame();
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
    check_bga(changed.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_bga(f.harness.drawn_text_center("+0.000 s").has_value());
}

TEST_CASE("Inspector BPM snap choice is shared with the timing-focus toolbar",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    (void)f.reveal("BPM snap");
    const auto combo = f.harness.drawn_text_center("1");
    REQUIRE(combo);
    (void)click(f.harness, combo->first, combo->second);
    const auto half = f.harness.drawn_text_center("0.5");
    REQUIRE(half);
    const auto selected = click(f.harness, half->first, half->second);
    check_bga(worm_ui_nuklear_compose_state().waveform_drag.beat_fit_snap_step_index == 2);
    check_bga(selected.front().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_bga(selected.front().get().compose_input_count == 0);
    check_bga(selected.front().get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
    f.harness.view().compose.timing_focus = true;
    (void)f.harness.frame();
    check_bga(f.harness.drawn_text_center("0.5").has_value());
    check_bga(worm_ui_nuklear_compose_state().waveform_drag.beat_fit_snap_step_index == 2);
}

TEST_CASE("Layers assist icon is independent of lock and sends one stamped session toggle",
          "[frontend-state][event-harness][layer-assist]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.inspector_visible = state.dock_layout.sounds_panel_visible = false;
    state.dock_layout.layers_panel_visible = true;
    WormUiComposeSemanticLayerView layer{.layer_id=7,.name="Lead",.red=255,
        .assigned_note_count=3,.visible=true,.locked=true};
    auto& view=harness.view().compose;
    view.semantic_layers=&layer;
    view.semantic_layer_count=view.semantic_layer_total_count=1;
    view.semantic_layer_assist_available=true;
    (void)harness.frame();
    const auto row=harness.drawn_text_center("Lead");
    REQUIRE(row);
    const auto toggled=click(harness,1176,row->second);
    check_bga(toggled.front().get().compose_action==WORM_UI_COMPOSE_ACTION_TOGGLE_LAYER_ASSIST_TICK);
    check_bga(toggled.front().get().compose_semantic_layer_id==7);
    check_bga(toggled.front().get().compose_input_project_id==1);
    check_bga(toggled.front().get().compose_input_chart_id==1);
    check_bga(toggled.front().get().compose_input_document_revision==1);
    check_bga(toggled.front().get().compose_input_count==0);
    check_bga(toggled.back().get().compose_action==WORM_UI_COMPOSE_ACTION_NONE);
    layer.assist_tick_enabled=true;
    (void)harness.frame(std::array{mouse_motion(1176,row->second)});
    check_bga(harness.drawn_text().find("Disable layer assist ticks")!=std::string::npos);
    view.semantic_layer_assist_available=false;
    (void)harness.frame();
    check_bga(click(harness,1176,row->second).front().get().compose_action==WORM_UI_COMPOSE_ACTION_NONE);
    view.semantic_layer_assist_available=true;
    harness.view().close_confirmation.visible=true;
    (void)harness.frame();
    check_bga(click(harness,1176,row->second).front().get().compose_action==WORM_UI_COMPOSE_ACTION_NONE);
}
