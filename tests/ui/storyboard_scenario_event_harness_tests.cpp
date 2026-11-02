#include "nuklear_event_harness.h"
#include "../support/document_assertions.h"
#include "sys/storyboard/storyboard_hierarchy_view.h"
#include "sys/app/application_command_state.h"
#include <wormedit/core/osbx_lua_link.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <cmath>

using namespace wormedit;
using namespace wormedit::ui;
namespace {
struct scenario_workflow_t {
    nuklear_event_harness_t& harness;
    document_history_t history{document_history_t::from({.id = song_id_t{1}, .revision = 1,
        .storyboards = {{.id = storyboard_id_t{11}, .name = "Scene", .osbx = osbx_document_t{.nodes = {
            {.id = "pass", .layer = osbx_layer_t::pass}, {.id = "fail", .layer = osbx_layer_t::fail}}}},
            {.id = storyboard_id_t{12}, .name = "Other", .osbx = osbx_document_t{}}}})};
    const song_document_t original{history.document};
    workspace_controller_t workspace;
    sys::application_command_state_t application;
    editor_interaction_state_t editor;
    notification_center_t notifications{};
    notification_sink_t sink{notifications};
    std::uint64_t session{7};
    bool playing{true};
    unsigned transport_calls{};
    void initialize(bool overlay_first = false) {
        REQUIRE(harness.ready());
        const auto bound = workspace_controller_t::from(history); REQUIRE(bound.value);
        workspace = *bound.value;
        workspace.active_tab = overlay_first ? workspace_tab_t::compose : workspace_tab_t::storyboard;
        application.project.compose_view.player_preview_visible = overlay_first;
        application.services.transport_preview_playing = [this] { return playing; };
        application.services.transport_play_pause = [this] { ++transport_calls; playing = false; return std::string{}; };
        project();
    }
    void project() {
        auto& view = harness.view();
        view.active_tab = workspace.active_tab == workspace_tab_t::storyboard
            ? WORM_UI_WORKSPACE_TAB_STORYBOARD : WORM_UI_WORKSPACE_TAB_COMPOSE;
        view.compose.visible = workspace.active_tab == workspace_tab_t::compose;
        auto& root = view.storyboard;
        root.visible = workspace.active_tab == workspace_tab_t::storyboard;
        root.storyboards = {{.id = storyboard_id_t{11}, .name = "Scene", .kind = storyboard_kind_t::osbx},
            {.id = storyboard_id_t{12}, .name = "Other", .kind = storyboard_kind_t::osbx}};
        application.project.storyboard.refill(root, history.document, session);
        root.canvas.prepared_generation = osbx_lua_prepared_scope_t::generation();
        view.player_preview.visible = application.project.compose_view.player_preview_visible;
        view.player_preview.viewport_width_pixels = 640;
        view.player_preview.viewport_height_pixels = 480;
        view.player_preview.status_text = "Choose a difficulty to preview.";
        (void)harness.frame();
    }
    auto dispatch(const storyboard_action_t& action, bool busy = false) -> bool {
        return application.project.storyboard.dispatch(action,
            {.history = history, .workspace = workspace, .notifications = sink}, application, &editor, busy);
    }
    auto choice_point(std::string_view label) -> std::pair<float, float> {
        const auto heading = harness.drawn_text_center("State"); REQUIRE(heading);
        const auto bounds = harness.drawn_text_bounds();
        const auto found = std::ranges::find_if(bounds, [&](const auto& text) {
            return text.text == label && std::abs(text.y + text.height * 0.5f - heading->second) < 5;
        });
        REQUIRE(found != bounds.end());
        return {found->x + found->width * 0.5f, found->y + found->height * 0.5f};
    }
    auto click(std::string_view label) -> storyboard_action_t {
        const auto point = choice_point(label);
        const auto frames = test::click_at(harness, point.first, point.second);
        const auto found = std::ranges::find_if(frames, [](const auto& frame) {
            return frame.get().storyboard.kind == storyboard_action_kind_t::set_conditional_layer;
        });
        REQUIRE(found != frames.end());
        return found->get().storyboard;
    }
    void choose(std::string_view label) {
        REQUIRE(dispatch(click(label)));
        project();
    }
    void begin_pending_edit() {
        auto& root = harness.view().storyboard;
        REQUIRE(dispatch(storyboard_action_t::from(root, storyboard_action_kind_t::select, storyboard_id_t{11})));
        project();
        auto select = storyboard_action_t::from(root, storyboard_action_kind_t::select_nodes, storyboard_id_t{11});
        select.nodes = {"pass"}; REQUIRE(dispatch(select)); project();
        auto begin = storyboard_action_t::from(root, storyboard_action_kind_t::inspector, storyboard_id_t{11});
        begin.inspector = {.field = storyboard_inspector_field_t::opacity,
            .phase = storyboard_inspector_phase_t::begin, .node = "pass", .value = {0.5}};
        playing = false;
        REQUIRE(dispatch(begin));
        REQUIRE(editor.is_pending());
    }
};
void check_choice(const scenario_workflow_t& flow, storyboard_conditional_layer_t choice) {
    CHECK(flow.application.project.storyboard.state().conditional_layer == choice);
    CHECK(flow.harness.view().storyboard.state.conditional_layer == choice);
}
void check_no_authoring_effect(const scenario_workflow_t& flow) {
    test::check_same_document(flow.history.document, flow.original);
    CHECK(flow.history.document.revision == 1);
    CHECK(flow.history.undo.empty());
    CHECK(flow.transport_calls == 0);
}
enum class stale_t { project, revision, session, invalid, hidden, busy };
void invalidate_scenario(storyboard_action_t& action, stale_t kind) {
    switch (kind) {
    case stale_t::project: action.project = song_id_t{999}; break;
    case stale_t::revision: ++action.revision; break;
    case stale_t::session: ++action.session_generation; break;
    case stale_t::invalid: action.conditional_layer = static_cast<storyboard_conditional_layer_t>(255); break;
    case stale_t::hidden: case stale_t::busy: break;
    }
}
} // namespace

