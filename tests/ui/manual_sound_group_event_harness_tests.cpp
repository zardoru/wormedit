#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <source_location>
#include <vector>

namespace {
void check_event(bool condition,
                 std::source_location location = std::source_location::current()) {
    INFO(location.file_name() << ':' << location.line());
    CHECK(condition);
}

void require_event(bool condition,
                   std::source_location location = std::source_location::current()) {
    INFO(location.file_name() << ':' << location.line());
    REQUIRE(condition);
}

auto click_at(nuklear_event_harness_t& harness, float x, float y)
    -> std::vector<workspace_action_snapshot_t> {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
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
    std::array<event_step_t, 2> steps{};
    steps[0].events = {motion, down};
    steps[1].events = {motion, up};
    return harness.run_script(steps);
}

auto right_click_at(nuklear_event_harness_t& harness, float x, float y)
    -> std::vector<workspace_action_snapshot_t> {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_RIGHT;
    down.button.down = true;
    down.button.clicks = 1;
    down.button.x = x;
    down.button.y = y;
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.down = false;
    std::array<event_step_t, 2> steps{};
    steps[0].events = {motion, down};
    steps[1].events = {motion, up};
    return harness.run_script(steps);
}
} // namespace

TEST_CASE("Manual sound group review confirms once on release with stable resource IDs",
          "[frontend-state][event-harness][sound-groups]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    view.keysounds_available = true;
    const std::array rows{
        WormUiComposeKeysoundRowView{.resource_id = 41, .slot = "01", .name = "Kick 1"},
        WormUiComposeKeysoundRowView{.resource_id = 7, .slot = "02", .name = "Kick 2"}};
    view.keysounds = rows.data();
    view.keysound_count = rows.size();
    view.keysound_total_count = rows.size();
    (void)harness.frame();
    auto& review = worm_ui_nuklear_compose_state().keysound_group_review;
    worm_ui_nuklear_compose_state().keysound_selection.selected = {7, 41};
    (void)harness.frame();
    const auto group_button = harness.drawn_text_center("Group...");
    require_event(group_button.has_value());
    const auto opened = click_at(harness, group_button->first, group_button->second);
    check_event(std::ranges::none_of(opened, [](const auto& frame) {
        return frame.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE;
    }));
    (void)harness.frame();
    require_event(review.open);
    check_event(review.manual);
    check_event(review.keep_ids_together);
    std::snprintf(review.manual_group_name.data(), review.manual_group_name.size(), "%s", "Kicks");
    (void)harness.frame();
    const auto confirm = harness.drawn_text_center("Confirm");
    require_event(confirm.has_value());
    const auto frames = click_at(harness, confirm->first, confirm->second);
    std::vector<const WormUiWorkspaceAction*> actions;
    for (const auto& frame : frames)
        if (frame.get().compose_action == WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS)
            actions.push_back(&frame.get());
    require_event(actions.size() == 1);
    check_event(!worm_ui_nuklear_compose_state().keysound_group_review.open);
    check_event(actions.front()->compose_sound_group_assignment_count == 1);
    require_event(actions.front()->compose_sound_group_assignments != nullptr);
    check_event(actions.front()->compose_sound_group_assignments[0].sound_count == 2);
    std::vector<std::uint64_t> assigned_ids(
        actions.front()->compose_sound_group_assignments[0].sound_ids,
        actions.front()->compose_sound_group_assignments[0].sound_ids + 2);
    std::ranges::sort(assigned_ids);
    check_event(assigned_ids == std::vector<std::uint64_t>{7, 41});
    check_event(actions.front()->compose_sound_group_keep_ids_together);
    const auto emitted = std::ranges::count_if(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS;
    });
    check_event(emitted == 1);
}

TEST_CASE("Rejecting every sound group suggestion closes without an edit",
          "[frontend-state][event-harness][sound-groups]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    view.keysounds_available = true;
    (void)harness.frame();
    auto& review = worm_ui_nuklear_compose_state().keysound_group_review;
    review.open = true;
    review.project_id = view.project_id;
    review.chart_id = view.chart_id;
    review.document_revision = view.document_revision;
    review.candidates = {{.id = 9, .name = "hat01"}, {.id = 4, .name = "hat02"}};
    review.groups = {{.name = "hat", .accepted = true, .member_ids = {9, 4}}};
    (void)harness.frame();
    const auto leave = harness.drawn_text_center("Leave all ungrouped");
    require_event(leave.has_value());
    auto ignored = click_at(harness, leave->first, leave->second);
    const auto confirm = harness.drawn_text_center("Confirm");
    require_event(confirm.has_value());
    const auto committed = click_at(harness, confirm->first, confirm->second);
    ignored.insert(ignored.end(), committed.begin(), committed.end());
    check_event(!worm_ui_nuklear_compose_state().keysound_group_review.open);
    check_event(std::ranges::none_of(ignored, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS ||
               frame.get().compose_action == WORM_UI_COMPOSE_ACTION_UNGROUP_SOUNDS ||
               frame.get().compose_input_count != 0;
    }));
}

TEST_CASE("Sounds right-click menu opens Group review without editing on the press",
          "[frontend-state][event-harness][sound-groups]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    view.keysounds_available = true;
    const std::array rows{
        WormUiComposeKeysoundRowView{.resource_id = 41, .slot = "01", .name = "Kick 1"}};
    view.keysounds = rows.data();
    view.keysound_count = rows.size();
    view.keysound_total_count = rows.size();
    (void)harness.frame();
    const auto row = harness.drawn_text_center("Kick 1");
    require_event(row.has_value());
    const auto clicked = right_click_at(harness, row->first, row->second);
    check_event(std::ranges::none_of(clicked, [](const auto& frame) {
        return frame.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE;
    }));
    const auto& state = worm_ui_nuklear_compose_state();
    require_event(state.keysound_group_menu.open);
    check_event(state.keysound_selection.selected == std::vector<std::uint64_t>{41});
    (void)harness.frame();
    const auto group = harness.drawn_text_center("Group...");
    require_event(group.has_value());
    const auto opened = click_at(harness, group->first, group->second);
    check_event(std::ranges::none_of(opened, [](const auto& frame) {
        return frame.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE;
    }));
    check_event(!state.keysound_group_menu.open);
    (void)harness.frame();
    check_event(state.keysound_group_review.open);
    check_event(state.keysound_group_review.manual);
}
