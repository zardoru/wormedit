#include "nuklear_event_harness.h"
#include "sys/audio/transport_player_actions.h"
#include "sys/commands/application_command_dispatch.h"
#include "sys/project/project_open.h"
#include "sys/storyboard/storyboard_transport_view.h"
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/core/lane_profiles.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>

using namespace wormedit;
using namespace wormedit::ui;
namespace {
auto transport_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 1, .charts = {
        {.id = chart_id_t{1}, .divisions_per_standard_measure = 192,
         .measures = {{.id = measure_id_t{1}}},
         .lane_layouts = {{.id = object_id_t{1}, .position = {.measure = measure_id_t{1}},
            .layout = make_bms_5k_profile().layout}}}}};
}
auto transport_key(bool down, SDL_Keycode code, SDL_Scancode scan) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.down = down; event.key.key = code; event.key.scancode = scan;
    return event;
}
struct transport_workflow_t {
    nuklear_event_harness_t& harness;
    document_history_t history{document_history_t::from(transport_song())};
    workspace_controller_t workspace;
    sys::application_command_state_t application;
    std::optional<editor_interaction_state_t> editor;
    std::optional<timing_lane_pseudonote_outcome_t> timing_stage;
    notification_center_t notifications{};
    notification_sink_t sink{notifications};
    command_registry_t registry;
    bool playing{};
    int starts{}, stops{}, rate_changes{}, loop_changes{};
    std::optional<storyboard_loop_region_t> applied_loop;

