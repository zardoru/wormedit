#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"

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
        wormedit::ui::compose_keysound_row_view_t{.resource_id = wormedit::resource_id_t{41}, .slot = "01", .name = "Kick 1"},
        wormedit::ui::compose_keysound_row_view_t{.resource_id = wormedit::resource_id_t{7}, .slot = "02", .name = "Kick 2"}};
    view.keysounds.assign(std::begin(rows), std::end(rows));
    view.keysounds.resize(rows.size());
    view.keysound_total_count = rows.size();
    (void)harness.frame();
    auto& review = worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_review;
    worm_ui_nuklear_compose_state().keysound_catalog.keysound_selection.selected = {7, 41};
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
    std::vector<const wormedit::ui::workspace_action_t*> actions;
    for (const auto& frame : frames)
        if (frame.get().compose_action == WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS)
            actions.push_back(&frame.get());
    require_event(actions.size() == 1);
    check_event(!worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_review.open);
    check_event(actions.front()->compose_sound_group_assignments.size() == 1);

    require_event(actions.front()->compose_sound_group_assignments.size() != 0);
    check_event(actions.front()->compose_sound_group_assignments[0].sound_ids.size() == 2);
    auto assigned_ids = actions.front()->compose_sound_group_assignments[0].sound_ids;
    std::ranges::sort(assigned_ids);
    check_event(assigned_ids == std::vector<wormedit::resource_id_t>{{7}, {41}});
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
    auto& review = worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_review;
    review.open = true;
    review.project_id = view.project_id.value;
    review.chart_id = view.chart_id.value;
    review.document_revision = view.document_revision;
    review.candidates = {{.id = 9, .name = "hat01"}, {.id = 4, .name = "hat02"}};
    review.groups = {{.name = "hat", .accepted = true, .member_ids = {9, 4}}};
    (void)harness.frame();
    (void)harness.frame();  // A dialog fits its content on its second frame.
    const auto leave = harness.drawn_text_center("Leave all ungrouped");
    require_event(leave.has_value());
    auto ignored = click_at(harness, leave->first, leave->second);
    const auto confirm = harness.drawn_text_center("Confirm");
    require_event(confirm.has_value());
    const auto committed = click_at(harness, confirm->first, confirm->second);
    ignored.insert(ignored.end(), committed.begin(), committed.end());
    check_event(!worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_review.open);
    check_event(std::ranges::none_of(ignored, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS ||
               frame.get().compose_action == WORM_UI_COMPOSE_ACTION_UNGROUP_SOUNDS ||
               frame.get().compose_inputs.size() != 0;
    }));
}

TEST_CASE("Sounds right-click menu opens Group review without editing on the press",
          "[frontend-state][event-harness][sound-groups]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    view.keysounds_available = true;
    const std::array rows{
        wormedit::ui::compose_keysound_row_view_t{.resource_id = wormedit::resource_id_t{41}, .slot = "01", .name = "Kick 1"}};
    view.keysounds.assign(std::begin(rows), std::end(rows));
    view.keysounds.resize(rows.size());
    view.keysound_total_count = rows.size();
    (void)harness.frame();
    const auto row = harness.drawn_text_center("Kick 1");
    require_event(row.has_value());
    const auto clicked = right_click_at(harness, row->first, row->second);
    check_event(std::ranges::none_of(clicked, [](const auto& frame) {
        return frame.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE;
    }));
    const auto& state = worm_ui_nuklear_compose_state();
    require_event(state.keysound_catalog.keysound_group_menu.open);
    check_event(state.keysound_catalog.keysound_selection.selected == std::vector<std::uint64_t>{41});
    (void)harness.frame();
    const auto group = harness.drawn_text_center("Group...");
    require_event(group.has_value());
    const auto opened = click_at(harness, group->first, group->second);
    check_event(std::ranges::none_of(opened, [](const auto& frame) {
        return frame.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE;
    }));
    check_event(!state.keysound_catalog.keysound_group_menu.open);
    (void)harness.frame();
    check_event(state.keysound_catalog.keysound_group_review.open);
    check_event(state.keysound_catalog.keysound_group_review.manual);
}

TEST_CASE("Group selected sounds dialog has Cancel, Confirm with Enter, and no title-repeating label",
          "[frontend-state][event-harness][sound-groups][dialog]") {
    const auto open_manual = [](nuklear_event_harness_t& harness) -> auto& {
        auto& view = harness.view().compose;
        view.keysounds_available = true;
        (void)harness.frame();
        auto& review = worm_ui_nuklear_compose_state().keysound_catalog.keysound_group_review;
        review.open = true;
        review.manual = true;
        review.project_id = view.project_id.value;
        review.chart_id = view.chart_id.value;
        review.document_revision = view.document_revision;
        review.candidates = {{.id = 9, .name = "hat01"}};
        std::snprintf(review.manual_group_name.data(), review.manual_group_name.size(), "%s", "Hats");
        (void)harness.frame();
        (void)harness.frame();
        return review;
    };
    {
        nuklear_event_harness_t harness{1280, 900};
        auto& review = open_manual(harness);
        const auto& text = harness.drawn_text();
        const auto first = text.find("Group selected sounds");
        check_event(first != std::string::npos && text.find("Group selected sounds", first + 1) == std::string::npos);
        const auto cancel = harness.drawn_text_center("Cancel");
        require_event(cancel.has_value());
        (void)click_at(harness, cancel->first, cancel->second);
        check_event(!review.open);
    }
    {
        nuklear_event_harness_t harness{1280, 900};
        auto& review = open_manual(harness);
        SDL_Event enter{};
        enter.type = SDL_EVENT_KEY_DOWN;
        enter.key.key = SDLK_RETURN;
        enter.key.scancode = SDL_SCANCODE_RETURN;
        enter.key.down = true;
        const auto action = harness.frame(std::array{enter});
        check_event(action.get().compose_action == WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS);
        check_event(!review.open);
    }
}
