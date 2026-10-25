#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace {

auto motion_to(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

auto button(bool down, float x, float y, int clicks = 1) -> SDL_Event {
    SDL_Event event = motion_to(x, y);
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = static_cast<Uint8>(clicks);
    event.button.x = x;
    event.button.y = y;
    return event;
}

auto click(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    return {harness.frame(std::array{motion_to(x, y), button(true, x, y)}),
            harness.frame(std::array{motion_to(x, y), button(false, x, y)})};
}

struct markers_fixture_t {
    nuklear_event_harness_t harness{1600, 900};
    std::array<wormedit::ui::compose_bookmark_view_t, 2> bookmarks{{
        {.bookmark_id = {21}, .label = "drop", .measure_number = 17, .row = 0, .chart_row = 16320},
        {.bookmark_id = {22}, .label = "break", .measure_number = 33, .row = 0, .chart_row = 30720}}};
    std::array<wormedit::ui::compose_section_view_t, 1> sections{{
        {.section_id = {31}, .label = "verse", .begin_measure_number = 1, .begin_row = 0,
         .end_measure_number = 16, .end_row = 0, .begin_chart_row = 0, .end_chart_row = 15360}}};

    markers_fixture_t() {
        REQUIRE(harness.ready());
        auto& view = harness.view().compose;
        view.project_id.value = 3;
        view.chart_id.value = 7;
        view.document_revision = 12;
        view.beat_bookmarks.assign(bookmarks.begin(), bookmarks.end());
        view.beat_bookmarks.resize(bookmarks.size());
        view.beat_sections.assign(sections.begin(), sections.end());
        view.beat_sections.resize(sections.size());
        view.beat_marker_navigation_available = true;
        worm_ui_nuklear_compose_state().dock_layout.layout.activate(wormedit::dock_panel_t::markers, false);
        (void)harness.frame();
        REQUIRE(harness.drawn_text().find("Bookmarks") != std::string::npos);
    }

    // Finds an icon of the group header at `y` by the tooltip it shows.
    auto icon(float y, const char* tooltip) -> std::optional<std::pair<float, float>> {
        for (float x = 1100.0f; x < 1590.0f; x += 3.0f) {
            (void)harness.frame(std::array{motion_to(x, y)});
            if (harness.drawn_text().find(tooltip) != std::string::npos)
                return std::pair<float, float>{x, y};
        }
        return std::nullopt;
    }
};

} // namespace

TEST_CASE("Bookmarks and Sections groups list their entries with positions",
          "[unit][frontend-state][event-harness][markers]") {
    markers_fixture_t fixture;
    const auto& text = fixture.harness.drawn_text();
    CHECK(text.find("Bookmarks") != std::string::npos);
    CHECK(text.find("Sections") != std::string::npos);
    CHECK(text.find("drop") != std::string::npos);
    CHECK(text.find("m17:0") != std::string::npos);
    CHECK(text.find("verse") != std::string::npos);
    CHECK(text.find("m1-m16") != std::string::npos);
    CHECK(text.find("Prev marker") == std::string::npos);
}

TEST_CASE("clicking a bookmark row navigates to it with the document stamps",
          "[unit][frontend-state][event-harness][markers]") {
    markers_fixture_t fixture;
    const auto row = fixture.harness.drawn_text_center("break");
    REQUIRE(row.has_value());
    const auto clicked = click(fixture.harness, row->first, row->second);
    bool navigated = false;
    for (const auto& frame : clicked) {
        const auto& action = frame.get();
        if (action.compose_action != WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK) continue;
        navigated = true;
        CHECK(action.compose_bookmark_id.value == 22);
        CHECK(action.compose_input_chart_id.value == 7);
        CHECK(action.compose_input_document_revision == 12);
    }
    CHECK(navigated);
}

TEST_CASE("double-clicking a bookmark row opens the inline rename editor",
          "[unit][frontend-state][event-harness][markers]") {
    markers_fixture_t fixture;
    const auto row = fixture.harness.drawn_text_center("drop");
    REQUIRE(row.has_value());
    (void)click(fixture.harness, row->first, row->second);
    (void)click(fixture.harness, row->first, row->second);
    (void)fixture.harness.frame();
    const auto& editor = worm_ui_nuklear_compose_state().bookmark_ui.bookmark_rename;
    CHECK(editor.active);
    CHECK(editor.anchored);
    CHECK(editor.bookmark_id == 21);
}

TEST_CASE("double-clicking a section row opens the inline section label editor",
          "[unit][frontend-state][event-harness][markers]") {
    markers_fixture_t fixture;
    const auto row = fixture.harness.drawn_text_center("verse");
    REQUIRE(row.has_value());
    (void)click(fixture.harness, row->first, row->second);
    (void)click(fixture.harness, row->first, row->second);
    (void)fixture.harness.frame();
    const auto& editor = worm_ui_nuklear_compose_state().bookmark_ui.bookmark_rename;
    CHECK(editor.active);
    CHECK(editor.kind == inline_label_kind_t::section_label);
    CHECK(editor.bookmark_id == 31);
}

