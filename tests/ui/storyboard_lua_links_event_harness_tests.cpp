#include "nuklear_event_harness.h"
#include "ui/frontend/storyboard.h"
#include "ui/nuklear/storyboard/storyboard_panel.h"
#include "sys/storyboard/storyboard_inspector_view.h"
#include "sys/storyboard/osbx_lua_link_runtime.h"
#include "sys/app/application_command_state.h"
#include "../support/temporary_directory.h"
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>

namespace {
using namespace wormedit;
using namespace wormedit::ui;
void show_linked_inspector(nuklear_event_harness_t& harness) {
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
    view.compose.visible = false;
    view.storyboard = {.visible = true, .project = view.project_id, .revision = view.document_revision,
        .session_generation = 7,
        .storyboards = {{.id = storyboard_id_t{11}, .name = "Scene", .kind = storyboard_kind_t::osbx}}};
    auto& root = view.storyboard;
    root.refill_state({.selection = {.storyboard = storyboard_id_t{11}, .nodes = {"sprite"}}});
    root.canvas.prepared_generation = 13;
    root.inspector = {.node = "sprite", .editable = true, .origin = "Centre", .order = 314,
        .opacity = {.value = {0.625}, .editable = false, .reason = "Linked to Lua: bake or unlink to edit."},
        .time = {.milliseconds = 50}, .key_time_ms = 50};
    (void)harness.frame();
}
auto label_point(nuklear_event_harness_t& harness, std::string_view label) -> std::pair<float, float> {
    INFO(harness.drawn_text());
    const auto point = harness.drawn_text_center(label);
    REQUIRE(point);
    return *point;
}
auto drag_label(nuklear_event_harness_t& harness, std::string_view label) -> workspace_action_snapshot_t {
    const auto point = label_point(harness, label);
    (void)harness.frame(std::array{test::mouse_motion(point.first, point.second),
        test::mouse_button(true, point.first, point.second)});
    const auto motion = test::mouse_motion(point.first + 20, point.second);
    auto action = harness.frame(std::span{&motion, 1});
    const auto up = test::mouse_button(false, point.first + 20, point.second);
    (void)harness.frame(std::span{&up, 1});
    return action;
}
void check_order_capture(const storyboard_action_t& action, nuklear_event_harness_t& harness) {
    CHECK(action.project == harness.view().project_id);
    CHECK(action.revision == harness.view().document_revision);
    CHECK(action.session_generation == 7);
    CHECK(action.prepared_generation == 13);
}
} // namespace

TEST_CASE("Storyboard Lua-linked value labels cannot start direct inspector scrubs",
          "[frontend-state][event-harness][storyboard-lua-links]") {
    nuklear_event_harness_t harness{1280, 1000};
    REQUIRE(harness.ready());
    show_linked_inspector(harness);
    const auto result = drag_label(harness, "Opacity ◇");
    CHECK(result.get().storyboard.kind == storyboard_action_kind_t::none);
    CHECK(result.get().compose_inputs.empty());
}

TEST_CASE("Storyboard Lua-linked value leaves unrelated inspector edits and their captures available",
          "[frontend-state][event-harness][storyboard-lua-links]") {
    nuklear_event_harness_t harness{1280, 1000};
    REQUIRE(harness.ready());
    show_linked_inspector(harness);
    const auto result = drag_label(harness, "Order");
    REQUIRE(result.get().storyboard.kind == storyboard_action_kind_t::inspector);
    CHECK(result.get().storyboard.inspector.field == storyboard_inspector_field_t::order);
    CHECK(result.get().storyboard.inspector.phase == storyboard_inspector_phase_t::begin);
    check_order_capture(result.get().storyboard, harness);
}

