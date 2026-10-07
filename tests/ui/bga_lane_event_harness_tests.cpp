#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>

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
    state.inspector_visible = false;
    state.sounds_panel_visible = false;
    state.layers_panel_visible = false;
    state.bga_panel_visible = false;
}

auto click(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    const auto down = std::array{mouse_motion(x, y), mouse_button(true, x, y)};
    const auto up = std::array{mouse_motion(x, y), mouse_button(false, x, y)};
    return {harness.frame(down), harness.frame(up)};
}

auto has_input(const workspace_action_snapshot_t& action, WormUiComposeInputKind kind,
               std::uint64_t object_id) -> bool {
    const auto& value = action.get();
    return std::ranges::any_of(std::span(value.compose_inputs, value.compose_input_count),
        [&](const auto& input) { return input.kind == kind && input.object_id == object_id; });
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
    CHECK_FALSE(worm_ui_nuklear_compose_state().bga_stack_open);
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
    CHECK(worm_ui_nuklear_compose_state().bga_event_move_active);
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
    check_bga(!worm_ui_nuklear_compose_state().bga_stack_open);
    check_bga(worm_ui_nuklear_compose_state().bga_stack_release_pending);
    check_bga(pressed.get().compose_input_count == 0);
    check_bga(!harness.drawn_text_center("Choose the event to select or edit").has_value());

    const auto up = std::array{mouse_motion(outside.first, outside.second),
                               mouse_button(false, outside.first, outside.second)};
    const auto released = harness.frame(up);
    check_bga(released.get().compose_input_count == 0);
    check_bga(worm_ui_nuklear_compose_state().bga_stack_release_pending);
    const auto quiet = harness.frame();
    check_bga(quiet.get().compose_input_count == 0);
    check_bga(!worm_ui_nuklear_compose_state().bga_stack_release_pending);
}

void verify_bga_stack_escape_dismissal(nuklear_event_harness_t& harness) {
    const auto escape = escape_key();
    const auto dismissed = harness.frame(std::span{&escape, 1});
    check_bga(!worm_ui_nuklear_compose_state().bga_stack_open);
    check_bga(worm_ui_nuklear_compose_state().bga_stack_release_pending);
    check_bga(dismissed.get().compose_input_count == 0);
    check_bga(!harness.drawn_text_center("Choose the event to select or edit").has_value());

    auto key_up = escape;
    key_up.type = SDL_EVENT_KEY_UP;
    key_up.key.type = SDL_EVENT_KEY_UP;
    key_up.key.down = false;
    const auto released = harness.frame(std::span{&key_up, 1});
    check_bga(released.get().compose_input_count == 0);
    check_bga(worm_ui_nuklear_compose_state().bga_stack_release_pending);
    const auto quiet = harness.frame();
    check_bga(quiet.get().compose_input_count == 0);
    check_bga(!worm_ui_nuklear_compose_state().bga_stack_release_pending);
}
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
    view.bgm_lanes = &bgm_lane;
    view.bgm_lane_count = 1;
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bga;
    state.armed_resource_id = 91;
    state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::place;
    const auto field = canvas_point(harness,
        (view.playable_field_left_pixels + view.playable_field_right_pixels) * 0.5,
        view.viewport_height_pixels * 0.5);
    const auto field_click = click(harness, field.first, field.second);
    const auto bgm_center = rect_center(harness, bgm_lane.bounds);
    const auto bgm_click = click(harness, bgm_center.first, bgm_center.second);
    const auto bga_center = rect_center(harness, view.bga_lane_bounds);
    const auto bga_click = click(harness, bga_center.first, bga_center.second);
    CHECK_FALSE(has_resource_place(field_click));
    CHECK_FALSE(has_resource_place(bgm_click));
    CHECK(has_resource_place(bga_click));
}

TEST_CASE("BGA event hit testing maps through the visible inspector and Sounds docks",
          "[frontend-state][event-harness][bga-lane]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    auto& compose_state = worm_ui_nuklear_compose_state();
    compose_state.inspector_visible = true;
    compose_state.sounds_panel_visible = true;
    compose_state.layers_panel_visible = false;
    compose_state.bga_panel_visible = false;
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
    INFO("BGA stack open=" << worm_ui_nuklear_compose_state().bga_stack_open
         << " release pending=" << worm_ui_nuklear_compose_state().bga_stack_release_pending
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
    WormUiComposeBgmLaneView lanes[2]{{.bounds = {604, 0, 640, 600}, .source_lane_id = 44},
                                      {.bounds = {644, 0, 680, 600}, .source_lane_id = 45}};
    WormUiComposeBgmEventView marker{.bounds = {604, 20, 640, 36}, .event_id = 501, .source_lane_id = 44,
                                     .bmson_slice = true};
    stem_lane_fixture_t() {
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
    CHECK(has_input(plain, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE, 501));

    const auto down = std::array{mouse_motion(empty.first, empty.second), mouse_button(true, empty.first, empty.second)};
    const auto up = std::array{mouse_motion(empty.first, empty.second), mouse_button(false, empty.first, empty.second)};
    const std::array shifted{f.harness.frame(down, SDL_KMOD_LSHIFT), f.harness.frame(up, SDL_KMOD_LSHIFT)};
    CHECK(has_input(shifted, WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART, 501));
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
    CHECK_FALSE(any_input_of({dragging, dragging}, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE));
    const std::array up{mouse_motion(to.first, to.second), mouse_button(false, to.first, to.second)};
    const auto released = f.harness.frame(up);
    const auto& inputs = released.get();
    const auto drop = std::ranges::find_if(std::span(inputs.compose_inputs, inputs.compose_input_count),
        [](const auto& input) { return input.kind == WORM_UI_COMPOSE_INPUT_NOTE_MOVE_TO_BGM; });
    REQUIRE(drop != std::span(inputs.compose_inputs, inputs.compose_input_count).end());
    CHECK(drop->lane_id == 45);
    CHECK_FALSE(any_input_of({released, released}, WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE));
}

TEST_CASE("dragging a selected BGM marker onto a playfield lane asks to move it into that lane",
          "[frontend-state][event-harness][bgm-to-lane]") {
    stem_lane_fixture_t f;
    auto& view = f.harness.view().compose;
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
