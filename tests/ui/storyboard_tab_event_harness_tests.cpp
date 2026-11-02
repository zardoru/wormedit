#include "sys/commands/application_command_dispatch.h"
#include "sys/project/project_open.h"
#include "sys/project/workspace_view.h"
#include "sys/storyboard/storyboard_runtime.h"
#include <wormedit/formats/native_project.h>
#include "nuklear_event_harness.h"
#include "ui/frontend/storyboard.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <source_location>

namespace {
using namespace wormedit;
using namespace wormedit::ui;
template<class Value>
void require_storyboard(const Value& value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    REQUIRE(static_cast<bool>(value));
}
void check_storyboard(bool value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    CHECK(value);
}
void show_storyboard(nuklear_event_harness_t& harness, storyboard_id_t selected = storyboard_id_t{11}) {
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
    view.tab_heading = "Storyboard";
    view.compose.visible = false;
    view.storyboard.visible = true;
    view.storyboard.create_available = true;
    view.storyboard.project = view.project_id;
    view.storyboard.revision = view.document_revision;
    view.storyboard.session_generation = 7;
    view.storyboard.storyboards = {
        {.id = storyboard_id_t{11}, .name = "Opening", .kind = storyboard_kind_t::osbx},
        {.id = storyboard_id_t{12}, .name = "Imported", .kind = storyboard_kind_t::osb},
        {.id = storyboard_id_t{13}, .name = "Generated", .kind = storyboard_kind_t::osbx, .generator_owned = true}};
    view.storyboard.refill_state({.selection = {.storyboard = selected}});
}
auto click_storyboard_text(nuklear_event_harness_t& harness, std::string_view text)
    -> std::vector<workspace_action_snapshot_t> {
    INFO(harness.drawn_text());
    const auto point = harness.drawn_text_center(text);
    require_storyboard(point);
    return test::click_at(harness, point->first, point->second);
}
auto storyboard_intent(const std::vector<workspace_action_snapshot_t>& frames) -> storyboard_action_t {
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().storyboard.kind != storyboard_action_kind_t::none;
    });
    return found == frames.end() ? storyboard_action_t{} : found->get().storyboard;
}
auto lane_key(bool down) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = SDLK_1;
    event.key.scancode = SDL_SCANCODE_1;
    event.key.down = down;
    return event;
}
} // namespace