    auto invoke(const command_invocation_context_t& context) -> dispatch_outcome_t {
        const auto command = sys::parse_application_command(context.command.value);
        REQUIRE(command);
        return sys::invoke_application_command(*command,
            {.document = {.history = history, .workspace = workspace, .notifications = sink},
             .editor = {.commands = application, .retained = editor, .timing_stage = timing_stage}});
    }
    void initialize() {
        REQUIRE(harness.ready());
        const auto bound = workspace_controller_t::from(history); REQUIRE(bound.value);
        workspace = *bound.value; workspace.active_tab = workspace_tab_t::storyboard;
        auto& view = harness.view();
        view.active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
        view.compose.visible = false;
        view.storyboard.visible = true;
        view.storyboard.project = view.project_id;
        view.storyboard.revision = view.document_revision;
        application.services.transport_preview_playing = [this] { return playing; };
        application.services.transport_play_pause = [this] { playing = !playing; ++starts; return std::string{}; };
        application.services.transport_stop = [this] { playing = false; ++stops; };
        application.services.transport_set_playback_rate = [this](rational_t) { ++rate_changes; return true; };
        registry = command_registry_t::from([this](const command_invocation_context_t& context) { return invoke(context); });
        project();
    }
    void project() {
        auto& view = harness.view();
        view.transport_available = true;
        view.transport_playing = playing;
        view.transport_rate_available = true;
        REQUIRE(sys::refresh_application_command_states(registry, history, workspace,
            editor, application, sys::project_identity_t{}).empty());
        (void)harness.frame();
    }
    auto dispatch_storyboard(const storyboard_action_t& action, bool busy = false) -> bool {
        return application.project.storyboard.dispatch(action,
            {.history = history, .workspace = workspace, .notifications = sink},
            application, editor ? &*editor : nullptr, busy);
    }
    void prepare_loop() {
        history.document.storyboards = {{.id = storyboard_id_t{11}, .osbx = osbx_document_t{
            .nodes = {{.id = "node", .tracks = {.opacity = {.keys = {
                {.id = "left", .time_ms = 100, .value = {0}},
                {.id = "right", .time_ms = 400, .value = {1}}}}}}}}}};
        auto& root = harness.view().storyboard;
        root.project = history.document.id; root.revision = history.document.revision; root.session_generation = 7;
        application.project.storyboard.refill(root, history.document, 7);
        REQUIRE(dispatch_storyboard(storyboard_action_t::from(root, storyboard_action_kind_t::select, storyboard_id_t{11})));
        application.project.storyboard.refill(root, history.document, 7);
        REQUIRE(root.state.selection.storyboard == storyboard_id_t{11});
        root.canvas.prepared_generation = osbx_lua_prepared_scope_t::generation();
        root.timeline.rows = {{.track = {.node = "node", .kind = osbx_track_kind_t::opacity},
            .keys = {{.id = "left", .time_ms = 100, .selected = true},
                     {.id = "right", .time_ms = 400, .selected = true}}}};
        application.services.transport_set_loop_audio_milliseconds =
            [this](std::int64_t begin, std::int64_t end, bool enabled) {
                ++loop_changes;
                applied_loop = enabled ? std::optional{storyboard_loop_region_t{.begin_ms = begin, .end_ms = end}}
                    : std::nullopt;
                return sys::compose_action_outcome_t::from("Loop request accepted.", notification_severity_t::hint,
                    notification_source_t::audio);
            };
        project_loop();
    }
    void project_loop() {
        auto& root = harness.view().storyboard;
        sys::build_storyboard_transport_view(root.transport, root.timeline, true, applied_loop.has_value());
        (void)harness.frame();
    }
    auto click_loop(std::string_view tooltip) -> storyboard_action_t {
        const auto frames = harness.click_widget(tooltip);
        const auto found = std::ranges::find_if(frames, [](const auto& frame) {
            return frame.get().storyboard.kind == storyboard_action_kind_t::transport;
        });
        REQUIRE(found != frames.end());
        return found->get().storyboard;
    }
    auto route(const ui::workspace_action_t& action) -> command_dispatch_outcome_t {
        if (action.shortcut_events.size() != 1) return {};
        const auto& key = action.shortcut_events[0];
        return dispatch_shortcut(registry,
            {.chord = {.key = static_cast<shortcut_key_t>(key.hid_usage), .modifiers = key.modifiers},
             .auto_repeat = key.repeat},
            {.modal_active = harness.view().options.visible,
             .text_entry_active = action.shortcut_text_entry_active,
             .ime_composition_active = action.shortcut_ime_composition_active,
             .workspace_active = action.shortcut_window_focused,
             .active_tab = workspace_tab_t::storyboard, .visible_tabs = {workspace_tab_t::storyboard}},
            shortcut_primary_modifier_t::control);
    }
    auto press(SDL_Keycode code, SDL_Scancode scan, SDL_Keymod modifiers = SDL_KMOD_NONE)
        -> command_dispatch_outcome_t {
        const auto down = transport_key(true, code, scan);
        const auto frame = harness.frame(std::span{&down, 1}, modifiers);
        const auto result = route(frame.get());
        const auto up = transport_key(false, code, scan);
        (void)harness.frame(std::span{&up, 1});
        return result;
    }
    void click(std::string_view tooltip, WormUiWorkspaceActionKind expected) {
        const auto frames = harness.click_widget(tooltip);
        const auto found = std::ranges::find_if(frames, [expected](const auto& frame) {
            return frame.get().kind == expected;
        });
        REQUIRE(found != frames.end());
        REQUIRE(sys::dispatch_transport_player_action(found->get(), application, sink));
        project();
    }
};
void check_transport_did_not_edit(const transport_workflow_t& flow) {
    CHECK(flow.history.document.revision == 1);
    CHECK(flow.history.undo.empty());
    CHECK_FALSE(flow.harness.view().compose.visible);
}
} // namespace

TEST_CASE("Storyboard Space reaches the production shared play stop command without Compose focus",
          "[frontend-state][event-harness][storyboard-transport]") {
    const auto width = GENERATE(800, 1280);
    nuklear_event_harness_t harness{width, 720};
    transport_workflow_t flow{.harness = harness}; flow.initialize();
    CHECK(flow.press(SDLK_SPACE, SDL_SCANCODE_SPACE).value.is_invoked());
    CHECK(flow.playing);
    flow.project();
    CHECK(flow.press(SDLK_SPACE, SDL_SCANCODE_SPACE).value.is_invoked());
    CHECK(flow.stops == 1);
    check_transport_did_not_edit(flow);
}

TEST_CASE("Storyboard global play and pause buttons reuse the shared audio service",
          "[frontend-state][event-harness][storyboard-transport]") {
    nuklear_event_harness_t harness{1280, 720};
    transport_workflow_t flow{.harness = harness}; flow.initialize();
    flow.click("Play chart (Space)", WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE);
    CHECK(flow.playing);
    flow.click("Pause playback (Space)", WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE);
    CHECK_FALSE(flow.playing);
    CHECK(flow.starts == 2);
    check_transport_did_not_edit(flow);
}

