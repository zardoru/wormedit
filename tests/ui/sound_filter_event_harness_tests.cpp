#include "nuklear_event_harness.h"
#include "sys/frame_timing.h"
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
        wormedit::ui::compose_sound_group_view_t{.group_id = wormedit::sound_group_id_t{80}, .name = "Percussion"},
        wormedit::ui::compose_sound_group_view_t{.group_id = wormedit::sound_group_id_t{81}, .name = "Piano"}};
    static const std::array rows{
        wormedit::ui::compose_keysound_row_view_t{.resource_id = wormedit::resource_id_t{5}, .slot = "01", .name = "Kick",
                                     .sound_group_id = wormedit::sound_group_id_t{80}},
        wormedit::ui::compose_keysound_row_view_t{.resource_id = wormedit::resource_id_t{9}, .slot = "02", .name = "Snare",
                                     .sound_group_id = wormedit::sound_group_id_t{80}}};
    static const std::array display{
        wormedit::ui::compose_keysound_display_row_view_t{.kind = WORM_UI_COMPOSE_KEYSOUND_GROUP_HEADER,
            .group_id = wormedit::sound_group_id_t{80}, .group_name = "Percussion", .group_sound_count = 2, .expanded = true},
        wormedit::ui::compose_keysound_display_row_view_t{.kind = WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW,
            .group_id = wormedit::sound_group_id_t{80}, .resource_id = wormedit::resource_id_t{5}},
        wormedit::ui::compose_keysound_display_row_view_t{.kind = WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW,
            .group_id = wormedit::sound_group_id_t{80}, .resource_id = wormedit::resource_id_t{9}}};
    static const std::array<std::uint64_t, 2> ids{5, 9};
    static const std::array layers{
        wormedit::ui::compose_keysound_layer_view_t{.layer_id = wormedit::layer_id_t{501}, .name = "Blue"}};
    view.keysounds_available = true;
    view.project_id = 31;
    view.chart_id = 7;
    view.keysounds.assign(std::begin(rows), std::end(rows));
    view.keysounds.resize(rows.size());
    view.keysound_total_count = rows.size();
    view.keysound_display_rows.assign(std::begin(display), std::end(display));
    view.keysound_display_rows.resize(display.size());
    view.keysound_display_total_count = display.size();
    view.keysound_section_projection_available = true;
    view.keysound_listed_resource_ids.assign(std::begin(ids), std::end(ids));
    view.keysound_listed_resource_ids.resize(ids.size());
    view.keysound_all_resource_ids.assign(std::begin(ids), std::end(ids));
    view.keysound_all_resource_ids.resize(ids.size());
    view.compose_sound_groups.assign(std::begin(groups), std::end(groups));
    view.compose_sound_groups.resize(groups.size());
    view.keysound_layers.assign(std::begin(layers), std::end(layers));
    view.keysound_layers.resize(layers.size());
    view.keysound_filter_layers.assign(std::begin(layers), std::end(layers));
    view.keysound_filter_layers.resize(layers.size());
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
    wormedit::ui::keysound_dock_request_t request{};
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
    require_filter(request.group_filter_ids.size() == 1);
    check_filter(request.group_filter_ids[0].value == 80);
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
    REQUIRE(worm_ui_nuklear_compose_state().keysound_filters.is_popup_open());

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
    CHECK_FALSE(worm_ui_nuklear_compose_state().keysound_filters.is_popup_open());
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
         << worm_ui_nuklear_compose_state().keysound_filters.keysound_filters.layer_filter_kind << "/"
         << worm_ui_nuklear_compose_state().keysound_filters.keysound_filters.layer_filter_id);
    wormedit::ui::keysound_dock_request_t request{};
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(request.layer_filter_kind == wormedit::keysound_layer_filter_kind_t::specific);
    check_filter(request.layer_filter_id.value == 501);
    (void)harness.frame();
    layer = harness.drawn_text_center("Blue");
    require_filter(layer.has_value());
    (void)click_filter(harness, layer->first, layer->second);
    auto no_layer = harness.drawn_text_center("No layer");
    require_filter(no_layer.has_value());
    (void)click_filter(harness, no_layer->first, no_layer->second);
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(request.layer_filter_kind == wormedit::keysound_layer_filter_kind_t::no_layer);
    auto clear = harness.drawn_text_center("Clear filters");
    require_filter(clear.has_value());
    (void)click_filter(harness, clear->first, clear->second);
    clear = harness.drawn_text_center("Clear filters");
    require_filter(clear.has_value());
    (void)click_filter(harness, clear->first, clear->second);
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(request.layer_filter_kind == wormedit::keysound_layer_filter_kind_t::any);
    auto& filters = worm_ui_nuklear_compose_state().keysound_filters.keysound_filters;
    filters.layer_filter_kind = WORM_UI_KEYSOUND_LAYER_FILTER_SPECIFIC;
    filters.layer_filter_id = 999;
    filters.layer_chart_id = harness.view().compose.chart_id;
    harness.view().compose.keysound_filter_selected_layer_valid = false;
    harness.view().compose.keysound_filter_selected_layer_name.clear();
    (void)harness.frame();
    worm_ui_frontend_keysound_dock_request(&request);
    check_filter(request.layer_filter_kind == wormedit::keysound_layer_filter_kind_t::any);
}

