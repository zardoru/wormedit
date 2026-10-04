#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <source_location>

namespace {
void check_filter(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}
void require_filter(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    REQUIRE(condition);
}

void seed_sound_filter_view(WormUiComposeView& view) {
    static const std::array groups{
        WormUiComposeSoundGroupView{.group_id = 80, .name = "Percussion"},
        WormUiComposeSoundGroupView{.group_id = 81, .name = "Piano"}};
    static const std::array rows{
        WormUiComposeKeysoundRowView{.resource_id = 5, .slot = "01", .name = "Kick",
                                     .sound_group_id = 80},
        WormUiComposeKeysoundRowView{.resource_id = 9, .slot = "02", .name = "Snare",
                                     .sound_group_id = 80}};
    static const std::array display{
        WormUiComposeKeysoundDisplayRowView{.kind = WORM_UI_COMPOSE_KEYSOUND_GROUP_HEADER,
            .group_id = 80, .group_name = "Percussion", .group_sound_count = 2, .expanded = true},
        WormUiComposeKeysoundDisplayRowView{.kind = WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW,
            .group_id = 80, .resource_id = 5},
        WormUiComposeKeysoundDisplayRowView{.kind = WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW,
            .group_id = 80, .resource_id = 9}};
    static const std::array<std::uint64_t, 2> ids{5, 9};
    static const std::array layers{
        WormUiComposeKeysoundLayerView{.layer_id = 501, .name = "Blue"}};
    view.keysounds_available = true;
    view.project_id = 31;
    view.chart_id = 7;
    view.keysounds = rows.data();
    view.keysound_count = rows.size();
    view.keysound_total_count = rows.size();
    view.keysound_display_rows = display.data();
    view.keysound_display_row_count = display.size();
    view.keysound_display_total_count = display.size();
    view.keysound_section_projection_available = true;
    view.keysound_listed_resource_ids = ids.data();
    view.keysound_listed_resource_count = ids.size();
    view.keysound_all_resource_ids = ids.data();
    view.keysound_all_resource_count = ids.size();
    view.compose_sound_groups = groups.data();
    view.compose_sound_group_count = groups.size();
    view.keysound_layers = layers.data();
    view.keysound_layer_count = layers.size();
    view.keysound_filter_layers = layers.data();
    view.keysound_filter_layer_count = layers.size();
    view.keysound_filter_layer_total = layers.size();
    view.keysound_filter_selected_layer_valid = true;
    view.keysound_filter_selected_layer_name = "Blue";
}

auto click_filter(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.clicks = 1;
    down.button.x = x;
    down.button.y = y;
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.down = false;
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    const std::array down_events{motion, down};
    const std::array up_events{motion, up};
    return {harness.frame(down_events), harness.frame(up_events)};
}
} // namespace

