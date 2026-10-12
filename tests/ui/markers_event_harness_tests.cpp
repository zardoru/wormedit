#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

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

void scroll_inspector(nuklear_event_harness_t& harness, int notches) {
    (void)harness.frame(std::array{motion_to(1500.0f, 800.0f)});
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.mouse_x = 1500.0f;
    wheel.wheel.mouse_y = 800.0f;
    wheel.wheel.y = -1.0f;
    for (int count = 0; count < notches; ++count) (void)harness.frame(std::array{wheel});
}

struct markers_fixture_t {
    nuklear_event_harness_t harness{1600, 900};
    std::array<WormUiComposeBookmarkView, 2> bookmarks{{
        {.bookmark_id = 21, .label = "drop", .measure_number = 17, .row = 0, .chart_row = 16320},
        {.bookmark_id = 22, .label = "break", .measure_number = 33, .row = 0, .chart_row = 30720}}};
    std::array<WormUiComposeSectionView, 1> sections{{
        {.section_id = 31, .label = "verse", .begin_measure_number = 1, .begin_row = 0,
         .end_measure_number = 16, .end_row = 0, .begin_chart_row = 0, .end_chart_row = 15360}}};

    markers_fixture_t() {
        REQUIRE(harness.ready());
        auto& view = harness.view().compose;
        view.project_id = 3;
        view.chart_id = 7;
        view.document_revision = 12;
        view.beat_bookmarks = bookmarks.data();
        view.beat_bookmark_count = bookmarks.size();
        view.beat_sections = sections.data();
        view.beat_section_count = sections.size();
        view.beat_marker_navigation_available = true;
        (void)harness.frame();
        scroll_inspector(harness, 40);
        const auto tree = harness.drawn_text_center("Bookmarks and sections");
        REQUIRE(tree.has_value());
        (void)click(harness, tree->first, tree->second);
        scroll_inspector(harness, 40);
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
        CHECK(action.compose_bookmark_id == 22);
        CHECK(action.compose_input_chart_id == 7);
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
    const auto& editor = worm_ui_nuklear_compose_state().bookmark_rename;
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
    const auto& editor = worm_ui_nuklear_compose_state().bookmark_rename;
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
    REQUIRE(worm_ui_nuklear_compose_state().bookmark_rename.active);
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
    CHECK(committed.compose_section_id == 31);
    CHECK(std::string{committed.compose_marker_label} == "intro");
    CHECK(committed.compose_input_chart_id == 7);
    CHECK(committed.compose_input_document_revision == 12);
}
