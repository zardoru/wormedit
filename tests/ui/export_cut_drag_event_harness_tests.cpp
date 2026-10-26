#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_panel.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/compose/compose_export_cuts.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <source_location>

namespace {
using namespace wormedit;

void check_cut_grip(bool condition, std::source_location where = std::source_location::current()) {
    INFO("Cut grip assertion at " << where.line());
    REQUIRE(condition);
}

auto cut_drag_input(const ui::workspace_action_t& action, WormUiComposeInputKind kind)
    -> const ui::compose_input_action_t* {
    const auto found = std::ranges::find(action.compose_inputs, kind, &ui::compose_input_action_t::kind);
    return found == action.compose_inputs.end() ? nullptr : &*found;
}

struct cut_drag_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    ui::testing_rect_view_t viewport{};
    double source_anchor_y{300};

    explicit cut_drag_fixture_t(bool timing_focus = true) {
        REQUIRE(harness.ready());
        auto& view = harness.view().compose;
        view.project_id = song_id_t{3};
        view.chart_id = chart_id_t{7};
        view.document_revision = 12;
        view.timing_focus = timing_focus;
        view.playable_field_left_pixels = 400;
        view.playable_field_right_pixels = 1000;
        view.timing_lane_visibility_mask = 255;
        view.export_cuts = {{.id = object_id_t{31}, .label = "Cut",
            .span = {.available = true, .has_length = true, .start_milliseconds = 500, .end_milliseconds = 1500,
                .begin_projection_visible = true, .end_projection_visible = true,
                .span_projection_visible = true, .projected_begin_y_pixels = 300, .projected_end_y_pixels = 500,
                .projected_span_top_y_pixels = 300, .projected_span_bottom_y_pixels = 500}}};
        auto& layout = worm_ui_nuklear_compose_state().dock_layout.layout;
        for (const auto panel : layout.order()) layout.show(panel, false);
        (void)harness.frame();
        REQUIRE(worm_ui_frontend_compose_viewport_for_testing(&viewport));
    }

    auto x() -> float {
        return viewport.x + static_cast<float>(500 * viewport.width / harness.view().compose.viewport_width_pixels);
    }
    auto y(double source_y) -> float {
        return viewport.y + static_cast<float>(source_y * viewport.height / harness.view().compose.viewport_height_pixels);
    }
    void press(std::uint8_t edge) {
        source_anchor_y = edge == WORM_UI_SPAN_HANDLE_END ? 500 : 300;
        const auto pointer_x = x();
        const auto pointer_y = y(source_anchor_y);
        (void)harness.frame(std::array{test::mouse_motion(pointer_x, pointer_y),
            test::mouse_button(true, pointer_x, pointer_y)});
    }
    void begin(std::uint8_t edge) {
        press(edge);
        REQUIRE(worm_ui_nuklear_compose_state().audio_span_drag.candidate_active);
        const auto motion = test::mouse_motion(x(), y(source_anchor_y + 30));
        (void)harness.frame(std::span{&motion, 1});
        REQUIRE(worm_ui_nuklear_compose_state().audio_span_drag.active);
    }
    auto release() -> workspace_action_snapshot_t {
        const auto up = test::mouse_button(false, x(), y(source_anchor_y + 30));
        return harness.frame(std::span{&up, 1});
    }
};

void check_cut_drag_stamp(const ui::workspace_action_t& action, const ui::compose_input_action_t& input,
    std::uint8_t edge, double anchor) {
    CHECK(input.object_id == object_id_t{31});
    CHECK(input.timing_chart_id == chart_id_t{7});
    CHECK(input.timing_document_revision == 12);
    CHECK(action.compose_input_project_id == song_id_t{3});
    CHECK(input.span_edge == edge);
    CHECK(std::abs(input.span_anchor_y_pixels - anchor) < 0.5);
}

