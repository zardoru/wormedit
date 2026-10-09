#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include <wormedit/core/command_registry.h>
#include <catch2/catch_test_macros.hpp>
#include <array>

using namespace wormedit;
namespace {
void check_dock(bool value) { CHECK(value); }
void require_dock(bool value) { REQUIRE(value); }
void prepare(nuklear_event_harness_t& harness) {
    require_dock(harness.ready());
    harness.view().compose.keysounds_available = true;
    static_cast<void>(harness.frame());
}
auto registry_for_docks() -> command_registry_t {
    return make_default_editor_command_registry([](const command_invocation_context_t& context) {
        const auto& id = context.command.value;
        const auto dock = id == "view.open_layers" ? WORM_UI_COMPOSE_DOCK_OPEN_LAYERS
            : id == "semantic_layer.create" ? WORM_UI_COMPOSE_DOCK_CREATE_LAYER
            : WORM_UI_COMPOSE_DOCK_TOGGLE_SOUNDS;
        return command_invocation_result_t{
            .accepted = worm_ui_frontend_compose_dock_command(dock, 1, 1, 1)};
    });
}
auto press_and_dispatch(nuklear_event_harness_t& harness, SDL_Keycode code,
                        SDL_Scancode scan, SDL_Keymod modifiers)
    -> command_dispatch_result_t {
    SDL_Event event{};
    event.type = event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    event.key.scancode = scan;
    event.key.down = true;
    const auto frame = harness.frame(std::span{&event, 1}, modifiers);
    require_dock(frame.get().shortcut_event_count == 1);
    const auto& input = frame.get().shortcut_events[0];
    const auto registry = registry_for_docks();
    return dispatch_shortcut(registry,
        {.chord = {static_cast<shortcut_key_t>(input.hid_usage), input.modifiers},
         .auto_repeat = input.repeat},
        {.workspace_active = true, .active_tab = workspace_tab_t::compose,
         .visible_tabs = {workspace_tab_t::compose}},
        (modifiers & SDL_KMOD_GUI) ? shortcut_primary_modifier_t::meta
                                  : shortcut_primary_modifier_t::control);
}
}

TEST_CASE("Dock shortcut opens Layers in a narrow window without creating a layer",
          "[frontend-state][event-harness][dock-shortcuts]") {
    nuklear_event_harness_t harness{960, 720};
    prepare(harness);
    check_dock(press_and_dispatch(harness, SDLK_L, SDL_SCANCODE_L, SDL_KMOD_CTRL).status ==
               command_dispatch_status_t::invoked);
    const auto action = harness.frame();
    check_dock(worm_ui_nuklear_compose_state().layers_panel_visible);
    check_dock(!worm_ui_nuklear_compose_state().sounds_panel_visible);
    check_dock(harness.drawn_text().find("Layers") != std::string::npos);
    check_dock(action.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Dock shortcut creation emits one existing Add layer action with its context stamp",
          "[frontend-state][event-harness][dock-shortcuts]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    check_dock(press_and_dispatch(harness, SDLK_L, SDL_SCANCODE_L,
        SDL_KMOD_GUI | SDL_KMOD_SHIFT).status == command_dispatch_status_t::invoked);
    const auto action = harness.frame();
    check_dock(action.get().compose_action == WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    check_dock(action.get().compose_input_project_id == 1);
    check_dock(action.get().compose_input_chart_id == 1);
    check_dock(action.get().compose_input_document_revision == 1);
    check_dock(worm_ui_nuklear_compose_state().layers_panel_visible);
    check_dock(!worm_ui_nuklear_compose_state().layer_creation_request);
    check_dock(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Dock shortcut toggles Sounds without opening the Go to sound picker",
          "[frontend-state][event-harness][dock-shortcuts]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    const bool visible = worm_ui_nuklear_compose_state().sounds_panel_visible;
    check_dock(press_and_dispatch(harness, SDLK_K, SDL_SCANCODE_K,
        SDL_KMOD_CTRL | SDL_KMOD_SHIFT).status == command_dispatch_status_t::invoked);
    check_dock(worm_ui_nuklear_compose_state().sounds_panel_visible != visible);
    check_dock(!worm_ui_nuklear_compose_state().sound_picker.open);
    check_dock(worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_TOGGLE_SOUNDS, 1, 1, 1));
    check_dock(worm_ui_nuklear_compose_state().sounds_panel_visible == visible);
}

