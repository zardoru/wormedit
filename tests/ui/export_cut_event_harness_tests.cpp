#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdio>

namespace {
using namespace wormedit;

struct cut_fixture_t {
    nuklear_event_harness_t harness{1280, 1000};

    cut_fixture_t() {
        REQUIRE(harness.ready());
        auto& view = harness.view().compose;
        view.project_id = song_id_t{3};
        view.chart_id = chart_id_t{7};
        view.document_revision = 12;
        view.export_cuts = {{.id = object_id_t{31}, .label = "Intro cut",
            .span = {.available = true, .has_length = true, .start_milliseconds = 1000, .end_milliseconds = 2000}},
            {.id = object_id_t{32}, .label = "Last cut",
            .span = {.available = true, .has_length = true, .start_milliseconds = 5000, .end_milliseconds = 6000}}};
        worm_ui_nuklear_compose_state().dock_layout.layout.activate(dock_panel_t::markers, false);
        (void)harness.frame();
    }

    auto click(std::string_view label) -> std::vector<workspace_action_snapshot_t> {
        const auto point = harness.drawn_text_center(label);
        REQUIRE(point);
        return test::click_at(harness, point->first, point->second);
    }

    void open_editor() {
        (void)click("Export cuts");
        (void)harness.frame();
    }

    auto command(std::string_view label, WormUiComposeActionKind kind) -> workspace_action_snapshot_t {
        const auto frames = click(label);
        const auto found = std::ranges::find_if(frames, [kind](const auto& frame) {
            return frame.get().compose_action == kind;
        });
        REQUIRE(found != frames.end());
        return *found;
    }

    void select_intro() {
        open_editor();
        (void)click("Intro cut");
        (void)harness.frame();
    }

    void open_area_menu() {
        auto& layout = worm_ui_nuklear_compose_state().dock_layout.layout;
        for (const auto panel : layout.order()) layout.show(panel, false);
        auto& view = harness.view().compose;
        view.area_range_available = true;
        view.area_rows_per_measure = 960;
        view.area_canvas_rows = 9600;
        view.area_range_begin_row = 0;
        view.area_range_end_row = 9600;
        view.preview_audio_range = ui::preview_audio_range_view_t::from(0, 20);
        (void)harness.frame();
        const auto motion = test::mouse_motion(900, 400);
        auto button = test::mouse_button(true, 900, 400);
        button.button.button = SDL_BUTTON_RIGHT;
        (void)harness.frame(std::array{motion, button});
        button.type = SDL_EVENT_MOUSE_BUTTON_UP;
        button.button.down = false;
        (void)harness.frame(std::span{&button, 1});
        (void)harness.frame();
    }
};
} // namespace

TEST_CASE("Area menu creates an export cut with the captured source selection", "[event-harness][export-cut]") {
    cut_fixture_t fixture;
    fixture.open_area_menu();
    const auto emitted = fixture.command("Create export cut", WORM_UI_COMPOSE_ACTION_ADD_EXPORT_CUT_FROM_SELECTION);
    CHECK(emitted.get().compose_input_project_id.value == 3);
    CHECK(emitted.get().compose_input_chart_id.value == 7);
    CHECK(emitted.get().compose_input_document_revision == 12);
}

TEST_CASE("Markers export-cut list selects the song cut and captures owned edit strings", "[event-harness][export-cut]") {
    cut_fixture_t fixture;
    fixture.select_intro();
    auto& editor = worm_ui_nuklear_compose_state().export_cut_editor;
    CHECK(editor.selected == object_id_t{31});
    std::snprintf(editor.label.data(), editor.label.size(), "%s", "Owned cut");
    std::snprintf(editor.begin.data(), editor.begin.size(), "%s", "525");
    std::snprintf(editor.end.data(), editor.end.size(), "%s", "1425");
    (void)fixture.harness.frame();
    const auto emitted = fixture.command("Apply cut", WORM_UI_COMPOSE_ACTION_UPDATE_EXPORT_CUT);
    std::snprintf(editor.label.data(), editor.label.size(), "%s", "Changed buffer");
    CHECK(emitted.get().compose_export_cut_id == object_id_t{31});
    CHECK(emitted.get().compose_marker_label == "Owned cut");
    CHECK(emitted.get().compose_marker_audio_begin == "525");
    CHECK(emitted.get().compose_marker_audio_end == "1425");
    CHECK(emitted.get().compose_input_document_revision == 12);
}

TEST_CASE("Markers export-cut deletion keeps its selected song identity", "[event-harness][export-cut]") {
    cut_fixture_t fixture;
    fixture.select_intro();
    const auto emitted = fixture.command("Delete cut", WORM_UI_COMPOSE_ACTION_DELETE_EXPORT_CUT);
    CHECK(emitted.get().compose_export_cut_id == object_id_t{31});
    CHECK(emitted.get().compose_input_project_id.value == 3);
    CHECK(emitted.get().compose_input_chart_id.value == 7);
    CHECK(emitted.get().compose_input_document_revision == 12);
}

TEST_CASE("Export-cut creation requires a materialized selection and an editable scene", "[event-harness][export-cut]") {
    cut_fixture_t fixture;
    fixture.harness.view().compose.preview_audio_range = ui::preview_audio_range_view_t::from(0, 2);
    fixture.open_editor();
    const auto point = fixture.harness.drawn_text_center("Create from selection");
    REQUIRE(point);
    SECTION("no selected range") { fixture.harness.view().compose.preview_audio_range.available = false; }
    SECTION("scene unavailable") { fixture.harness.view().compose.scene_available = false; }
    (void)fixture.harness.frame();
    const auto frames = test::click_at(fixture.harness, point->first, point->second);
    CHECK(std::ranges::none_of(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_ADD_EXPORT_CUT_FROM_SELECTION;
    }));
}

TEST_CASE("Markers export-cut creation emits the shared action from a valid selected range", "[event-harness][export-cut]") {
    cut_fixture_t fixture;
    fixture.harness.view().compose.preview_audio_range = ui::preview_audio_range_view_t::from(0, 2);
    fixture.open_editor();
    const auto emitted = fixture.command("Create from selection", WORM_UI_COMPOSE_ACTION_ADD_EXPORT_CUT_FROM_SELECTION);
    CHECK(emitted.get().compose_input_project_id.value == 3);
    CHECK(emitted.get().compose_input_document_revision == 12);
}

TEST_CASE("Removing a selected export cut retires its editor target before a late click", "[event-harness][export-cut]") {
    cut_fixture_t fixture;
    fixture.select_intro();
    auto& view = fixture.harness.view().compose;
    view.export_cuts.erase(view.export_cuts.begin());
    ++view.document_revision;
    (void)fixture.harness.frame();
    CHECK_FALSE(worm_ui_nuklear_compose_state().export_cut_editor.selected);
    const auto frames = fixture.click("Delete cut");
    CHECK(std::ranges::none_of(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_DELETE_EXPORT_CUT;
    }));
}