TEST_CASE("Storyboard shell reserves hierarchy canvas inspector and timeline", "[frontend-state][event-harness][storyboard-tab]") {
    const auto width = GENERATE(800, 1280);
    nuklear_event_harness_t harness{width, 720};
    require_storyboard(harness.ready());
    show_storyboard(harness);
    (void)harness.frame();
    CAPTURE(width);
    INFO(harness.drawn_text());
    check_storyboard(harness.drawn_text().contains("Hierarchy / Layers"));
    check_storyboard(harness.drawn_text().contains("Assets"));
    check_storyboard(harness.drawn_text().contains("Canvas"));
    check_storyboard(harness.drawn_text().contains("Inspector"));
    check_storyboard(harness.drawn_text().contains("Add key (K)"));
    check_storyboard(harness.drawn_text().contains("Delete keys"));
    check_storyboard(harness.is_widget_fully_visible("New storyboard"));
    check_storyboard(harness.is_widget_fully_visible("Delete storyboard"));
}
TEST_CASE("Storyboard picker emits stable identity and captured context", "[frontend-state][event-harness][storyboard-tab]") {
    nuklear_event_harness_t harness;
    require_storyboard(harness.ready());
    show_storyboard(harness);
    // Presentation order is unrelated to the identity sent by the picker.
    std::ranges::reverse(harness.view().storyboard.storyboards);
    (void)harness.frame();
    (void)click_storyboard_text(harness, "Opening");
    (void)harness.frame();
    const auto clicks = click_storyboard_text(harness, "Imported  [OSB]");
    const auto& action = storyboard_intent(clicks);
    check_storyboard(action.kind == storyboard_action_kind_t::select);
    check_storyboard(action.storyboard == storyboard_id_t{12});
    check_storyboard(action.project == harness.view().project_id);
    check_storyboard(action.revision == harness.view().document_revision);
    check_storyboard(action.session_generation == 7);
}
TEST_CASE("Storyboard New and Delete use typed owned intents", "[frontend-state][event-harness][storyboard-tab]") {
    nuklear_event_harness_t harness;
    require_storyboard(harness.ready());
    show_storyboard(harness);
    (void)harness.frame();
    const auto created = harness.click_widget("New storyboard");
    check_storyboard(storyboard_intent(created).kind == storyboard_action_kind_t::create);
    const auto deleted = harness.click_widget("Delete storyboard");
    check_storyboard(storyboard_intent(deleted).kind == storyboard_action_kind_t::erase);
    check_storyboard(storyboard_intent(deleted).storyboard == storyboard_id_t{11});
    // The snapshot owns the entire typed intent beyond a subsequent compose.
    (void)harness.frame();
    check_storyboard(storyboard_intent(deleted).project == harness.view().project_id);
}
TEST_CASE("Storyboard OSB generated and empty selections disable Delete", "[frontend-state][event-harness][storyboard-tab]") {
    const auto id = GENERATE(storyboard_id_t{}, storyboard_id_t{12}, storyboard_id_t{13});
    nuklear_event_harness_t harness;
    require_storyboard(harness.ready());
    show_storyboard(harness, id);
    (void)harness.frame();
    const auto deleted = harness.click_widget("Delete storyboard");
    check_storyboard(storyboard_intent(deleted).kind == storyboard_action_kind_t::none);
    if (id == storyboard_id_t{12}) check_storyboard(harness.drawn_text().contains("Imported OSB storyboard"));
    if (id == storyboard_id_t{13}) check_storyboard(harness.drawn_text().contains("Generated storyboard"));
    const auto created = harness.click_widget("New storyboard");
    check_storyboard(storyboard_intent(created).kind == storyboard_action_kind_t::create);
}
TEST_CASE("Storyboard empty project has an explicit usable empty state", "[frontend-state][event-harness][storyboard-tab]") {
    nuklear_event_harness_t harness;
    require_storyboard(harness.ready());
    show_storyboard(harness, {});
    harness.view().storyboard.storyboards.clear();
    harness.view().storyboard.refill_state({});
    (void)harness.frame();
    check_storyboard(harness.drawn_text().contains("Create a storyboard to begin."));
    check_storyboard(harness.drawn_text().contains("Choose storyboard"));
    const auto result = harness.click_widget("New storyboard");
    check_storyboard(storyboard_intent(result).kind == storyboard_action_kind_t::create);
}
TEST_CASE("Storyboard focus does not emit Compose lane input", "[frontend-state][event-harness][storyboard-tab]") {
    nuklear_event_harness_t harness;
    require_storyboard(harness.ready());
    harness.view().compose.lane_key_bindings = {{.scancode = SDL_SCANCODE_1, .lane_id = lane_id_t{1}}};
    harness.view().compose.note_paint_mode_active = true;
    (void)harness.frame();
    const auto down = lane_key(true), up = lane_key(false);
    require_storyboard(harness.frame(std::span{&down, 1}).get().compose_inputs.size() == 1);
    (void)harness.frame(std::span{&up, 1});
    show_storyboard(harness);
    (void)harness.frame();
    const auto pressed = harness.frame(std::span{&down, 1});
    check_storyboard(pressed.get().compose_inputs.empty());
    check_storyboard(pressed.get().compose_viewport_inputs.empty());
    check_storyboard(pressed.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_storyboard(harness.frame(std::span{&up, 1}).get().compose_inputs.empty());
}


namespace {
using namespace wormedit::sys;
auto storyboard_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 7,
        .generator_recipes = {{.id = recipe_id_t{40}}},
        .storyboards = {
            {.id = storyboard_id_t{11}, .name = "Opening", .osbx = osbx_document_t{}},
            {.id = storyboard_id_t{12}, .name = "Imported", .model = storyboard_document_model_t{}},
            {.id = storyboard_id_t{13}, .name = "Generated", .osbx = osbx_document_t{},
                .owning_generator_recipe = recipe_id_t{40}}}};
}
auto storyboard_bytes(song_document_t document) -> std::string {
    document.revision = 1;
    const auto written = write_native_project(document);
    require_storyboard(written.value);
    return *written.value;
}
struct storyboard_workflow_t {
    document_history_t history{document_history_t::from(storyboard_song())};
    workspace_controller_t workspace;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> compose;
    std::optional<timing_lane_pseudonote_outcome_t> timing_stage;
    notification_center_t notifications;
    notification_sink_t sink{notifications};
    project_identity_t identity{.session_loaded = true};
    frontend_workspace_view_t projection;
    native_project_recovery_controller_view_t recovery;
    std::string status;
    std::uint64_t session{7};
    nuklear_event_harness_t harness;

