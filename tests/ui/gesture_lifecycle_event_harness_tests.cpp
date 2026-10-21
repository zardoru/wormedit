#include "ui/nuklear/compose_input_queries.h"
#include "ui/nuklear/compose_canvas.h"
#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/compose_panel.h"
#include "ui/compose_timing_geometry.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <tuple>

namespace {
auto lifecycle_key(SDL_Keycode key, SDL_Scancode scan, bool down = true) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = key; event.key.scancode = scan; event.key.down = down;
    return event;
}
auto lifecycle_button(float x, float y, bool down, bool right = false) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
    event.button.down = down; event.button.x = x; event.button.y = y;
    return event;
}
auto lifecycle_motion(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x; event.motion.y = y;
    return event;
}
auto lifecycle_has(const wormedit::ui::workspace_action_t& action, WormUiComposeInputKind kind) -> bool {
    for (std::size_t i = 0; i < action.compose_inputs.size(); ++i)
        if (action.compose_inputs[i].kind == kind) return true;
    return false;
}
void lifecycle_require(bool value) { REQUIRE(value); }
void lifecycle_check(bool value) { CHECK(value); }

enum class lifecycle_family { paint, erase, semantic, pointer_hold, lane_hold, range,
    area, move, kiai, speed, recipe, timing_marker, timing_stage, beat_fit, offset_fit,
    overview, measure, stem, bga, bookmark, bmson, beat_zero, chip, mix, razor, navigation };

