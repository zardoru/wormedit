#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

namespace {
auto key_event(SDL_Keycode key, SDL_Scancode scan, SDL_Keymod modifiers) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    event.key.scancode = scan;
    event.key.mod = modifiers;
    event.key.down = true;
    return event;
}

auto key_release_event(SDL_Keycode key, SDL_Scancode scan) -> SDL_Event {
    auto event = key_event(key, scan, SDL_KMOD_NONE);
    event.type = SDL_EVENT_KEY_UP;
    event.key.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    return event;
}

auto text_contains(std::string_view text, std::string_view expected) -> bool {
    return text.find(expected) != std::string_view::npos;
}

void prepare_view(nuklear_event_harness_t& harness,
                  std::array<WormUiBookmarkChooserRowView, 2>& rows) {
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.project_id = 3;
    view.chart_id = 7;
    view.document_revision = 11;
    view.bookmark_chooser = {.project_id = 3, .chart_id = 7,
        .document_revision = 11, .rows = rows.data(), .row_count = rows.size(),
        .total_count = rows.size(), .first_row = 0};
    rows[0] = {.bookmark_id = 80, .anchor = 0,
        .position_label = "Measure 001 · row 240", .name = "",
        .display_label = "Measure 001 · row 240 · (unnamed) · #80",
        .navigation_available = true};
    rows[1] = {.bookmark_id = 81, .anchor = 1,
        .position_label = "Audio 00:00.500", .name = "Bridge",
        .display_label = "Audio 00:00.500 · Bridge · #81",
        .navigation_available = true};
}

void open_chooser(nuklear_event_harness_t& harness) {
    REQUIRE(worm_ui_frontend_open_bookmark_chooser(3, 7, 11));
    static_cast<void>(harness.frame());
}

auto click(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.x = x;
    down.button.y = y;
    const std::array press{motion, down};
    const auto pressed = harness.frame(press);
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.down = false;
    const std::array release{motion, up};
    return {pressed, harness.frame(release)};
}

auto has_bookmark_action(const WormUiWorkspaceAction& action, std::uint64_t id) -> bool {
    return action.compose_action == WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK &&
           action.compose_bookmark_id == id;
}
void check_opened_rows(const nuklear_event_harness_t& harness,
                       const std::array<WormUiBookmarkChooserRowView, 2>& rows) {
    CHECK(text_contains(harness.drawn_text(), "Select a bookmark by position and name"));
    CHECK(text_contains(harness.drawn_text(), rows[0].display_label));
    CHECK(text_contains(harness.drawn_text(), rows[1].display_label));
}
void check_selected_action(const workspace_action_snapshot_t& selected, std::uint64_t id,
                           std::uint64_t chart, std::uint64_t revision) {
    CHECK(has_bookmark_action(selected.get(), id));
    CHECK(selected.get().compose_input_project_id == 3);
    CHECK(selected.get().compose_input_chart_id == chart);
    CHECK(selected.get().compose_input_document_revision == revision);
}
void prepare_paged_view(nuklear_event_harness_t& harness,
                        std::array<WormUiBookmarkChooserRowView, 13>& rows,
                        std::array<std::string, 13>& labels) {
    for (std::size_t index = 0; index < rows.size(); ++index) {
        labels[index] = "Measure 001 · Entry · #" + std::to_string(100 + index);
        rows[index] = {.bookmark_id = 100 + index, .anchor = 0,
            .position_label = "Measure 001", .name = "Entry",
            .display_label = labels[index].c_str(), .navigation_available = true};
    }
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.project_id = 3;
    view.chart_id = 7;
    view.document_revision = 11;
    view.bookmark_chooser = {.project_id = 3, .chart_id = 7, .document_revision = 11,
        .rows = rows.data(), .row_count = 12, .total_count = rows.size(), .first_row = 0};
}

void next_page_control(nuklear_event_harness_t& harness) {
    const auto next = harness.drawn_text_center("Next");
    REQUIRE(next);
    static_cast<void>(click(harness, next->first, next->second));
    WormUiBookmarkChooserRequest request{};
    worm_ui_frontend_bookmark_chooser_request(&request);
    CHECK(request.open);
    CHECK(request.first_row == 12);
    CHECK(request.row_limit == 12);
}

