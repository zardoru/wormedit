#include "sys/storyboard/storyboard_runtime.h"
#include "sys/app/application_command_state.h"
#include "nuklear_event_harness.h"
#include "ui/frontend/storyboard_assets.h"
#include "ui/nuklear/storyboard/assets.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <cmath>

using namespace wormedit;
using namespace wormedit::ui;
namespace {
auto assets_intent(const std::vector<workspace_action_snapshot_t>& frames) -> storyboard_action_t {
    for (const auto& frame : frames) {
        const auto& action = frame.get().storyboard;
        if (action.kind == storyboard_action_kind_t::assets &&
            action.assets.kind != storyboard_assets_action_kind_t::set_thumbnail_demand) return action;
    }
    return {};
}
void show_assets(nuklear_event_harness_t& harness) {
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
    view.tab_heading = "Storyboard";
    view.compose.visible = false;
    auto& root = view.storyboard;
    root.visible = true; root.project = view.project_id; root.revision = view.document_revision;
    root.session_generation = 7;
    root.storyboards = {{.id = storyboard_id_t{11}, .name = "Assets fixture", .kind = storyboard_kind_t::osbx}};
    root.refill_state({.selection = {.storyboard = storyboard_id_t{11}}});
    root.canvas.available = true; root.canvas.prepared_generation = 13;
    root.assets.editable = true; root.assets.import_available = true;
    root.assets.images = {{.id = "image-a", .file = "a.bmp", .caption = "Unused", .thumbnail_requested = true}};
    root.assets.resources = {{.resource = resource_id_t{21}, .name = "Song image", .file = "b.bmp",
        .caption = "Available", .available = true, .thumbnail_requested = true}};
    (void)harness.frame();
    const auto tab = harness.drawn_text_center("Assets");
    REQUIRE(tab);
    (void)test::click_at(harness, tab->first, tab->second);
    (void)harness.frame();
}
void check_asset_capture(const storyboard_action_t& action, storyboard_assets_action_kind_t kind) {
    CHECK(action.kind == storyboard_action_kind_t::assets);
    CHECK(action.assets.kind == kind);
    CHECK(action.storyboard == storyboard_id_t{11});
    CHECK(action.session_generation == 7);
    CHECK(action.prepared_generation == 13);
}
auto press_asset(nuklear_event_harness_t& harness) -> std::pair<float, float> {
    const auto row = harness.drawn_text_center("a.bmp");
    INFO(harness.drawn_text());
    REQUIRE(row);
    REQUIRE(harness.drawn_text_center("Unused"));
    (void)harness.frame(std::array{test::mouse_motion(row->first, row->second),
        test::mouse_button(true, row->first, row->second)});
    REQUIRE(storyboard_assets_drag_active());
    return *row;
}
auto canvas_point(nuklear_event_harness_t& harness) -> std::pair<float, float> {
    const auto fit = harness.drawn_text_center("Fit");
    REQUIRE(fit);
    return {fit->first, fit->second + 75};
}
void interrupt_asset_drag(nuklear_event_harness_t& harness, int kind) {
    if (kind == 0) {
        SDL_Event escape{}; escape.type = SDL_EVENT_KEY_DOWN;
        escape.key.down = true; escape.key.key = SDLK_ESCAPE; escape.key.scancode = SDL_SCANCODE_ESCAPE;
        (void)harness.frame(std::span{&escape, 1});
        escape.type = SDL_EVENT_KEY_UP; escape.key.down = false;
        (void)harness.frame(std::span{&escape, 1});
    }
    if (kind == 1) {
        SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        (void)harness.frame(std::span{&focus, 1});
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        (void)harness.frame(std::span{&focus, 1});
    }
    if (kind == 2) {
        harness.view().options.visible = true; (void)harness.frame();
        harness.view().options.visible = false;
    }
    if (kind == 3) {
        harness.view().storyboard.visible = false; (void)harness.frame();
        harness.view().storyboard.visible = true;
    }
}
void check_completed_asset_drag(nuklear_event_harness_t& harness, const storyboard_action_t& action) {
    CHECK(action.assets.image == "image-a");
    CHECK(std::isfinite(action.assets.position.x));
    CHECK(std::isfinite(action.assets.position.y));
    CHECK_FALSE(storyboard_assets_drag_active());
    CHECK(harness.frame().get().storyboard.kind == storyboard_action_kind_t::none);
}
} // namespace