namespace {
auto lua_runtime_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 3, .storyboards = {{.id = storyboard_id_t{11},
        .osbx = osbx_document_t{.images = {{.id = "image", .file = "image.png"}},
            .nodes = {{.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image"},
                {.id = "empty-group"}}}}}};
}
auto first_lua_label(nuklear_event_harness_t& harness, std::string_view label) -> std::pair<float, float> {
    INFO(harness.drawn_text());
    for (const auto& text : harness.drawn_text_bounds())
        if (std::string_view{text.text} == label) return {text.x + text.width * 0.5f, text.y + text.height * 0.5f};
    FAIL("Lua workflow control was not visible");
    return {};
}
auto lua_intent(const std::vector<workspace_action_snapshot_t>& frames) -> storyboard_action_t {
    for (const auto& frame : frames)
        if (frame.get().storyboard.kind != storyboard_action_kind_t::none) return frame.get().storyboard;
    return {};
}
struct lua_workflow_t {
    nuklear_event_harness_t harness{1280, 1600};
    result_t<test::temporary_directory_t, std::string> directory{test::temporary_directory_t::from()};
    task_scheduler_t scheduler{1, 2};
    sys::osbx_lua_link_runtime_t worker{scheduler};
    document_history_t history{document_history_t::from(lua_runtime_song())};
    workspace_controller_t workspace;
    sys::application_command_state_t application;
    editor_interaction_state_t editor;
    notification_center_t notifications;
    notification_sink_t sink{notifications};
    int source_requests{};
    auto dispatch(const storyboard_action_t& action, bool busy = false) -> bool {
        const osbx_lua_prepared_scope_t scope{worker.scenes(history.document), worker.generation()};
        return application.project.storyboard.dispatch(action,
            {.history = history, .workspace = workspace, .notifications = sink}, application, &editor, busy);
    }
    void project() {
        auto& view = harness.view();
        view.project_id = history.document.id; view.document_revision = history.document.revision;
        auto& root = view.storyboard;
        root.project = view.project_id; root.revision = view.document_revision;
        root.canvas.prepared_generation = worker.generation();
        const osbx_lua_prepared_scope_t scope{worker.scenes(history.document), worker.generation()};
        application.project.storyboard.refill(root, history.document, 7);
        const auto prepared = osbx_lua_prepared_scope_t::find(*history.document.storyboards[0].osbx);
        sys::build_storyboard_inspector_view(root, history.document, {.milliseconds = 50}, nullptr, prepared.get());
        (void)harness.frame();
    }
    void select(std::string id) {
        auto action = storyboard_action_t::from(harness.view().storyboard, storyboard_action_kind_t::select_nodes, storyboard_id_t{11});
        action.nodes = {std::move(id)};
        REQUIRE(dispatch(action)); project();
    }
    void initialize() {
        REQUIRE(harness.ready()); REQUIRE(directory);
        const auto bound = workspace_controller_t::from(history); REQUIRE(bound.value);
        workspace = *bound.value; workspace.active_tab = workspace_tab_t::storyboard;
        auto& view = harness.view();
        view.active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD; view.compose.visible = false;
        view.storyboard = {.visible = true, .project = history.document.id, .revision = history.document.revision,
            .session_generation = 7, .storyboards = {{.id = storyboard_id_t{11}, .name = "Scene", .kind = storyboard_kind_t::osbx}}};
        project();
        REQUIRE(dispatch(storyboard_action_t::from(view.storyboard, storyboard_action_kind_t::select, storyboard_id_t{11})));
        application.services.load_storyboard_lua = [this](const storyboard_action_t& action, const lua_source_descriptor_t& source) {
            ++source_requests;
            const auto started = worker.request_source(action, source, directory->path());
            return started ? sys::compose_action_outcome_t::from("Reading Lua", notification_severity_t::hint,
                notification_source_t::input) : sys::compose_action_outcome_t::blocked(started.error());
        };
        project(); select("sprite");
    }
    void write(std::string_view source) {
        std::ofstream file{directory->path() / "curve.lua"}; file << source;
        REQUIRE(file.good());
    }
    auto click(std::string_view label) -> storyboard_action_t {
        (void)test::scroll_to_text(harness, label, 1160, 500, -1, 12);
        const auto point = first_lua_label(harness, label);
        return lua_intent(test::click_at(harness, point.first, point.second));
    }
    auto form_action(std::string_view label = "Link to Lua…") -> storyboard_action_t {
        (void)click(label);
        REQUIRE(storyboard_inspector_form_active());
        const auto generation = begin_storyboard_lua_picker_for_testing(); REQUIRE(generation);
        complete_storyboard_lua_picker_for_testing(*generation, (directory->path() / "curve.lua").string());
        (void)harness.frame();
        return click("Apply Lua link");
    }
    void finish_source() {
        worker.wait(history.document, 7);
        const auto result = worker.take_source(); REQUIRE(result); REQUIRE(result->source);
        const osbx_lua_prepared_scope_t scope{worker.scenes(history.document), worker.generation()};
        REQUIRE(application.project.storyboard.complete_lua_source(result->action, result->source,
            {.history = history, .workspace = workspace, .notifications = sink}, application, &editor, false));
    }
    void evaluate() {
        worker.update(history.document, 7); worker.wait(history.document, 7); project();
    }
    void commit_source(const storyboard_action_t& action) {
        REQUIRE(action.kind == storyboard_action_kind_t::lua_link);
        REQUIRE(dispatch(action)); finish_source(); evaluate();
    }
    void link_from_ui(std::string_view title = "Link to Lua…") { commit_source(form_action(title)); }
};
auto stale_lua_capture(storyboard_action_t action, int change) -> storyboard_action_t {
    switch (change) {
    case 0: action.project = song_id_t{99}; break;
    case 1: ++action.revision; break;
    case 2: ++action.session_generation; break;
    case 3: action.storyboard = storyboard_id_t{99}; break;
    case 4: ++action.prepared_generation; break;
    case 5: action.lua_link.target.track.node = "missing"; break;
    case 6: action.lua_link.expected_link_id = "missing"; break;
    case 7: action.lua_link.selected_keys = {"missing"}; break;
    }
    return action;
}
}

