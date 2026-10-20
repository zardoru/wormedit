#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <vector>

namespace {

auto click_at(nuklear_event_harness_t& harness, float x, float y)
    -> std::vector<workspace_action_snapshot_t> {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    SDL_Event press{};
    press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    press.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    press.button.button = SDL_BUTTON_LEFT;
    press.button.down = true;
    press.button.clicks = 1;
    press.button.x = x;
    press.button.y = y;
    SDL_Event release = press;
    release.type = SDL_EVENT_MOUSE_BUTTON_UP;
    release.button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    release.button.down = false;
    const std::array<event_step_t, 2> script{{
        {{motion, press}, SDL_KMOD_NONE}, {{motion, release}, SDL_KMOD_NONE}}};
    return harness.run_script(script);
}

void set_audio_fixture(nuklear_event_harness_t& harness,
                       wormedit::ui::sound_row_view_t* rows, std::size_t row_count) {
    auto& workspace = harness.view();
    workspace.active_tab = WORM_UI_WORKSPACE_TAB_AUDIO_SLICES;
    workspace.compose.visible = false;
    auto& audio = workspace.audio_slices;
    audio.visible = true;
    audio.sound_rows.assign(rows, rows + row_count);
    audio.has_selected_asset = row_count != 0;
    audio.selected_asset_id = wormedit::asset_id_t{row_count != 0 ? rows[0].asset_id : wormedit::asset_id_t{}};
    audio.has_chart = true;
    audio.chart_id = wormedit::chart_id_t{7};
    audio.document_revision = 91;
    audio.offset_denominator = 1;
}

void check_copy_action(const wormedit::ui::workspace_action_t& action) {
    CHECK(action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT);
    CHECK(action.compose_resource_asset_id == 42);
    CHECK(action.compose_input_chart_id == 7);
    CHECK(action.compose_input_document_revision == 91);
}

} // namespace

TEST_CASE("Sounds copy action carries the selected asset and current chart revision",
          "[frontend-state][event-harness][sound-copy]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    std::array<wormedit::ui::sound_row_view_t, 1> rows{{
        {.resource_id = wormedit::resource_id_t{3}, .asset_id = wormedit::asset_id_t{42}, .name = "kick.wav", .tags_text = "keysound",
         .source_path = "/outside/kick.wav", .kind = static_cast<wormedit::sound_kind_t>(2), .use_count = 4}}};
    set_audio_fixture(harness, rows.data(), rows.size());
    (void)harness.frame();

    const auto button = harness.drawn_text_center("Copy source into project");
    REQUIRE(button);
    const auto frames = click_at(harness, button->first, button->second);
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT;
    });
    REQUIRE(found != frames.end());
    check_copy_action(found->get());
}

TEST_CASE("Sounds copy action stays inert when the selected source is missing",
          "[frontend-state][event-harness][sound-copy]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    std::array<wormedit::ui::sound_row_view_t, 1> rows{{
        {.resource_id = wormedit::resource_id_t{3}, .asset_id = wormedit::asset_id_t{42}, .name = "missing.wav", .tags_text = "keysound",
         .source_path = "/outside/missing.wav", .kind = static_cast<wormedit::sound_kind_t>(2), .missing = true}}};
    set_audio_fixture(harness, rows.data(), rows.size());
    (void)harness.frame();

    const auto button = harness.drawn_text_center("Copy source into project");
    REQUIRE(button);
    const auto frames = click_at(harness, button->first, button->second);
    CHECK(frames.back().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}