TEST_CASE("Queued layer creation is discarded on stale contexts focus loss and modal input",
          "[frontend-state][event-harness][dock-shortcuts]") {
    for (int cause = 0; cause < 7; ++cause) {
        INFO("cause " << cause);
        nuklear_event_harness_t harness{1280, 800};
        prepare(harness);
        require_dock(worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_CREATE_LAYER, 1, 1, 1));
        std::vector<SDL_Event> events;
        if (cause == 0) ++harness.view().compose.document_revision;
        if (cause == 1) ++harness.view().compose.chart_id;
        if (cause == 2) ++harness.view().compose.project_id;
        if (cause == 3) harness.view().active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        if (cause == 4) harness.view().close_confirmation.visible = true;
        if (cause == 5) harness.view().compose.input_occluded = true;
        if (cause == 6) {
            SDL_Event lost{};
            lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            events.push_back(lost);
        }
        check_dock(harness.frame(events).get().compose_action != WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
        check_dock(!worm_ui_nuklear_compose_state().layer_creation_request);
    }
}

TEST_CASE("Dock requests cannot bypass stale stamps or popup and IME ownership",
          "[frontend-state][event-harness][dock-shortcuts]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    check_dock(!worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_CREATE_LAYER, 1, 1, 2));
    require_dock(worm_ui_frontend_open_sound_picker(1, 1, 1));
    for (const auto dock : {WORM_UI_COMPOSE_DOCK_OPEN_LAYERS, WORM_UI_COMPOSE_DOCK_CREATE_LAYER,
                           WORM_UI_COMPOSE_DOCK_TOGGLE_SOUNDS})
        check_dock(!worm_ui_frontend_compose_dock_command(dock, 1, 1, 1));
    cancel_compose_sound_picker(worm_ui_nuklear_compose_state().sound_picker);
    static_cast<void>(harness.frame());
    SDL_Event composition{};
    composition.type = SDL_EVENT_TEXT_EDITING;
    composition.edit.text = "x";
    static_cast<void>(harness.frame(std::span{&composition, 1}));
    check_dock(!worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_CREATE_LAYER, 1, 1, 1));
}

TEST_CASE("Creating a layer reveals the appended target beyond the visible layer window",
          "[frontend-state][event-harness][dock-shortcuts]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    auto& view = harness.view().compose;
    view.semantic_layer_total_count = 200;
    require_dock(worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_OPEN_LAYERS, 1, 1, 1));
    static_cast<void>(harness.frame());
    require_dock(worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_CREATE_LAYER, 1, 1, 1));
    require_dock(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    ++view.document_revision;
    view.semantic_layer_total_count = 201;
    view.semantic_layer_target_row = 200;
    view.semantic_layer_target = {.layer_id = 900, .name = "Layer"};
    const auto requested = harness.frame();
    check_dock(requested.get().compose_action == WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW);
    check_dock(requested.get().compose_semantic_layer_first_row > 180);
    const auto first = requested.get().compose_semantic_layer_first_row;
    view.semantic_layer_first_row = first;
    view.semantic_layer_count = 201 - first;
    std::vector<WormUiComposeSemanticLayerView> layers(view.semantic_layer_count);
    for (std::size_t index = 0; index < layers.size(); ++index)
        layers[index] = {.layer_id = 900 - index, .name = "Layer"};
    view.semantic_layers = layers.data();
    static_cast<void>(harness.frame());
    check_dock(harness.drawn_text().find("Layer: 0 notes") != std::string::npos);
}

TEST_CASE("Create layer from a closed populated dock takes priority over list projection requests",
          "[frontend-state][event-harness][dock-shortcuts]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    harness.view().compose.semantic_layer_total_count = 200;
    require_dock(!worm_ui_nuklear_compose_state().layers_panel_visible);
    require_dock(worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_CREATE_LAYER, 1, 1, 1));
    check_dock(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    check_dock(!worm_ui_nuklear_compose_state().layer_creation_request);
}
