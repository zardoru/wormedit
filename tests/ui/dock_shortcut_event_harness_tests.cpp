#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/compose_resources.h"
#include <wormedit/core/command_registry.h>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <source_location>

using namespace wormedit;
namespace {
void check_dock(bool value, std::source_location at = std::source_location::current()) { INFO("caller " << at.line()); CHECK(value); }
void require_dock(bool value, std::source_location at = std::source_location::current()) { INFO("caller " << at.line()); REQUIRE(value); }
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
    check_dock(press_and_dispatch(harness, SDLK_L, SDL_SCANCODE_L, SDL_KMOD_CTRL).status ==
               command_dispatch_status_t::invoked);
    static_cast<void>(harness.frame());
    check_dock(!worm_ui_nuklear_compose_state().layers_panel_visible);
}

TEST_CASE("Dock shortcut creation emits one existing Add layer action with its context stamp",
          "[frontend-state][event-harness][dock-shortcuts]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    check_dock(press_and_dispatch(harness, SDLK_L, SDL_SCANCODE_L,
        SDL_KMOD_GUI | SDL_KMOD_SHIFT).status == command_dispatch_status_t::invoked);
    check_dock(harness.frame().get().compose_action != WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    check_dock(worm_ui_nuklear_compose_state().layer_creation_context.has_value());
    static_cast<void>(harness.frame());
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "Chorus";
    static_cast<void>(harness.frame({&text, 1}));
    SDL_Event enter{};
    enter.type = enter.key.type = SDL_EVENT_KEY_DOWN;
    enter.key.down = true;
    enter.key.key = SDLK_RETURN;
    enter.key.scancode = SDL_SCANCODE_RETURN;
    const auto action = harness.frame({&enter, 1});
    check_dock(action.get().compose_action == WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    check_dock(std::string_view(action.get().compose_semantic_layer_name) == "Chorus");
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
    static_cast<void>(harness.frame());
    static_cast<void>(harness.frame());
    SDL_Event enter{};
    enter.type = enter.key.type = SDL_EVENT_KEY_DOWN;
    enter.key.down = true;
    enter.key.key = SDLK_RETURN;
    enter.key.scancode = SDL_SCANCODE_RETURN;
    require_dock(harness.frame({&enter, 1}).get().compose_action == WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
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
    check_dock(harness.drawn_text().find("Layer") != std::string::npos);
}

TEST_CASE("Create layer from a closed populated dock takes priority over list projection requests",
          "[frontend-state][event-harness][dock-shortcuts]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    harness.view().compose.semantic_layer_total_count = 200;
    require_dock(!worm_ui_nuklear_compose_state().layers_panel_visible);
    require_dock(worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_CREATE_LAYER, 1, 1, 1));
    static_cast<void>(harness.frame());
    static_cast<void>(harness.frame());
    SDL_Event enter{};
    enter.type = enter.key.type = SDL_EVENT_KEY_DOWN;
    enter.key.down = true;
    enter.key.key = SDLK_RETURN;
    enter.key.scancode = SDL_SCANCODE_RETURN;
    check_dock(harness.frame({&enter, 1}).get().compose_action == WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    check_dock(!worm_ui_nuklear_compose_state().layer_creation_request);
}

TEST_CASE("Compact dock shortcuts switch panels and close the active panel", "[frontend-state][event-harness][dock-shortcuts]") {
    nuklear_event_harness_t harness{960, 720};
    prepare(harness);
    auto& state = worm_ui_nuklear_compose_state();
    require_dock(worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_OPEN_LAYERS, 1, 1, 1));
    static_cast<void>(harness.frame());
    check_dock(state.layers_panel_visible);
    require_dock(worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_TOGGLE_SOUNDS, 1, 1, 1));
    static_cast<void>(harness.frame());
    check_dock(state.sounds_panel_visible);
    check_dock(!state.layers_panel_visible);
    require_dock(worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_TOGGLE_SOUNDS, 1, 1, 1));
    static_cast<void>(harness.frame());
    check_dock(!state.sounds_panel_visible);
    check_dock(!state.layers_panel_visible);
}