struct lifecycle_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    wormedit::ui::compose_note_view_t note{};
    wormedit::ui::compose_waveform_row_view_t wave{};
    wormedit::ui::compose_grid_line_view_t beat{};
    wormedit::ui::compose_speed_span_view_t speed{};
    wormedit::ui::compose_recipe_area_view_t recipe{};
    wormedit::ui::compose_timing_marker_view_t marker{};
    WormUiComposeLaneKeyBinding binding{};
    wormedit::ui::compose_waveform_overview_row_view_t overview{};
    std::array<WormUiComposeMeasureRowView, 2> measures{};
    std::array<std::uint64_t, 2> measure_ids{77, 78};
    std::array<wormedit::ui::compose_grid_line_view_t, 2> measure_lines{};
    wormedit::ui::compose_bgm_lane_view_t lane{};
    wormedit::ui::compose_bgm_event_view_t bgm{};
    wormedit::ui::compose_bga_event_view_t bga{};
    WormUiComposeBookmarkView bookmark{};
    wormedit::ui::testing_rect_view_t rect{};
    float x{}, y{};
    SDL_Keymod modifiers{SDL_KMOD_NONE};
    lifecycle_family family;

    explicit lifecycle_fixture_t(lifecycle_family value) : family(value) {
        lifecycle_require(harness.ready());
        auto& v = harness.view().compose;
        v.playable_field_left_pixels = 400;
        v.playable_field_right_pixels = 1000;
        v.timing_lane_visibility_mask = 255;
        note.note_id.value = 7; note.bounds = {450, 295, 550, 305};
        note.selected = true;
        binding = {SDL_SCANCODE_2, 2};
        v.lane_key_bindings = &binding; v.lane_key_binding_count = 1;
        configure_notes();
        configure_timing();
        configure_other();
        configure_auxiliary();
        (void)harness.frame();
        auto& ui = worm_ui_nuklear_compose_state();
        ui.dock_layout.inspector_visible = ui.dock_layout.sounds_panel_visible = ui.dock_layout.layers_panel_visible = false;
        ui.dock_layout.bga_panel_visible = ui.dock_layout.measures_panel_visible = false;
        (void)harness.frame();
        lifecycle_require(worm_ui_frontend_compose_viewport_for_testing(&rect));
        x = screen_x(500); y = screen_y(300);
    }
    auto screen_x(double value) -> float {
        return rect.x + static_cast<float>(value * rect.width / harness.view().compose.viewport_width_pixels);
    }
    auto screen_y(double value) -> float {
        return rect.y + static_cast<float>(value * rect.height / harness.view().compose.viewport_height_pixels);
    }
    void configure_notes() {
        auto& v = harness.view().compose;
        v.note_paint_mode_active = family == lifecycle_family::paint || family == lifecycle_family::erase ||
                                   family == lifecycle_family::lane_hold;
        if (family == lifecycle_family::semantic || family == lifecycle_family::move) {
            v.note_endpoints = {note};
        }
        v.semantic_layer_paint_mode_active = family == lifecycle_family::semantic;
        if (family == lifecycle_family::pointer_hold) modifiers = SDL_KMOD_ALT;
    }
    void configure_timing() {
        auto& v = harness.view().compose;
        auto& t = harness.view().timing;
        speed.bounds = {0, 300, 0, 500}; speed.object_id.value = 17;
        recipe.bounds = {0, 300, 0, 500}; recipe.recipe_id.value = 18;
        recipe.lane = wormedit::timing_lane_kind_t::scroll; recipe.enabled = true;
        recipe.evaluable = recipe.begin_visible = recipe.end_visible = true;
        recipe.label = "Test recipe";
        marker.object_id.value = 19; marker.lane = wormedit::timing_lane_kind_t::bpm;
        marker.marker_y_pixels = 300; marker.marker_right_fraction = 1;
        if (family == lifecycle_family::speed) { v.speed_spans = {speed}; }
        if (family == lifecycle_family::recipe) { v.recipe_areas = {recipe}; }
        if (family == lifecycle_family::timing_marker) { v.timing_markers = {marker}; }
        if (family == lifecycle_family::timing_stage) {
            v.timing_stage_active = t.stage_active = true;
            v.timing_stage_lane = t.stage_lane = wormedit::timing_lane_kind_t::bpm;
            v.timing_stage_y_pixels = 300; t.stage_existing_object_id = wormedit::object_id_t{19};
        }
        if (family == lifecycle_family::beat_fit || family == lifecycle_family::offset_fit) {
            beat.chart_row = 480; beat.kind = wormedit::editor_grid_line_kind_t::beat; beat.y_pixels = 300;
            v.waveform_rows = {wave};
            v.grid_lines = {beat};
            v.selected_audio_asset_id = 6; v.waveform_overview_sample_rate = 44100;
            if (family == lifecycle_family::offset_fit) modifiers = SDL_KMOD_SHIFT;
        }
    }
    auto begin() -> workspace_action_snapshot_t {
        if (family == lifecycle_family::navigation) {
            const auto tab = lifecycle_key(SDLK_TAB, SDL_SCANCODE_TAB);
            (void)harness.frame(std::span{&tab, 1});
            const auto arrow = lifecycle_key(SDLK_RIGHT, SDL_SCANCODE_RIGHT);
            return harness.frame(std::span{&arrow, 1});
        }
        if (family == lifecycle_family::lane_hold || family == lifecycle_family::range) {
            const auto key = family == lifecycle_family::lane_hold
                ? lifecycle_key(SDLK_2, SDL_SCANCODE_2) : lifecycle_key(SDLK_SPACE, SDL_SCANCODE_SPACE);
            return harness.frame(std::span{&key, 1}, family == lifecycle_family::range
                ? SDL_KMOD_CTRL : SDL_KMOD_NONE);
        }
        choose_pointer();
        choose_other_pointer();
        if (family == lifecycle_family::beat_zero) calibrate_beat_zero();
        if (family == lifecycle_family::razor)
            worm_ui_nuklear_compose_state().bmson_input.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::continuation;
        if (family == lifecycle_family::pointer_hold) {
            const auto alt = lifecycle_key(SDLK_LALT, SDL_SCANCODE_LALT);
            (void)harness.frame(std::span{&alt, 1}, modifiers);
        }
        const std::array press{lifecycle_motion(x, y), lifecycle_button(x, y, true,
            family == lifecycle_family::erase)};
        auto result = harness.frame(press, modifiers);
        if (family == lifecycle_family::pointer_hold)
            harness.view().compose.editor_pointer_hold_active = true;
        if (family == lifecycle_family::area || family == lifecycle_family::move ||
            family == lifecycle_family::erase || family == lifecycle_family::timing_marker ||
            family == lifecycle_family::bga || family == lifecycle_family::bookmark ||
            family == lifecycle_family::bmson) {
            const auto move = lifecycle_motion(x, y + 30);
            result = harness.frame(std::span{&move, 1}, modifiers);
        }
        if (family == lifecycle_family::beat_zero)
            harness.view().compose.waveform_offset_drag_active = true;
        return result;
    }
    void choose_pointer() {
        auto& v = harness.view().compose;
        std::optional<wormedit::timing_lane_kind_t> lane;
        switch (family) {
        case lifecycle_family::kiai: lane = wormedit::timing_lane_kind_t::kiai; break;
        case lifecycle_family::speed: lane = wormedit::timing_lane_kind_t::speed; break;
        case lifecycle_family::recipe: lane = wormedit::timing_lane_kind_t::scroll; break;
        case lifecycle_family::timing_marker:
        case lifecycle_family::timing_stage: lane = wormedit::timing_lane_kind_t::bpm; break;
        case lifecycle_family::beat_fit:
        case lifecycle_family::offset_fit: x = screen_x(32); return;
        default: return;
        }
        const auto extent = wormedit::ui::compose_timing_lane_extent(v, *lane);
        lifecycle_require(extent.has_value());
        x = screen_x((extent->first + extent->second) / 2);
    }
    void configure_other() {
        auto& v = harness.view().compose;
        lane.bounds = {1010, 0, 1110, 900}; lane.source_lane_id = wormedit::object_id_t{77};
        lane.stem = true; lane.width_pixels = 100; lane.resize_edge_pixels = 1110;
        bgm.bounds = {1010, 295, 1110, 305}; bgm.event_id = wormedit::object_id_t{21};
        bgm.source_lane_id = wormedit::object_id_t{77}; bgm.selected = true;
        if (family == lifecycle_family::overview || family == lifecycle_family::beat_zero) {
            v.bgm_bar_reserved = true; v.bgm_bar_rows = {overview};
            v.bgm_bar_begin_fraction = 0; v.bgm_bar_end_fraction = 1;
            v.selected_audio_asset_id = 12; v.waveform_overview_sample_rate = 48000;
            v.waveform_overview_total_frames = 480000;
        }
        if (family == lifecycle_family::measure) {
            measures[0].measure_id = 77; measures[1].measure_id = 78;
            for (auto& m : measures) { m.length_numerator = m.length_denominator = 1; }
            measure_lines[0].kind = measure_lines[1].kind = wormedit::editor_grid_line_kind_t::measure;
            measure_lines[0].measure_number = 1; measure_lines[1].measure_number = 2;
            measure_lines[0].y_pixels = 100; measure_lines[1].y_pixels = 500;
            v.measure_rows = measures.data(); v.measure_row_ids = measure_ids.data();
            v.measure_row_count = 2; v.grid_lines.assign(measure_lines.data(), measure_lines.data() + 2);
            v.pixels_per_standard_measure = 400; v.area_snap_denominator = 8; v.edit_snap_enabled = true;
            modifiers = SDL_KMOD_ALT;
        }
        if (family == lifecycle_family::stem || family == lifecycle_family::bmson ||
            family == lifecycle_family::mix || family == lifecycle_family::razor) {
            v.bmson_tools_available = true; v.bgm_lanes = {lane};
            if (family == lifecycle_family::bmson) { v.bgm_events = {bgm};  }
        }
        configure_markers();
    }
    void configure_auxiliary() {
        auto& v = harness.view().compose;
        if (family == lifecycle_family::chip) {
            v.note_paint_mode_active = v.keysounds_available = true;
            v.draw_source_kind = 1; v.draw_source_id = 77; v.draw_source_label = "Lifecycle source";
        }
        if (family == lifecycle_family::mix) v.bgm_mix_available = true;
        if (family == lifecycle_family::razor) {
            v.selected_object_count = 1;
            v.selected_bmson_bgm_event_id = 21;
            v.selected_bmson_source_lane_id = 77;
        }
    }
    void calibrate_beat_zero() {
        const std::array press{lifecycle_motion(x, y), lifecycle_button(x, y, true)};
        (void)harness.frame(press);
        const auto capture = worm_ui_nuklear_compose_state().waveform_drag.waveform_overview_capture;
        lifecycle_require(capture.width > 0);
        const auto release = lifecycle_button(x, y, false);
        (void)harness.frame(std::span{&release, 1});
        auto& v = harness.view().compose;
        v.waveform_proposed_audio_offset_denominator = 1;
        v.waveform_beat_zero_visible = true;
        v.waveform_beat_zero_fraction = (x - capture.left) / capture.width;
    }
    void configure_markers() {
        auto& v = harness.view().compose;
        if (family == lifecycle_family::bga) {
            v.bga_lane_visible = true; v.bga_lane_bounds = {350, 0, 400, 900};
            v.bga_lane_count = 1; v.bga_lane_rects[0] = v.bga_lane_bounds;
            bga.bounds = {350, 295, 400, 305}; bga.event_id.value = 22; bga.selected = true;
            bga.name = "BGA"; bga.bmp_id = "01";
            v.bga_events = {bga}; v.bga_events.resize(1);
        }
        if (family == lifecycle_family::bookmark) {
            bookmark.bookmark_id = 23; bookmark.label = "Bookmark"; bookmark.measure_number = 1;
            bookmark.projection_visible = true; bookmark.projected_y_pixels = 300;
            v.beat_bookmarks = &bookmark; v.beat_bookmark_count = 1;
        }
    }
    void choose_other_pointer() {
        switch (family) {
        case lifecycle_family::overview:
        case lifecycle_family::beat_zero: x = 351; y = 120; break;
        case lifecycle_family::measure: x = screen_x(500); y = screen_y(500); break;
        case lifecycle_family::stem: x = screen_x(1110); break;
        case lifecycle_family::bmson:
        case lifecycle_family::razor: x = screen_x(1050); break;
        case lifecycle_family::chip: {
            const auto label = harness.drawn_text_center("Lifecycle source");
            lifecycle_require(label.has_value());
            x = label->first; y = label->second; break;
        }
        case lifecycle_family::mix: {
            const auto button = compose_bgm_mix_button_rect(lane, harness.view().compose,
                nk_rect(rect.x, rect.y, rect.width, rect.height), false);
            x = button.x + button.w / 2; y = button.y + button.h / 2; break;
        }
        case lifecycle_family::bga: x = screen_x(375); break;
        default: break;
        }
    }
    auto other_active() const -> bool {
        const auto& s = worm_ui_nuklear_compose_state();
        switch (family) {
        case lifecycle_family::overview: return s.waveform_drag.waveform_overview_drag_active;
        case lifecycle_family::measure: return s.measure_resize.measure_resize_active;
        case lifecycle_family::stem: return s.stem_lanes.stem_lane_resize.has_value();
        case lifecycle_family::bga: return s.bga_interaction.bga_event_move_active;
        case lifecycle_family::bookmark: return s.bookmark_ui.bookmark_move_active;
        case lifecycle_family::bmson: return s.bmson_input.bmson_marker_move_active;
        case lifecycle_family::beat_zero: return s.waveform_drag.waveform_beat_zero_drag_active;
        case lifecycle_family::chip: return s.pointer_capture.tool_chip_pointer_active;
        case lifecycle_family::mix: return s.stem_lanes.bgm_mix_pointer_active;
        case lifecycle_family::razor: return s.bmson_input.bmson_razor_pointer_active;
        case lifecycle_family::navigation: return s.keyboard_navigation.tab_navigation.direction.has_value();
        default: return false;
        }
    }
    auto active() const -> bool {
        if (family >= lifecycle_family::overview) return other_active();
        const auto& s = worm_ui_nuklear_compose_state();
        switch (family) {
        case lifecycle_family::paint: return s.pointer_capture.note_paint_pointer_active;
        case lifecycle_family::erase: return s.pointer_capture.draw_erase_pointer_active;
        case lifecycle_family::semantic: return s.pointer_capture.semantic_layer_paint_pointer_active;
        case lifecycle_family::pointer_hold: return s.pointer_capture.pointer_hold_begin_pending || s.pointer_capture.pointer_hold_active;
        case lifecycle_family::lane_hold: return s.range_selection.lane_hold_active;
        case lifecycle_family::range: return s.range_selection.space_range_hold_active;
        case lifecycle_family::area: return s.area_selection.area_selection_active;
        case lifecycle_family::move: return s.note_move.note_move_active;
        case lifecycle_family::kiai:
        case lifecycle_family::speed: return s.timing_drag.kiai_drag_active;
        case lifecycle_family::recipe: return s.timing_drag.recipe_drag_active;
        case lifecycle_family::timing_marker: return s.timing_marker.timing_marker_drag_active;
        case lifecycle_family::timing_stage: return s.timing_drag.timing_stage_drag_active;
        case lifecycle_family::beat_fit: return s.waveform_drag.beat_fit_drag_active;
        case lifecycle_family::offset_fit: return s.waveform_drag.offset_fit_drag_active;
        default: return false;
        }
        return false;
    }
};