    void initialize() {
        require_storyboard(harness.ready());
        const auto bound = workspace_controller_t::from(history);
        require_storyboard(bound.value);
        workspace = *bound.value;
        open();
    }
    void open() {
        auto registry = command_registry_t::from([&](const command_invocation_context_t& invocation) {
            const auto command = parse_application_command(invocation.command.value);
            if (!command) return dispatch_outcome_t{};
            return invoke_application_command(*command, {
                .document = {.history = history, .workspace = workspace, .notifications = sink},
                .editor = {.commands = application, .retained = compose, .timing_stage = timing_stage}});
        });
        require_storyboard(refresh_application_command_states(registry, history, workspace, compose,
            application, identity).empty());
        const auto* command = find_command(registry, {"workspace.open_storyboard"});
        require_storyboard(command);
        require_storyboard(command->definition.enabled);
        const auto invoked = invoke_command(registry, {"workspace.open_storyboard"},
            {.workspace_active = true, .active_tab = workspace.active_tab,
             .visible_tabs = {workspace_tab_t::setup, workspace_tab_t::compose, workspace_tab_t::storyboard}});
        require_storyboard(invoked.value.is_invoked());
        project();
    }
    void project() {
        const auto model = wormedit::workspace_view_t::from(workspace, history.document);
        require_storyboard(model.value);
        projection.query_session_generation = session;
        projection.refill(*model.value, history.document, status, recovery, false);
        application.project.storyboard.refill(projection.view.storyboard, history.document, session);
        harness.view() = projection.view;
        harness.view().project_loaded = true;
        (void)harness.frame();
    }
    auto dispatch(const ui::storyboard_action_t& action, bool busy = false) -> bool {
        return application.project.storyboard.dispatch(action,
            {.history = history, .workspace = workspace, .notifications = sink},
            application, compose ? &*compose : nullptr, busy);
    }
    void pick(std::string_view label) {
        const auto* selected = harness.view().storyboard.selected();
        (void)click_storyboard_text(harness, selected ? std::string_view{selected->name} : "Choose storyboard");
        (void)harness.frame();
        const auto choice = click_storyboard_text(harness, label);
        require_storyboard(dispatch(storyboard_intent(choice)));
        project();
    }
};
} // namespace

