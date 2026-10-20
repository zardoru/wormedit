#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>
#include <array>

namespace {
void enable_slots(nuklear_event_harness_t& harness) {
    (void)harness.frame();
    worm_ui_nuklear_compose_state().keysound_filters.keysound_slots_mode = true;
    harness.view().compose.sound_slots_mode = true;
    (void)harness.frame();
}
void stage_batch(nuklear_event_harness_t& harness, const char* reason = "") {
    const auto& view = harness.view().compose;
    const std::array<wormedit::ui::sound_slot_import_item_t, 2> items{{
        {"/tmp/sound2.wav", "sound2.wav", 0, "", false},
        {"/tmp/sound10.wav", "sound10.wav", 0, "", false}}};
    REQUIRE(worm_ui_frontend_stage_sound_slot_batch(view.project_id, view.chart_id,
        view.document_revision, 35, items.data(), items.size(), reason));
    (void)harness.frame();
    (void)harness.frame();  // A dialog fits its content on its second frame.
}
auto click(nuklear_event_harness_t& harness, const char* text) -> workspace_action_snapshot_t {
    const auto center = harness.drawn_text_center(text);
    REQUIRE(center);
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = center->first;
    motion.motion.y = center->second;
    SDL_Event press{};
    press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    press.button.button = SDL_BUTTON_LEFT;
    press.button.down = true;
    press.button.x = center->first;
    press.button.y = center->second;
    const std::array down{motion, press};
    const auto pressed = harness.frame(down);
    press.type = SDL_EVENT_MOUSE_BUTTON_UP;
    press.button.down = false;
    const auto released = harness.frame(std::array{motion, press});
    return pressed.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE ? pressed : released;
}
void check_confirmed_batch(const wormedit::ui::workspace_action_t& action) {
    REQUIRE(action.compose_action == WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH);
    CHECK(action.compose_sound_slot == 35);
    REQUIRE(action.sound_import_item_count == 2);
    CHECK(std::string{action.sound_import_items[0].path} == "/tmp/sound2.wav");
    CHECK(std::string{action.sound_import_items[1].path} == "/tmp/sound10.wav");
}
}

TEST_CASE("WAV batch review keeps ordered paths and original starting slot through confirmation",
          "[event-harness][sound-slot-batch]") {
    nuklear_event_harness_t harness{960, 720};
    REQUIRE(harness.ready());
    enable_slots(harness);
    stage_batch(harness);
    worm_ui_nuklear_compose_state().keysound_catalog.keysound_selected_slot = 100;
    const auto snapshot = click(harness, "Assign files");
    check_confirmed_batch(snapshot.get());
    worm_ui_frontend_sound_slot_batch_result(true, "Imported");
    CHECK_FALSE(worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch.open);
}

TEST_CASE("Invalid WAV batches stay visible and cannot confirm", "[event-harness][sound-slot-batch]") {
    nuklear_event_harness_t harness{640, 480};
    REQUIRE(harness.ready());
    enable_slots(harness);
    stage_batch(harness, "One file cannot be decoded.");
    const auto snapshot = click(harness, "Assign files");
    CHECK(snapshot.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK(worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch.open);
    CHECK(harness.drawn_text().find("One file cannot be decoded.") != std::string::npos);
}

TEST_CASE("Escape and a stale document cancel WAV batch review", "[event-harness][sound-slot-batch]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    enable_slots(harness);
    stage_batch(harness);
    SDL_Event escape{};
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.key = SDLK_ESCAPE;
    escape.key.down = true;
    (void)harness.frame(std::array{escape});
    CHECK_FALSE(worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch.open);
    stage_batch(harness);
    ++harness.view().compose.document_revision;
    (void)harness.frame();
    CHECK_FALSE(worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch.open);
}

TEST_CASE("WAV batch dialog Cancel closes it and Enter confirms",
          "[event-harness][sound-slot-batch][dialog]") {
    {
        nuklear_event_harness_t harness{960, 720};
        REQUIRE(harness.ready());
        enable_slots(harness);
        stage_batch(harness);
        (void)click(harness, "Cancel");
        CHECK_FALSE(worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch.open);
    }
    {
        nuklear_event_harness_t harness{960, 720};
        REQUIRE(harness.ready());
        enable_slots(harness);
        stage_batch(harness);
        SDL_Event enter{};
        enter.type = SDL_EVENT_KEY_DOWN;
        enter.key.key = SDLK_RETURN;
        enter.key.scancode = SDL_SCANCODE_RETURN;
        enter.key.down = true;
        const auto snapshot = harness.frame(std::array{enter});
        check_confirmed_batch(snapshot.get());
    }
    {
        nuklear_event_harness_t harness{960, 720};
        REQUIRE(harness.ready());
        enable_slots(harness);
        stage_batch(harness, "One file cannot be decoded.");
        SDL_Event enter{};
        enter.type = SDL_EVENT_KEY_DOWN;
        enter.key.key = SDLK_RETURN;
        enter.key.scancode = SDL_SCANCODE_RETURN;
        enter.key.down = true;
        CHECK(harness.frame(std::array{enter}).get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    }
}
