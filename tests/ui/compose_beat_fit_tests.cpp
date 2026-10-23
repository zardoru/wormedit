#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/compose_timing_geometry.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <source_location>
#include <catch2/generators/catch_generators.hpp>

namespace {
void verify_fit_event(bool value, std::source_location source = std::source_location::current()) {
    INFO("Fit expectation at " << source.line()); REQUIRE(value);
}
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
auto find_fit_input(const wormedit::ui::workspace_action_t& action, WormUiComposeInputKind kind)
    -> const wormedit::ui::compose_input_action_t* {
    for (std::size_t i = 0; i < action.compose_inputs.size(); ++i)
        if (action.compose_inputs[i].kind == kind) return &action.compose_inputs[i];
    return nullptr;
}
}

TEST_CASE("Fit drag switches literal Ctrl Alt onset mode without pointer motion",
          "[frontend-state][event-harness][onset-snap]") {
    nuklear_event_harness_t h{1280, 900};
    verify_fit_event(h.ready());
    auto& v = h.view().compose;
    wormedit::ui::compose_waveform_row_view_t wave{};
    wormedit::ui::compose_grid_line_view_t beat{};
    beat.chart_row = 4; beat.kind = wormedit::editor_grid_line_kind_t::beat;
    beat.y_pixels = 120;
    v.waveform_rows = {wave};
    v.grid_lines = {beat};
    v.selected_audio_asset_id.value = 6; v.waveform_overview_sample_rate = 44100;
    (void)h.frame();
    wormedit::ui::testing_rect_view_t rect{};
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
    INFO("active=" << worm_ui_nuklear_compose_state().waveform_drag.beat_fit_drag_active << " ctrl=" << worm_ui_nuklear_compose_state().waveform_drag.beat_fit_ctrl_held << " events=" << plain.get().compose_inputs.size());
    for (std::size_t i = 0; i < plain.get().compose_inputs.size(); ++i) INFO("kind=" << plain.get().compose_inputs[i].kind);
    verify_fit_event(pi); verify_fit_event(pi->beat_fit_ctrl_held); verify_fit_event(!(pi->beat_fit_onset_snap_held));
    const auto onset = h.frame({}, static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_ALT));
    const auto* oi = find_fit_input(onset.get(), WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE);
    verify_fit_event(oi); verify_fit_event(oi->beat_fit_ctrl_held); verify_fit_event(oi->beat_fit_onset_snap_held);
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y = 1;
    wheel.wheel.mouse_x = x; wheel.wheel.mouse_y = y;
    const auto step_before = worm_ui_nuklear_compose_state().waveform_drag.beat_fit_snap_step_index;
    const std::array wheels{wheel};
    (void)h.frame(wheels, static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_ALT));
    verify_fit_event(worm_ui_nuklear_compose_state().waveform_drag.beat_fit_snap_step_index == step_before);
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
    wormedit::ui::compose_waveform_row_view_t wave{};
    wormedit::ui::compose_grid_line_view_t beat{};
    beat.chart_row = 480; beat.kind = static_cast<wormedit::editor_grid_line_kind_t>(kind); beat.y_pixels = 120;
    v.waveform_rows = {wave};
    v.grid_lines = {beat};
    v.selected_audio_asset_id.value = 6; v.waveform_overview_sample_rate = 44100;
    v.waveform_timing_locked = locked;
    (void)h.frame();
    wormedit::ui::testing_rect_view_t rect{};
    verify_fit_event(worm_ui_frontend_compose_viewport_for_testing(&rect));
    const auto x = rect.x + static_cast<float>(32 * rect.width / v.viewport_width_pixels);
    const auto y = rect.y + static_cast<float>(120 * rect.height / v.viewport_height_pixels);
    const std::array press{fit_motion(x,y), fit_button(x,y,true)};
    const auto begun = h.frame(press);
    const auto* fit = find_fit_input(begun.get(), WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN);
    verify_fit_event((fit != nullptr) == (kind != 0 && !locked));
    if (fit) verify_fit_event(fit->object_id.value == 480);
    beat.chart_row = 0;
    verify_fit_event(!worm_ui_nuklear_beat_fit_line(beat));
}

