#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <catch2/generators/catch_generators.hpp>

namespace {
void verify_fit_event(bool value) { REQUIRE(value); }
auto fit_motion(float x, float y) -> SDL_Event {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = x; e.motion.y = y; e.motion.state = SDL_BUTTON_LMASK;
    return e;
}
auto fit_button(float x, float y, bool down) -> SDL_Event {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT; e.button.down = down;
    e.button.x = x; e.button.y = y;
    return e;
}
auto find_fit_input(const WormUiWorkspaceAction& action, WormUiComposeInputKind kind)
    -> const WormUiComposeInputAction* {
    for (std::size_t i = 0; i < action.compose_input_count; ++i)
        if (action.compose_inputs[i].kind == kind) return &action.compose_inputs[i];
    return nullptr;
}
}

TEST_CASE("Fit drag switches literal Ctrl Alt onset mode without pointer motion",
          "[frontend-state][event-harness][onset-snap]") {
    nuklear_event_harness_t h{1280, 900};
    verify_fit_event(h.ready());
    auto& v = h.view().compose;
    WormUiComposeWaveformRowView wave{};
    WormUiComposeGridLineView beat{};
    beat.chart_row = 4; beat.kind = 1;
    beat.y_pixels = 120;
    v.waveform_rows = &wave; v.waveform_row_count = 1;
    v.grid_lines = &beat; v.grid_line_count = 1;
    v.selected_audio_asset_id = 6; v.waveform_overview_sample_rate = 44100;
    (void)h.frame();
    WormUiTestingRect rect{};
    verify_fit_event(worm_ui_frontend_compose_viewport_for_testing(&rect));
    const auto x = rect.x + static_cast<float>(32 * rect.width / v.viewport_width_pixels);
    const auto y = rect.y + static_cast<float>(120 * rect.height / v.viewport_height_pixels);
    const std::array press{fit_motion(x,y), fit_button(x,y,true)};
    const auto begun = h.frame(press);
    verify_fit_event(find_fit_input(begun.get(), WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN));
    v.beat_fit_drag_active = true; v.beat_fit_chart_row = 4;
    v.waveform_overview_total_frames = 441000;
    v.beat_fit_original_bpm_denominator = 1; v.beat_fit_original_bpm_numerator = 120;
    v.beat_fit_proposed_bpm_denominator = 1; v.beat_fit_proposed_bpm_numerator = 121;
    v.beat_fit_target_y_pixels = 120;
    const auto plain = h.frame({}, SDL_KMOD_CTRL);
    const auto* pi = find_fit_input(plain.get(), WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE);
    INFO("active=" << worm_ui_nuklear_compose_state().beat_fit_drag_active << " ctrl=" << worm_ui_nuklear_compose_state().beat_fit_ctrl_held << " events=" << plain.get().compose_input_count);
    for (std::size_t i = 0; i < plain.get().compose_input_count; ++i) INFO("kind=" << plain.get().compose_inputs[i].kind);
    verify_fit_event(pi); verify_fit_event(pi->beat_fit_ctrl_held); verify_fit_event(!(pi->beat_fit_onset_snap_held));
    const auto onset = h.frame({}, static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_ALT));
    const auto* oi = find_fit_input(onset.get(), WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE);
    verify_fit_event(oi); verify_fit_event(oi->beat_fit_ctrl_held); verify_fit_event(oi->beat_fit_onset_snap_held);
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y = 1;
    wheel.wheel.mouse_x = x; wheel.wheel.mouse_y = y;
    const auto step_before = worm_ui_nuklear_compose_state().beat_fit_snap_step_index;
    const std::array wheels{wheel};
    (void)h.frame(wheels, static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_ALT));
    verify_fit_event(worm_ui_nuklear_compose_state().beat_fit_snap_step_index == step_before);
    const auto restored = h.frame({}, SDL_KMOD_CTRL);
    const auto* ri = find_fit_input(restored.get(), WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE);
    verify_fit_event(ri); verify_fit_event(!(ri->beat_fit_onset_snap_held));
    const auto meta = h.frame({}, static_cast<SDL_Keymod>(SDL_KMOD_GUI | SDL_KMOD_ALT));
    const auto* mi = find_fit_input(meta.get(), WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE);
    verify_fit_event(mi); verify_fit_event(!(mi->beat_fit_ctrl_held)); verify_fit_event(!(mi->beat_fit_onset_snap_held));
    const std::array release{fit_motion(x, y + 20), fit_button(x, y + 20, false)};
    const auto released = h.frame(release, static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_ALT));
    const auto* end = find_fit_input(released.get(), WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE);
    verify_fit_event(end); verify_fit_event(end->beat_fit_onset_snap_held);

}

TEST_CASE("Group beat rail handles start fitting with the same guards as other beats",
          "[frontend-state][event-harness][beat-fit]") {
    const auto kind = GENERATE(0, 1, 2, 3);
    const auto locked = GENERATE(false, true);
    nuklear_event_harness_t h{1280, 900};
    verify_fit_event(h.ready());
    auto& v = h.view().compose;
    WormUiComposeWaveformRowView wave{};
    WormUiComposeGridLineView beat{};
    beat.chart_row = 480; beat.kind = kind; beat.y_pixels = 120;
    v.waveform_rows = &wave; v.waveform_row_count = 1;
    v.grid_lines = &beat; v.grid_line_count = 1;
    v.selected_audio_asset_id = 6; v.waveform_overview_sample_rate = 44100;
    v.waveform_timing_locked = locked;
    (void)h.frame();
    WormUiTestingRect rect{};
    verify_fit_event(worm_ui_frontend_compose_viewport_for_testing(&rect));
    const auto x = rect.x + static_cast<float>(32 * rect.width / v.viewport_width_pixels);
    const auto y = rect.y + static_cast<float>(120 * rect.height / v.viewport_height_pixels);
    const std::array press{fit_motion(x,y), fit_button(x,y,true)};
    const auto begun = h.frame(press);
    const auto* fit = find_fit_input(begun.get(), WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN);
    verify_fit_event((fit != nullptr) == (kind != 0 && !locked));
    if (fit) verify_fit_event(fit->object_id == 480);
    beat.chart_row = 0;
    verify_fit_event(!worm_ui_nuklear_beat_fit_line(beat));
}
