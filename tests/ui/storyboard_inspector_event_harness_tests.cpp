#include "nuklear_event_harness.h"
#include "../support/document_assertions.h"
#include "ui/frontend/storyboard.h"
#include "sys/app/application_command_state.h"
#include "sys/storyboard/storyboard_inspector_view.h"
#include <wormedit/core/osbx_lua_link.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <source_location>

namespace {
using namespace wormedit;
using namespace wormedit::ui;
void check_inspector_event(bool value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line()); CHECK(value);
}
void show_inspector(nuklear_event_harness_t& harness) {
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
    view.compose.visible = false;
    auto& storyboard = view.storyboard;
    storyboard.visible = true; storyboard.project = view.project_id;
    storyboard.revision = view.document_revision; storyboard.session_generation = 7;
    storyboard.storyboards = {{.id = storyboard_id_t{11}, .name = "Scene", .kind = storyboard_kind_t::osbx}};
    storyboard.refill_state({.selection = {.storyboard = storyboard_id_t{11}, .nodes = {"sprite"}}});
    storyboard.inspector = {.node = "sprite", .editable = true, .origin = "Centre", .order = 314,
        .opacity = {.value = {0.625}, .editable = true},
        .time = {.milliseconds = -2, .remainder = 1, .denominator = 2}, .key_time_ms = -2};
}
auto key_event(bool down, SDL_Keycode key, SDL_Scancode scan) -> SDL_Event {
    SDL_Event event{}; event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.down = down; event.key.key = key; event.key.scancode = scan; return event;
}
auto text_event(const char* text) -> SDL_Event {
    SDL_Event event{}; event.type = SDL_EVENT_TEXT_INPUT; event.text.text = text; return event;
}
auto find_text(nuklear_event_harness_t& harness, std::string_view text) -> std::pair<float, float> {
    INFO(harness.drawn_text());
    const auto point = harness.drawn_text_center(text); REQUIRE(point); return *point;
}
void focus_value(nuklear_event_harness_t& harness, std::string_view text) {
    const auto point = find_text(harness, text);
    (void)test::click_at(harness, point.first + 8, point.second);
    const auto select = key_event(true, SDLK_A, SDL_SCANCODE_A);
    (void)harness.frame(std::span{&select, 1}, test::primary_modifier);
    const auto release = key_event(false, SDLK_A, SDL_SCANCODE_A);
    (void)harness.frame(std::span{&release, 1});
}
auto inspector_runtime_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 1, .storyboards = {
        {.id = storyboard_id_t{11}, .name = "Scene", .osbx = osbx_document_t{
            .nodes = {{.id = "sprite", .order = 314}}}}}};
}
struct inspector_runtime_workflow_t {
    nuklear_event_harness_t harness{1280, 900};
    document_history_t history{document_history_t::from(inspector_runtime_song())};
    workspace_controller_t workspace;
    sys::application_command_state_t application;
    editor_interaction_state_t editor;
    notification_center_t notifications;
    notification_sink_t sink{notifications};
    std::pair<float, float> scrub_point;
    bool playing{};
    int pause_count{};
    auto dispatch(const storyboard_action_t& action, bool busy = false) -> bool {
        return application.project.storyboard.dispatch(action,
            {.history = history, .workspace = workspace, .notifications = sink}, application, &editor, busy);
    }
    void project() {
        harness.view().project_id = history.document.id; harness.view().document_revision = history.document.revision;
        auto& view = harness.view().storyboard;
        view.project = history.document.id; view.revision = history.document.revision;
        view.canvas.prepared_generation = osbx_lua_prepared_scope_t::generation();
        application.project.storyboard.refill(view, history.document, 7);
        sys::build_storyboard_inspector_view(view, history.document, {});
        (void)harness.frame();
    }
    void initialize() {
        REQUIRE(harness.ready());
        const auto bound = workspace_controller_t::from(history); REQUIRE(bound.value);
        workspace = *bound.value; workspace.active_tab = workspace_tab_t::storyboard;
        harness.view().project_id = history.document.id; harness.view().document_revision = history.document.revision;
        show_inspector(harness);
        auto& view = harness.view().storyboard;
        application.project.storyboard.refill(view, history.document, 7);
        REQUIRE(dispatch(storyboard_action_t::from(view, storyboard_action_kind_t::select, storyboard_id_t{11})));
        application.project.storyboard.refill(view, history.document, 7);
        view.canvas.prepared_generation = osbx_lua_prepared_scope_t::generation();
        auto selection = storyboard_action_t::from(view, storyboard_action_kind_t::select_nodes, storyboard_id_t{11});
        selection.nodes = {"sprite"}; REQUIRE(dispatch(selection));
        application.services.transport_preview_playing = [this] { return playing; };
        application.services.transport_play_pause = [this] { playing = false; ++pause_count; return std::string{}; };
        project();
    }
    auto begin_order_scrub() -> storyboard_action_t {
        scrub_point = find_text(harness, "Order");
        const auto [x, y] = scrub_point;
        (void)harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)});
        const auto action = harness.frame(std::array{test::mouse_motion(x + 12, y)});
        REQUIRE(action.get().storyboard.inspector.phase == storyboard_inspector_phase_t::begin);
        REQUIRE(dispatch(action.get().storyboard));
        return action.get().storyboard;
    }
};
} // namespace