auto interrupt_lifecycle(lifecycle_fixture_t& f, int transition) -> workspace_action_snapshot_t {
    auto& h = f.harness;
    if (transition == 0) {
        const auto escape = lifecycle_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        return h.frame(std::span{&escape, 1}, f.modifiers);
    }
    if (transition == 1) {
        SDL_Event lost{}; lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        return h.frame(std::span{&lost, 1}, f.modifiers);
    }
    if (transition == 2) { ++h.view().compose.document_revision; return h.frame({}, f.modifiers); }
    if (transition == 4) {
        h.view().active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        h.view().compose.visible = false;
        return h.frame();
    }
    if (transition == 5) {
        SDL_Event text{};
        text.type = SDL_EVENT_TEXT_EDITING; text.edit.text = "a";
        return h.frame(std::span{&text, 1});
    }
    if (transition == 6) {
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y = 1;
        wheel.wheel.mouse_x = 500; wheel.wheel.mouse_y = 400;
        const std::vector<SDL_Event> overflow(worm_ui_nuklear_compose_state().input_queue.wheel_events.size() + 1, wheel);
        return h.frame(overflow);
    }
    if (transition == 7) {
        const auto key = lifecycle_key(SDLK_F24, SDL_SCANCODE_F24, false);
        const std::vector<SDL_Event> overflow(worm_ui_nuklear_compose_state().input_queue.key_events.size() + 1, key);
        return h.frame(overflow, f.modifiers);
    }
    const auto go = lifecycle_key(SDLK_G, SDL_SCANCODE_G);
    return h.frame(std::span{&go, 1}, SDL_KMOD_CTRL);
}
}

