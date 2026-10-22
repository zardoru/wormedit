#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"

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
                  std::array<wormedit::ui::bookmark_chooser_row_view_t, 2>& rows) {
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.project_id = {3};
    view.chart_id = {7};
    view.document_revision = 11;

    rows[0] = {.bookmark_id = {80}, .anchor = 0,
        .position_label = "Measure 001 · row 240", .name = "",
        .display_label = "Measure 001 · row 240 · (unnamed) · #80",
        .navigation_available = true};
    rows[1] = {.bookmark_id = {81}, .anchor = 1,
        .position_label = "Audio 00:00.500", .name = "Bridge",
        .display_label = "Audio 00:00.500 · Bridge · #81",
        .navigation_available = true};
    view.bookmark_chooser = {.project_id = {3}, .chart_id = {7},
        .document_revision = 11, .rows = {rows.begin(), rows.end()},
        .total_count = rows.size(), .first_row = 0};
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

auto has_bookmark_action(const wormedit::ui::workspace_action_t& action, std::uint64_t id) -> bool {
    return action.compose_action == WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK &&
           action.compose_bookmark_id.value == id;
}
void check_opened_rows(const nuklear_event_harness_t& harness,
                       const std::array<wormedit::ui::bookmark_chooser_row_view_t, 2>& rows) {
    CHECK(text_contains(harness.drawn_text(), "Select a bookmark by position and name"));
    CHECK(text_contains(harness.drawn_text(), rows[0].display_label));
    CHECK(text_contains(harness.drawn_text(), rows[1].display_label));
}
void check_selected_action(const workspace_action_snapshot_t& selected, std::uint64_t id,
                           std::uint64_t chart, std::uint64_t revision) {
    CHECK(has_bookmark_action(selected.get(), id));
    CHECK(selected.get().compose_input_project_id.value == 3);
    CHECK(selected.get().compose_input_chart_id.value == chart);
    CHECK(selected.get().compose_input_document_revision == revision);
}
void prepare_paged_view(nuklear_event_harness_t& harness,
                        std::array<wormedit::ui::bookmark_chooser_row_view_t, 13>& rows,
                        std::array<std::string, 13>& labels) {
    for (std::size_t index = 0; index < rows.size(); ++index) {
        labels[index] = "Measure 001 · Entry · #" + std::to_string(100 + index);
        rows[index] = {.bookmark_id = {100 + index}, .anchor = 0,
            .position_label = "Measure 001", .name = "Entry",
            .display_label = labels[index].c_str(), .navigation_available = true};
    }
    auto& view = harness.view().compose;
    view.scene_available = true;
    view.project_id = {3};
    view.chart_id = {7};
    view.document_revision = 11;
    view.bookmark_chooser = {.project_id = {3}, .chart_id = {7}, .document_revision = 11,
        .rows = {rows.begin(), rows.begin() + 12}, .total_count = rows.size(), .first_row = 0};
}

void next_page_control(nuklear_event_harness_t& harness) {
    const auto next = harness.drawn_text_center("Next");
    REQUIRE(next);
    static_cast<void>(click(harness, next->first, next->second));
    wormedit::ui::bookmark_chooser_request_t request{};
    worm_ui_frontend_bookmark_chooser_request(&request);
    CHECK(request.open);
    CHECK(request.first_row == 12);
    CHECK(request.row_limit == 12);
}

void populate_requested_bookmark_page(nuklear_event_harness_t& harness,
                                      const std::array<wormedit::ui::bookmark_chooser_row_view_t, 13>& rows) {
    wormedit::ui::bookmark_chooser_request_t request{};
    worm_ui_frontend_bookmark_chooser_request(&request);
    auto& view = harness.view().compose.bookmark_chooser;
    view.rows = {rows[request.first_row]};
    view.first_row = request.first_row;
    static_cast<void>(harness.frame());
}
} // namespace