TEST_CASE("Storyboard Lua form native choice links frozen source and Reload replaces it through the worker",
          "[frontend-state][event-harness][storyboard-lua-links][runtime]") {
    lua_workflow_t fixture; fixture.initialize();
    fixture.write("return function() return 0.5 end"); fixture.link_from_ui();
    const auto before = fixture.history.document.storyboards[0].osbx->nodes[0].tracks.opacity.lua;
    REQUIRE(before);
    fixture.write("return function() return 0.25 end");
    const auto reload = fixture.click("Reload");
    REQUIRE(reload.lua_link.kind == storyboard_lua_action_kind_t::reload);
    REQUIRE(fixture.dispatch(reload)); fixture.finish_source(); fixture.evaluate();
    const auto& after = fixture.history.document.storyboards[0].osbx->nodes[0].tracks.opacity.lua;
    CHECK(after->link.id == before->link.id);
    CHECK(after->sampling == before->sampling);
    CHECK(after->link.source.frozen_source != before->link.source.frozen_source);
}
TEST_CASE("Storyboard Lua Unlink routes from the inspector and retains authored hand data with one undo",
          "[frontend-state][event-harness][storyboard-lua-links][runtime]") {
    lua_workflow_t fixture; fixture.initialize();
    fixture.write("return function() return 0.5 end"); fixture.link_from_ui();
    const auto before = fixture.history.document.storyboards[0].osbx->nodes[0].tracks.opacity.keys;
    const auto action = fixture.click("Unlink");
    REQUIRE(action.lua_link.kind == storyboard_lua_action_kind_t::unlink);
    REQUIRE(fixture.dispatch(action));
    CHECK_FALSE(fixture.history.document.storyboards[0].osbx->nodes[0].tracks.opacity.lua);
    CHECK(fixture.history.document.storyboards[0].osbx->nodes[0].tracks.opacity.keys == before);
    CHECK(fixture.history.undo.size() == 2);
}
TEST_CASE("Storyboard Lua form captures reject stale project revision session board prepared node link and key selection",
          "[frontend-state][event-harness][storyboard-lua-links][runtime]") {
    const auto change = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    lua_workflow_t fixture; fixture.initialize();
    const auto action = stale_lua_capture(fixture.form_action(), change);
    const auto before = fixture.history.document.storyboards;
    REQUIRE(fixture.dispatch(action));
    CHECK(fixture.source_requests == 0);
    CHECK(fixture.history.document.storyboards == before);
    CHECK(fixture.history.undo.empty());
}
TEST_CASE("Storyboard Lua busy input never starts a source read or edits history",
          "[frontend-state][event-harness][storyboard-lua-links][runtime]") {
    lua_workflow_t fixture; fixture.initialize();
    const auto action = fixture.form_action();
    REQUIRE(fixture.dispatch(action, true));
    CHECK(fixture.source_requests == 0);
    CHECK(fixture.history.undo.empty());
}
TEST_CASE("Storyboard Lua native picker result survives its modal focus loss but cannot survive a changed context",
          "[frontend-state][event-harness][storyboard-lua-links]") {
    const bool stale = GENERATE(false, true);
    lua_workflow_t fixture; fixture.initialize();
    fixture.write("return function() return 0.5 end");
    (void)fixture.click("Link to Lua…");
    const auto generation = begin_storyboard_lua_picker_for_testing(); REQUIRE(generation);
    SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    (void)fixture.harness.frame(std::span{&focus, 1});
    if (stale) fixture.harness.view().storyboard.project = song_id_t{99};
    complete_storyboard_lua_picker_for_testing(*generation, (fixture.directory->path() / "curve.lua").string());
    (void)fixture.harness.frame();
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    (void)fixture.harness.frame(std::span{&focus, 1});
    CHECK(storyboard_inspector_form_active() == !stale);
    CHECK(fixture.source_requests == 0);
    CHECK(fixture.history.undo.empty());
}
TEST_CASE("Storyboard Lua form Escape and hidden-tab transitions cancel without a document edit",
          "[frontend-state][event-harness][storyboard-lua-links]") {
    const bool hidden = GENERATE(false, true);
    lua_workflow_t fixture; fixture.initialize();
    (void)fixture.click("Link to Lua…");
    if (hidden) {
        fixture.harness.view().storyboard.visible = false;
        fixture.harness.view().active_tab = WORM_UI_WORKSPACE_TAB_COMPOSE;
        (void)fixture.harness.frame();
    } else {
        SDL_Event key{}; key.type = SDL_EVENT_KEY_DOWN; key.key.key = SDLK_ESCAPE;
        key.key.scancode = SDL_SCANCODE_ESCAPE; key.key.down = true;
        (void)fixture.harness.frame(std::span{&key, 1});
    }
    CHECK_FALSE(storyboard_inspector_form_active());
    CHECK(fixture.source_requests == 0);
    CHECK(fixture.history.undo.empty());
}

