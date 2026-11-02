#include "nuklear_event_harness.h"
#include "../support/document_assertions.h"
#include "sys/app/application_command_state.h"
#include "sys/storyboard/storyboard_timeline_view.h"
#include "ui/frontend/storyboard_timeline.h"
#include <wormedit/core/osbx_lua_link.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>

namespace {
using namespace wormedit;
using namespace wormedit::ui;
auto timeline_key(bool down, SDL_Keycode code, SDL_Scancode scan) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.down = down; event.key.key = code; event.key.scancode = scan;
    return event;
}
auto show_timeline(nuklear_event_harness_t& harness) -> std::pair<float, float> {
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
    view.compose.visible = false;
    view.storyboard = {.visible = true, .project = view.project_id, .revision = view.document_revision,
        .session_generation = 7,
        .storyboards = {{.id = storyboard_id_t{11}, .name = "Scene", .kind = storyboard_kind_t::osbx}}};
    auto& root = view.storyboard;
    root.refill_state({.selection = {.storyboard = storyboard_id_t{11}, .nodes = {"node"}},
        .viewport = {.timeline_pixels_per_ms = 0.5}});
    root.canvas.prepared_generation = 13;
    root.timeline.rows = {{.track = {.node = "node", .kind = osbx_track_kind_t::opacity},
        .label = "Timeline opacity", .editable = true, .keys = {
            {.id = "key-a", .time_ms = 100, .position = 50, .easing_label = "Linear"},
            {.id = "key-b", .time_ms = 400, .position = 200, .easing_label = "Linear"}}}};
    root.timeline.time = {.milliseconds = 150};
    (void)harness.frame();
    const auto label = harness.drawn_text_center("Timeline opacity");
    INFO(harness.drawn_text());
    REQUIRE(label);
    // Labels span 160 px; marker 100 ms lies 50 px into the time lane at 0.5 px/ms.
    return {label->first + 130, label->second};
}

void check_marker_key_identity(const storyboard_action_t& action) {
    CHECK(action.kind == storyboard_action_kind_t::timeline);
    CHECK(action.timeline.kind == storyboard_timeline_action_kind_t::select_keys);
    REQUIRE(action.timeline.keys.size() == 1);
    CHECK(action.timeline.keys[0].key == "key-a");
    CHECK(action.timeline.keys[0].track.node == "node");
}

void check_ruler_seek(const storyboard_action_t& action) {
    CHECK(action.timeline.kind == storyboard_timeline_action_kind_t::seek);
    CHECK(action.timeline.pointer_time_ms == Catch::Approx(100));
}

void check_drag_begin(const workspace_action_snapshot_t& begin) {
    CHECK(begin.get().storyboard.timeline.phase == storyboard_inspector_phase_t::begin);
    CHECK(begin.get().storyboard.timeline.anchor_time_ms == 100);
    CHECK(begin.get().storyboard.timeline.pointer_delta_ms == 0);
}

void check_shift_drag_revision(const workspace_action_snapshot_t& revise) {
    CHECK(revise.get().storyboard.timeline.phase == storyboard_inspector_phase_t::revise);
    CHECK(revise.get().storyboard.timeline.shift);
    CHECK(revise.get().storyboard.timeline.pointer_delta_ms == Catch::Approx(50));
}

void check_drag_applied_key(const workspace_action_snapshot_t& apply) {
    CHECK(apply.get().storyboard.timeline.phase == storyboard_inspector_phase_t::apply);
    REQUIRE(apply.get().storyboard.timeline.keys.size() == 1);
    CHECK(apply.get().storyboard.timeline.keys[0].key == "key-a");
}

void check_add_key_shortcut(const workspace_action_snapshot_t& add) {
    CHECK(add.get().storyboard.timeline.kind == storyboard_timeline_action_kind_t::add_key);
    CHECK(add.get().storyboard.timeline.track.node == "node");
    CHECK(add.get().shortcut_events.empty());
}