TEST_CASE("Storyboard rate shortcuts reach the shared rate owner even at compact width",
          "[frontend-state][event-harness][storyboard-transport]") {
    nuklear_event_harness_t harness{800, 720};
    transport_workflow_t flow{.harness = harness}; flow.initialize();
    CHECK(flow.press(SDLK_F11, SDL_SCANCODE_F11, SDL_KMOD_CTRL).value.is_invoked());
    CHECK(flow.rate_changes == 1);
    check_transport_did_not_edit(flow);
}

TEST_CASE("Storyboard transport shortcuts respect the options modal and focus loss",
          "[frontend-state][event-harness][storyboard-transport]") {
    const bool modal = GENERATE(false, true);
    nuklear_event_harness_t harness{1280, 720};
    transport_workflow_t flow{.harness = harness}; flow.initialize();
    if (modal) {
        harness.view().options.visible = true;
    } else {
        SDL_Event lost{}; lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        (void)harness.frame(std::span{&lost, 1});
    }
    CHECK_FALSE(flow.press(SDLK_SPACE, SDL_SCANCODE_SPACE).value.is_invoked());
    CHECK(flow.starts == 0);
    CHECK_FALSE(flow.playing);
}

TEST_CASE("Storyboard timeline focused keyboard keeps Space in the shared transport scope",
          "[frontend-state][event-harness][storyboard-transport]") {
    nuklear_event_harness_t harness{1280, 720};
    transport_workflow_t flow{.harness = harness}; flow.initialize();
    auto& root = harness.view().storyboard;
    root.timeline.rows = {{.track = {.node = "node", .kind = osbx_track_kind_t::opacity},
        .label = "Transport timeline", .editable = true}};
    (void)harness.frame();
    const auto label = harness.drawn_text_center("Transport timeline"); REQUIRE(label);
    (void)test::click_at(harness, label->first + 130, label->second);
    CHECK(flow.press(SDLK_SPACE, SDL_SCANCODE_SPACE).value.is_invoked());
    CHECK(flow.playing);
    check_transport_did_not_edit(flow);
}

TEST_CASE("Storyboard inspector numeric text focus suppresses transport Space",
          "[frontend-state][event-harness][storyboard-transport]") {
    nuklear_event_harness_t harness{1280, 900};
    transport_workflow_t flow{.harness = harness}; flow.initialize();
    auto& root = harness.view().storyboard;
    root.inspector = {.node = "sprite", .editable = true, .origin = "Centre",
        .opacity = {.value = {0.625}, .editable = true}};
    (void)harness.frame();
    const auto field = harness.drawn_text_center("0.625"); REQUIRE(field);
    (void)test::click_at(harness, field->first + 8, field->second);
    CHECK_FALSE(flow.press(SDLK_SPACE, SDL_SCANCODE_SPACE).value.is_invoked());
    CHECK(flow.starts == 0);
}

TEST_CASE("Storyboard loop button sends exact selected endpoints through the production runtime and common finisher",
          "[frontend-state][event-harness][storyboard-transport][runtime]") {
    nuklear_event_harness_t harness{1280, 720};
    transport_workflow_t flow{.harness = harness}; flow.initialize(); flow.prepare_loop();
    const auto action = flow.click_loop("Loop selected storyboard key times");
    REQUIRE(action.transport.loop);
    CHECK(flow.dispatch_storyboard(action));
    CHECK(flow.applied_loop == std::optional{storyboard_loop_region_t{.begin_ms = 100, .end_ms = 400}});
    CHECK(flow.loop_changes == 1);
    check_transport_did_not_edit(flow);
}

TEST_CASE("Storyboard active loop can be disabled after the timeline selection disappears",
          "[frontend-state][event-harness][storyboard-transport][runtime]") {
    nuklear_event_harness_t harness{1280, 720};
    transport_workflow_t flow{.harness = harness}; flow.initialize(); flow.prepare_loop();
    flow.applied_loop = storyboard_loop_region_t{.begin_ms = 100, .end_ms = 400};
    harness.view().storyboard.timeline.rows.clear(); flow.project_loop();
    const auto action = flow.click_loop("Disable storyboard selection loop");
    CHECK_FALSE(action.transport.loop);
    CHECK(flow.dispatch_storyboard(action));
    CHECK_FALSE(flow.applied_loop);
    CHECK(flow.loop_changes == 1);
    check_transport_did_not_edit(flow);
}

