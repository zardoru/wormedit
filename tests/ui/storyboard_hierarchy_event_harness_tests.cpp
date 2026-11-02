#include "nuklear_event_harness.h"
#include "sys/storyboard/storyboard_hierarchy_view.h"
#include "sys/storyboard/storyboard_runtime.h"
#include "sys/app/application_command_state.h"
#include "sys/project/workspace_view.h"
#include <wormedit/formats/native_project.h>
#include <wormedit/core/storyboard_canvas.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <source_location>

namespace {
using namespace wormedit;
using namespace wormedit::ui;
void check_tree_input(bool value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    CHECK(value);
}
template<class Value>
void require_tree_input(const Value& value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    REQUIRE(static_cast<bool>(value));
}
auto tree_intent(const std::vector<workspace_action_snapshot_t>& frames) -> storyboard_action_t {
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().storyboard.kind != storyboard_action_kind_t::none;
    });
    return found == frames.end() ? storyboard_action_t{} : found->get().storyboard;
}
auto tree_key(SDL_Keycode code, SDL_Scancode scan, bool down = true) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = code;
    event.key.scancode = scan;
    event.key.down = down;
    return event;
}
auto tree_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 7, .storyboards = {
        {.id = storyboard_id_t{11}, .name = "Opening", .osbx = osbx_document_t{
            .images = {{.id = "image", .file = "image.png"}}, .nodes = {
                {.id = "root"},
                {.id = "child", .kind = osbx_node_kind_t::sprite, .parent = "root", .order = 1, .image = "image"},
                {.id = "other", .order = 2},
                {.id = "foreground", .layer = osbx_layer_t::foreground}}}}}};
}
void show_tree(nuklear_event_harness_t& harness) {
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
    view.compose.visible = false;
    view.storyboard = {.visible = true, .project = view.project_id, .revision = view.document_revision,
        .session_generation = 7,
        .storyboards = {{.id = storyboard_id_t{11}, .name = "Opening", .kind = storyboard_kind_t::osbx}}};
    view.storyboard.refill_state({.selection = {.storyboard = storyboard_id_t{11}, .nodes = {"root"}}});
    sys::build_storyboard_hierarchy_view(view.storyboard, tree_song());
    (void)harness.frame();
}
auto click_tree_label(nuklear_event_harness_t& harness, std::string_view label)
    -> std::vector<workspace_action_snapshot_t> {
    const auto point = harness.drawn_text_center(label);
    INFO(harness.drawn_text());
    require_tree_input(point);
    return test::click_at(harness, point->first, point->second);
}
} // namespace

TEST_CASE("Hierarchy layer headings remain readable in a narrow Storyboard panel",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness{800, 720};
    REQUIRE(harness.ready());
    show_tree(harness);
    INFO(harness.drawn_text());
    CHECK(harness.drawn_text_center("Background"));
    CHECK(harness.drawn_text_center("Fail"));
    CHECK(harness.drawn_text_center("Pass"));
}