TEST_CASE("Storyboard scenario toolbar emits captured fixed choices through the production runtime",
          "[frontend-state][event-harness][storyboard-scenario]") {
    const auto width = GENERATE(800, 1280);
    nuklear_event_harness_t harness{width, 720}; scenario_workflow_t flow{.harness = harness}; flow.initialize();
    check_choice(flow, storyboard_conditional_layer_t::pass);
    const auto action = flow.click("Fail");
    CHECK(action.project == flow.history.document.id);
    CHECK(action.session_generation == flow.session);
    REQUIRE(flow.dispatch(action)); flow.project();
    check_choice(flow, storyboard_conditional_layer_t::fail);
    flow.choose("Neither"); check_choice(flow, storyboard_conditional_layer_t::neither);
    check_no_authoring_effect(flow);
}
TEST_CASE("Player preview selector shares current project state while Storyboard is hidden and unselected",
          "[frontend-state][event-harness][storyboard-scenario]") {
    nuklear_event_harness_t harness{1280, 720}; scenario_workflow_t flow{.harness = harness}; flow.initialize();
    flow.choose("Fail");
    flow.workspace.active_tab = workspace_tab_t::compose;
    flow.application.project.compose_view.player_preview_visible = true;
    flow.project();
    CHECK_FALSE(harness.view().storyboard.visible);
    CHECK_FALSE(harness.view().storyboard.state.selection.storyboard);
    flow.choose("Neither"); check_choice(flow, storyboard_conditional_layer_t::neither);
    flow.workspace.active_tab = workspace_tab_t::storyboard;
    flow.application.project.compose_view.player_preview_visible = false; flow.project();
    check_choice(flow, storyboard_conditional_layer_t::neither);
    check_no_authoring_effect(flow);
}
TEST_CASE("Storyboard scenario runtime rejects stale invalid hidden and busy captures without side effects",
          "[frontend-state][event-harness][storyboard-scenario]") {
    const auto reason = GENERATE(stale_t::project, stale_t::revision, stale_t::session,
        stale_t::invalid, stale_t::hidden, stale_t::busy);
    nuklear_event_harness_t harness{1280, 720}; scenario_workflow_t flow{.harness = harness}; flow.initialize();
    auto action = flow.click("Fail"); invalidate_scenario(action, reason);
    if (reason == stale_t::hidden) flow.workspace.active_tab = workspace_tab_t::compose;
    REQUIRE(flow.dispatch(action, reason == stale_t::busy)); flow.project();
    check_choice(flow, storyboard_conditional_layer_t::pass);
    check_no_authoring_effect(flow);
}
TEST_CASE("Storyboard scenario change preserves an already admitted document gesture",
          "[frontend-state][event-harness][storyboard-scenario]") {
    nuklear_event_harness_t harness{1280, 720}; scenario_workflow_t flow{.harness = harness}; flow.initialize();
    flow.begin_pending_edit();
    auto action = storyboard_action_t::from(harness.view().storyboard, storyboard_action_kind_t::set_conditional_layer);
    action.conditional_layer = storyboard_conditional_layer_t::fail;
    REQUIRE(flow.dispatch(action)); flow.project();
    CHECK(flow.editor.is_pending());
    check_choice(flow, storyboard_conditional_layer_t::pass);
    check_no_authoring_effect(flow);
}
TEST_CASE("Storyboard scenario choice survives selection and resets on same-revision session reopen",
          "[frontend-state][event-harness][storyboard-scenario]") {
    nuklear_event_harness_t harness{1280, 720}; scenario_workflow_t flow{.harness = harness}; flow.initialize();
    flow.choose("Fail");
    REQUIRE(flow.dispatch(storyboard_action_t::from(harness.view().storyboard,
        storyboard_action_kind_t::select, storyboard_id_t{12})));
    flow.project(); check_choice(flow, storyboard_conditional_layer_t::fail);
    ++flow.session; flow.project(); check_choice(flow, storyboard_conditional_layer_t::pass);
    check_no_authoring_effect(flow);
}