TEST_CASE("Registered Compose gestures cancel through production interruption emitters",
          "[frontend-state][event-harness][gesture-registry]") {
    const auto family = GENERATE(lifecycle_family::paint, lifecycle_family::erase,
        lifecycle_family::semantic, lifecycle_family::pointer_hold, lifecycle_family::lane_hold,
        lifecycle_family::range, lifecycle_family::area, lifecycle_family::move,
        lifecycle_family::kiai, lifecycle_family::speed, lifecycle_family::recipe,
        lifecycle_family::timing_marker, lifecycle_family::timing_stage,
        lifecycle_family::beat_fit, lifecycle_family::offset_fit, lifecycle_family::overview,
        lifecycle_family::measure, lifecycle_family::stem, lifecycle_family::bga,
        lifecycle_family::bookmark, lifecycle_family::bmson, lifecycle_family::beat_zero,
        lifecycle_family::chip, lifecycle_family::mix, lifecycle_family::razor, lifecycle_family::navigation);
    const auto transition = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    INFO("family=" << static_cast<int>(family) << " transition=" << transition);
    lifecycle_fixture_t f{family};
    (void)f.begin();
    lifecycle_require(f.active());
    const auto interrupted = interrupt_lifecycle(f, transition);
    lifecycle_check(!f.active());
    if (family == lifecycle_family::overview) {
        lifecycle_check(!worm_ui_nuklear_compose_state().waveform_drag.waveform_overview_gesture.active);
        lifecycle_check(worm_ui_nuklear_compose_state().waveform_drag.waveform_overview_capture.chart_id == 0);
    }
    if (family == lifecycle_family::beat_zero)
        lifecycle_check(interrupted.get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL);
    else if (family == lifecycle_family::navigation)
        lifecycle_check(lifecycle_has(interrupted.get(), WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL));
    else if (family != lifecycle_family::chip)
        lifecycle_check(lifecycle_has(interrupted.get(), WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE));
    const std::array release{lifecycle_button(f.x, f.y + 50, false),
        lifecycle_button(f.x, f.y + 50, false, true),
        lifecycle_key(SDLK_2, SDL_SCANCODE_2, false),
        lifecycle_key(SDLK_SPACE, SDL_SCANCODE_SPACE, false)};
    const auto late = f.harness.frame(release, f.modifiers);
    lifecycle_check(late.get().audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_COMMIT);
    for (const auto kind : {WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE,
         WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE, WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
         WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE,
         WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT,
         WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, WORM_UI_COMPOSE_INPUT_TIMING_GROUP_MOVE,
         WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE,
         WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE, WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE,
         WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE, WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_CONTINUE,
         WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK}) {
        CAPTURE(kind);
        lifecycle_check(!lifecycle_has(late.get(), kind));
    }
}

