#include "nuklear_event_harness.h"
#include "sys/storyboard/storyboard_runtime.h"
#include "sys/app/application_command_state.h"
#include <wormedit/core/osbx_document.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <algorithm>
using namespace wormedit;
using namespace wormedit::ui;
namespace {
auto show_canvas(nuklear_event_harness_t& harness) -> std::pair<float, float> {
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
    view.tab_heading = "Storyboard";
    view.compose.visible = false;
    auto& storyboard = view.storyboard;
    storyboard.visible = true;
    storyboard.project = view.project_id;
    storyboard.revision = view.document_revision;
    storyboard.session_generation = 7;
    storyboard.storyboards = {{.id = storyboard_id_t{11}, .name = "Canvas", .kind = storyboard_kind_t::osbx}};
    storyboard.refill_state({.selection = {.storyboard = storyboard_id_t{11}}});
    storyboard.canvas.available = true;
    storyboard.canvas.prepared_generation = 13;
    storyboard.canvas.sprites = {
        {.storyboard = storyboard_id_t{11}, .node_id = "back", .transform = {1, 0, 0, 1, 320, 240},
            .origin_x = 0.5, .origin_y = 0.5, .opacity = 1, .image_ready = true, .image_width = 6400, .image_height = 4800},
        {.storyboard = storyboard_id_t{11}, .node_id = "front", .transform = {-1, 0.3, 0.4, 1, 320, 240},
            .origin_x = 0.5, .origin_y = 0.5, .opacity = 1, .image_ready = true, .image_width = 6400, .image_height = 4800}};
    (void)harness.frame();
    const auto fit = harness.drawn_text_center("Fit");
    REQUIRE(fit);
    return {fit->first, fit->second + 75};
}
auto intent(const std::vector<workspace_action_snapshot_t>& frames) -> storyboard_action_t {
    for (const auto& frame : frames) if (frame.get().storyboard.kind != storyboard_action_kind_t::none) return frame.get().storyboard;
    return {};
}

void check_canvas_selection_capture(const storyboard_action_t& action) {
    CHECK(action.kind == storyboard_action_kind_t::select_nodes);
    CHECK(action.nodes == std::vector<std::string>{"front"});
    CHECK(action.storyboard == storyboard_id_t{11});
    CHECK(action.session_generation == 7);
    CHECK(action.prepared_generation == 13);
}

void check_canvas_fit_intent(nuklear_event_harness_t& harness) {
    const auto fit = harness.drawn_text_center("Fit");
    REQUIRE(fit);
    const auto action = intent(test::click_at(harness, fit->first, fit->second));
    CHECK(action.kind == storyboard_action_kind_t::set_viewport);
    CHECK(action.viewport.zoom == 1);
    CHECK(action.viewport.pan_x == 0);
    CHECK(action.viewport.pan_y == 0);
}

void check_captured_pan_intent(const workspace_action_snapshot_t& result) {
    CHECK(result.get().storyboard.kind == storyboard_action_kind_t::set_viewport);
    CHECK(result.get().storyboard.viewport.pan_x > 0);
    CHECK(result.get().storyboard.viewport.pan_y > 0);
    CHECK(result.get().storyboard.nodes.empty());
    CHECK(result.get().storyboard.storyboard == storyboard_id_t{11});
}

auto select_canvas_node(nuklear_event_harness_t& harness, std::pair<float, float> point,
    sys::application_command_state_t& application, const sys::document_edit_inputs_t& inputs)
    -> storyboard_action_t {
    auto& runtime = application.project.storyboard;
    auto& view = harness.view().storyboard;
    auto& history = inputs.history;
    REQUIRE(runtime.dispatch(storyboard_action_t::from(view, storyboard_action_kind_t::select, storyboard_id_t{11}), inputs, application, nullptr, false));
    runtime.refill(view, history.document, 7);
    view.canvas.prepared_generation = 0;
    const auto selected = intent(test::click_at(harness, point.first, point.second));
    REQUIRE(selected.kind == storyboard_action_kind_t::select_nodes);
    REQUIRE(runtime.dispatch(selected, inputs, application, nullptr, false));
    CHECK(runtime.state().selection.nodes == std::vector<std::string>{"front"});
    return selected;
}
void check_rejected_canvas_selections(sys::application_command_state_t& application,
    const sys::document_edit_inputs_t& inputs, const storyboard_action_t& selected) {
    auto& runtime = application.project.storyboard;
    auto stale = selected;
    stale.nodes = {"back"}; ++stale.prepared_generation;
    REQUIRE(runtime.dispatch(stale, inputs, application, nullptr, false));
    CHECK(runtime.state().selection.nodes == std::vector<std::string>{"front"});
    stale = selected; stale.nodes = {"missing"};
    REQUIRE(runtime.dispatch(stale, inputs, application, nullptr, false));
    CHECK(runtime.state().selection.nodes == std::vector<std::string>{"front"});
}
void check_canvas_viewport_without_history(sys::application_command_state_t& application,
    const sys::document_edit_inputs_t& inputs, const storyboard_view_t& view) {
    auto& runtime = application.project.storyboard;
    auto& history = inputs.history;
    auto viewport = storyboard_action_t::from(view, storyboard_action_kind_t::set_viewport, storyboard_id_t{11});
    viewport.viewport.zoom = 2; viewport.viewport.pan_x = 17;
    REQUIRE(runtime.dispatch(viewport, inputs, application, nullptr, false));
    CHECK(runtime.state().viewport.zoom == 2);
    CHECK(runtime.state().viewport.pan_x == 17);
    CHECK(history.undo.empty());
    CHECK(history.document.revision == view.revision);
}
} // namespace