TEST_CASE("Sounds filter layer paging keeps its release off the sound rows",
          "[frontend-state][event-harness][sound-groups]") {
    static const std::array choices{
        wormedit::ui::compose_keysound_layer_view_t{.layer_id = wormedit::layer_id_t{601}, .name = "Layer 1"},
        wormedit::ui::compose_keysound_layer_view_t{.layer_id = wormedit::layer_id_t{602}, .name = "Layer 2"},
        wormedit::ui::compose_keysound_layer_view_t{.layer_id = wormedit::layer_id_t{603}, .name = "Layer 3"},
        wormedit::ui::compose_keysound_layer_view_t{.layer_id = wormedit::layer_id_t{604}, .name = "Layer 4"},
        wormedit::ui::compose_keysound_layer_view_t{.layer_id = wormedit::layer_id_t{605}, .name = "Layer 5"}};
    nuklear_event_harness_t harness{1280, 900};
    seed_sound_filter_view(harness.view().compose);
    auto& view = harness.view().compose;
    view.keysound_filter_layers.assign(std::begin(choices), std::end(choices));
    view.keysound_filter_layers.resize(choices.size());
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
    check_filter(worm_ui_nuklear_compose_state().keysound_filters.keysound_filter_layer_first == choices.size());
    check_filter(worm_ui_nuklear_compose_state().keysound_catalog.keysound_selection.selected.empty());
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
    wormedit::ui::keysound_dock_request_t request{};
    worm_ui_frontend_keysound_dock_request(&request);
    REQUIRE(request.collapsed_group_ids.size() == 1);
    CHECK(request.collapsed_group_ids[0].value == 80);
}

TEST_CASE("Dock requests retain typed selection and group buffers on warmed reads",
          "[frontend-state][event-harness][sound-filter][allocation]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.keysound_catalog.keysound_selection.selected = {7, 9};
    state.keysound_catalog.keysound_selection.primary = 9;
    state.keysound_filters.keysound_filters.group_ids = {80, 81};
    state.keysound_filters.keysound_filters.collapsed_group_ids = {81};
    wormedit::ui::keysound_dock_request_t request;
    worm_ui_frontend_keysound_dock_request(&request);
    const auto before = wormedit::sys::frame_heap_allocation_snapshot();
    worm_ui_frontend_keysound_dock_request(&request);
    const auto after = wormedit::sys::frame_heap_allocation_snapshot();
    check_filter(after.count == before.count);
    check_filter(after.bytes == before.bytes);
    check_filter(request.selected_resource_ids == std::vector{wormedit::resource_id_t{7}, wormedit::resource_id_t{9}});
    check_filter(request.primary_resource_id == wormedit::resource_id_t{9});
    check_filter(request.group_filter_ids == std::vector{wormedit::sound_group_id_t{80}, wormedit::sound_group_id_t{81}});
    check_filter(request.collapsed_group_ids == std::vector{wormedit::sound_group_id_t{81}});
}