namespace {
enum class loop_refusal_t { revision, session, prepared, storyboard, busy, negative, equal };
void invalidate_loop_request(storyboard_action_t& action, loop_refusal_t reason) {
    switch (reason) {
    case loop_refusal_t::revision: ++action.revision; break;
    case loop_refusal_t::session: ++action.session_generation; break;
    case loop_refusal_t::prepared: ++action.prepared_generation; break;
    case loop_refusal_t::storyboard: action.storyboard = storyboard_id_t{999}; break;
    case loop_refusal_t::negative: action.transport.loop->begin_ms = -1; break;
    case loop_refusal_t::equal: action.transport.loop->end_ms = action.transport.loop->begin_ms; break;
    case loop_refusal_t::busy: break;
    }
}
} // namespace

TEST_CASE("Storyboard loop runtime rejects stale busy and invalid requests without replacing the active loop",
          "[frontend-state][event-harness][storyboard-transport][runtime]") {
    const auto reason = GENERATE(loop_refusal_t::revision, loop_refusal_t::session,
        loop_refusal_t::prepared, loop_refusal_t::storyboard, loop_refusal_t::busy,
        loop_refusal_t::negative, loop_refusal_t::equal);
    nuklear_event_harness_t harness{1280, 720};
    transport_workflow_t flow{.harness = harness}; flow.initialize(); flow.prepare_loop();
    auto action = flow.click_loop("Loop selected storyboard key times");
    flow.applied_loop = storyboard_loop_region_t{.begin_ms = 200, .end_ms = 300};
    invalidate_loop_request(action, reason);
    CHECK(flow.dispatch_storyboard(action, reason == loop_refusal_t::busy));
    CHECK(flow.loop_changes == 0);
    CHECK(flow.applied_loop == std::optional{storyboard_loop_region_t{.begin_ms = 200, .end_ms = 300}});
    check_transport_did_not_edit(flow);
}

TEST_CASE("Stopped Storyboard scrub uses the shared seek bar without a Compose scene",
          "[frontend-state][event-harness][storyboard-transport][seek-bar]") {
    nuklear_event_harness_t harness{1280, 900};
    transport_workflow_t flow{.harness = harness}; flow.initialize();
    harness.view().compose.scene_available = false;
    harness.view().transport_end_frame = 4'410'000;
    std::optional<std::uint64_t> sought;
    flow.application.services.transport_seek = [&](std::uint64_t frame) {
        sought = frame;
        return sys::compose_action_outcome_t::from("Seek accepted.", notification_severity_t::hint,
            notification_source_t::audio);
    };
    (void)harness.frame();
    const auto frames = test::click_at(harness, 640, 88);
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK;
    });
    REQUIRE(found != frames.end());
    REQUIRE(sys::dispatch_transport_player_action(found->get(), flow.application, flow.sink));
    REQUIRE(sought);
    CHECK(*sought > 2'000'000);
    CHECK(*sought < 2'400'000);
    check_transport_did_not_edit(flow);
}

TEST_CASE("Storyboard unavailable or nonpositive loop selection cannot emit an audio request",
          "[frontend-state][event-harness][storyboard-transport]") {
    const bool available = GENERATE(false, true);
    nuklear_event_harness_t harness{1280, 720};
    transport_workflow_t flow{.harness = harness}; flow.initialize(); flow.prepare_loop();
    auto& transport = harness.view().storyboard.transport;
    transport.available = available;
    transport.selection = storyboard_loop_region_t{.begin_ms = -1, .end_ms = 400};
    (void)harness.frame();
    const auto frames = harness.click_widget("Loop selected storyboard key times");
    CHECK(std::ranges::none_of(frames, [](const auto& frame) {
        return frame.get().storyboard.kind == storyboard_action_kind_t::transport;
    }));
    CHECK(flow.loop_changes == 0);
}