#if defined(WORMEDIT_HAS_LUA)
TEST_CASE("Storyboard Lua Bake to keys routes the prepared worker result through one materialization undo",
          "[frontend-state][event-harness][storyboard-lua-links][runtime]") {
    lua_workflow_t fixture; fixture.initialize();
    fixture.write("return function(t,ctx) return t/1000 end"); fixture.link_from_ui();
    const auto action = fixture.click("Bake to keys");
    REQUIRE(action.lua_link.kind == storyboard_lua_action_kind_t::bake);
    REQUIRE(fixture.dispatch(action));
    const auto& track = fixture.history.document.storyboards[0].osbx->nodes[0].tracks.opacity;
    CHECK_FALSE(track.lua);
    CHECK_FALSE(track.keys.empty());
    CHECK(fixture.history.undo.size() == 2);
}
TEST_CASE("Storyboard Lua Fill and Detach children route the scripted group without replacing authored siblings",
          "[frontend-state][event-harness][storyboard-lua-links][runtime]") {
    lua_workflow_t fixture; fixture.initialize(); fixture.select("empty-group");
    fixture.write(R"lua(return function(ctx)
image {id='generated-image',file='generated.png'}
sprite('child')
  image('generated-image')
  transform.translate {5,7}
end)lua");
    fixture.link_from_ui("Fill from Lua…");
    REQUIRE(fixture.history.document.storyboards[0].osbx->nodes[1].children_link);
    const auto action = fixture.click("Detach children");
    REQUIRE(action.lua_link.kind == storyboard_lua_action_kind_t::detach_children);
    REQUIRE(fixture.dispatch(action));
    CHECK_FALSE(fixture.history.document.storyboards[0].osbx->nodes[1].children_link);
    CHECK(fixture.history.document.storyboards[0].osbx->nodes[0].id == "sprite");
    CHECK(fixture.history.undo.size() == 2);
}
TEST_CASE("Storyboard Lua failed Reload keeps the last-good worker scene and shows its link error",
          "[frontend-state][event-harness][storyboard-lua-links][runtime]") {
    lua_workflow_t fixture; fixture.initialize();
    fixture.write("return function() return 0.5 end"); fixture.link_from_ui();
    const auto before = fixture.worker.snapshot()->front().scene;
    fixture.write("return function()\n error('broken update')\nend");
    REQUIRE(fixture.dispatch(fixture.click("Reload"))); fixture.finish_source(); fixture.evaluate();
    const auto* link = fixture.harness.view().storyboard.inspector.lua_link_for({.track = {.node = "sprite"}});
    REQUIRE(link);
    CHECK(link->last_good);
    CHECK(link->error.find("curve.lua:2:") != std::string::npos);
    CHECK(*fixture.worker.snapshot()->front().scene == *before);
}
#endif