TEST_CASE("Storyboard inspector numeric Enter commits once with captured identity and key time",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    (void)harness.frame(); focus_value(harness, "314");
    const auto text = text_event("42");
    check_inspector_event(harness.frame(std::span{&text, 1}).get().storyboard.kind == storyboard_action_kind_t::none);
    const auto enter = key_event(true, SDLK_RETURN, SDL_SCANCODE_RETURN);
    const auto snapshot = harness.frame(std::span{&enter, 1});
    const auto& action = snapshot.get().storyboard;
    check_inspector_event(action.kind == storyboard_action_kind_t::inspector);
    check_inspector_event(action.inspector.field == storyboard_inspector_field_t::order && action.inspector.order == 42);
    check_inspector_event(action.inspector.phase == storyboard_inspector_phase_t::commit);
    check_inspector_event(action.project == harness.view().project_id && action.session_generation == 7);
    check_inspector_event(action.storyboard == storyboard_id_t{11} && action.inspector.node == "sprite");
    check_inspector_event(action.inspector.key_time_ms == -2);
    const auto release = key_event(false, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_inspector_event(harness.frame(std::span{&release, 1}).get().storyboard.kind == storyboard_action_kind_t::none);
    check_inspector_event(harness.frame().get().storyboard.kind == storyboard_action_kind_t::none);
}
TEST_CASE("Storyboard inspector blur commits and Escape discards a field draft",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    (void)harness.frame(); focus_value(harness, "314");
    const auto text = text_event("56"); (void)harness.frame(std::span{&text, 1});
    const auto point = find_text(harness, "Inspector");
    const auto clicks = test::click_at(harness, point.first, point.second);
    const auto commit = std::ranges::find_if(clicks, [](const auto& frame) {
        return frame.get().storyboard.kind == storyboard_action_kind_t::inspector &&
            frame.get().storyboard.inspector.phase == storyboard_inspector_phase_t::commit;
    });
    REQUIRE(commit != clicks.end()); check_inspector_event(commit->get().storyboard.inspector.order == 56);
    (void)harness.frame(); focus_value(harness, "314");
    const auto changed = text_event("99"); (void)harness.frame(std::span{&changed, 1});
    const auto escape = key_event(true, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    const auto cancelled = harness.frame(std::span{&escape, 1});
    check_inspector_event(cancelled.get().storyboard.kind == storyboard_action_kind_t::none);
    check_inspector_event(cancelled.get().shortcut_events.empty());
    const auto release = key_event(false, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    (void)harness.frame(std::span{&release, 1});
    check_inspector_event(harness.frame().get().storyboard.kind == storyboard_action_kind_t::none);
}

TEST_CASE("Storyboard inspector occupied auto-key captures the stable key ID separately from fractional time",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    auto& track = harness.view().storyboard.inspector.opacity;
    track.keyed = true; track.key = "occupied";
    (void)harness.frame(); focus_value(harness, "0.625");
    const auto text = text_event("0.4"); (void)harness.frame(std::span{&text, 1});
    const auto enter = key_event(true, SDLK_RETURN, SDL_SCANCODE_RETURN);
    const auto committed = harness.frame(std::span{&enter, 1});
    const auto& action = committed.get().storyboard.inspector;
    check_inspector_event(action.field == storyboard_inspector_field_t::opacity);
    check_inspector_event(action.key == "occupied" && action.key_time_ms == -2);
    check_inspector_event(action.value == std::vector<double>{0.4});
}
TEST_CASE("Storyboard inspector field keeps its activation stamp through a refreshed revision",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    (void)harness.frame(); focus_value(harness, "314");
    const auto captured = harness.view().storyboard.revision;
    ++harness.view().storyboard.revision;
    const auto changed = text_event("77"); (void)harness.frame(std::span{&changed, 1});
    const auto enter = key_event(true, SDLK_RETURN, SDL_SCANCODE_RETURN);
    const auto action = harness.frame(std::span{&enter, 1});
    check_inspector_event(action.get().storyboard.revision == captured);
    check_inspector_event(action.get().storyboard.inspector.order == 77);
}
TEST_CASE("Storyboard inspector readonly node refuses keyboard and scrub edits",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    auto& inspector = harness.view().storyboard.inspector;
    inspector.editable = false; inspector.opacity.editable = false;
    inspector.reason = "Lua-linked children: detach before editing.";
    (void)harness.frame();
    const auto point = find_text(harness, "314");
    const auto clicked = test::click_at(harness, point.first, point.second);
    check_inspector_event(std::ranges::all_of(clicked, [](const auto& frame) {
        return frame.get().storyboard.kind == storyboard_action_kind_t::none;
    }));
    check_inspector_event(harness.drawn_text().contains("Lua-linked children"));
}
TEST_CASE("Storyboard inspector label scrub emits one captured begin revise apply sequence",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    (void)harness.frame();
    const auto point = find_text(harness, "Opacity ◇");
    (void)harness.frame(std::array{test::mouse_motion(point.first, point.second), test::mouse_button(true, point.first, point.second)});
    const auto start = harness.frame(std::array{test::mouse_motion(point.first + 10, point.second)});
    check_inspector_event(start.get().storyboard.inspector.phase == storyboard_inspector_phase_t::begin);
    const auto moved = harness.frame(std::array{test::mouse_motion(point.first + 20, point.second)});
    check_inspector_event(moved.get().storyboard.inspector.phase == storyboard_inspector_phase_t::revise);
    const auto released = harness.frame(std::array{test::mouse_motion(point.first + 20, point.second), test::mouse_button(false, point.first + 20, point.second)});
    check_inspector_event(released.get().storyboard.inspector.phase == storyboard_inspector_phase_t::apply);
    check_inspector_event(released.get().storyboard.inspector.value == std::vector<double>{2.625});
    check_inspector_event(released.get().storyboard.inspector.node == "sprite");
}

TEST_CASE("Storyboard inspector readonly tracks do not reenable later transform buttons",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    auto& inspector = harness.view().storyboard.inspector;
    inspector.editable = false; inspector.opacity.editable = false;
    inspector.ops = {{.id = "move", .label = "Translate", .ordinal = 1,
        .can_move_up = true, .can_move_down = true, .track = {.value = {10, 20}}}};
    (void)harness.frame();
    const auto point = find_text(harness, "Remove");
    const auto clicks = test::click_at(harness, point.first, point.second);
    check_inspector_event(std::ranges::all_of(clicks, [](const auto& frame) {
        return frame.get().storyboard.kind == storyboard_action_kind_t::none;
    }));
}
TEST_CASE("Storyboard inspector Escape cancels a scrub without a commit",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    (void)harness.frame(); const auto point = find_text(harness, "Opacity ◇");
    (void)harness.frame(std::array{test::mouse_motion(point.first, point.second), test::mouse_button(true, point.first, point.second)});
    (void)harness.frame(std::array{test::mouse_motion(point.first + 10, point.second)});
    const auto escape = key_event(true, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    const auto cancelled = harness.frame(std::span{&escape, 1});
    check_inspector_event(cancelled.get().storyboard.inspector.phase == storyboard_inspector_phase_t::cancel);
    const auto released = harness.frame(std::array{test::mouse_button(false, point.first + 10, point.second)});
    check_inspector_event(released.get().storyboard.kind == storyboard_action_kind_t::none);
}

TEST_CASE("Storyboard inspector invalid numeric text emits a captured refusal request",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    (void)harness.frame(); focus_value(harness, "314");
    const auto changed = text_event("bad"); (void)harness.frame(std::span{&changed, 1});
    const auto enter = key_event(true, SDLK_RETURN, SDL_SCANCODE_RETURN);
    const auto action = harness.frame(std::span{&enter, 1});
    check_inspector_event(action.get().storyboard.kind == storyboard_action_kind_t::inspector);
    check_inspector_event(!action.get().storyboard.inspector.has_valid_value);
}

TEST_CASE("Storyboard inspector lifetime label scrub keeps exact signed endpoints",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    constexpr auto high = std::numeric_limits<std::int64_t>::max();
    harness.view().storyboard.inspector.lifetime = osbx_lifetime_t{.begin_ms = high - 100, .end_ms = high};
    (void)harness.frame(); const auto point = find_text(harness, "Begin ms");
    (void)harness.frame(std::array{test::mouse_motion(point.first, point.second), test::mouse_button(true, point.first, point.second)});
    const auto begin = harness.frame(std::array{test::mouse_motion(point.first + 12, point.second)});
    check_inspector_event(begin.get().storyboard.inspector.phase == storyboard_inspector_phase_t::begin);
    const auto released = harness.frame(std::array{test::mouse_button(false, point.first + 12, point.second)});
    const auto& action = released.get().storyboard.inspector;
    REQUIRE(action.lifetime);
    check_inspector_event(action.phase == storyboard_inspector_phase_t::apply);
    check_inspector_event(action.lifetime->begin_ms == high - 98 && action.lifetime->end_ms == high);
}

TEST_CASE("Storyboard inspector blur commit survives another field gaining focus in the same frame",
          "[frontend-state][event-harness][storyboard-inspector]") {
    nuklear_event_harness_t harness{1280, 900}; REQUIRE(harness.ready()); show_inspector(harness);
    (void)harness.frame(); focus_value(harness, "314");
    const auto changed = text_event("42"); (void)harness.frame(std::span{&changed, 1});
    const auto point = find_text(harness, "sprite");
    const auto clicks = test::click_at(harness, point.first + 8, point.second);
    const auto commit = std::ranges::find_if(clicks, [](const auto& frame) {
        return frame.get().storyboard.kind == storyboard_action_kind_t::inspector &&
            frame.get().storyboard.inspector.phase == storyboard_inspector_phase_t::commit;
    });
    REQUIRE(commit != clicks.end());
    check_inspector_event(commit->get().storyboard.inspector.field == storyboard_inspector_field_t::order);
    check_inspector_event(commit->get().storyboard.inspector.order == 42);
    const auto renamed = text_event("renamed");
    (void)harness.frame(std::span{&renamed, 1});
    const auto enter = key_event(true, SDLK_RETURN, SDL_SCANCODE_RETURN);
    const auto rename = harness.frame(std::span{&enter, 1});
    REQUIRE(rename.get().storyboard.kind == storyboard_action_kind_t::inspector);
    check_inspector_event(rename.get().storyboard.inspector.phase == storyboard_inspector_phase_t::commit);
    check_inspector_event(rename.get().storyboard.inspector.field == storyboard_inspector_field_t::id);
    check_inspector_event(rename.get().storyboard.inspector.text == "renamed");
}

TEST_CASE("Storyboard inspector typed event commits through production runtime and stale replay refuses",
          "[frontend-state][event-harness][storyboard-inspector]") {
    inspector_runtime_workflow_t flow; flow.initialize();
    focus_value(flow.harness, "314");
    const auto changed = text_event("42"); (void)flow.harness.frame(std::span{&changed, 1});
    const auto enter = key_event(true, SDLK_RETURN, SDL_SCANCODE_RETURN);
    const auto captured = flow.harness.frame(std::span{&enter, 1});
    REQUIRE(flow.dispatch(captured.get().storyboard));
    check_inspector_event(flow.history.document.storyboards[0].osbx->nodes[0].order == 42);
    check_inspector_event(flow.history.undo.size() == 1);
    REQUIRE(flow.dispatch(captured.get().storyboard));
    check_inspector_event(flow.history.undo.size() == 1);
    check_inspector_event(flow.workspace.document_revision == flow.history.document.revision);
}

TEST_CASE("Storyboard inspector zero-chart scrub uses the retained editor state and one Undo",
          "[frontend-state][event-harness][storyboard-inspector]") {
    inspector_runtime_workflow_t flow; flow.initialize();
    const auto before = flow.history.document;
    (void)flow.begin_order_scrub();
    check_inspector_event(flow.history.document.charts.empty() && !flow.editor.chart && flow.editor.is_pending());
    test::check_same_document(flow.history.document, before);
    check_inspector_event(flow.history.undo.empty());
    const auto [x, y] = flow.scrub_point;
    const auto revised = flow.harness.frame(std::array{test::mouse_motion(x + 24, y)});
    REQUIRE(flow.dispatch(revised.get().storyboard));
    const auto applied = flow.harness.frame(std::array{test::mouse_button(false, x + 24, y)});
    REQUIRE(flow.dispatch(applied.get().storyboard));
    check_inspector_event(!flow.editor.is_pending() && flow.history.undo.size() == 1);
    check_inspector_event(flow.history.document.storyboards[0].osbx->nodes[0].order == 318);
    REQUIRE(undo_document_command(flow.history));
    check_inspector_event(flow.history.document.storyboards == before.storyboards);
}

TEST_CASE("Storyboard inspector focus pauses transport without creating a key or edit",
          "[frontend-state][event-harness][storyboard-inspector]") {
    inspector_runtime_workflow_t flow; flow.initialize(); flow.playing = true;
    const auto before = flow.history.document;
    const auto point = find_text(flow.harness, "314");
    const auto clicks = test::click_at(flow.harness, point.first + 8, point.second);
    const auto focused = std::ranges::find_if(clicks, [](const auto& frame) {
        return frame.get().storyboard.kind == storyboard_action_kind_t::inspector &&
            frame.get().storyboard.inspector.phase == storyboard_inspector_phase_t::focus;
    });
    REQUIRE(focused != clicks.end()); REQUIRE(flow.dispatch(focused->get().storyboard));
    check_inspector_event(!flow.playing && flow.pause_count == 1);
    check_inspector_event(!flow.editor.is_pending() && flow.history.undo.empty());
    test::check_same_document(flow.history.document, before);
}

TEST_CASE("Storyboard inspector focus hidden and modal loss cancel the admitted backend gesture",
          "[frontend-state][event-harness][storyboard-inspector]") {
    const auto loss = GENERATE(0, 1, 2);
    inspector_runtime_workflow_t flow; flow.initialize();
    const auto before = flow.history.document;
    (void)flow.begin_order_scrub(); REQUIRE(flow.editor.is_pending());
    std::vector<SDL_Event> events;
    if (loss == 0) {
        SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; events.push_back(event);
    } else if (loss == 1) {
        flow.harness.view().storyboard.visible = false;
        flow.harness.view().active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
        flow.workspace.active_tab = workspace_tab_t::setup;
    } else flow.harness.view().options.visible = true;
    const auto cancelled = flow.harness.frame(events);
    check_inspector_event(cancelled.get().storyboard.inspector.phase == storyboard_inspector_phase_t::cancel);
    REQUIRE(flow.dispatch(cancelled.get().storyboard, true));
    check_inspector_event(!flow.editor.is_pending());
    test::check_same_document(flow.history.document, before);
    check_inspector_event(flow.history.undo.empty());
}

TEST_CASE("Storyboard inspector stale terminal apply clears its admitted gesture without publishing",
          "[frontend-state][event-harness][storyboard-inspector]") {
    inspector_runtime_workflow_t flow; flow.initialize();
    (void)flow.begin_order_scrub();
    const auto [x, y] = flow.scrub_point;
    const auto terminal = flow.harness.frame(std::array{test::mouse_button(false, x + 12, y)});
    REQUIRE(apply_osbx_edit(flow.history, {.storyboard = storyboard_id_t{11},
        .change = osbx_set_origin_t{.node = "sprite", .origin = "TopLeft"}}).applied);
    REQUIRE(flow.dispatch(terminal.get().storyboard));
    check_inspector_event(!flow.editor.is_pending() && flow.history.undo.size() == 1);
    check_inspector_event(flow.history.document.storyboards[0].osbx->nodes[0].order == 314);
}
