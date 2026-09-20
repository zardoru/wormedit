#include "ui/nuklear/audio_state.h"

#include <catch2/catch_test_macros.hpp>

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

    CHECK_FALSE(state.offset_drag_active);
    CHECK(state.offset_drag_anchor_x == 0.0f);
    CHECK(state.offset_drag_width == 0.0f);
    CHECK(state.offset_drag_chart_id == 0);
    CHECK(state.offset_drag_document_revision == 0);
    CHECK(state.offset_drag_asset_id == 0);
    CHECK(state.offset_drag_total_frames == 0);
    CHECK(state.offset_drag_sample_rate == 0);
    CHECK(state.offset_drag_delta_frames == 0);
    CHECK_FALSE(state.offset_escape_pending);
    CHECK_FALSE(state.offset_cancel_pending);
}