TEST_CASE("Object selection waits for release and Escape consumes a pending selection press",
          "[frontend-state][event-harness][selection-history]") {
    lifecycle_fixture_t f{lifecycle_family::move};
    f.harness.view().compose.note_endpoints.front().selected = false;
    (void)f.harness.frame();
    const std::array press{lifecycle_motion(f.x, f.y), lifecycle_button(f.x, f.y, true)};
    const auto pressed = f.harness.frame(press);
    CHECK_FALSE(lifecycle_has(pressed.get(), WORM_UI_COMPOSE_INPUT_POINTER_CLICK));
    CHECK(worm_ui_nuklear_compose_state().note_move.note_move_candidate_active);
    const auto escape = lifecycle_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    const auto cancelled = f.harness.frame(std::span{&escape, 1});
    CHECK(lifecycle_has(cancelled.get(), WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE));
    CHECK(cancelled.get().shortcut_events.size() == 0);
    const auto release = lifecycle_button(f.x, f.y, false);
    const auto late = f.harness.frame(std::span{&release, 1});
    CHECK_FALSE(lifecycle_has(late.get(), WORM_UI_COMPOSE_INPUT_POINTER_CLICK));
    (void)f.harness.frame(press);
    const auto clicked = f.harness.frame(std::span{&release, 1});
    CHECK(lifecycle_has(clicked.get(), WORM_UI_COMPOSE_INPUT_POINTER_CLICK));
}

TEST_CASE("Pressing outside a selected area keeps it until a replacement gesture finishes",
          "[frontend-state][event-harness][selection-history]") {
    lifecycle_fixture_t f{lifecycle_family::area};
    auto& view = f.harness.view().compose;
    view.area_range_available = true;
    view.area_rows_per_measure = 960;
    view.area_canvas_rows = 9600;
    view.area_range_begin_row = 480;
    view.area_range_end_row = 960;
    (void)f.harness.frame();
    const std::array press{lifecycle_motion(f.x, f.y), lifecycle_button(f.x, f.y, true)};
    const auto pressed = f.harness.frame(press);
    CHECK(worm_ui_nuklear_compose_state().area_selection.area_selection_candidate_active);
    CHECK_FALSE(pressed.get().compose_area_clear_requested);
    CHECK(pressed.get().compose_action != WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR);
    const auto escape = lifecycle_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    const auto cancelled = f.harness.frame(std::span{&escape, 1});
    CHECK(cancelled.get().compose_action != WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR);
    CHECK(cancelled.get().shortcut_events.size() == 0);
}

TEST_CASE("Keyboard queue overflow fences held paint until release and permits a fresh stroke",
          "[frontend-state][event-harness][keyboard-overflow]") {
    lifecycle_fixture_t f{lifecycle_family::paint};
    (void)f.begin();
    lifecycle_require(f.active());
    const auto interrupted = interrupt_lifecycle(f, 7);
    lifecycle_check(lifecycle_has(interrupted.get(), WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE));
    lifecycle_check(!lifecycle_has(interrupted.get(), WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN));
    lifecycle_check(!f.active());
    const auto held = f.harness.frame();
    lifecycle_check(!lifecycle_has(held.get(), WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN));
    lifecycle_check(!f.active());
    const auto release = lifecycle_button(f.x, f.y, false);
    const auto released = f.harness.frame(std::span{&release, 1});
    INFO("late physical release must not publish a paint release");
    lifecycle_check(!lifecycle_has(released.get(), WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE));
    const auto fresh = f.begin();
    lifecycle_check(lifecycle_has(fresh.get(), WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN));
    lifecycle_check(f.active());
}