TEST_CASE("Sounds group filter updates request before paging without editing the chart",
          "[frontend-state][event-harness][sound-groups]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    seed_sound_filter_view(view);
    require_filter(harness.ready());
    (void)harness.frame();
    WormUiKeysoundDockRequest request{};
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(!request.group_filter_active);
    check_filter(request.include_ungrouped);

    const auto group_filter = harness.drawn_text_center("Groups: all");
    require_filter(group_filter.has_value());
    (void)click_filter(harness, group_filter->first, group_filter->second);
    const auto choice = harness.drawn_text_center("Piano");
    require_filter(choice.has_value());
    auto actions = click_filter(harness, choice->first, choice->second);
    auto ungrouped = harness.drawn_text_center("Ungrouped");
    require_filter(ungrouped.has_value());
    const auto ungrouped_actions = click_filter(harness, ungrouped->first, ungrouped->second);
    INFO("Ungrouped action codes down/up: " << ungrouped_actions[0].get().compose_action << "/"
         << ungrouped_actions[1].get().compose_action);
    check_filter(actions[0].get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_filter(actions[1].get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_filter(ungrouped_actions[0].get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_filter(ungrouped_actions[1].get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(request.group_filter_active);
    require_filter(request.group_filter_count == 1);
    check_filter(request.group_filter_ids[0] == 80);
    check_filter(!request.include_ungrouped);
    check_filter(request.automatic_first_row == 0);
    view.project_id = 32;
    (void)harness.frame();
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(!request.group_filter_active);
    view.project_id = 31;
    (void)harness.frame();
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(request.group_filter_active);
    auto clear = harness.drawn_text_center("Clear filters");
    require_filter(clear.has_value());
    (void)click_filter(harness, clear->first, clear->second);
    clear = harness.drawn_text_center("Clear filters");
    require_filter(clear.has_value());
    (void)click_filter(harness, clear->first, clear->second);
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(!request.group_filter_active);
    check_filter(request.include_ungrouped);
}

TEST_CASE("Sounds filter popup closes on Escape without a compose action",
          "[frontend-state][event-harness][sound-groups]") {
    nuklear_event_harness_t harness{1280, 900};
    seed_sound_filter_view(harness.view().compose);
    REQUIRE(harness.ready());
    (void)harness.frame();
    const auto group_filter = harness.drawn_text_center("Groups: all");
    REQUIRE(group_filter.has_value());
    (void)click_filter(harness, group_filter->first, group_filter->second);
    REQUIRE(worm_ui_nuklear_compose_state().keysound_filter_popup_open);

    SDL_Event escape_down{};
    escape_down.type = SDL_EVENT_KEY_DOWN;
    escape_down.key.type = SDL_EVENT_KEY_DOWN;
    escape_down.key.key = SDLK_ESCAPE;
    escape_down.key.scancode = SDL_SCANCODE_ESCAPE;
    escape_down.key.down = true;
    const auto down_action = harness.frame(std::span{&escape_down, 1});
    SDL_Event escape_up = escape_down;
    escape_up.type = SDL_EVENT_KEY_UP;
    escape_up.key.type = SDL_EVENT_KEY_UP;
    escape_up.key.down = false;
    const auto up_action = harness.frame(std::span{&escape_up, 1});
    CHECK_FALSE(worm_ui_nuklear_compose_state().keysound_filter_popup_open);
    CHECK(down_action.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK(up_action.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Sounds layer filter offers no layer and Clear filters resets it",
          "[frontend-state][event-harness][sound-groups]") {
    nuklear_event_harness_t harness{1280, 900};
    seed_sound_filter_view(harness.view().compose);
    require_filter(harness.ready());
    (void)harness.frame();
    auto layer = harness.drawn_text_center("Layer: any");
    require_filter(layer.has_value());
    (void)click_filter(harness, layer->first, layer->second);
    auto blue = harness.drawn_text_center("Blue");
    require_filter(blue.has_value());
    const auto blue_actions = click_filter(harness, blue->first, blue->second);
    INFO("Blue action codes down/up: " << blue_actions[0].get().compose_action << "/"
         << blue_actions[1].get().compose_action << "; layer kind/id: "
         << worm_ui_nuklear_compose_state().keysound_filters.layer_filter_kind << "/"
         << worm_ui_nuklear_compose_state().keysound_filters.layer_filter_id);
    WormUiKeysoundDockRequest request{};
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(request.layer_filter_kind == WORM_UI_KEYSOUND_LAYER_FILTER_SPECIFIC);
    check_filter(request.layer_filter_id == 501);
    (void)harness.frame();
    layer = harness.drawn_text_center("Blue");
    require_filter(layer.has_value());
    (void)click_filter(harness, layer->first, layer->second);
    auto no_layer = harness.drawn_text_center("No layer");
    require_filter(no_layer.has_value());
    (void)click_filter(harness, no_layer->first, no_layer->second);
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(request.layer_filter_kind == WORM_UI_KEYSOUND_LAYER_FILTER_NONE);
    auto clear = harness.drawn_text_center("Clear filters");
    require_filter(clear.has_value());
    (void)click_filter(harness, clear->first, clear->second);
    clear = harness.drawn_text_center("Clear filters");
    require_filter(clear.has_value());
    (void)click_filter(harness, clear->first, clear->second);
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(request.layer_filter_kind == WORM_UI_KEYSOUND_LAYER_FILTER_ANY);
    auto& filters = worm_ui_nuklear_compose_state().keysound_filters;
    filters.layer_filter_kind = WORM_UI_KEYSOUND_LAYER_FILTER_SPECIFIC;
    filters.layer_filter_id = 999;
    filters.layer_chart_id = harness.view().compose.chart_id;
    harness.view().compose.keysound_filter_selected_layer_valid = false;
    harness.view().compose.keysound_filter_selected_layer_name = nullptr;
    (void)harness.frame();
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(request.layer_filter_kind == WORM_UI_KEYSOUND_LAYER_FILTER_ANY);
}

TEST_CASE("Sounds filter layer paging keeps its release off the sound rows",
          "[frontend-state][event-harness][sound-groups]") {
    static const std::array choices{
        WormUiComposeKeysoundLayerView{.layer_id = 601, .name = "Layer 1"},
        WormUiComposeKeysoundLayerView{.layer_id = 602, .name = "Layer 2"},
        WormUiComposeKeysoundLayerView{.layer_id = 603, .name = "Layer 3"},
        WormUiComposeKeysoundLayerView{.layer_id = 604, .name = "Layer 4"},
        WormUiComposeKeysoundLayerView{.layer_id = 605, .name = "Layer 5"}};
    nuklear_event_harness_t harness{1280, 900};
    seed_sound_filter_view(harness.view().compose);
    auto& view = harness.view().compose;
    view.keysound_filter_layers = choices.data();
    view.keysound_filter_layer_count = choices.size();
    view.keysound_filter_layer_total = choices.size() + 1;
    require_filter(harness.ready());
    (void)harness.frame();
    const auto layer = harness.drawn_text_center("Layer: any");
    require_filter(layer.has_value());
    (void)click_filter(harness, layer->first, layer->second);
    const auto more = harness.drawn_text_center("More layers...");
    require_filter(more.has_value());
    const auto actions = click_filter(harness, more->first, more->second);
    (void)harness.frame();
    check_filter(actions[0].get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_filter(actions[1].get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_filter(worm_ui_nuklear_compose_state().keysound_filter_layer_first == choices.size());
    check_filter(worm_ui_nuklear_compose_state().keysound_selection.selected.empty());
}

TEST_CASE("Sounds group headers collapse by stable group ID",
          "[frontend-state][event-harness][sound-groups]") {
    nuklear_event_harness_t harness{1280, 900};
    seed_sound_filter_view(harness.view().compose);
    REQUIRE(harness.ready());
    (void)harness.frame();
    const auto header = harness.drawn_text_center("v Percussion (2)");
    REQUIRE(header.has_value());
    (void)click_filter(harness, header->first, header->second);
    WormUiKeysoundDockRequest request{};
    worm_ui_frontend_keysound_dock_request(&request);
    REQUIRE(request.collapsed_group_count == 1);
    CHECK(request.collapsed_group_ids[0] == 80);
}