namespace {
auto dialog_key(SDL_Keycode code, SDL_Scancode scan) -> SDL_Event {
    SDL_Event event{};
    event.type = event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    event.key.scancode = scan;
    event.key.down = true;
    return event;
}
}

TEST_CASE("Layer naming Escape cancels and blank names cannot create",
          "[frontend-state][event-harness][compose-dialogs]") {
    nuklear_event_harness_t harness{960, 720};
    prepare(harness);
    require_dock(worm_ui_frontend_compose_dock_command(WORM_UI_COMPOSE_DOCK_CREATE_LAYER, 1, 1, 1));
    static_cast<void>(harness.frame());
    static_cast<void>(harness.frame());
    auto& state = worm_ui_nuklear_compose_state();
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "   ";
    static_cast<void>(harness.frame({&text, 1}));
    const auto enter = dialog_key(SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_dock(harness.frame({&enter, 1}).get().compose_action != WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    check_dock(state.layer_creation_context.has_value());
    const auto escape = dialog_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    const auto action = harness.frame({&escape, 1});
    check_dock(action.get().compose_action != WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    check_dock(action.get().shortcut_event_count == 0);
    check_dock(!state.layer_creation_context);
}

TEST_CASE("Export dialog fits a small viewport and submits captured group and overwrite",
          "[frontend-state][event-harness][compose-dialogs]") {
    nuklear_event_harness_t harness{640, 480};
    prepare(harness);
    WormUiComposeOutputGroupView group{};
    group.document_index = 7;
    group.name = "BMS output";
    group.relative_output_path = "exports/chart.bms";
    group.relative_asset_directory = "exports";
    harness.view().compose.output_groups = &group;
    harness.view().compose.output_group_count = 1;
    auto& state = worm_ui_nuklear_compose_state();
    worm_ui_nuklear_open_export_dialog(harness.view().compose, state);
    state.export_overwrite = true;
    static_cast<void>(harness.frame());
    static_cast<void>(harness.frame());
    check_dock(state.export_dialog.bounds.x >= 0 && state.export_dialog.bounds.y >= 0);
    check_dock(state.export_dialog.bounds.y + state.export_dialog.bounds.h <= 480);
    check_dock(harness.drawn_text_center("Export group").has_value());
    const auto enter = dialog_key(SDLK_RETURN, SDL_SCANCODE_RETURN);
    const auto action = harness.frame({&enter, 1});
    check_dock(action.get().compose_action == WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP);
    check_dock(action.get().compose_output_group_index == 7);
    check_dock(action.get().compose_export_overwrite);
    check_dock(action.get().compose_input_project_id == 1);
    check_dock(!state.export_dialog_open);
}

TEST_CASE("Open compose dialogs cancel on context changes and external modals",
          "[frontend-state][event-harness][compose-dialogs]") {
    for (int cause = 0; cause < 7; ++cause) {
        nuklear_event_harness_t harness{960, 720};
        prepare(harness);
        auto& state = worm_ui_nuklear_compose_state();
        worm_ui_nuklear_open_layer_creation(harness.view().compose, state);
        static_cast<void>(harness.frame());
        if (cause == 0) ++harness.view().compose.document_revision;
        if (cause == 1) ++harness.view().compose.chart_id;
        if (cause == 2) ++harness.view().compose.project_id;
        if (cause == 3) harness.view().active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        if (cause == 4) harness.view().close_confirmation.visible = true;
        if (cause == 5) harness.view().compose.input_occluded = true;
        if (cause == 6) harness.view().options.visible = true;
        const auto enter = dialog_key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        check_dock(harness.frame({&enter, 1}).get().compose_action != WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
        check_dock(!state.layer_creation_context);
    }
}