TEST_CASE("Keyboard queue overflow drains frame input and defers stamped audio cancellation",
          "[frontend-state][event-harness][keyboard-overflow]") {
    lifecycle_fixture_t f{lifecycle_family::beat_zero};
    (void)f.begin();
    lifecycle_require(f.active());
    auto& state = worm_ui_nuklear_compose_state();
    const auto capture = state.timing_drag.beat_zero_capture;
    worm_ui_nuklear_queue_compose_wheel(0, 1, f.x, f.y, 0, true);
    worm_ui_nuklear_queue_compose_middle_click(f.x, f.y);
    for (std::size_t index = 0; index <= state.input_queue.key_events.size(); ++index)
        worm_ui_nuklear_queue_compose_key(SDLK_F24, SDL_SCANCODE_F24, 0, false, false);
    lifecycle_check(state.input_queue.cancel_pending);
    lifecycle_check(state.input_queue.key_event_count == 0);
    lifecycle_check(!state.input_queue.keyboard_focus);
    const auto cancelled = f.harness.frame();
    lifecycle_check(cancelled.get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL);
    lifecycle_check(cancelled.get().audio_offset_drag_chart_id.value == capture.chart);
    lifecycle_check(cancelled.get().audio_offset_drag_document_revision == capture.revision);
    lifecycle_check(cancelled.get().audio_offset_drag_sample_rate == capture.sample_rate);
    lifecycle_check(cancelled.get().compose_viewport_inputs.size() == 0);
    lifecycle_check(!lifecycle_has(cancelled.get(), WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS));
    lifecycle_check(!state.input_queue.waveform_offset_cancel_pending);
    // The next view acknowledges cancellation, as the application dispatcher does.
    f.harness.view().compose.waveform_offset_drag_active = false;
    const auto following = f.harness.frame();
    lifecycle_check(following.get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
    lifecycle_check(following.get().compose_viewport_inputs.size() == 0);
}

TEST_CASE("Go-to-measure drains queued production wheel middle-click and pinch input",
          "[frontend-state][event-harness][gesture-registry][modal-input]") {
    nuklear_event_harness_t h{1280, 900};
    (void)h.frame();
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y = 1;
    wheel.wheel.mouse_x = 500; wheel.wheel.mouse_y = 400;
    SDL_Event middle = lifecycle_button(500, 400, true);
    middle.button.button = SDL_BUTTON_MIDDLE;
    const auto go = lifecycle_key(SDLK_G, SDL_SCANCODE_G);
    std::vector<SDL_Event> events{lifecycle_motion(500, 400), wheel, middle};
#if SDL_VERSION_ATLEAST(3, 4, 0)
    SDL_Event pinch{};
    pinch.type = SDL_EVENT_PINCH_BEGIN; pinch.pinch.scale = 1;
    events.push_back(pinch);
    pinch.type = SDL_EVENT_PINCH_UPDATE; pinch.pinch.scale = 1.25;
    events.push_back(pinch);
#endif
    events.push_back(go);
    const auto opened = h.frame(events, SDL_KMOD_CTRL);
    lifecycle_require(worm_ui_nuklear_compose_state().dialogs.go_to_measure_open);
    lifecycle_check(opened.get().compose_viewport_inputs.size() == 0);
    lifecycle_check(!opened.get().compose_viewport_middle_click);
    const auto escape = lifecycle_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    (void)h.frame(std::span{&escape, 1});
    const auto quiet = h.frame();
    lifecycle_check(quiet.get().compose_viewport_inputs.size() == 0);
    lifecycle_check(!quiet.get().compose_viewport_middle_click);
    lifecycle_check(!worm_ui_nuklear_compose_state().pinch.pinch.active);
}

TEST_CASE("Bookmark drag capture cannot continue in another project with equal chart and revision",
          "[frontend-state][event-harness][gesture-registry][markers]") {
    const auto switches_project = GENERATE(false, true);
    const auto releases_on_switch = GENERATE(false, true);
    lifecycle_fixture_t f{lifecycle_family::bookmark};
    (void)f.begin();
    lifecycle_require(f.active());
    lifecycle_require(worm_ui_nuklear_compose_state().bookmark_ui.bookmark_move_candidate_active);
    auto& view = f.harness.view().compose;
    const auto captured_chart = view.chart_id;
    const auto captured_revision = view.document_revision;
    const auto captured_project = view.project_id;
    if (switches_project) ++view.project_id;
    const auto event = releases_on_switch ? lifecycle_button(f.x, f.y + 50, false)
                                         : lifecycle_motion(f.x, f.y + 50);
    auto released = f.harness.frame(std::span{&event, 1});
    lifecycle_check(view.chart_id == captured_chart);
    lifecycle_check(view.document_revision == captured_revision);
    lifecycle_check(view.project_id == captured_project + (switches_project ? 1 : 0));
    if (switches_project) {
        lifecycle_check(!f.active());
        lifecycle_check(!worm_ui_nuklear_compose_state().bookmark_ui.bookmark_move_candidate_active);
        lifecycle_check(lifecycle_has(released.get(), WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE));
        lifecycle_check(!lifecycle_has(released.get(), WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE));
    }
    if (!releases_on_switch) {
        const auto release = lifecycle_button(f.x, f.y + 50, false);
        released = f.harness.frame(std::span{&release, 1});
    }
    lifecycle_check(lifecycle_has(released.get(), WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE) == !switches_project);
    lifecycle_check(!f.active());
    lifecycle_check(!worm_ui_nuklear_compose_state().bookmark_ui.bookmark_move_candidate_active);
}

TEST_CASE("Marker release movement counts while bookmark release movement does not",
          "[frontend-state][event-harness][capture-continuation]") {
    const auto family = GENERATE(lifecycle_family::timing_marker, lifecycle_family::bookmark);
    lifecycle_fixture_t f{family};
    f.choose_pointer();
    const std::array press{lifecycle_motion(f.x, f.y), lifecycle_button(f.x, f.y, true)};
    (void)f.harness.frame(press);
    const std::array release{lifecycle_motion(f.x, f.y + 30), lifecycle_button(f.x, f.y + 30, false)};
    const auto result = f.harness.frame(release);
    lifecycle_check(lifecycle_has(result.get(), WORM_UI_COMPOSE_INPUT_TIMING_GROUP_MOVE) ==
                    (family == lifecycle_family::timing_marker));
    lifecycle_check(!lifecycle_has(result.get(), WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE));
    lifecycle_check(!f.active());
}

TEST_CASE("Fit release near its anchor cancels even after a held excursion",
          "[frontend-state][event-harness][capture-continuation]") {
    const auto offset = GENERATE(false, true);
    lifecycle_fixture_t f{offset ? lifecycle_family::offset_fit : lifecycle_family::beat_fit};
    (void)f.begin();
    auto& view = f.harness.view().compose;
    view.beat_fit_drag_active = !offset;
    view.offset_fit_drag_active = offset;
    view.beat_fit_chart_row = 480;
    view.beat_fit_original_bpm_denominator = view.beat_fit_proposed_bpm_denominator = 1;
    view.waveform_overview_total_frames = 441000;
    const auto motion = lifecycle_motion(f.x, f.y + 30);
    (void)f.harness.frame(std::span{&motion, 1}, f.modifiers);
    const std::array release{lifecycle_motion(f.x, f.y), lifecycle_button(f.x, f.y, false)};
    const auto result = f.harness.frame(release, f.modifiers);
    lifecycle_check(lifecycle_has(result.get(), offset ? WORM_UI_COMPOSE_INPUT_OFFSET_FIT_CANCEL
                                                     : WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL));
    lifecycle_check(!lifecycle_has(result.get(), offset ? WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE
                                                      : WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE));
    lifecycle_check(!f.active());
}

TEST_CASE("Timing stage release clears its pointer capture and retains the staged edit",
          "[frontend-state][event-harness][capture-continuation]") {
    lifecycle_fixture_t f{lifecycle_family::timing_stage};
    (void)f.begin();
    const auto motion = lifecycle_motion(f.x, f.y + 30);
    const auto moved = f.harness.frame(std::span{&motion, 1});
    lifecycle_check(moved.get().timing_stage_action == wormedit::ui::timing_stage_action_t::move);
    const auto release = lifecycle_button(f.x, f.y + 40, false);
    const auto result = f.harness.frame(std::span{&release, 1});
    lifecycle_check(result.get().timing_stage_action == wormedit::ui::timing_stage_action_t::none);
    lifecycle_check(!f.active());
    lifecycle_check(f.harness.view().timing.stage_active);
}

TEST_CASE("Beat-zero continuation rejects changed audio source geometry",
          "[frontend-state][event-harness][capture-continuation]") {
    const auto changed = GENERATE(0, 1, 2);
    lifecycle_fixture_t f{lifecycle_family::beat_zero};
    (void)f.begin();
    auto& view = f.harness.view().compose;
    if (changed == 0) ++view.selected_audio_asset_id;
    if (changed == 1) ++view.waveform_overview_total_frames;
    if (changed == 2) ++view.waveform_overview_sample_rate;
    const auto release = lifecycle_button(f.x + 30, f.y, false);
    const auto result = f.harness.frame(std::span{&release, 1});
    lifecycle_check(result.get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL);
    lifecycle_check(!f.active());
}

TEST_CASE("Capture continuation owns threshold release and invalid-source decisions",
          "[frontend-state][event-harness][capture-continuation]") {
    using policy = compose_drag_release_policy_t;
    using phase = compose_drag_phase_t;
    struct release_case_t {
        policy release;
        compose_drag_pointer_t pointer;
        bool was_moved;
        compose_drag_continuation_t expected;
    };
    const std::array cases{
        release_case_t{policy::latched_vertical, {.released = true}, true, {phase::commit, true}},
        release_case_t{policy::latched_vertical, {.released = true, .y = 4}, false, {phase::commit, true}},
        release_case_t{policy::latched_planar, {.released = true, .x = 4}, false, {phase::commit, true}},
        release_case_t{policy::held_vertical, {.released = true, .y = 4}, false, {phase::click, false}},
        release_case_t{policy::vertical_at_release, {.released = true}, true, {phase::cancel, false}},
        release_case_t{policy::horizontal_at_release, {.released = true, .y = 4}, true, {phase::cancel, false}},
        release_case_t{policy::clear_on_release, {.released = true, .y = 4}, false, {phase::click, true}},
        release_case_t{policy::commit_on_release, {.released = true}, false, {phase::commit, true}}};
    const compose_drag_capture_t capture;
    for (const auto& test : cases) {
        CAPTURE(test.release);
        lifecycle_check(capture.advance(test.pointer, true, test.release, test.was_moved) == test.expected);
        lifecycle_check(capture.advance(test.pointer, false, test.release, test.was_moved).phase == phase::cancel);
        lifecycle_check(capture.advance({}, true, test.release, test.was_moved).phase == phase::cancel);
    }
    lifecycle_check(capture.advance({.down = true, .y = 4}, true, policy::held_vertical) ==
                    compose_drag_continuation_t{phase::update, true});
}

TEST_CASE("A bookmark click replays painting without emitting a bookmark move",
          "[frontend-state][event-harness][capture-continuation]") {
    lifecycle_fixture_t f{lifecycle_family::bookmark};
    f.harness.view().compose.note_paint_mode_active = true;
    const std::array press{lifecycle_motion(f.x, f.y), lifecycle_button(f.x, f.y, true)};
    (void)f.harness.frame(press);
    const auto release = lifecycle_button(f.x, f.y, false);
    const auto result = f.harness.frame(std::span{&release, 1});
    lifecycle_check(lifecycle_has(result.get(), WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN));
    lifecycle_check(lifecycle_has(result.get(), WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE));
    lifecycle_check(!lifecycle_has(result.get(), WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE));
}

TEST_CASE("Compose input claims preserve consumer differences and read live state",
          "[frontend-state][event-harness][compose-input-queries]") {
    nuklear_event_harness_t harness{1280, 900};
    (void)harness.frame();
    auto& compose = worm_ui_nuklear_compose_state();
    const compose_input_queries_t claims{compose};
    lifecycle_require(!claims.is_canvas_input_blocked());
    lifecycle_require(!claims.is_canvas_hover_blocked());
    lifecycle_require(!claims.is_paint_blocked());
    lifecycle_require(!claims.is_editor_modal_blocked());

    SECTION("BMSON candidates reserve paint admission before they capture hover") {
        compose.bmson_input.bmson_marker_move_candidate_active = true;
        lifecycle_check(claims.is_paint_blocked());
        lifecycle_check(!claims.is_canvas_hover_blocked());
        compose.bmson_input.bmson_marker_move_candidate_active = false;
        compose.bmson_input.bmson_marker_move_active = true;
        lifecycle_check(claims.is_paint_blocked());
        lifecycle_check(claims.is_canvas_hover_blocked());
    }
    SECTION("Hold candidates suppress hover while their continuation remains admitted") {
        compose.pointer_capture.pointer_hold_begin_pending = true;
        lifecycle_check(claims.is_canvas_hover_blocked());
        lifecycle_check(!claims.is_paint_blocked());
        compose.pointer_capture.pointer_hold_begin_pending = false;
        lifecycle_check(!claims.is_canvas_hover_blocked());
    }
    SECTION("Opening an editor between hover and paint is observed without a snapshot") {
        compose.inline_timing.inline_timing_open = true;
        lifecycle_check(claims.is_paint_blocked());
        lifecycle_check(claims.is_editor_modal_blocked());
        lifecycle_check(claims.is_context_invocation_blocked());
        compose.inline_timing.inline_timing_open = false;
        lifecycle_check(!claims.is_paint_blocked());
    }
    SECTION("Bookmark release blocks canvas and modal editors but keeps the context policy") {
        compose.bookmark_ui.bookmark_rename.release_pending = true;
        lifecycle_check(claims.is_canvas_input_blocked());
        lifecycle_check(claims.is_canvas_hover_blocked());
        lifecycle_check(claims.is_paint_blocked());
        lifecycle_check(claims.is_editor_modal_blocked());
        lifecycle_check(!claims.is_context_invocation_blocked());
        lifecycle_check(!claims.has_popup_input());
    }
    SECTION("Filter release is a frontend popup gate without occluding the canvas locally") {
        compose.keysound_filters.keysound_filter_release_pending = true;
        lifecycle_check(claims.has_popup_input());
        lifecycle_check(claims.is_editor_modal_blocked());
        lifecycle_check(claims.is_context_invocation_blocked());
        lifecycle_check(!claims.is_canvas_input_blocked());
    }
}

TEST_CASE("Production Compose captures preserve hover and paint participation",
          "[frontend-state][event-harness][compose-input-queries]") {
    const auto [family, hover, paint] = GENERATE(
        std::tuple{lifecycle_family::paint, true, true},
        std::tuple{lifecycle_family::erase, false, false},
        std::tuple{lifecycle_family::semantic, true, true},
        std::tuple{lifecycle_family::pointer_hold, true, false},
        std::tuple{lifecycle_family::lane_hold, true, false},
        std::tuple{lifecycle_family::range, false, false},
        std::tuple{lifecycle_family::area, true, false},
        std::tuple{lifecycle_family::move, true, false},
        std::tuple{lifecycle_family::kiai, true, true},
        std::tuple{lifecycle_family::speed, true, true},
        std::tuple{lifecycle_family::recipe, true, true},
        std::tuple{lifecycle_family::timing_marker, true, false},
        std::tuple{lifecycle_family::timing_stage, true, true},
        std::tuple{lifecycle_family::beat_fit, false, false},
        std::tuple{lifecycle_family::offset_fit, false, false},
        std::tuple{lifecycle_family::overview, true, false},
        std::tuple{lifecycle_family::measure, false, false},
        std::tuple{lifecycle_family::stem, false, false},
        std::tuple{lifecycle_family::bga, false, false},
        std::tuple{lifecycle_family::bookmark, false, false},
        std::tuple{lifecycle_family::bmson, true, true},
        std::tuple{lifecycle_family::beat_zero, true, false},
        std::tuple{lifecycle_family::chip, false, false},
        std::tuple{lifecycle_family::mix, false, false},
        std::tuple{lifecycle_family::razor, true, true},
        std::tuple{lifecycle_family::navigation, false, false});
    CAPTURE(family);
    lifecycle_fixture_t fixture{family};
    const compose_input_queries_t claims{worm_ui_nuklear_compose_state()};
    (void)fixture.begin();
    REQUIRE(fixture.active());
    CHECK(claims.is_canvas_hover_blocked() == hover);
    CHECK(claims.is_paint_blocked() == paint);
}