namespace {
struct timing_drag_fixture_t {
    nuklear_event_harness_t h{1600, 900};
    wormedit::ui::compose_kiai_span_view_t kiai{};
    wormedit::ui::compose_speed_span_view_t speed{};
    wormedit::ui::compose_recipe_area_view_t recipe{};
    timing_drag_fixture_t() {
        auto& v = h.view().compose;
        v.viewport_width_pixels = 900; v.viewport_height_pixels = 600;
        v.playable_field_left_pixels = 500; v.playable_field_right_pixels = 850;
        v.timing_lane_visibility_mask = 255;
        kiai.bounds = {0, 200, 0, 400}; kiai.head_object_id.value = 10;
        kiai.head_visible = kiai.tail_visible = true; kiai.end_row = 960;
        speed.bounds = {0, 200, 0, 400}; speed.object_id.value = 20;
        recipe.bounds = {0, 200, 0, 400}; recipe.recipe_id.value = 30;
        recipe.lane = wormedit::timing_lane_kind_t::scroll; recipe.label = "Test";
        recipe.begin_visible = recipe.end_visible = true;
        v.kiai_spans = {kiai};
        v.speed_spans = {speed};
        v.recipe_areas = {recipe};
        auto& s = worm_ui_nuklear_compose_state();
        s.dock_layout.layout.show(wormedit::dock_panel_t::inspector, false);
        s.dock_layout.layout.show(wormedit::dock_panel_t::layers, false);
        s.dock_layout.layout.show(wormedit::dock_panel_t::sounds, false);
        (void)h.frame();
    }
    auto point(wormedit::timing_lane_kind_t lane, double row_y) -> std::pair<float, float> {
        const auto extent = wormedit::ui::compose_timing_lane_extent(h.view().compose, lane);
        verify_fit_event(extent.has_value());
        wormedit::ui::testing_rect_view_t rect{};
        verify_fit_event(worm_ui_frontend_compose_viewport_for_testing(&rect));
        return {rect.x + static_cast<float>((extent->first + extent->second) * .5 / 900 * rect.width),
                rect.y + static_cast<float>(row_y / 600 * rect.height)};
    }
    auto press(wormedit::timing_lane_kind_t lane, double row_y) -> std::pair<float, float> {
        const auto p = point(lane, row_y);
        (void)h.frame(std::array{fit_motion(p.first, p.second), fit_button(p.first, p.second, true)});
        return p;
    }
};
}

TEST_CASE("Timing drags respect the timing lock before capture", "[event-harness][drag-capture]") {
    const auto lane = GENERATE(wormedit::timing_lane_kind_t::kiai, wormedit::timing_lane_kind_t::speed,
                               wormedit::timing_lane_kind_t::scroll, wormedit::timing_lane_kind_t::bpm);
    timing_drag_fixture_t f;
    auto& v = f.h.view().compose;
    if (lane == wormedit::timing_lane_kind_t::bpm) {
        v.timing_stage_active = true; v.timing_stage_lane = lane; v.timing_stage_y_pixels = 200;
        auto& t = f.h.view().timing;
        t.stage_active = true; t.stage_lane = lane; t.stage_existing_object_id = wormedit::object_id_t{40};
    }
    v.waveform_timing_locked = true;
    (void)f.h.frame();
    (void)f.press(lane, 200);
    const auto& s = worm_ui_nuklear_compose_state();
    verify_fit_event(!s.timing_drag.kiai_drag_active && !s.timing_drag.recipe_drag_active && !s.timing_drag.timing_stage_drag_active);
}