TEST_CASE("Storyboard canvas toolbar labels remain readable at narrow and wide widths",
          "[frontend-state][event-harness][storyboard-canvas]") {
    const auto width = GENERATE(800, 1280);
    nuklear_event_harness_t harness{width, 720};
    REQUIRE(harness.ready());
    (void)show_canvas(harness);
    INFO(harness.drawn_text());
    CHECK(harness.drawn_text_center("Canvas  100% · 2 sprites"));
    CHECK(harness.drawn_text_center("16:9 safe"));
    CHECK(harness.drawn_text_center("Theme"));
}

TEST_CASE("Storyboard canvas click selects topmost stable ID with owning captures", "[frontend-state][event-harness][storyboard-canvas]") {
    const auto width = GENERATE(800, 1280);
    nuklear_event_harness_t harness{width, 720};
    REQUIRE(harness.ready());
    const auto point = show_canvas(harness);
    const auto action = intent(test::click_at(harness, point.first, point.second));
    check_canvas_selection_capture(action);
    harness.view().storyboard.canvas.sprites.clear();
    (void)harness.frame();
    CHECK(action.nodes == std::vector<std::string>{"front"});
}
TEST_CASE("Storyboard canvas Alt click cycles and Shift click toggles canonical selection", "[frontend-state][event-harness][storyboard-canvas]") {
    const bool shift = GENERATE(false, true);
    nuklear_event_harness_t harness{1280, 720};
    REQUIRE(harness.ready());
    const auto point = show_canvas(harness);
    harness.view().storyboard.state.selection.nodes = {"front"};
    const auto down = test::mouse_button(true, point.first, point.second), up = test::mouse_button(false, point.first, point.second);
    const auto modifier = shift ? SDL_KMOD_SHIFT : SDL_KMOD_ALT;
    (void)harness.frame(std::array{test::mouse_motion(point.first, point.second), down}, modifier);
    const auto result = harness.frame(std::array{test::mouse_motion(point.first, point.second), up}, modifier);
    CHECK(result.get().storyboard.kind == storyboard_action_kind_t::select_nodes);
    CHECK(result.get().storyboard.nodes == (shift ? std::vector<std::string>{} : std::vector<std::string>{"back"}));
}
TEST_CASE("Storyboard canvas retires pointer captures after revision session or prepared changes", "[frontend-state][event-harness][storyboard-canvas]") {
    const auto change = GENERATE(0, 1, 2, 3);
    nuklear_event_harness_t harness{1280, 720};
    REQUIRE(harness.ready());
    const auto point = show_canvas(harness);
    const auto down = test::mouse_button(true, point.first, point.second), up = test::mouse_button(false, point.first, point.second);
    (void)harness.frame(std::array{test::mouse_motion(point.first, point.second), down});
    auto& view = harness.view().storyboard;
    if (change == 0) ++view.revision;
    if (change == 1) ++view.session_generation;
    if (change == 2) ++view.canvas.prepared_generation;
    if (change == 3) { view.visible = false; (void)harness.frame(); view.visible = true; }
    CHECK(harness.frame(std::array{test::mouse_motion(point.first, point.second), up}).get().storyboard.kind == storyboard_action_kind_t::none);
}
TEST_CASE("Storyboard canvas wheel anchors zoom and Fit emits view-only intent", "[frontend-state][event-harness][storyboard-canvas]") {
    nuklear_event_harness_t harness{1280, 720};
    REQUIRE(harness.ready());
    const auto point = show_canvas(harness);
    const auto motion = test::mouse_motion(point.first, point.second);
    (void)harness.frame(std::span{&motion, 1});
    SDL_Event wheel{}; wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y = 1;
    const auto zoom = harness.frame(std::span{&wheel, 1});
    CHECK(zoom.get().storyboard.kind == storyboard_action_kind_t::set_viewport);
    CHECK(zoom.get().storyboard.viewport.zoom > 1);
    harness.view().storyboard.state.viewport = zoom.get().storyboard.viewport;
    (void)harness.frame();
    check_canvas_fit_intent(harness);
}