TEST_CASE("Storyboard opens through its enabled registry command without a chart", "[frontend-state][event-harness][storyboard-tab]") {
    storyboard_workflow_t flow;
    flow.initialize();
    check_storyboard(flow.history.document.charts.empty());
    check_storyboard(flow.workspace.active_tab == workspace_tab_t::storyboard);
    check_storyboard(flow.harness.view().storyboard.visible);
    check_storyboard(!flow.harness.view().compose.visible);
    check_storyboard(flow.harness.view().storyboard.storyboards.size() == 3);
    check_storyboard(flow.history.undo.empty());
}
TEST_CASE("Storyboard picker reuses cached rows while canonical selection changes", "[frontend-state][event-harness][storyboard-tab]") {
    storyboard_workflow_t flow;
    flow.initialize();
    const auto refills = flow.projection.storyboard.rows.build_count();
    const auto* rows = flow.projection.view.storyboard.storyboards.data();
    flow.pick("Opening  [OSBX]");
    check_storyboard(flow.application.project.storyboard.state().selection.storyboard == storyboard_id_t{11});
    flow.pick("Imported  [OSB]");
    check_storyboard(flow.application.project.storyboard.state().selection.storyboard == storyboard_id_t{12});
    check_storyboard(flow.harness.view().storyboard.selected()->kind == storyboard_kind_t::osb);
    check_storyboard(flow.projection.storyboard.rows.build_count() == refills);
    check_storyboard(flow.projection.view.storyboard.storyboards.data() == rows);
    check_storyboard(flow.history.undo.empty());
}
TEST_CASE("Storyboard New Delete and Undo run SB-1 with explicit created identity", "[frontend-state][event-harness][storyboard-tab]") {
    storyboard_workflow_t flow;
    flow.initialize();
    flow.pick("Opening  [OSBX]");
    const auto before = storyboard_bytes(flow.history.document);
    const auto created = flow.harness.click_widget("New storyboard");
    require_storyboard(flow.dispatch(storyboard_intent(created)));
    const auto id = flow.application.project.storyboard.state().selection.storyboard;
    require_storyboard(id && id != storyboard_id_t{11});
    const auto found = std::ranges::find(flow.history.document.storyboards, id, &storyboard_document_t::id);
    require_storyboard(found != flow.history.document.storyboards.end());
    check_storyboard(found->osbx.has_value());
    check_storyboard(flow.history.undo.size() == 1);
    check_storyboard(flow.workspace.document_revision == flow.history.document.revision);
    const auto after = storyboard_bytes(flow.history.document);
    require_storyboard(undo_document_command(flow.history));
    check_storyboard(storyboard_bytes(flow.history.document) == before);
    require_storyboard(redo_document_command(flow.history));
    check_storyboard(storyboard_bytes(flow.history.document) == after);
    const auto rebound = rebind_workspace(flow.workspace, flow.history);
    require_storyboard(rebound.value);
    flow.project();
    const auto deleted = flow.harness.click_widget("Delete storyboard");
    require_storyboard(flow.dispatch(storyboard_intent(deleted)));
    check_storyboard(!flow.application.project.storyboard.state().selection.storyboard);
    check_storyboard(std::ranges::find(flow.history.document.storyboards, id,
        &storyboard_document_t::id) == flow.history.document.storyboards.end());
    require_storyboard(undo_document_command(flow.history));
    check_storyboard(storyboard_bytes(flow.history.document) == after);
}
TEST_CASE("Storyboard rejected stale missing read-only and busy actions preserve selection", "[frontend-state][event-harness][storyboard-tab]") {
    const auto mode = GENERATE(0, 1, 2, 3, 4, 5, 6);
    storyboard_workflow_t flow;
    flow.initialize();
    flow.pick(mode == 2 ? "Imported  [OSB]" : mode == 3 ? "Generated  [OSBX · Generated]" : "Opening  [OSBX]");
    const auto before = storyboard_bytes(flow.history.document);
    const auto selection = flow.application.project.storyboard.state().selection;
    auto action = storyboard_action_t::from(flow.harness.view().storyboard,
        storyboard_action_kind_t::erase, selection.storyboard);
    if (mode == 0) --action.revision;
    if (mode == 1) action.storyboard = storyboard_id_t{999};
    if (mode == 4) --action.session_generation;
    if (mode == 6) action.project = song_id_t{999};
    require_storyboard(flow.dispatch(action, mode == 5));
    check_storyboard(storyboard_bytes(flow.history.document) == before);
    check_storyboard(flow.application.project.storyboard.state().selection == selection);
    check_storyboard(flow.history.undo.empty());
    flow.notifications.drain_inbox(notification_time_t{});
    const auto entries = flow.notifications.snapshot(16);
    require_storyboard(!entries.empty());
    check_storyboard(std::ranges::any_of(entries, [](const auto& entry) {
        return entry.severity == notification_severity_t::error || entry.severity == notification_severity_t::warning;
    }));
}
TEST_CASE("Storyboard project reset and same-revision reopen clear state and refresh rows", "[frontend-state][event-harness][storyboard-tab]") {
    storyboard_workflow_t flow;
    flow.initialize();
    flow.pick("Opening  [OSBX]");
    const auto old = storyboard_action_t::from(flow.harness.view().storyboard,
        storyboard_action_kind_t::erase, storyboard_id_t{11});
    const auto refills = flow.projection.storyboard.rows.build_count();
    reset_application_project_edit_state(flow.application);
    ++flow.session; // The application's existing project-session generation owner.
    flow.history.document.storyboards[0].name = "Reopened";
    flow.project();
    check_storyboard(!flow.application.project.storyboard.state().selection.storyboard);
    check_storyboard(flow.harness.view().storyboard.storyboards[0].name == "Reopened");
    check_storyboard(flow.projection.storyboard.rows.build_count() == refills + 1);
    const auto before = storyboard_bytes(flow.history.document);
    require_storyboard(flow.dispatch(old));
    check_storyboard(storyboard_bytes(flow.history.document) == before);
    check_storyboard(flow.history.undo.empty());
}