TEST_CASE("bookmark chooser mouse selection carries stable identity and context",
          "[frontend-state][event-harness][bookmark-chooser]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    std::array<wormedit::ui::bookmark_chooser_row_view_t, 2> rows{};
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
    std::array<wormedit::ui::bookmark_chooser_row_view_t, 2> rows{};
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
    std::array<wormedit::ui::bookmark_chooser_row_view_t, 2> rows{};
    prepare_view(harness, rows);
    (void)open_chooser(harness);
    harness.view().compose.document_revision = 12;
    harness.view().compose.bookmark_chooser.document_revision = 12;
    const auto refreshed = harness.frame();
    CHECK(refreshed.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK(text_contains(harness.drawn_text(), "Bookmarks changed"));
    CHECK(text_contains(harness.drawn_text(), "Select a bookmark by position and name"));

    auto& projection = harness.view().compose.bookmark_chooser;
    projection.rows.clear();
    projection.total_count = 0;
    (void)harness.frame();
    CHECK(text_contains(harness.drawn_text(), "No bookmarks in this chart."));
}

TEST_CASE("bookmark chooser requests the next page without hiding later bookmarks",
          "[frontend-state][event-harness][bookmark-chooser]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    std::array<wormedit::ui::bookmark_chooser_row_view_t, 13> rows{};
    std::array<std::string, 13> labels{};
    prepare_paged_view(harness, rows, labels);
    open_chooser(harness);
    CHECK(text_contains(harness.drawn_text(), "1-12 of 13"));
    next_page_control(harness);
    populate_requested_bookmark_page(harness, rows);
    CHECK(text_contains(harness.drawn_text(), "13-13 of 13"));
    CHECK(text_contains(harness.drawn_text(), "#112"));
}

TEST_CASE("bookmark chooser auto focused search requests matches and guards stale Enter",
          "[frontend-state][event-harness][bookmark-chooser]") {
    nuklear_event_harness_t harness{1280, 800};
    REQUIRE(harness.ready());
    std::array<wormedit::ui::bookmark_chooser_row_view_t, 2> rows{};
    prepare_view(harness, rows);
    open_chooser(harness);
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "bridge";
    (void)harness.frame(std::span{&text, 1});
    wormedit::ui::bookmark_chooser_request_t request{};
    worm_ui_frontend_bookmark_chooser_request(&request);
    CHECK(std::string_view{request.query} == "bridge");
    CHECK(request.first_row == 0);
    CHECK(text_contains(harness.drawn_text(), "Searching…"));
    const auto enter = key_event(SDLK_RETURN, SDL_SCANCODE_RETURN, SDL_KMOD_NONE);
    CHECK(harness.frame(std::span{&enter, 1}).get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    const auto release = key_release_event(SDLK_RETURN, SDL_SCANCODE_RETURN);
    (void)harness.frame(std::span{&release, 1});
    auto& view = harness.view().compose.bookmark_chooser;
    view.query = "bridge";
    view.rows = {rows[1]};
    view.total_count = 1;
    (void)harness.frame();
    const auto chosen = harness.frame(std::span{&enter, 1});
    check_selected_action(chosen, 81, 7, 11);
}

namespace {
void require_chooser_lifecycle(bool value) { REQUIRE(value); }
void check_chooser_lifecycle(bool value) { CHECK(value); }

auto chooser_glyph_width(nk_handle, float, const char*, int length) -> float {
    return static_cast<float>(length) * 8.0f;
}

struct chooser_host_fixture_t {
    nk_user_font font{};
    nk_context context{};
    bookmark_chooser_state_t state{};
    wormedit::ui::bookmark_chooser_view_t view{.project_id = {3}, .chart_id = {7},
                                  .document_revision = 11};
    wormedit::ui::workspace_action_t action{};
    bool owned{};

    chooser_host_fixture_t() {
        font.height = 15.0f;
        font.width = chooser_glyph_width;
        require_chooser_lifecycle(nk_init_default(&context, &font));
        worm_ui_nuklear_bookmark_chooser_open(state, 3, 7, 11);
    }
    ~chooser_host_fixture_t() { nk_free(&context); }
    chooser_host_fixture_t(const chooser_host_fixture_t&) = delete;
    auto operator=(const chooser_host_fixture_t&) -> chooser_host_fixture_t& = delete;

    auto frame(struct nk_rect host_bounds, bool block_begin = false) -> struct nk_rect {
        nk_input_begin(&context);
        nk_input_end(&context);
        nk_window_set_bounds(&context, "Chooser host", host_bounds);
        require_chooser_lifecycle(nk_begin(&context, "Chooser host", host_bounds, NK_WINDOW_NO_SCROLLBAR));
        auto* host = context.current;
        if (block_begin) {
            require_chooser_lifecycle(nk_popup_begin(&context, NK_POPUP_STATIC, "Other popup",
                NK_WINDOW_BORDER, nk_rect(10, 10, 100, 100)));
            nk_popup_end(&context);
        }
        owned = worm_ui_nuklear_render_compose_bookmark_chooser(
            &context, view, &action, true, &state);
        require_chooser_lifecycle(context.current == host);
        require_chooser_lifecycle(host->popup.win != nullptr);
        require_chooser_lifecycle(host->popup.active);
        const auto popup_bounds = host->popup.win->bounds;
        nk_end(&context);
        nk_clear(&context);
        return popup_bounds;
    }
};
} // namespace

TEST_CASE("bookmark chooser retains origin-based positioning when its host moves",
          "[frontend-state][event-harness][bookmark-chooser][picker-lifecycle]") {
    // Characterization requested by T-649, not a desired geometry contract:
    // preserve this behavior until the owner decides whether it should change.
    chooser_host_fixture_t fixture;
    const auto origin = fixture.frame(nk_rect(0, 0, 1280, 800));
    const auto moved = fixture.frame(nk_rect(120, 80, 1280, 800));
    check_chooser_lifecycle(moved.x == origin.x);
    check_chooser_lifecycle(moved.y == origin.y);
    check_chooser_lifecycle(moved.w == origin.w);
    check_chooser_lifecycle(moved.h == origin.h);
    check_chooser_lifecycle(fixture.state.open);
    check_chooser_lifecycle(fixture.owned);
    check_chooser_lifecycle(fixture.action.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("bookmark chooser begin failure preserves focus and reports prior release ownership",
          "[frontend-state][event-harness][bookmark-chooser][picker-lifecycle]") {
    for (const bool guarded : {false, true}) {
        chooser_host_fixture_t fixture;
        fixture.state.release_pending = guarded;
        static_cast<void>(fixture.frame(nk_rect(0, 0, 1280, 800), true));
        check_chooser_lifecycle(fixture.state.open);
        check_chooser_lifecycle(fixture.state.focus_pending);
        check_chooser_lifecycle(!fixture.state.close_pending);
        check_chooser_lifecycle(!fixture.state.release_pending);
        check_chooser_lifecycle(fixture.owned == guarded);
    }
}

TEST_CASE("bookmark chooser retains its Enter routing during synthetic IME composition",
          "[frontend-state][event-harness][bookmark-chooser]") {
    // Characterize existing frontend wiring; this does not exercise an OS IME.
    nuklear_event_harness_t harness{1280, 800};
    require_chooser_lifecycle(harness.ready());
    std::array<wormedit::ui::bookmark_chooser_row_view_t, 2> rows{};
    prepare_view(harness, rows);
    open_chooser(harness);
    SDL_Event editing{};
    editing.type = SDL_EVENT_TEXT_EDITING;
    editing.edit.text = "bridge";
    static_cast<void>(harness.frame(std::span{&editing, 1}));
    const auto enter = key_event(SDLK_RETURN, SDL_SCANCODE_RETURN, SDL_KMOD_NONE);
    const auto selected = harness.frame(std::span{&enter, 1});
    check_selected_action(selected, 80, 7, 11);
    check_chooser_lifecycle(selected.get().shortcut_events.size() == 0);
    check_chooser_lifecycle(!worm_ui_nuklear_compose_state().bookmark_ui.bookmark_chooser.open);
}

TEST_CASE("bookmark chooser context loss cancels without navigation or shortcut leakage",
          "[frontend-state][event-harness][bookmark-chooser]") {
    for (int cause = 0; cause < 3; ++cause) {
        nuklear_event_harness_t harness{1280, 800};
        require_chooser_lifecycle(harness.ready());
        std::array<wormedit::ui::bookmark_chooser_row_view_t, 2> rows{};
        prepare_view(harness, rows);
        open_chooser(harness);
        SDL_Event event{};
        if (cause == 0) {
            event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        } else {
            if (cause == 1) ++harness.view().compose.bookmark_chooser.project_id.value;
            else ++harness.view().compose.bookmark_chooser.chart_id.value;
            event = key_event(SDLK_RETURN, SDL_SCANCODE_RETURN, SDL_KMOD_NONE);
        }
        const auto canceled = harness.frame(std::span{&event, 1});
        const auto& state = worm_ui_nuklear_compose_state().bookmark_ui.bookmark_chooser;
        check_chooser_lifecycle(!state.open);
        check_chooser_lifecycle(state.release_pending);
        check_chooser_lifecycle(canceled.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
        check_chooser_lifecycle(canceled.get().shortcut_events.size() == 0);
    }
}