auto interrupt_cut_drag(cut_drag_fixture_t& fixture, int transition) -> workspace_action_snapshot_t {
    auto& view = fixture.harness.view().compose;
    SDL_Event event{};
    switch (transition) {
    case 0:
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_ESCAPE;
        event.key.scancode = SDL_SCANCODE_ESCAPE;
        event.key.down = true;
        return fixture.harness.frame(std::span{&event, 1});
    case 1:
        event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        return fixture.harness.frame(std::span{&event, 1});
    case 2: ++view.document_revision; break;
    case 3: view.timing_focus = false; break;
    case 4: ++view.project_id.value; break;
    case 5: ++view.chart_id.value; break;
    }
    return fixture.harness.frame();
}
} // namespace

TEST_CASE("Timing-focus export-cut edge drags retain cut identity and captured stamps", "[event-harness][export-cut][cut-drag]") {
    cut_drag_fixture_t fixture;
    const auto edge = GENERATE(WORM_UI_SPAN_HANDLE_BEGIN, WORM_UI_SPAN_HANDLE_END);
    fixture.begin(edge);
    const auto emitted = fixture.release();
    const auto* input = cut_drag_input(emitted.get(), WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT);
    REQUIRE(input);
    check_cut_drag_stamp(emitted.get(), *input, edge, fixture.source_anchor_y);
    CHECK_FALSE(worm_ui_nuklear_compose_state().audio_span_drag.candidate_active);
    CHECK_FALSE(worm_ui_nuklear_compose_state().audio_span_drag.active);
}

TEST_CASE("Interrupted export-cut captures cannot commit after a late release", "[event-harness][export-cut][cut-drag]") {
    cut_drag_fixture_t fixture;
    fixture.begin(WORM_UI_SPAN_HANDLE_BEGIN);
    const auto interrupted = interrupt_cut_drag(fixture, GENERATE(0, 1, 2, 3, 4, 5));
    CHECK_FALSE(worm_ui_nuklear_compose_state().audio_span_drag.candidate_active);
    CHECK_FALSE(cut_drag_input(interrupted.get(), WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT));
    const auto late = fixture.release();
    CHECK_FALSE(cut_drag_input(late.get(), WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT));
}

TEST_CASE("Ordinary Compose does not admit export-cut drags even with retained edge geometry", "[event-harness][export-cut][cut-drag]") {
    cut_drag_fixture_t fixture{false};
    fixture.press(WORM_UI_SPAN_HANDLE_BEGIN);
    CHECK_FALSE(worm_ui_nuklear_compose_state().audio_span_drag.candidate_active);
    const auto motion = test::mouse_motion(fixture.x(), fixture.y(330));
    (void)fixture.harness.frame(std::span{&motion, 1});
    const auto released = fixture.release();
    CHECK_FALSE(cut_drag_input(released.get(), WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT));
}

TEST_CASE("Shared audio-span capture retains the preview input kind", "[event-harness][export-cut][cut-drag][song-preview]") {
    cut_drag_fixture_t fixture{false};
    auto& view = fixture.harness.view().compose;
    view.preview_span = view.export_cuts.front().span;
    view.export_cuts.clear();
    (void)fixture.harness.frame();
    fixture.begin(WORM_UI_SPAN_HANDLE_BEGIN);
    const auto released = fixture.release();
    const auto* preview = cut_drag_input(released.get(), WORM_UI_COMPOSE_INPUT_PREVIEW_SPAN_EDIT);
    REQUIRE(preview);
    CHECK_FALSE(preview->object_id);
    CHECK_FALSE(cut_drag_input(released.get(), WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT));
}

TEST_CASE("Overlapping export-cut handles prefer the selected song cut", "[event-harness][export-cut][cut-drag]") {
    cut_drag_fixture_t fixture;
    auto cut = fixture.harness.view().compose.export_cuts.front();
    cut.id = object_id_t{32};
    fixture.harness.view().compose.export_cuts.push_back(cut);
    worm_ui_nuklear_compose_state().export_cut_editor.selected = cut.id;
    (void)fixture.harness.frame();
    fixture.begin(WORM_UI_SPAN_HANDLE_BEGIN);
    const auto released = fixture.release();
    const auto* input = cut_drag_input(released.get(), WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT);
    REQUIRE(input);
    CHECK(input->object_id == cut.id);
}