TEST_CASE("Span drags retire before a locked stale or occluded release",
          "[event-harness][drag-capture]") {
    const auto lane = GENERATE(wormedit::timing_lane_kind_t::kiai, wormedit::timing_lane_kind_t::speed,
                               wormedit::timing_lane_kind_t::scroll);
    const auto interruption = GENERATE(0, 1, 2, 3);
    timing_drag_fixture_t f;
    auto& v = f.h.view().compose;
    const auto p = f.press(lane, 200);
    verify_fit_event(worm_ui_nuklear_compose_state().timing_drag.kiai_drag_active ||
                     worm_ui_nuklear_compose_state().timing_drag.recipe_drag_active);
    (void)f.h.frame(std::array{fit_motion(p.first, p.second + 25)});
    switch (interruption) {
    case 0: v.waveform_timing_locked = true; break;
    case 1: v.input_occluded = true; break;
    case 2: ++v.document_revision; ++f.h.view().document_revision; break;
    case 3: ++v.project_id.value; ++f.h.view().project_id.value; break;
    }
    const auto end = f.h.frame(std::array{fit_button(p.first, p.second + 25, false)});
    verify_fit_event(!find_fit_input(end.get(), WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT));
    verify_fit_event(!find_fit_input(end.get(), WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT));
    verify_fit_event(!find_fit_input(end.get(), WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT));
    const auto& s = worm_ui_nuklear_compose_state();
    verify_fit_event(!s.timing_drag.kiai_drag_active && !s.timing_drag.recipe_drag_active);
    verify_fit_event(!s.timing_drag.span_capture.active && !s.timing_drag.recipe_capture.active);
}

TEST_CASE("Span release uses the captured stamp and a screen pixel threshold",
          "[event-harness][drag-capture]") {
    const auto lane = GENERATE(wormedit::timing_lane_kind_t::kiai, wormedit::timing_lane_kind_t::speed,
                               wormedit::timing_lane_kind_t::scroll);
    const auto distance = GENERATE(0.0f, 3.0f, 4.0f, 20.0f);
    timing_drag_fixture_t f;
    const auto p = f.press(lane, 400);
    const auto end = f.h.frame(std::array{fit_motion(p.first, p.second + distance),
                                        fit_button(p.first, p.second + distance, false)});
    const auto kind = lane == wormedit::timing_lane_kind_t::kiai ? WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT
                    : lane == wormedit::timing_lane_kind_t::speed ? WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT
                                                       : WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT;
    const auto* edit = find_fit_input(end.get(), kind);
    verify_fit_event((edit != nullptr) == (distance >= 4));
    if (edit) {
        verify_fit_event(edit->timing_chart_id.value == 1 && edit->timing_document_revision == 1);
        verify_fit_event(edit->span_edge == WORM_UI_SPAN_HANDLE_END);
    }
    const auto& s = worm_ui_nuklear_compose_state();
    verify_fit_event(!s.timing_drag.span_capture.active && !s.timing_drag.recipe_capture.active);
}

TEST_CASE("Staged timing uses the shared lock and threshold while retaining Apply semantics",
          "[event-harness][drag-capture]") {
    timing_drag_fixture_t f;
    auto& v = f.h.view().compose;
    auto& t = f.h.view().timing;
    v.timing_stage_active = t.stage_active = true;
    v.timing_stage_lane = t.stage_lane = wormedit::timing_lane_kind_t::bpm;
    v.timing_stage_y_pixels = 200; t.stage_existing_object_id = wormedit::object_id_t{40};
    (void)f.h.frame();
    const auto p = f.press(wormedit::timing_lane_kind_t::bpm, 200);
    const auto small = f.h.frame(std::array{fit_motion(p.first, p.second + 3)});
    verify_fit_event(small.get().timing_stage_action == wormedit::ui::timing_stage_action_t::none);
    const auto move = f.h.frame(std::array{fit_motion(p.first, p.second + 20)});
    verify_fit_event(move.get().timing_stage_action == wormedit::ui::timing_stage_action_t::move);
    verify_fit_event(move.get().timing_chart_id.value == 1 && move.get().timing_document_revision == 1);
    const auto end = f.h.frame(std::array{fit_button(p.first, p.second + 20, false)});
    verify_fit_event(end.get().timing_stage_action == wormedit::ui::timing_stage_action_t::none);
    verify_fit_event(!worm_ui_nuklear_compose_state().timing_drag.timing_stage_capture.active);
}