TEST_CASE("Storyboard layout is project view state and New uses shared pause service", "[frontend-state][event-harness][storyboard-tab]") {
    storyboard_workflow_t flow;
    flow.initialize();
    bool playing = true;
    int pauses = 0;
    flow.application.services.transport_preview_playing = [&] { return playing; };
    flow.application.services.transport_play_pause = [&] { ++pauses; playing = false; return std::string{}; };
    flow.pick("Opening  [OSBX]");
    check_storyboard(playing && pauses == 0);
    auto layout = storyboard_action_t::from(flow.harness.view().storyboard, storyboard_action_kind_t::set_dock);
    layout.dock.set_width(260);
    require_storyboard(flow.dispatch(layout));
    flow.project();
    check_storyboard(flow.harness.view().storyboard.state.dock.width() == 260);
    check_storyboard(flow.history.undo.empty());
    const auto created = flow.harness.click_widget("New storyboard");
    require_storyboard(flow.dispatch(storyboard_intent(created)));
    check_storyboard(!playing && pauses == 1);
    check_storyboard(flow.history.undo.size() == 1);
    reset_application_project_edit_state(flow.application);
    ++flow.session;
    flow.project();
    check_storyboard(flow.harness.view().storyboard.state.dock.width() == ui_layout_preferences_t{}.width());
}
TEST_CASE("Storyboard creation limit disables New and rejects forged creation", "[frontend-state][event-harness][storyboard-tab]") {
    storyboard_workflow_t flow;
    flow.initialize();
    flow.workspace.limits.maximum_storyboards = flow.history.document.storyboards.size();
    flow.project();
    check_storyboard(!flow.harness.view().storyboard.create_available);
    const auto disabled = flow.harness.click_widget("New storyboard");
    check_storyboard(storyboard_intent(disabled).kind == storyboard_action_kind_t::none);
    const auto before = storyboard_bytes(flow.history.document);
    require_storyboard(flow.dispatch(storyboard_action_t::from(flow.harness.view().storyboard,
        storyboard_action_kind_t::create)));
    check_storyboard(storyboard_bytes(flow.history.document) == before);
    check_storyboard(flow.history.undo.empty());
}

TEST_CASE("Storyboard dock pointer retires when the tab is hidden", "[frontend-state][event-harness][storyboard-tab]") {
    nuklear_event_harness_t harness;
    require_storyboard(harness.ready());
    show_storyboard(harness);
    (void)harness.frame();
    const auto handle = harness.widget_center("Resize dock width");
    const std::array down{test::mouse_motion(handle.first, handle.second),
        test::mouse_button(true, handle.first, handle.second)};
    (void)harness.frame(down);
    harness.view().storyboard.visible = false;
    harness.view().compose.visible = true;
    harness.view().active_tab = WORM_UI_WORKSPACE_TAB_COMPOSE;
    (void)harness.frame();
    show_storyboard(harness);
    const auto motion = test::mouse_motion(handle.first - 30, handle.second);
    const auto returned = harness.frame(std::span{&motion, 1});
    check_storyboard(returned.get().storyboard.kind == storyboard_action_kind_t::none);
    const auto up = test::mouse_button(false, handle.first - 30, handle.second);
    (void)harness.frame(std::span{&up, 1});
}