TEST_CASE("Storyboard asset buttons retain stable source identities and captured context",
          "[frontend-state][event-harness][storyboard-assets]") {
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    show_assets(harness);
    const auto sprite = assets_intent(harness.click_widget("Add sprite from storyboard image"));
    check_asset_capture(sprite, storyboard_assets_action_kind_t::add_sprite);
    CHECK(sprite.assets.image == "image-a");
    const auto resource = assets_intent(harness.click_widget("Add song image to storyboard"));
    check_asset_capture(resource, storyboard_assets_action_kind_t::add_image);
    CHECK(resource.assets.resource == resource_id_t{21});
    const auto imported = assets_intent(harness.click_widget("Import storyboard image"));
    check_asset_capture(imported, storyboard_assets_action_kind_t::import_images);
    CHECK(imported.assets.paths.empty());
}
TEST_CASE("Storyboard asset drag emits one captured sprite intent at the canvas drop point",
          "[frontend-state][event-harness][storyboard-assets]") {
    const auto width = GENERATE(800, 1280);
    nuklear_event_harness_t harness{width, 720}; REQUIRE(harness.ready());
    show_assets(harness);
    (void)press_asset(harness);
    const auto point = canvas_point(harness);
    (void)harness.frame(std::array{test::mouse_motion(point.first, point.second)});
    const auto released = harness.frame(std::array{test::mouse_button(false, point.first, point.second)});
    const auto action = released.get().storyboard;
    check_asset_capture(action, storyboard_assets_action_kind_t::add_sprite);
    check_completed_asset_drag(harness, action);
}
TEST_CASE("Storyboard asset drag is discarded after captured identity changes",
          "[frontend-state][event-harness][storyboard-assets]") {
    const auto change = GENERATE(0, 1, 2);
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    show_assets(harness);
    (void)press_asset(harness);
    auto& root = harness.view().storyboard;
    if (change == 0) ++root.revision;
    if (change == 1) ++root.session_generation;
    if (change == 2) ++root.canvas.prepared_generation;
    const auto point = canvas_point(harness);
    const auto released = harness.frame(std::array{test::mouse_motion(point.first, point.second),
        test::mouse_button(false, point.first, point.second)});
    CHECK_FALSE(storyboard_assets_drag_active());
    CHECK_FALSE((released.get().storyboard.kind == storyboard_action_kind_t::assets &&
        released.get().storyboard.assets.kind == storyboard_assets_action_kind_t::add_sprite));
}
TEST_CASE("Storyboard asset drag release outside canvas never emits an edit",
          "[frontend-state][event-harness][storyboard-assets]") {
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    show_assets(harness);
    const auto point = press_asset(harness);
    const auto released = harness.frame(std::array{test::mouse_button(false, point.first, point.second)});
    CHECK_FALSE(storyboard_assets_drag_active());
    CHECK_FALSE((released.get().storyboard.kind == storyboard_action_kind_t::assets &&
        released.get().storyboard.assets.kind == storyboard_assets_action_kind_t::add_sprite));
}
TEST_CASE("Storyboard asset drags cancel across Escape focus modal and hidden panel returns",
          "[frontend-state][event-harness][storyboard-assets]") {
    const auto interruption = GENERATE(0, 1, 2, 3);
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    show_assets(harness);
    (void)press_asset(harness);
    const auto point = canvas_point(harness);
    interrupt_asset_drag(harness, interruption);
    CHECK_FALSE(storyboard_assets_drag_active());
    const auto released = harness.frame(std::array{test::mouse_motion(point.first, point.second),
        test::mouse_button(false, point.first, point.second)});
    CHECK_FALSE((released.get().storyboard.kind == storyboard_action_kind_t::assets &&
        released.get().storyboard.assets.kind == storyboard_assets_action_kind_t::add_sprite));
}