TEST_CASE("Beat and offset fit share zero movement and source change cancellation",
          "[event-harness][drag-capture]") {
    const auto offset = GENERATE(false, true);
    const auto interruption = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(offset, interruption);
    nuklear_event_harness_t h{1280, 900};
    auto& v = h.view().compose;
    wormedit::ui::compose_waveform_row_view_t wave{};
    wormedit::ui::compose_grid_line_view_t beat{};
    beat.chart_row = 480; beat.kind = wormedit::editor_grid_line_kind_t::beat; beat.y_pixels = 120;
    v.waveform_rows = {wave};
    v.grid_lines = {beat};
    v.selected_audio_asset_id.value = 6; v.waveform_overview_sample_rate = 44100;
    (void)h.frame();
    wormedit::ui::testing_rect_view_t rect{};
    verify_fit_event(worm_ui_frontend_compose_viewport_for_testing(&rect));
    const auto x = rect.x + static_cast<float>(32 * rect.width / v.viewport_width_pixels);
    const auto y = rect.y + static_cast<float>(120 * rect.height / v.viewport_height_pixels);
    const auto modifiers = offset ? SDL_KMOD_SHIFT : SDL_KMOD_NONE;
    const auto begin = h.frame(std::array{fit_motion(x, y), fit_button(x, y, true)}, modifiers);
    verify_fit_event(find_fit_input(begin.get(), offset ? WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN
                                                       : WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN));
    v.offset_fit_drag_active = offset; v.beat_fit_drag_active = !offset;
    v.waveform_overview_total_frames = 441000;
    v.beat_fit_chart_row = 480;
    v.beat_fit_original_bpm_denominator = v.beat_fit_proposed_bpm_denominator = 1;
    v.beat_fit_original_bpm_numerator = v.beat_fit_proposed_bpm_numerator = 120;
    v.beat_fit_target_y_pixels = 120;
    const auto distance = interruption == 0 ? 0.0f : 20.0f;
    switch (interruption) {
    case 1: ++v.selected_audio_asset_id.value; break;
    case 2: ++v.waveform_overview_sample_rate; break;
    case 3: v.waveform_timing_locked = true; break;
    case 4: v.input_occluded = true; break;
    default: break;
    }
    const auto end = h.frame(std::array{fit_motion(x, y + distance),
                                       fit_button(x, y + distance, false)}, modifiers);
    const auto expected = interruption == 5
        ? (offset ? WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE : WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE)
        : (offset ? WORM_UI_COMPOSE_INPUT_OFFSET_FIT_CANCEL : WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL);
    // Occlusion uses the registry's one unified cancellation instead of a verb-specific cancel.
    verify_fit_event(find_fit_input(end.get(), expected) ||
                     (interruption == 4 && find_fit_input(end.get(), WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE)));
    const auto& s = worm_ui_nuklear_compose_state();
    verify_fit_event(!s.waveform_drag.beat_fit_drag_active && !s.waveform_drag.offset_fit_drag_active && !s.timing_drag.fit_capture.active);
}

TEST_CASE("BGA drag releases under occlusion cannot submit a move",
          "[event-harness][drag-capture]") {
    const auto occluded = GENERATE(false, true);
    timing_drag_fixture_t f;
    auto& v = f.h.view().compose;
    wormedit::ui::compose_bga_event_view_t bga{};
    v.bga_lane_visible = true; v.bga_lane_bounds = {450, 0, 500, 600};
    v.bga_lane_count = 1; v.bga_lane_rects[0] = v.bga_lane_bounds;
    bga.bounds = {450, 195, 500, 205}; bga.event_id.value = 22; bga.selected = true;
    bga.name = "BGA"; bga.bmp_id = "01";
    v.bga_events = {bga}; v.bga_events.resize(1);
    (void)f.h.frame();
    wormedit::ui::testing_rect_view_t rect{};
    verify_fit_event(worm_ui_frontend_compose_viewport_for_testing(&rect));
    const auto x = rect.x + 475.0f / 900 * rect.width;
    const auto y = rect.y + 200.0f / 600 * rect.height;
    (void)f.h.frame(std::array{fit_motion(x, y), fit_button(x, y, true)});
    verify_fit_event(worm_ui_nuklear_compose_state().bga_interaction.bga_event_move_candidate_active);
    (void)f.h.frame(std::array{fit_motion(x, y + 20)});
    verify_fit_event(worm_ui_nuklear_compose_state().bga_interaction.bga_event_move_active);
    v.input_occluded = occluded;
    const auto end = f.h.frame(std::array{fit_button(x, y + 20, false)});
    verify_fit_event((find_fit_input(end.get(), WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE) != nullptr) == !occluded);
    verify_fit_event(!worm_ui_nuklear_compose_state().timing_drag.bga_capture.active);
}