TEST_CASE("the Bookmarks add icon adds an unnamed bookmark and delete needs a selection",
          "[unit][frontend-state][event-harness][markers]") {
    markers_fixture_t fixture;
    const auto header = fixture.harness.drawn_text_center("Bookmarks");
    REQUIRE(header.has_value());
    const auto add = fixture.icon(header->second, "Add a bookmark at the cursor");
    REQUIRE(add.has_value());
    const auto remove = fixture.icon(header->second, "Select a bookmark to delete it");
    REQUIRE(remove.has_value());
    for (const auto& frame : click(fixture.harness, remove->first, remove->second))
        CHECK(frame.get().compose_action != WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK);
    bool added = false;
    for (const auto& frame : click(fixture.harness, add->first, add->second))
        added = added || std::string_view{frame.get().menu_command_id} == "markers.add_bookmark";
    CHECK(added);
}

TEST_CASE("renaming a section from the list stages the label update with its identity",
          "[unit][frontend-state][event-harness][markers]") {
    markers_fixture_t fixture;
    const auto row = fixture.harness.drawn_text_center("verse");
    REQUIRE(row.has_value());
    (void)click(fixture.harness, row->first, row->second);
    (void)click(fixture.harness, row->first, row->second);
    (void)fixture.harness.frame();
    REQUIRE(worm_ui_nuklear_compose_state().bookmark_ui.bookmark_rename.active);
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "intro";
    (void)fixture.harness.frame(std::array{text});
    SDL_Event enter{};
    enter.type = SDL_EVENT_KEY_DOWN;
    enter.key.type = SDL_EVENT_KEY_DOWN;
    enter.key.down = true;
    enter.key.key = SDLK_RETURN;
    enter.key.scancode = SDL_SCANCODE_RETURN;
    const auto committed = fixture.harness.frame(std::array{enter}).get();
    CHECK(committed.compose_action == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL);
    CHECK(committed.compose_section_id.value == 31);
    CHECK(std::string{committed.compose_marker_label} == "intro");
    CHECK(committed.compose_input_chart_id.value == 7);
    CHECK(committed.compose_input_document_revision == 12);
}

TEST_CASE("marker editing lives in the Markers dock instead of the Inspector", "[event-harness][markers]") {
    markers_fixture_t fixture;
    CHECK(fixture.harness.drawn_text().find("Audio-time bookmarks") != std::string::npos);
    worm_ui_nuklear_compose_state().dock_layout.layout.activate(wormedit::dock_panel_t::inspector, false);
    (void)fixture.harness.frame();
    CHECK(fixture.harness.drawn_text().find("Audio-time bookmarks") == std::string::npos);
    CHECK(fixture.harness.drawn_text().find("Bookmarks and sections") == std::string::npos);
}
TEST_CASE("Markers dock yields its controls to the bookmark chooser and restores them after closing", "[event-harness][markers]") {
    markers_fixture_t fixture;
    worm_ui_nuklear_compose_state().bookmark_ui.bookmark_chooser.open = true;
    (void)fixture.harness.frame();
    CHECK(fixture.harness.drawn_text().find("Bookmark chooser is open.") != std::string::npos);
    worm_ui_nuklear_compose_state().bookmark_ui.bookmark_chooser.open = false;
    (void)fixture.harness.frame();
    CHECK(fixture.harness.drawn_text().find("Bookmarks") != std::string::npos);
}

namespace {
// Setting a preview changes the group's height; let the scrolled panel settle before clicking.
auto clicked_action(nuklear_event_harness_t& harness, const char* label) -> WormUiComposeActionKind {
    for (int frame = 0; frame < 3; ++frame) (void)harness.frame();
    const auto center = harness.drawn_text_center(label);
    if (!center) return WORM_UI_COMPOSE_ACTION_NONE;
    for (const auto& frame : click(harness, center->first, center->second))
        if (frame.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE) return frame.get().compose_action;
    return WORM_UI_COMPOSE_ACTION_NONE;
}
auto song_preview_view(bool chart_override) -> wormedit::ui::compose_preview_span_view_t {
    return {.available = true, .has_length = true, .chart_override = chart_override,
            .start_milliseconds = 500, .end_milliseconds = 2500, .begin_row = 240, .end_row = 1200};
}
} // namespace

TEST_CASE("The Song preview group clears a song preview", "[unit][frontend-state][event-harness][markers][song-preview]") {
    markers_fixture_t fixture;
    fixture.harness.view().compose.preview_span = song_preview_view(false);
    CHECK(clicked_action(fixture.harness, "Clear preview") == WORM_UI_COMPOSE_ACTION_CLEAR_PREVIEW);
}

TEST_CASE("The Song preview group returns a chart's own preview to the song's",
          "[unit][frontend-state][event-harness][markers][song-preview]") {
    markers_fixture_t fixture;
    fixture.harness.view().compose.preview_span = song_preview_view(true);
    CHECK(clicked_action(fixture.harness, "Use song preview") == WORM_UI_COMPOSE_ACTION_USE_SONG_PREVIEW);
}

TEST_CASE("The Song preview group exports the authored span", "[unit][frontend-state][event-harness][markers][song-preview]") {
    markers_fixture_t fixture;
    fixture.harness.view().compose.preview_span = song_preview_view(false);
    (void)clicked_action(fixture.harness, "Export preview audio…");
    const auto& dialog = worm_ui_nuklear_compose_state().preview_audio;
    CHECK((dialog.open && dialog.draft.begin_row == 240 && dialog.draft.end_row == 1200));
}