namespace {
void check_runtime_sprite(const document_history_t& history, const storyboard_action_t& action,
    const sys::storyboard_runtime_t& runtime) {
    CHECK(history.undo.size() == 1);
    const auto& scene = *history.document.storyboards.front().osbx;
    REQUIRE(scene.nodes.size() == 1);
    CHECK(runtime.state().selection.nodes == std::vector<std::string>{scene.nodes.front().id});
    CHECK(scene.nodes.front().image == "image-a");
    REQUIRE(scene.nodes.front().ops.size() == 1);
    CHECK(scene.nodes.front().ops.front().track.default_value ==
        std::vector<double>{action.assets.position.x, action.assets.position.y});
}
void bind_assets_runtime(nuklear_event_harness_t& harness, sys::application_command_state_t& application,
    const sys::document_edit_inputs_t& inputs) {
    auto& root = harness.view().storyboard;
    auto& runtime = application.project.storyboard;
    runtime.refill(root, inputs.history.document, 7);
    REQUIRE(runtime.dispatch(storyboard_action_t::from(root, storyboard_action_kind_t::select, storyboard_id_t{11}),
        inputs, application, nullptr, false));
    runtime.refill(root, inputs.history.document, 7);
    root.canvas.prepared_generation = 0;
    (void)harness.frame();
}
}
TEST_CASE("Storyboard asset canvas drop enters the production runtime as exactly one Undo and refuses used removal",
          "[frontend-state][event-harness][storyboard-assets][runtime]") {
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    show_assets(harness);
    auto& root = harness.view().storyboard;
    auto history = document_history_t::from(song_document_t{.id = root.project, .revision = root.revision,
        .storyboards = {{.id = storyboard_id_t{11}, .osbx = osbx_document_t{
            .images = {{.id = "image-a", .file = "a.bmp"}}}}}});
    const auto bound = workspace_controller_t::from(history); REQUIRE(bound.value);
    auto workspace = *bound.value; workspace.active_tab = workspace_tab_t::storyboard;
    sys::application_command_state_t application;
    notification_center_t notifications; notification_sink_t sink{notifications};
    const sys::document_edit_inputs_t inputs{.history = history, .workspace = workspace, .notifications = sink};
    bind_assets_runtime(harness, application, inputs);
    (void)press_asset(harness);
    const auto point = canvas_point(harness);
    const auto released = harness.frame(std::array{test::mouse_motion(point.first, point.second),
        test::mouse_button(false, point.first, point.second)});
    const auto action = released.get().storyboard;
    auto& runtime = application.project.storyboard;
    REQUIRE(runtime.dispatch(action, inputs, application, nullptr, false));
    check_runtime_sprite(history, action, runtime);
    root.revision = history.document.revision;
    const auto remove = assets_intent(harness.click_widget("Remove unused storyboard image"));
    REQUIRE(runtime.dispatch(remove, inputs, application, nullptr, false));
    CHECK(history.undo.size() == 1);
    CHECK(history.document.storyboards.front().osbx->images.size() == 1);
}

TEST_CASE("Storyboard asset production runtime refuses stale busy and generated-board edits",
          "[frontend-state][event-harness][storyboard-assets][runtime][negative]") {
    const auto refusal = GENERATE(0, 1, 2);
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    show_assets(harness);
    auto& root = harness.view().storyboard;
    auto history = document_history_t::from(song_document_t{.id = root.project, .revision = root.revision,
        .storyboards = {{.id = storyboard_id_t{11}, .osbx = osbx_document_t{
            .images = {{.id = "image-a", .file = "a.bmp"}}}}}});
    const auto bound = workspace_controller_t::from(history); REQUIRE(bound.value);
    auto workspace = *bound.value; workspace.active_tab = workspace_tab_t::storyboard;
    sys::application_command_state_t application;
    notification_center_t notifications; notification_sink_t sink{notifications};
    const sys::document_edit_inputs_t inputs{.history = history, .workspace = workspace, .notifications = sink};
    bind_assets_runtime(harness, application, inputs);
    auto action = assets_intent(harness.click_widget("Add sprite from storyboard image"));
    if (refusal == 0) ++action.revision;
    if (refusal == 2) history.document.storyboards.front().owning_generator_recipe = recipe_id_t{99};
    REQUIRE(application.project.storyboard.dispatch(action, inputs, application, nullptr, refusal == 1));
    CHECK(history.undo.empty());
    CHECK(history.document.storyboards.front().osbx->nodes.empty());
}

namespace {
auto begin_timeline_with_assets(nuklear_event_harness_t& harness) -> storyboard_action_t {
    auto& root = harness.view().storyboard;
    root.state.viewport.timeline_pixels_per_ms = 0.5;
    root.timeline.rows = {{.track = {.node = "node", .kind = osbx_track_kind_t::opacity},
        .label = "Asset timeline", .editable = true,
        .keys = {{.id = "key-a", .time_ms = 100, .position = 50, .easing_label = "Linear"}}}};
    (void)harness.frame();
    const auto label = harness.drawn_text_center("Asset timeline"); REQUIRE(label);
    const auto x = label->first + 130, y = label->second;
    (void)harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)});
    return harness.frame(std::array{test::mouse_motion(x + 20, y)}).get().storyboard;
}
}
TEST_CASE("Hidden assets thumbnail-demand clearing yields to the admitted timeline terminal cancel",
          "[frontend-state][event-harness][storyboard-assets][storyboard-timeline][cancel]") {
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    show_assets(harness);
    const auto begin = begin_timeline_with_assets(harness);
    REQUIRE(begin.is_document_gesture_begin());
    harness.view().storyboard.visible = false; harness.view().compose.visible = true;
    const auto terminal = harness.frame().get().storyboard;
    REQUIRE(terminal.is_document_gesture_cancel());
    CHECK(terminal.kind == storyboard_action_kind_t::timeline);
    CHECK(terminal.storyboard == begin.storyboard);
    CHECK(terminal.timeline.keys == begin.timeline.keys);
}