TEST_CASE("Storyboard canvas selection and viewport dispatch use canonical project state without Undo",
          "[frontend-state][event-harness][storyboard-canvas]") {
    nuklear_event_harness_t harness{1280, 720};
    REQUIRE(harness.ready());
    const auto point = show_canvas(harness);
    auto& view = harness.view().storyboard;
    auto history = document_history_t::from(song_document_t{.id = view.project, .revision = view.revision,
        .storyboards = {{.id = storyboard_id_t{11}, .osbx = osbx_document_t{
            .images = {{.id = "image", .file = "sprite.png"}},
            .nodes = {{.id = "back", .kind = osbx_node_kind_t::sprite, .image = "image"},
                      {.id = "front", .kind = osbx_node_kind_t::sprite, .image = "image"}}}}}});
    const auto bound = workspace_controller_t::from(history);
    REQUIRE(bound.value);
    auto workspace = *bound.value;
    workspace.active_tab = workspace_tab_t::storyboard;
    sys::application_command_state_t application;
    notification_center_t notifications;
    notification_sink_t sink{notifications};
    auto& runtime = application.project.storyboard;
    runtime.refill(view, history.document, 7);
    const sys::document_edit_inputs_t inputs{.history = history, .workspace = workspace, .notifications = sink};
    const auto selected = select_canvas_node(harness, point, application, inputs);
    check_rejected_canvas_selections(application, inputs, selected);
    check_canvas_viewport_without_history(application, inputs, view);
    runtime.refill(view, history.document, 8);
    REQUIRE(runtime.dispatch(selected, inputs, application, nullptr, false));
    CHECK(runtime.state().selection.nodes.empty());
}

TEST_CASE("Storyboard canvas pointer release is rejected after focus loss or modal occlusion",
          "[frontend-state][event-harness][storyboard-canvas]") {
    const bool modal = GENERATE(false, true);
    nuklear_event_harness_t harness{1280, 720};
    REQUIRE(harness.ready());
    const auto point = show_canvas(harness);
    const auto down = test::mouse_button(true, point.first, point.second), up = test::mouse_button(false, point.first, point.second);
    (void)harness.frame(std::array{test::mouse_motion(point.first, point.second), down});
    if (modal) {
        harness.view().options.visible = true;
        (void)harness.frame();
        harness.view().options.visible = false;
    } else {
        SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        (void)harness.frame(std::span{&focus, 1});
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        (void)harness.frame(std::span{&focus, 1});
    }
    CHECK(harness.frame(std::array{test::mouse_motion(point.first, point.second), up}).get().storyboard.kind == storyboard_action_kind_t::none);
}
TEST_CASE("Storyboard canvas middle drag emits only captured viewport state",
          "[frontend-state][event-harness][storyboard-canvas]") {
    nuklear_event_harness_t harness{1280, 720};
    REQUIRE(harness.ready());
    const auto point = show_canvas(harness);
    auto down = test::mouse_button(true, point.first, point.second);
    down.button.button = SDL_BUTTON_MIDDLE;
    auto up = test::mouse_button(false, point.first + 20, point.second + 10);
    up.button.button = SDL_BUTTON_MIDDLE;
    (void)harness.frame(std::array{test::mouse_motion(point.first, point.second), down});
    const auto motion = test::mouse_motion(point.first + 20, point.second + 10);
    CHECK(harness.frame(std::span{&motion, 1}).get().storyboard.kind == storyboard_action_kind_t::none);
    const auto result = harness.frame(std::span{&up, 1});
    check_captured_pan_intent(result);
}