TEST_CASE("Visible cut grips take priority over coincident notes and bookmark lines",
          "[event-harness][export-cut][cut-grip]") {
    cut_drag_fixture_t fixture;
    const auto edge = GENERATE(WORM_UI_SPAN_HANDLE_BEGIN, WORM_UI_SPAN_HANDLE_END);
    auto& view = fixture.harness.view().compose;
    const auto grip = ui::compose_export_cut_handle_t::from(view, view.export_cuts.front(), edge);
    check_cut_grip(grip.has_value());
    view.note_endpoints.push_back({.bounds = grip->bounds, .note_id = object_id_t{42}, .lane_id = lane_id_t{1}});
    view.beat_bookmarks = {{.bookmark_id = object_id_t{43}, .measure_number = 1, .projection_visible = true,
        .projected_y_pixels = grip->y_pixels}};
    (void)fixture.harness.frame();
    check_cut_grip(worm_ui_frontend_compose_viewport_for_testing(&fixture.viewport));
    const auto x = fixture.viewport.x + static_cast<float>((grip->bounds.left + grip->bounds.right) / 2 *
        fixture.viewport.width / view.viewport_width_pixels);
    // Grab away from the original invisible line; the edge must retain this offset.
    const auto y = fixture.y(grip->y_pixels + 5);
    CAPTURE(edge, x, y);
    const auto hover = test::mouse_motion(x, y);
    (void)fixture.harness.frame(std::span{&hover, 1});
    check_cut_grip(fixture.harness.drawn_text().find("Drag to resize the export cut") != std::string::npos);
    (void)fixture.harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)});
    check_cut_grip(worm_ui_nuklear_compose_state().audio_span_drag.candidate_active);
    check_cut_grip(!worm_ui_nuklear_compose_state().bookmark_ui.bookmark_move_candidate_active);
    const auto destination = fixture.y(grip->y_pixels + 35);
    const auto motion = test::mouse_motion(x, destination);
    (void)fixture.harness.frame(std::span{&motion, 1});
    const auto release = test::mouse_button(false, x, destination);
    const auto emitted = fixture.harness.frame(std::span{&release, 1});
    const auto* input = cut_drag_input(emitted.get(), WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT);
    check_cut_grip(input != nullptr);
    check_cut_grip(input->span_edge == edge);
    check_cut_grip(input->object_id == grip->cut);
    check_cut_grip(std::abs(input->y_pixels - grip->y_pixels - 30) <
        view.viewport_height_pixels / fixture.viewport.height + 0.001);
    check_cut_grip(!worm_ui_nuklear_compose_state().audio_span_drag.candidate_active);
}

TEST_CASE("Short cuts and viewport edges retain separate visible grips",
          "[event-harness][export-cut][cut-grip]") {
    cut_drag_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    auto& cut = view.export_cuts.front();
    const auto y = GENERATE(0.0, 300.0, 900.0);
    cut.span.projected_begin_y_pixels = y;
    cut.span.projected_end_y_pixels = std::min(view.viewport_height_pixels, y + 1);
    for (const auto edge : {WORM_UI_SPAN_HANDLE_BEGIN, WORM_UI_SPAN_HANDLE_END}) {
        const auto grip = ui::compose_export_cut_handle_t::from(view, cut, edge);
        check_cut_grip(grip.has_value());
        check_cut_grip(grip->bounds.top >= 0);
        check_cut_grip(grip->bounds.bottom <= view.viewport_height_pixels);
        const auto hit = ui::compose_export_cut_handle_t::at(view,
            (grip->bounds.left + grip->bounds.right) / 2,
            (grip->bounds.top + grip->bounds.bottom) / 2);
        check_cut_grip(hit.has_value());
        check_cut_grip(hit->edge == edge);
    }
}