void check_delete_key_shortcut(const workspace_action_snapshot_t& deleted) {
    CHECK(deleted.get().storyboard.timeline.kind == storyboard_timeline_action_kind_t::erase_keys);
    REQUIRE(deleted.get().storyboard.timeline.keys.size() == 1);
    CHECK(deleted.get().storyboard.timeline.keys[0].key == "key-a");
    CHECK(deleted.get().shortcut_events.empty());
}
}
using namespace wormedit;
using namespace wormedit::ui;
TEST_CASE("Storyboard timeline marker selection and ruler seek retain canonical identities",
          "[frontend-state][event-harness][storyboard-timeline]") {
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    const auto [x, y] = show_timeline(harness);
    const auto press = harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)});
    const auto& action = press.get().storyboard;
    check_marker_key_identity(action);
    CHECK(action.project == harness.view().project_id);
    CHECK(action.session_generation == 7);
    CHECK(action.prepared_generation == 13);
    (void)harness.frame(std::array{test::mouse_button(false, x, y)});
    // Focusing a track adds its controls above the ruler. Use its new bounds.
    const auto label = harness.drawn_text_center("Timeline opacity");
    REQUIRE(label);
    const auto ruler_y = label->second - 25;
    const auto seek = harness.frame(std::array{test::mouse_motion(x, ruler_y), test::mouse_button(true, x, ruler_y)});
    check_ruler_seek(seek.get().storyboard);
}
TEST_CASE("Storyboard timeline drags carry fixed key IDs through begin revise apply",
          "[frontend-state][event-harness][storyboard-timeline]") {
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    const auto [x, y] = show_timeline(harness);
    (void)harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)});
    const auto begin = harness.frame(std::array{test::mouse_motion(x + 20, y)});
    check_drag_begin(begin);
    const auto revise = harness.frame(std::array{test::mouse_motion(x + 25, y)}, SDL_KMOD_SHIFT);
    check_shift_drag_revision(revise);
    const auto apply = harness.frame(std::array{test::mouse_motion(x + 25, y), test::mouse_button(false, x + 25, y)});
    check_drag_applied_key(apply);
    CHECK(harness.frame().get().storyboard.kind == storyboard_action_kind_t::none);
}
TEST_CASE("Storyboard timeline Escape delivers cancellation and suppresses add and delete outside timeline focus",
          "[frontend-state][event-harness][storyboard-timeline]") {
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    const auto [x, y] = show_timeline(harness);
    (void)harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)});
    (void)harness.frame(std::array{test::mouse_motion(x + 20, y)});
    const auto escape = timeline_key(true, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    const auto canceled = harness.frame(std::span{&escape, 1});
    CHECK(canceled.get().storyboard.timeline.phase == storyboard_inspector_phase_t::cancel);
    CHECK(canceled.get().shortcut_events.empty());
    (void)harness.frame(std::array{test::mouse_button(false, x + 20, y)});
    const auto k = timeline_key(true, SDLK_K, SDL_SCANCODE_K);
    const auto unfocused = harness.frame(std::span{&k, 1});
    CHECK(unfocused.get().storyboard.kind == storyboard_action_kind_t::none);
    CHECK_FALSE(unfocused.get().shortcut_events.empty());
}
TEST_CASE("Storyboard timeline focused shortcuts own K and key Delete before hierarchy shortcuts",
          "[frontend-state][event-harness][storyboard-timeline]") {
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    const auto [x, y] = show_timeline(harness);
    (void)test::click_at(harness, x, y);
    auto& root = harness.view().storyboard;
    root.state.selection.keys = {"key-a"}; root.timeline.rows[0].keys[0].selected = true;
    const auto k = timeline_key(true, SDLK_K, SDL_SCANCODE_K);
    const auto add = harness.frame(std::span{&k, 1});
    check_add_key_shortcut(add);
    const auto erase = timeline_key(true, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto deleted = harness.frame(std::span{&erase, 1});
    check_delete_key_shortcut(deleted);
}

namespace {
enum class ending_t { apply, escape_busy, hidden, occupied, stale };

struct timeline_workflow_t {
    nuklear_event_harness_t& harness;
    document_history_t history;
    song_document_t before;
    workspace_controller_t workspace;
    sys::application_command_state_t application;
    editor_interaction_state_t editor;
    notification_center_t notifications{};
    notification_sink_t sink{notifications};
    sys::storyboard_timeline_projection_t projection;

    auto dispatch(const storyboard_action_t& action, bool busy = false) {
        return application.project.storyboard.dispatch(action,
            {.history = history, .workspace = workspace, .notifications = sink}, application, &editor, busy);
    }

    void bind_document() {
        (void)show_timeline(harness);
        auto& root = harness.view().storyboard;
        history = document_history_t::from({.id = root.project, .revision = root.revision,
            .storyboards = {{.id = storyboard_id_t{11}, .name = "Scene", .osbx = osbx_document_t{.nodes = {
                {.id = "node", .tracks = {.opacity = {.default_value = {1}, .keys = {
                    {.id = "key-a", .time_ms = 100, .value = {0}},
                    {.id = "key-b", .time_ms = 400, .value = {1}}}}},
                 .lifetime = osbx_lifetime_t{.begin_ms = -50, .end_ms = 900}}}}}}});
        before = history.document;
        const auto bound = workspace_controller_t::from(history); REQUIRE(bound.value);
        workspace = *bound.value; workspace.active_tab = workspace_tab_t::storyboard;
    }

    void select_node_and_viewport() {
        auto& root = harness.view().storyboard;
        application.project.storyboard.refill(root, history.document, 7);
        REQUIRE(dispatch(storyboard_action_t::from(root, storyboard_action_kind_t::select, storyboard_id_t{11})));
        application.project.storyboard.refill(root, history.document, 7);
        root.canvas.prepared_generation = osbx_lua_prepared_scope_t::generation();
        auto selection = storyboard_action_t::from(root, storyboard_action_kind_t::select_nodes, storyboard_id_t{11});
        selection.nodes = {"node"}; REQUIRE(dispatch(selection));
        auto viewport = storyboard_action_t::from(root, storyboard_action_kind_t::set_viewport, storyboard_id_t{11});
        viewport.viewport.timeline_pixels_per_ms = 0.5; REQUIRE(dispatch(viewport));
        application.project.storyboard.refill(root, history.document, 7);
    }

    auto begin_drag() -> std::pair<float, float> {
        auto& root = harness.view().storyboard;
        projection.refill(root.timeline, history.document, root, {.milliseconds = 150});
        (void)harness.frame();
        const auto label = harness.drawn_text_center("node / Opacity"); REQUIRE(label);
        const auto x = label->first + 130, y = label->second;
        const auto selected = harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)});
        REQUIRE(dispatch(selected.get().storyboard));
        application.project.storyboard.refill(root, history.document, 7);
        const auto begin = harness.frame(std::array{test::mouse_motion(x + 20, y)});
        REQUIRE(begin.get().storyboard.timeline.phase == storyboard_inspector_phase_t::begin);
        REQUIRE(dispatch(begin.get().storyboard));
        return {x, y};
    }

    void check_staged_document() const {
        CHECK(editor.is_pending()); CHECK_FALSE(editor.chart);
        test::check_same_document(history.document, before); CHECK(history.undo.empty());
    }

    void cancel_drag(ending_t ending) {
        storyboard_action_t terminal;
        if (ending == ending_t::hidden) {
            harness.view().storyboard.visible = false; harness.view().compose.visible = true;
            workspace.active_tab = workspace_tab_t::compose;
            terminal = harness.frame().get().storyboard;
        } else {
            const auto escape = timeline_key(true, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
            terminal = harness.frame(std::span{&escape, 1}).get().storyboard;
        }
        REQUIRE(terminal.is_document_gesture_cancel());
        REQUIRE(dispatch(terminal, true));
    }

    void finish_drag(ending_t ending, float x, float y) {
        const auto delta = ending == ending_t::occupied ? 150.0f : 25.0f;
        const auto revise = harness.frame(std::array{test::mouse_motion(x + delta, y)});
        REQUIRE(dispatch(revise.get().storyboard));
        const auto apply = harness.frame(std::array{test::mouse_motion(x + delta, y), test::mouse_button(false, x + delta, y)});
        if (ending == ending_t::stale) {
            REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard_id_t{11},
                .change = osbx_set_order_t{.node = "node", .order = 77}}).applied);
        }
        REQUIRE(dispatch(apply.get().storyboard));
    }

    void check_retired_gesture_and_lifetime() const {
        CHECK_FALSE(editor.is_pending());
        const auto& node = history.document.storyboards[0].osbx->nodes[0];
        CHECK(node.lifetime == before.storyboards[0].osbx->nodes[0].lifetime);
    }

    void check_applied_move_and_undo() {
        const auto& node = history.document.storyboards[0].osbx->nodes[0];
        CHECK(node.tracks.opacity.keys[0].time_ms == 150); CHECK(history.undo.size() == 1);
        REQUIRE(undo_document_command(history)); CHECK(history.document.storyboards == before.storyboards);
    }

    void check_stale_move_refusal() const {
        const auto& node = history.document.storyboards[0].osbx->nodes[0];
        CHECK(node.order == 77); CHECK(node.tracks.opacity.keys[0].time_ms == 100);
        CHECK(history.undo.size() == 1);
    }

    void check_unchanged_document() const {
        test::check_same_document(history.document, before); CHECK(history.undo.empty());
    }
};
} // namespace

TEST_CASE("Storyboard timeline events run one shared no-chart gesture through apply cancel occupied and stale outcomes",
          "[frontend-state][event-harness][storyboard-timeline][runtime]") {
    const auto ending = GENERATE(ending_t::apply, ending_t::escape_busy, ending_t::hidden, ending_t::occupied, ending_t::stale);
    nuklear_event_harness_t harness{1280, 720}; REQUIRE(harness.ready());
    timeline_workflow_t workflow{.harness = harness};
    workflow.bind_document();
    workflow.select_node_and_viewport();
    const auto [x, y] = workflow.begin_drag();
    workflow.check_staged_document();
    if (ending == ending_t::escape_busy || ending == ending_t::hidden) {
        workflow.cancel_drag(ending);
    } else {
        workflow.finish_drag(ending, x, y);
    }
    workflow.check_retired_gesture_and_lifetime();
    if (ending == ending_t::apply) {
        workflow.check_applied_move_and_undo();
    } else if (ending == ending_t::stale) {
        workflow.check_stale_move_refusal();
    } else {
        workflow.check_unchanged_document();
    }
}