namespace {
void initialize_detach_workflow(storyboard_workflow_t& flow) {
    auto& song = flow.history.document;
    song.charts = {{.id = chart_id_t{100}, .measures = {{.id = measure_id_t{101}}}}};
    auto& board = song.storyboards.back();
    board.osbx->nodes = {{.id = "generated-root", .lifetime = osbx_lifetime_t{.begin_ms = 10, .end_ms = 300}}};
    const std::string source = "return {}";
    song.generator_recipes.front() = {.id = recipe_id_t{40}, .api_version = "1",
        .source_path = "scripts/story.lua", .source_hash = native_project_content_checksum(source),
        .source = source, .chart = chart_id_t{100},
        .generated_storyboards = {{.key = "scene", .storyboard = board.id, .value = *board.osbx}}};
    flow.initialize();
    flow.pick("Generated  [OSBX · Generated]");
}
auto click_detach_dock(storyboard_workflow_t& flow, int dock) -> storyboard_action_t {
    std::vector<std::pair<float, float>> points;
    for (const auto& text : flow.harness.drawn_text_bounds())
        if (std::string_view{text.text} == "Detach")
            points.emplace_back(text.x + text.width * 0.5f, text.y + text.height * 0.5f);
    REQUIRE(points.size() == 3);
    const auto point = dock == 2 ? *std::ranges::max_element(points, {}, &std::pair<float, float>::second) :
        dock == 0 ? *std::ranges::min_element(points, {}, &std::pair<float, float>::first) :
                    *std::ranges::max_element(points, {}, &std::pair<float, float>::first);
    return storyboard_intent(test::click_at(flow.harness, point.first, point.second));
}
void check_detach_action_capture(const storyboard_workflow_t& flow, const storyboard_action_t& action) {
    CHECK(action.kind == storyboard_action_kind_t::detach_generator);
    CHECK(action.storyboard == storyboard_id_t{13});
    CHECK(action.expected_owner == recipe_id_t{40});
    CHECK(action.project == flow.history.document.id);
    CHECK(action.revision == flow.history.document.revision);
    CHECK(action.session_generation == flow.session);
}
void check_detached_workflow(const storyboard_workflow_t& flow, const storyboard_document_t& generated) {
    auto expected = generated;
    expected.owning_generator_recipe.reset();
    CHECK(flow.history.document.storyboards.back() == expected);
    CHECK(flow.history.document.generator_recipes.size() == 1);
    CHECK(flow.history.document.generator_recipes.front().generated_storyboards.empty());
    CHECK(flow.history.undo.size() == 1);
    CHECK(flow.application.project.storyboard.state().selection.storyboard == storyboard_id_t{13});
}
void check_detach_projection(storyboard_workflow_t& flow, bool editable) {
    flow.project();
    const auto* entry = flow.harness.view().storyboard.selected();
    REQUIRE(entry);
    CHECK(entry->is_editable() == editable);
    CHECK(entry->generator_owned == !editable);
    CHECK(flow.harness.drawn_text().contains("Detach") == !editable);
}
} // namespace

TEST_CASE("Storyboard each read-only dock detaches only its captured generator board through the runtime", "[frontend-state][event-harness][storyboard-detach]") {
    const auto dock = GENERATE(0, 1, 2);
    storyboard_workflow_t flow;
    initialize_detach_workflow(flow);
    const auto generated = flow.history.document.storyboards.back();
    bool playing = true;
    int pauses = 0;
    flow.application.services.transport_preview_playing = [&] { return playing; };
    flow.application.services.transport_play_pause = [&] { ++pauses; playing = false; return std::string{}; };
    CHECK(flow.harness.drawn_text().contains("scripts/story.lua"));
    const auto action = click_detach_dock(flow, dock);
    check_detach_action_capture(flow, action);
    REQUIRE(flow.dispatch(action));
    check_detached_workflow(flow, generated);
    CHECK(pauses == 1);
    CHECK_FALSE(playing);
    check_detach_projection(flow, true);
}

TEST_CASE("Storyboard generator detach undo restores read-only docks and redo restores editing", "[frontend-state][event-harness][storyboard-detach]") {
    storyboard_workflow_t flow;
    initialize_detach_workflow(flow);
    const auto before = storyboard_bytes(flow.history.document);
    REQUIRE(flow.dispatch(click_detach_dock(flow, 1)));
    REQUIRE(undo_document_command(flow.history));
    REQUIRE(rebind_workspace(flow.workspace, flow.history).value);
    CHECK(storyboard_bytes(flow.history.document) == before);
    check_detach_projection(flow, false);
    REQUIRE(redo_document_command(flow.history));
    REQUIRE(rebind_workspace(flow.workspace, flow.history).value);
    check_detach_projection(flow, true);
}

TEST_CASE("Storyboard generator detach refuses another selection stale owner and busy work", "[frontend-state][event-harness][storyboard-detach]") {
    const auto mode = GENERATE(0, 1, 2, 3);
    storyboard_workflow_t flow;
    initialize_detach_workflow(flow);
    auto action = click_detach_dock(flow, 0);
    if (mode == 0) action.expected_owner = recipe_id_t{41};
    if (mode == 1) flow.pick("Opening  [OSBX]");
    if (mode == 3) --action.revision;
    const auto before = storyboard_bytes(flow.history.document);
    REQUIRE(flow.dispatch(action, mode == 2));
    CHECK(storyboard_bytes(flow.history.document) == before);
    CHECK(flow.history.undo.empty());
}