TEST_CASE("Overview beat zero cancels clicks and commits only travelled drags",
          "[event-harness][drag-capture]") {
    const auto distance = GENERATE(0.0f, 3.0f, 10.0f);
    nuklear_event_harness_t h{1280, 900};
    auto& v = h.view().compose;
    wormedit::ui::compose_waveform_overview_row_view_t wave{};
    v.playable_field_left_pixels = 400; v.playable_field_right_pixels = 1000;
    v.bgm_bar_reserved = true; v.bgm_bar_rows = {wave};
    v.bgm_bar_begin_fraction = 0; v.bgm_bar_end_fraction = 1;
    v.selected_audio_asset_id.value = 12; v.waveform_overview_sample_rate = 48000;
    v.waveform_overview_total_frames = 480000;
    auto& s = worm_ui_nuklear_compose_state();
    s.dock_layout.layout.show(wormedit::dock_panel_t::inspector, false);
    s.dock_layout.layout.show(wormedit::dock_panel_t::sounds, false);
    s.dock_layout.layout.show(wormedit::dock_panel_t::layers, false);
    (void)h.frame();
    constexpr auto x = 351.0f, y = 120.0f;
    (void)h.frame(std::array{fit_motion(x, y), fit_button(x, y, true)});
    const auto overview = s.waveform_drag.waveform_overview_capture;
    verify_fit_event(overview.width > 0);
    (void)h.frame(std::array{fit_button(x, y, false)});
    v.waveform_proposed_audio_offset_denominator = 1;
    v.waveform_beat_zero_visible = true;
    v.waveform_beat_zero_fraction = (x - overview.left) / overview.width;
    const auto start = h.frame(std::array{fit_motion(x, y), fit_button(x, y, true)});
    verify_fit_event(start.get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_BEGIN);
    v.waveform_offset_drag_active = true;
    const auto end = h.frame(std::array{fit_motion(x + distance, y), fit_button(x + distance, y, false)});
    verify_fit_event(end.get().audio_offset_drag_action == (distance >= 4
        ? WORM_UI_AUDIO_OFFSET_DRAG_COMMIT : WORM_UI_AUDIO_OFFSET_DRAG_CANCEL));
    verify_fit_event(end.get().audio_offset_drag_chart_id.value == 1 &&
                     end.get().audio_offset_drag_document_revision == 1);
    verify_fit_event(!s.waveform_drag.waveform_beat_zero_drag_active && !s.timing_drag.beat_zero_capture.active);
}

TEST_CASE("Empty timing lanes preserve their distinct creation and selection verbs",
          "[event-harness][drag-capture]") {
    const auto lane = GENERATE(wormedit::timing_lane_kind_t::kiai, wormedit::timing_lane_kind_t::speed,
                               wormedit::timing_lane_kind_t::scroll);
    timing_drag_fixture_t f;
    f.h.view().compose.recipe_areas.front().selected = true;
    const auto p = f.press(lane, 500);
    const auto& s = worm_ui_nuklear_compose_state();
    verify_fit_event(s.timing_drag.kiai_drag_active == (lane == wormedit::timing_lane_kind_t::kiai));
    verify_fit_event(!s.timing_drag.recipe_drag_active);
    if (s.timing_drag.kiai_drag_active) verify_fit_event(s.timing_drag.kiai_drag_edge == WORM_UI_SPAN_HANDLE_CREATE);
    const auto end = f.h.frame(std::array{fit_button(p.first, p.second, false)});
    verify_fit_event(!find_fit_input(end.get(), WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT));
    verify_fit_event(!find_fit_input(end.get(), WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT));
    verify_fit_event(!find_fit_input(end.get(), WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT));
}