TEST_CASE("Hierarchy row clicks capture stable node and existing project context",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness;
    require_tree_input(harness.ready());
    show_tree(harness);
    const auto emitted = tree_intent(click_tree_label(harness, "  child"));
    check_tree_input(emitted.kind == storyboard_action_kind_t::select_nodes);
    check_tree_input(emitted.nodes == std::vector<std::string>{"child"});
    check_tree_input(emitted.storyboard == storyboard_id_t{11});
    check_tree_input(emitted.project == harness.view().project_id);
    check_tree_input(emitted.revision == harness.view().document_revision);
    check_tree_input(emitted.session_generation == 7);
}
TEST_CASE("Hierarchy editor eye emits a view intent without authored visibility edits",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness;
    require_tree_input(harness.ready());
    show_tree(harness);
    const auto action = tree_intent(harness.click_widget("Hide child in editor"));
    check_tree_input(action.hierarchy.kind == storyboard_hierarchy_mutation_kind_t::toggle_editor_visibility);
    check_tree_input(action.hierarchy.node == "child");
}
TEST_CASE("Hierarchy focus owns Delete F2 and duplicate without Compose lane input",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness;
    require_tree_input(harness.ready());
    show_tree(harness);
    (void)click_tree_label(harness, "root");
    const auto del = tree_key(SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto deleted = harness.frame(std::span{&del, 1});
    check_tree_input(deleted.get().storyboard.hierarchy.kind == storyboard_hierarchy_mutation_kind_t::erase);
    check_tree_input(deleted.get().storyboard.hierarchy.node == "root");
    check_tree_input(deleted.get().compose_inputs.empty());
    const auto up = tree_key(SDLK_DELETE, SDL_SCANCODE_DELETE, false);
    (void)harness.frame(std::span{&up, 1});
    const auto copy = tree_key(SDLK_D, SDL_SCANCODE_D);
    const auto duplicated = harness.frame(std::span{&copy, 1}, test::primary_modifier);
    check_tree_input(duplicated.get().storyboard.hierarchy.kind == storyboard_hierarchy_mutation_kind_t::duplicate);
    check_tree_input(duplicated.get().storyboard.hierarchy.node == "root");
}
TEST_CASE("Hierarchy drag emits one stable-target drop on release",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness;
    require_tree_input(harness.ready());
    show_tree(harness);
    const auto grip = harness.widget_center("Drag node other");
    const auto target = harness.drawn_text_center("root");
    require_tree_input(target);
    const std::array down{test::mouse_motion(grip.first, grip.second),
        test::mouse_button(true, grip.first, grip.second)};
    const auto began = harness.frame(down);
    check_tree_input(began.get().storyboard.kind == storyboard_action_kind_t::none);
    const auto motion = test::mouse_motion(target->first, target->second);
    check_tree_input(harness.frame(std::span{&motion, 1}).get().storyboard.kind == storyboard_action_kind_t::none);
    const std::array release{motion, test::mouse_button(false, target->first, target->second)};
    const auto finished = harness.frame(release);
    check_tree_input(finished.get().storyboard.hierarchy.kind == storyboard_hierarchy_mutation_kind_t::drop);
    check_tree_input(finished.get().storyboard.hierarchy.node == "other");
    check_tree_input(finished.get().storyboard.hierarchy.parent == "root");
}
TEST_CASE("Hierarchy F2 inline rename emits captured identity and new text on Enter",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness;
    require_tree_input(harness.ready());
    show_tree(harness);
    (void)click_tree_label(harness, "root");
    const auto f2 = tree_key(SDLK_F2, SDL_SCANCODE_F2);
    (void)harness.frame(std::span{&f2, 1});
    const auto up = tree_key(SDLK_F2, SDL_SCANCODE_F2, false);
    (void)harness.frame(std::span{&up, 1});
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "renamed";
    (void)harness.frame(std::span{&text, 1});
    const auto enter = tree_key(SDLK_RETURN, SDL_SCANCODE_RETURN);
    const auto committed = harness.frame(std::span{&enter, 1});
    check_tree_input(committed.get().storyboard.hierarchy.kind == storyboard_hierarchy_mutation_kind_t::rename);
    check_tree_input(committed.get().storyboard.hierarchy.node == "root");
    check_tree_input(committed.get().storyboard.hierarchy.text == "renamed");
    check_tree_input(committed.get().storyboard.session_generation == 7);
}
TEST_CASE("Hierarchy Escape cancels drag without publishing an edit intent",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness;
    require_tree_input(harness.ready());
    show_tree(harness);
    const auto grip = harness.widget_center("Drag node other");
    const auto target = harness.drawn_text_center("root");
    require_tree_input(target);
    const std::array down{test::mouse_motion(grip.first, grip.second),
        test::mouse_button(true, grip.first, grip.second)};
    (void)harness.frame(down);
    const std::array moved{test::mouse_motion(target->first, target->second),
        tree_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)};
    check_tree_input(harness.frame(moved).get().storyboard.kind == storyboard_action_kind_t::none);
    const auto release = test::mouse_button(false, target->first, target->second);
    check_tree_input(harness.frame(std::span{&release, 1}).get().storyboard.kind == storyboard_action_kind_t::none);
}
TEST_CASE("Hierarchy Add Group captures the selected parent and typed layer",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness;
    require_tree_input(harness.ready());
    show_tree(harness);
    const auto action = tree_intent(harness.click_widget("Add group in Background"));
    check_tree_input(action.kind == storyboard_action_kind_t::hierarchy);
    check_tree_input(action.hierarchy.kind == storyboard_hierarchy_mutation_kind_t::add_group);
    check_tree_input(action.hierarchy.parent == "root");
    check_tree_input(action.hierarchy.layer == osbx_layer_t::background);
}
TEST_CASE("Hierarchy Add Sprite chooses an explicit existing image ID",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness{1280, 1100};
    require_tree_input(harness.ready());
    show_tree(harness);
    harness.view().storyboard.inspector.images = {{.id = "stable-image-id", .label = "image.png"}};
    (void)harness.frame();
    (void)click_tree_label(harness, "+ Sprite");
    (void)harness.frame();
    const auto action = tree_intent(click_tree_label(harness, "image.png"));
    check_tree_input(action.kind == storyboard_action_kind_t::hierarchy);
    check_tree_input(action.hierarchy.kind == storyboard_hierarchy_mutation_kind_t::add_sprite);
    check_tree_input(action.hierarchy.image == "stable-image-id");
    check_tree_input(action.hierarchy.layer == osbx_layer_t::foreground);
}
TEST_CASE("Hierarchy modal input loss retires keyboard ownership",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness;
    require_tree_input(harness.ready());
    show_tree(harness);
    (void)click_tree_label(harness, "root");
    harness.view().options.visible = true;
    const auto del = tree_key(SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto blocked = harness.frame(std::span{&del, 1});
    check_tree_input(blocked.get().storyboard.kind == storyboard_action_kind_t::none);
    check_tree_input(blocked.get().compose_inputs.empty());
}

namespace {
using namespace wormedit::sys;
auto tree_bytes(song_document_t song) -> std::string {
    song.revision = 1;
    const auto written = write_native_project(song);
    require_tree_input(written.value);
    return *written.value;
}
struct hierarchy_workflow_t {
    document_history_t history{document_history_t::from(tree_song())};
    workspace_controller_t workspace;
    application_command_state_t application;
    notification_center_t notifications;
    notification_sink_t sink{notifications};
    frontend_workspace_view_t projection;
    native_project_recovery_controller_view_t recovery;
    nuklear_event_harness_t harness;
    std::uint64_t session{7};
    std::string status;

    void initialize() {
        require_tree_input(harness.ready());
        const auto bound = workspace_controller_t::from(history);
        require_tree_input(bound.value);
        workspace = *bound.value;
        require_tree_input(apply_workspace_action(workspace, history.document,
            workspace_switch_tab_action_t{workspace_tab_t::storyboard}).value);
        project();
        require_tree_input(dispatch(storyboard_action_t::from(harness.view().storyboard,
            storyboard_action_kind_t::select, storyboard_id_t{11})));
        project();
    }
    void project() {
        const auto model = wormedit::workspace_view_t::from(workspace, history.document);
        require_tree_input(model.value);
        projection.query_session_generation = session;
        projection.refill(*model.value, history.document, status, recovery, false);
        application.project.storyboard.refill(projection.view.storyboard, history.document, session);
        build_storyboard_hierarchy_view(projection.view.storyboard, history.document);
        harness.view() = projection.view;
        harness.view().project_loaded = true;
        (void)harness.frame();
    }
    auto dispatch(const storyboard_action_t& action) -> bool {
        return application.project.storyboard.dispatch(action,
            {.history = history, .workspace = workspace, .notifications = sink}, application, nullptr, false);
    }
    void select_root() {
        require_tree_input(dispatch(tree_intent(click_tree_label(harness, "root"))));
        project();
    }
    auto press(SDL_Keycode code, SDL_Scancode scan, SDL_Keymod modifiers = SDL_KMOD_NONE)
        -> storyboard_action_t {
        const auto down = tree_key(code, scan);
        const auto action = harness.frame(std::span{&down, 1}, modifiers).get().storyboard;
        const auto up = tree_key(code, scan, false);
        (void)harness.frame(std::span{&up, 1}, modifiers);
        return action;
    }
};
} // namespace

TEST_CASE("Hierarchy production select and Delete route through one shared Undo transaction",
          "[frontend-state][event-harness][storyboard-hierarchy][workflow]") {
    hierarchy_workflow_t flow;
    flow.initialize();
    flow.select_root();
    check_tree_input(flow.application.project.storyboard.state().selection.nodes == std::vector<std::string>{"root"});
    const auto before = tree_bytes(flow.history.document);
    const auto action = flow.press(SDLK_DELETE, SDL_SCANCODE_DELETE);
    require_tree_input(action.hierarchy.kind == storyboard_hierarchy_mutation_kind_t::erase);
    require_tree_input(flow.dispatch(action));
    check_tree_input(flow.history.document.storyboards[0].osbx->nodes.size() == 2);
    check_tree_input(flow.history.undo.size() == 1);
    const auto after = tree_bytes(flow.history.document);
    require_tree_input(undo_document_command(flow.history));
    check_tree_input(tree_bytes(flow.history.document) == before);
    require_tree_input(redo_document_command(flow.history));
    check_tree_input(tree_bytes(flow.history.document) == after);
}
TEST_CASE("Hierarchy production inline rename updates parent references and shared selection",
          "[frontend-state][event-harness][storyboard-hierarchy][workflow]") {
    hierarchy_workflow_t flow;
    flow.initialize();
    flow.select_root();
    (void)flow.press(SDLK_F2, SDL_SCANCODE_F2);
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "renamed";
    (void)flow.harness.frame(std::span{&text, 1});
    const auto action = flow.press(SDLK_RETURN, SDL_SCANCODE_RETURN);
    require_tree_input(action.hierarchy.kind == storyboard_hierarchy_mutation_kind_t::rename);
    require_tree_input(flow.dispatch(action));
    check_tree_input(flow.application.project.storyboard.state().selection.nodes == std::vector<std::string>{"renamed"});
    check_tree_input(flow.history.document.storyboards[0].osbx->nodes[1].parent == "renamed");
    check_tree_input(flow.history.undo.size() == 1);
}
TEST_CASE("Hierarchy production duplicate selects only the canonical created root identity",
          "[frontend-state][event-harness][storyboard-hierarchy][workflow]") {
    hierarchy_workflow_t flow;
    flow.initialize();
    flow.select_root();
    const auto before = tree_bytes(flow.history.document);
    require_tree_input(flow.dispatch(flow.press(SDLK_D, SDL_SCANCODE_D, test::primary_modifier)));
    const auto& selected = flow.application.project.storyboard.state().selection.nodes;
    require_tree_input(selected.size() == 1);
    check_tree_input(selected[0] != "root");
    const auto& nodes = flow.history.document.storyboards[0].osbx->nodes;
    check_tree_input(std::ranges::find(nodes, selected[0], &osbx_node_t::id) != nodes.end());
    check_tree_input(std::ranges::count(nodes, std::optional<std::string>{selected[0]}, &osbx_node_t::parent) == 1);
    check_tree_input(flow.history.undo.size() == 1);
    require_tree_input(undo_document_command(flow.history));
    check_tree_input(tree_bytes(flow.history.document) == before);
}
TEST_CASE("Hierarchy production editor eye leaves authored bytes and history untouched",
          "[frontend-state][event-harness][storyboard-hierarchy][workflow]") {
    hierarchy_workflow_t flow;
    flow.initialize();
    const auto before = tree_bytes(flow.history.document);
    const auto action = tree_intent(flow.harness.click_widget("Hide child in editor"));
    require_tree_input(flow.dispatch(action));
    check_tree_input(flow.application.project.storyboard.state().hidden_nodes == std::vector<std::string>{"child"});
    check_tree_input(tree_bytes(flow.history.document) == before);
    check_tree_input(flow.history.undo.empty() && flow.history.redo.empty());
    flow.project();
    require_tree_input(flow.dispatch(tree_intent(flow.harness.click_widget("Show child in editor"))));
    check_tree_input(flow.application.project.storyboard.state().hidden_nodes.empty());
}
TEST_CASE("Hierarchy production rejects stale and missing captures without changing selection",
          "[frontend-state][event-harness][storyboard-hierarchy][workflow]") {
    hierarchy_workflow_t flow;
    flow.initialize();
    flow.select_root();
    auto action = storyboard_action_t::from(flow.harness.view().storyboard, storyboard_action_kind_t::hierarchy,
                                           storyboard_id_t{11});
    action.hierarchy = {.kind = storyboard_hierarchy_mutation_kind_t::erase, .node = "missing"};
    const auto before = tree_bytes(flow.history.document);
    require_tree_input(flow.dispatch(action));
    action.hierarchy.node = "root";
    ++action.revision;
    require_tree_input(flow.dispatch(action));
    --action.revision;
    ++action.session_generation;
    require_tree_input(flow.dispatch(action));
    check_tree_input(tree_bytes(flow.history.document) == before);
    check_tree_input(flow.history.undo.empty());
    check_tree_input(flow.application.project.storyboard.state().selection.nodes == std::vector<std::string>{"root"});
}
TEST_CASE("Hierarchy production drop commits reparent and order as one Undo",
          "[frontend-state][event-harness][storyboard-hierarchy][workflow]") {
    hierarchy_workflow_t flow;
    flow.initialize();
    flow.select_root();
    const auto grip = flow.harness.widget_center("Drag node other");
    const auto target = flow.harness.drawn_text_center("root");
    require_tree_input(target);
    const std::array down{test::mouse_motion(grip.first, grip.second), test::mouse_button(true, grip.first, grip.second)};
    (void)flow.harness.frame(down);
    const auto motion = test::mouse_motion(target->first, target->second);
    (void)flow.harness.frame(std::span{&motion, 1});
    check_tree_input(flow.history.undo.empty());
    const std::array release{motion, test::mouse_button(false, target->first, target->second)};
    const auto action = flow.harness.frame(release).get().storyboard;
    require_tree_input(action.hierarchy.kind == storyboard_hierarchy_mutation_kind_t::drop);
    require_tree_input(flow.dispatch(action));
    const auto& nodes = flow.history.document.storyboards[0].osbx->nodes;
    const auto moved = std::ranges::find(nodes, "other", &osbx_node_t::id);
    require_tree_input(moved != nodes.end());
    check_tree_input(moved->parent == "root");
    check_tree_input(flow.history.undo.size() == 1);
}
TEST_CASE("Hierarchy production rejects generated document mutation atomically",
          "[frontend-state][event-harness][storyboard-hierarchy][workflow]") {
    hierarchy_workflow_t flow;
    flow.history.document.generator_recipes.push_back({.id = recipe_id_t{40}});
    flow.history.document.storyboards[0].owning_generator_recipe = recipe_id_t{40};
    flow.initialize();
    flow.select_root();
    const auto before = tree_bytes(flow.history.document);
    auto action = storyboard_action_t::from(flow.harness.view().storyboard, storyboard_action_kind_t::hierarchy,
                                           storyboard_id_t{11});
    action.hierarchy = {.kind = storyboard_hierarchy_mutation_kind_t::erase, .node = "root"};
    require_tree_input(flow.dispatch(action));
    check_tree_input(tree_bytes(flow.history.document) == before);
    check_tree_input(flow.history.undo.empty());
    check_tree_input(flow.application.project.storyboard.state().selection.nodes == std::vector<std::string>{"root"});
}
TEST_CASE("Hierarchy production cross-layer drop leaves bytes and selection unchanged",
          "[frontend-state][event-harness][storyboard-hierarchy][workflow]") {
    hierarchy_workflow_t flow;
    flow.initialize();
    flow.select_root();
    const auto before = tree_bytes(flow.history.document);
    auto action = storyboard_action_t::from(flow.harness.view().storyboard, storyboard_action_kind_t::hierarchy,
                                           storyboard_id_t{11});
    action.hierarchy = {.kind = storyboard_hierarchy_mutation_kind_t::drop, .node = "root",
        .parent = "foreground", .layer = osbx_layer_t::foreground};
    require_tree_input(flow.dispatch(action));
    check_tree_input(tree_bytes(flow.history.document) == before);
    check_tree_input(flow.history.undo.empty());
    check_tree_input(flow.application.project.storyboard.state().selection.nodes == std::vector<std::string>{"root"});
}

TEST_CASE("Hierarchy production OSB selection shares canvas ID but refuses authored edits",
          "[frontend-state][event-harness][storyboard-hierarchy][workflow]") {
    hierarchy_workflow_t flow;
    flow.history.document.storyboards[0].osbx.reset();
    flow.history.document.storyboards[0].model = storyboard_document_model_t{.sprites = {
        {.stable_id = 9, .layer = "Background", .origin = "Centre", .path = "image.png"}}};
    flow.initialize();
    const auto id = storyboard_canvas_target_t::legacy_node_id(9);
    require_tree_input(flow.dispatch(tree_intent(click_tree_label(flow.harness, id))));
    flow.project();
    const auto before = tree_bytes(flow.history.document);
    auto action = storyboard_action_t::from(flow.harness.view().storyboard, storyboard_action_kind_t::hierarchy,
                                           storyboard_id_t{11});
    action.hierarchy = {.kind = storyboard_hierarchy_mutation_kind_t::erase, .node = id};
    require_tree_input(flow.dispatch(action));
    check_tree_input(tree_bytes(flow.history.document) == before);
    check_tree_input(flow.history.undo.empty());
    check_tree_input(flow.application.project.storyboard.state().selection.nodes == std::vector<std::string>{id});
}
TEST_CASE("Hierarchy arrows select owning stable IDs only while hierarchy has focus",
          "[frontend-state][event-harness][storyboard-hierarchy]") {
    nuklear_event_harness_t harness;
    require_tree_input(harness.ready());
    show_tree(harness);
    (void)click_tree_label(harness, "root");
    const auto down = tree_key(SDLK_DOWN, SDL_SCANCODE_DOWN);
    const auto moved = harness.frame(std::span{&down, 1});
    check_tree_input(moved.get().storyboard.kind == storyboard_action_kind_t::select_nodes);
    check_tree_input(moved.get().storyboard.nodes == std::vector<std::string>{"child"});
    const auto up = tree_key(SDLK_DOWN, SDL_SCANCODE_DOWN, false);
    (void)harness.frame(std::span{&up, 1});
    (void)test::click_at(harness, 600, 300);
    check_tree_input(harness.frame(std::span{&down, 1}).get().storyboard.kind == storyboard_action_kind_t::none);
}