TEST_CASE("Storyboard scenario selectors do not emit through modal or window focus loss",
          "[frontend-state][event-harness][storyboard-scenario]") {
    const bool overlay = GENERATE(false, true);
    const bool modal = GENERATE(false, true);
    nuklear_event_harness_t harness{1280, 720}; scenario_workflow_t flow{.harness = harness}; flow.initialize();
    if (overlay) {
        flow.workspace.active_tab = workspace_tab_t::compose;
        flow.application.project.compose_view.player_preview_visible = true;
        flow.project();
    }
    const auto point = flow.choice_point("Fail");
    if (modal) {
        harness.view().options.visible = true; (void)harness.frame();
    } else {
        SDL_Event lost{}; lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        (void)harness.frame(std::span{&lost, 1});
    }
    const auto frames = test::click_at(harness, point.first, point.second);
    CHECK(std::ranges::none_of(frames, [](const auto& frame) {
        return frame.get().storyboard.kind == storyboard_action_kind_t::set_conditional_layer;
    }));
    check_choice(flow, storyboard_conditional_layer_t::pass);
    check_no_authoring_effect(flow);
}

TEST_CASE("Player scenario selector works before Storyboard tab row projection ever runs",
          "[frontend-state][event-harness][storyboard-scenario]") {
    nuklear_event_harness_t harness{1280, 720}; scenario_workflow_t flow{.harness = harness}; flow.initialize(true);
    CHECK_FALSE(harness.view().storyboard.visible);
    CHECK(harness.view().storyboard.project == flow.history.document.id);
    flow.choose("Fail"); check_choice(flow, storyboard_conditional_layer_t::fail);
    check_no_authoring_effect(flow);
}
TEST_CASE("Visible player scenario controls do not admit unrelated Storyboard actions on Compose",
          "[frontend-state][event-harness][storyboard-scenario]") {
    nuklear_event_harness_t harness{1280, 720}; scenario_workflow_t flow{.harness = harness}; flow.initialize(true);
    const auto action = storyboard_action_t::from(harness.view().storyboard,
        storyboard_action_kind_t::select, storyboard_id_t{11});
    REQUIRE(flow.dispatch(action)); flow.project();
    CHECK_FALSE(harness.view().storyboard.state.selection.storyboard);
    check_no_authoring_effect(flow);
}

TEST_CASE("Storyboard scenario hierarchy labels inactive layers while retaining their editable rows",
          "[frontend-state][event-harness][storyboard-scenario]") {
    nuklear_event_harness_t harness{1280, 900}; scenario_workflow_t flow{.harness = harness}; flow.initialize();
    flow.choose("Fail");
    REQUIRE(flow.dispatch(storyboard_action_t::from(harness.view().storyboard,
        storyboard_action_kind_t::select, storyboard_id_t{11})));
    flow.project();
    sys::build_storyboard_hierarchy_view(harness.view().storyboard, flow.history.document);
    (void)harness.frame();
    CHECK(harness.drawn_text().find("Not shown in this state") != std::string::npos);
    const auto& layer = harness.view().storyboard.hierarchy.layers[2];
    CHECK_FALSE(layer.shown_in_state);
    REQUIRE(layer.rows.size() == 1);
    CHECK(layer.rows[0].editable);
    check_no_authoring_effect(flow);
}
