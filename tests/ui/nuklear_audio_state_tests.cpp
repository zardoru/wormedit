#include "ui/nuklear/audio/audio_state.h"

#include <catch2/catch_test_macros.hpp>
#include <source_location>
#include <array>
#include <cstring>

namespace {
void expect_state(bool condition, std::source_location at = std::source_location::current()) {
    INFO("Audio state expectation at " << at.line());
    CHECK(condition);
}

TEST_CASE("Sounds tag filters match individual tags without case sensitivity",
          "[unit][frontend][audio][sound-tags]") {
    worm_ui_nuklear_audio_state_t state;
    const std::array<std::string, 2> tags{"Vocal", "Big Drum"};
    CHECK(state.matches_tags(tags));
    std::strcpy(state.tag_filter, "DRUM");
    CHECK(state.matches_tags(tags));
    CHECK_FALSE(state.matches_tags({}));
    std::strcpy(state.tag_filter, "Vocal, Big");
    CHECK_FALSE(state.matches_tags(tags));
    auto& shared = worm_ui_nuklear_audio_state();
    std::strcpy(shared.tag_filter, "Vocal");
    std::strcpy(shared.new_tag, "Draft");
    worm_ui_nuklear_reset_audio_state();
    CHECK(shared.tag_filter[0] == '\0');
    CHECK(shared.new_tag[0] == '\0');
}
}

TEST_CASE("Nuklear audio state resets offset drag identity and cancellation",
          "[unit][frontend][audio]") {
    auto& state = worm_ui_nuklear_audio_state();
    state.offset_drag_active = true;
    state.offset_drag_anchor_x = 42.0f;
    state.offset_drag_width = 320.0f;
    state.offset_drag_chart_id = 11;
    state.offset_drag_document_revision = 12;
    state.offset_drag_asset_id = 13;
    state.offset_drag_total_frames = 48'000;
    state.offset_drag_sample_rate = 48'000;
    state.offset_drag_delta_frames = -240;
    state.offset_escape_pending = true;
    state.offset_cancel_pending = true;

    worm_ui_nuklear_reset_audio_state();

    expect_state(!state.offset_drag_active);
    expect_state(state.offset_drag_anchor_x == 0.0f);
    expect_state(state.offset_drag_width == 0.0f);
    expect_state(state.offset_drag_chart_id == 0);
    expect_state(state.offset_drag_document_revision == 0);
    expect_state(state.offset_drag_asset_id == 0);
    expect_state(state.offset_drag_total_frames == 0);
    expect_state(state.offset_drag_sample_rate == 0);
    expect_state(state.offset_drag_delta_frames == 0);
    expect_state(!state.offset_escape_pending);
    expect_state(!state.offset_cancel_pending);
}

TEST_CASE("Sounds keyboard commands require list ownership and coalesce held input",
          "[unit][frontend][audio][sounds-workflow]") {
    worm_ui_nuklear_reset_audio_state();
    auto& state = worm_ui_nuklear_audio_state();
    expect_state(!worm_ui_nuklear_audio_list_key_command(1, true));
    state.sound_list_valid = true;
    expect_state(!worm_ui_nuklear_audio_list_key_command(1, false));
    state.sound_list_focused = true;
    expect_state(worm_ui_nuklear_audio_list_key_command(1, false));
    expect_state(state.list_move_pending == 1);
    expect_state(worm_ui_nuklear_audio_list_key_command(1, false));
    expect_state(state.list_move_pending == 1);
    expect_state(worm_ui_nuklear_audio_list_key_command(0, true));
    expect_state(state.list_audition_pending);
    expect_state(worm_ui_nuklear_audio_list_key_command(0, false));
    expect_state(state.list_audition_pending);
    worm_ui_nuklear_reset_audio_state();
    expect_state(!state.sound_list_focused);
    expect_state(!state.list_audition_pending);
    expect_state(state.list_move_pending == 0);
    expect_state(!state.import_drop_valid);
    expect_state(state.waveform_asset_id == 0);
    expect_state(state.properties_resource_id == 0);
}