void populate_requested_bookmark_page(nuklear_event_harness_t& harness,
                                      const std::array<WormUiBookmarkChooserRowView, 13>& rows) {
    WormUiBookmarkChooserRequest request{};
    worm_ui_frontend_bookmark_chooser_request(&request);
    auto& view = harness.view().compose.bookmark_chooser;
    view.rows = rows.data() + request.first_row;
    view.row_count = 1;
    view.first_row = request.first_row;
    static_cast<void>(harness.frame());
}
} // namespace

TEST_CASE("bookmark chooser mouse selection carries stable identity and context",
          "[frontend-state][event-harness][bookmark-chooser]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    std::array<WormUiBookmarkChooserRowView, 2> rows{};
    prepare_view(harness, rows);
    open_chooser(harness);
    check_opened_rows(harness, rows);
    const auto target = harness.drawn_text_center(rows[1].display_label);
    REQUIRE(target);
    const auto selected = click(harness, target->first, target->second);
    check_selected_action(selected.front(), 81, 7, 11);
}

TEST_CASE("bookmark chooser moves by keyboard and Escape cancels without navigation",
          "[frontend-state][event-harness][bookmark-chooser]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    std::array<WormUiBookmarkChooserRowView, 2> rows{};
    prepare_view(harness, rows);
    open_chooser(harness);
    const auto down = key_event(SDLK_DOWN, SDL_SCANCODE_DOWN, SDL_KMOD_NONE);
    (void)harness.frame(std::span{&down, 1});
    const auto enter = key_event(SDLK_RETURN, SDL_SCANCODE_RETURN, SDL_KMOD_NONE);
    const auto selected = harness.frame(std::span{&enter, 1});
    CHECK(has_bookmark_action(selected.get(), 81));

    const std::array releases{
        key_release_event(SDLK_DOWN, SDL_SCANCODE_DOWN),
        key_release_event(SDLK_RETURN, SDL_SCANCODE_RETURN)};
    static_cast<void>(harness.frame(releases));
    static_cast<void>(harness.frame());
    open_chooser(harness);
    const auto escape = key_event(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, SDL_KMOD_NONE);
    const auto cancelled = harness.frame(std::span{&escape, 1});
    CHECK(cancelled.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK_FALSE(text_contains(harness.drawn_text(), "Go to bookmark"));
}

TEST_CASE("bookmark chooser refreshes stale revisions and explains empty charts",
          "[frontend-state][event-harness][bookmark-chooser]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    std::array<WormUiBookmarkChooserRowView, 2> rows{};
    prepare_view(harness, rows);
    (void)open_chooser(harness);
    harness.view().compose.document_revision = 12;
    harness.view().compose.bookmark_chooser.document_revision = 12;
    const auto refreshed = harness.frame();
    CHECK(refreshed.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK(text_contains(harness.drawn_text(), "Bookmarks changed"));
    CHECK(text_contains(harness.drawn_text(), "Select a bookmark by position and name"));

    auto& projection = harness.view().compose.bookmark_chooser;
    projection.rows = nullptr;
    projection.row_count = 0;
    projection.total_count = 0;
    (void)harness.frame();
    CHECK(text_contains(harness.drawn_text(), "No bookmarks in this chart."));
}

TEST_CASE("bookmark chooser requests the next page without hiding later bookmarks",
          "[frontend-state][event-harness][bookmark-chooser]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    std::array<WormUiBookmarkChooserRowView, 13> rows{};
    std::array<std::string, 13> labels{};
    prepare_paged_view(harness, rows, labels);
    open_chooser(harness);
    CHECK(text_contains(harness.drawn_text(), "1-12 of 13"));
    next_page_control(harness);
    populate_requested_bookmark_page(harness, rows);
    CHECK(text_contains(harness.drawn_text(), "13-13 of 13"));
    CHECK(text_contains(harness.drawn_text(), "#112"));
}