TEST_CASE("Storyboard Lua sampled bands refuse pointer dragging while exposing the bake or unlink reason",
          "[frontend-state][event-harness][storyboard-lua-links]") {
    nuklear_event_harness_t harness{1280, 1000};
    REQUIRE(harness.ready());
    show_linked_inspector(harness);
    auto& root = harness.view().storyboard;
    root.timeline.rows = {{.track = {.node = "sprite"}, .label = "Opacity", .editable = true,
        .lua = storyboard_lua_band_view_t{.begin = 50, .end = 150, .samples = {50, 100, 150}}}};
    (void)harness.frame();
    const auto label = label_point(harness, "ƒ Opacity");
    // Row labels occupy the existing 160px time-lane gutter. Their recorded
    // center is 80px from its left edge; target the sampled band's middle.
    const auto x = label.first + 80 + 100;
    const auto down = test::mouse_button(true, x, label.second);
    (void)harness.frame(std::array{test::mouse_motion(x, label.second), down});
    const auto motion = test::mouse_motion(x + 10, label.second);
    const auto moved = harness.frame(std::span{&motion, 1});
    CHECK(moved.get().storyboard.kind == storyboard_action_kind_t::none);
    CHECK(harness.drawn_text().find("Linked to Lua: bake or unlink to edit.") != std::string::npos);
}

namespace {
void set_lua_form_text(nuklear_event_harness_t& harness, std::string_view label, const char* value) {
    const auto point = label_point(harness, label);
    // The popup's existing two-column text row puts the field one half-width
    // to the right of its recorded label, including empty entry-point fields.
    (void)test::click_at(harness, point.first + 250, point.second);
    SDL_Event key{}; key.type = SDL_EVENT_KEY_DOWN; key.key.key = SDLK_A;
    key.key.scancode = SDL_SCANCODE_A; key.key.down = true;
    (void)harness.frame(std::span{&key, 1}, test::primary_modifier);
    key.type = SDL_EVENT_KEY_UP; key.key.down = false;
    (void)harness.frame(std::span{&key, 1});
    SDL_Event text{}; text.type = SDL_EVENT_TEXT_INPUT; text.text.text = value;
    (void)harness.frame(std::span{&text, 1});
}
}
TEST_CASE("Storyboard Lua form entry parameters seed and signed range reach the frozen typed edit",
          "[frontend-state][event-harness][storyboard-lua-links][runtime]") {
    lua_workflow_t fixture; fixture.initialize();
    fixture.write("return {curve=function(t,ctx) return ctx.parameters.amount end}");
    (void)fixture.click("Link to Lua…");
    const auto generation = begin_storyboard_lua_picker_for_testing(); REQUIRE(generation);
    complete_storyboard_lua_picker_for_testing(*generation, (fixture.directory->path() / "curve.lua").string());
    (void)fixture.harness.frame();
    set_lua_form_text(fixture.harness, "Entry point", "curve");
    set_lua_form_text(fixture.harness, "Seed", "42");
    set_lua_form_text(fixture.harness, "Begin (ms)", "-20");
    set_lua_form_text(fixture.harness, "End (ms)", "120");
    (void)fixture.click("Add parameter");
    set_lua_form_text(fixture.harness, "Name", "amount");
    set_lua_form_text(fixture.harness, "Value", "1/3");
    fixture.commit_source(fixture.click("Apply Lua link"));
    const auto& link = fixture.history.document.storyboards[0].osbx->nodes[0].tracks.opacity.lua;
    REQUIRE(link);
    CHECK(link->link.source.entry_point == "curve");
    CHECK(link->link.source.seed == 42);
    CHECK(link->link.source.parameters == std::vector<lua_parameter_t>{{.name = "amount", .value = {1, 3}}});
    CHECK(std::pair{link->begin_ms, link->end_ms} == std::pair<std::int64_t, std::int64_t>{-20, 120});
}
TEST_CASE("Storyboard Lua timeline focused row opens the same captured form for its stable track",
          "[frontend-state][event-harness][storyboard-lua-links]") {
    nuklear_event_harness_t harness{1280, 1000}; REQUIRE(harness.ready());
    show_linked_inspector(harness);
    auto& root = harness.view().storyboard;
    const osbx_track_target_t target{.node = "sprite", .kind = osbx_track_kind_t::color};
    root.timeline.rows = {{.track = target, .label = "Color track", .editable = true}};
    root.inspector.lua_links = {{.target = {.track = target}, .form = {.module = "curve.lua", .end_ms = 100},
        .supported = true, .editable = true}};
    (void)harness.frame();
    const auto row = label_point(harness, "Color track");
    (void)test::click_at(harness, row.first, row.second);
    const auto button = label_point(harness, "Link to Lua…");
    (void)test::click_at(harness, button.first, button.second);
    const auto apply = label_point(harness, "Apply Lua link");
    const auto action = lua_intent(test::click_at(harness, apply.first, apply.second));
    REQUIRE(action.kind == storyboard_action_kind_t::lua_link);
    CHECK(action.lua_link.target.track == target);
    CHECK(action.lua_link.form.module == "curve.lua");
    CHECK(action.prepared_generation == 13);
}
