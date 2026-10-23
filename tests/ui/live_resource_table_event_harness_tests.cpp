#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <span>
#include <source_location>
#include <string_view>

namespace {
template <typename T>
void check_resource_table(const T& value,
    std::source_location where = std::source_location::current()) {
    INFO("resource table assertion at " << where.line());
    CHECK(static_cast<bool>(value));
}
template <typename T>
void check_no_resource_table(const T& value,
    std::source_location where = std::source_location::current()) {
    INFO("resource table assertion at " << where.line());
    CHECK_FALSE(static_cast<bool>(value));
}
template <typename T>
void require_resource_table(const T& value,
    std::source_location where = std::source_location::current()) {
    INFO("resource table assertion at " << where.line());
    REQUIRE(static_cast<bool>(value));
}
auto button_event(bool down, float x, float y, bool twice = false) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = twice ? 2 : 1;
    event.button.x = x; event.button.y = y;
    return event;
}
auto click(nuklear_event_harness_t& harness, float x, float y, bool twice = false)
    -> std::array<workspace_action_snapshot_t, 2> {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x; motion.motion.y = y;
    const auto down = std::array{motion, button_event(true, x, y, twice)};
    const auto up = std::array{motion, button_event(false, x, y, twice)};
    return {harness.frame(down), harness.frame(up)};
}
auto click_text(nuklear_event_harness_t& harness, std::string_view text, bool twice = false)
    -> std::array<workspace_action_snapshot_t, 2> {
    const auto point = harness.drawn_text_center(text);
    require_resource_table(point);
    return click(harness, point->first, point->second, twice);
}
auto has_action(const std::array<workspace_action_snapshot_t, 2>& actions, WormUiComposeActionKind kind) -> bool {
    return std::ranges::any_of(actions, [&](const auto& action) { return action.get().compose_action == kind; });
}
auto action_point(const nuklear_event_harness_t& harness, std::string_view state, int cell)
    -> std::pair<float, float> {
    const auto rows = harness.drawn_text_bounds();
    const auto found = std::ranges::find_if(rows, [&](const auto& row) { return std::string_view{row.text} == state; });
    require_resource_table(found != rows.end());
    return {found->x + found->width + 4 + 11 + static_cast<float>(cell * 26), found->y + found->height * 0.5f};
}
void show_dock(nuklear_event_harness_t& harness, bool images) {
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.layout.show(wormedit::dock_panel_t::inspector, false); state.dock_layout.layout.show(wormedit::dock_panel_t::layers, false);
    state.dock_layout.layout.show(wormedit::dock_panel_t::sounds, !images); state.dock_layout.layout.show(wormedit::dock_panel_t::images, images);
    state.dock_layout.sounds_preview_on_click = false;
    (void)harness.frame();
}
struct sound_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    std::array<wormedit::ui::compose_keysound_row_view_t, 3> rows{{
        {.resource_id = wormedit::resource_id_t{11}, .slot = "0A", .name = "Used.wav", .use_count = 4, .source_path = "drums/Used.wav"},
        {.resource_id = wormedit::resource_id_t{12}, .slot = "0B", .name = "Free.wav", .use_count = 0, .source_path = "other/Free.wav"},
        {.resource_id = wormedit::resource_id_t{13}, .slot = "0C", .name = "Lost.wav", .missing = true, .use_count = 1, .source_path = "drums/Lost.wav"}}};
    sound_fixture_t() {
        require_resource_table(harness.ready());
        auto& view = harness.view().compose;
        view.keysounds.assign(std::begin(rows), std::end(rows)); view.keysounds.resize(view.keysound_total_count = rows.size());
        view.keysounds_available = view.keysound_ids_available = true;
        show_dock(harness, false);
    }
};
struct image_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    std::array<std::uint64_t, 3> ids{21, 22, 23};
    std::array<wormedit::ui::compose_bga_row_view_t, 3> rows{{
        {.resource_id = wormedit::resource_id_t{21}, .slot = "01", .name = "Used.png", .use_count = 3, .source_path = "bg/Used.png"},
        {.resource_id = wormedit::resource_id_t{22}, .slot = "02", .name = "Free.png", .use_count = 0, .source_path = "bg/Free.png"},
        {.resource_id = wormedit::resource_id_t{23}, .slot = "03", .name = "Lost.png", .use_count = 0, .missing = true, .source_path = "bg/Lost.png"}}};
    image_fixture_t() {
        require_resource_table(harness.ready());
        auto& view = harness.view().compose;
        view.bga_rows.assign(std::begin(rows), std::end(rows)); view.bga_rows.resize(rows.size());
        view.bga_matching_ids.assign(std::begin(ids), std::end(ids)); view.bga_total_matching = ids.size(); view.bga_search = "";
        view.bga_all_resource_ids.assign(std::begin(ids), std::end(ids)); view.bga_all_resource_ids.resize(ids.size());
        view.bga_advance_ids.assign(std::begin(ids), std::end(ids)); view.bga_advance_ids.resize(2);
        show_dock(harness, true);
    }
};
}

TEST_CASE("Live Sounds table separates selection from arming and keeps filtered sources", "[event-harness][resource-table]") {
    sound_fixture_t fixture;
    auto& state = worm_ui_nuklear_compose_state();
    (void)click_text(fixture.harness, "Used.wav");
    check_resource_table(state.keysound_catalog.keysound_selection.primary == 11);
    check_resource_table(state.resource_picker.armed_resource_id == 0);
    (void)click_text(fixture.harness, "Used.wav", true);
    check_resource_table(state.resource_picker.armed_resource_id == 11);
    (void)click_text(fixture.harness, "Lost.wav", true);
    check_resource_table(state.keysound_catalog.keysound_selection.primary == 13);
    check_resource_table(state.resource_picker.armed_resource_id == 11);
    (void)click_text(fixture.harness, "Unused");
    check_resource_table(state.keysound_filters.keysound_filters.use_filter == 2);
    (void)fixture.harness.frame();
    check_no_resource_table(fixture.harness.drawn_text_center("Used.wav"));
    check_resource_table(fixture.harness.drawn_text_center("Free.wav"));
    check_resource_table(state.resource_picker.armed_resource_id == 11);
    wormedit::ui::keysound_dock_request_t request{};
    worm_ui_frontend_keysound_dock_request(&request);
    check_resource_table(request.use_filter == wormedit::resource_use_filter_t::unused);
}

TEST_CASE("Sound row audition and protected deletion use the standard actions", "[event-harness][resource-table]") {
    sound_fixture_t fixture;
    auto point = action_point(fixture.harness, "ok", 0);
    check_resource_table(has_action(click(fixture.harness, point.first, point.second), WORM_UI_COMPOSE_ACTION_AUDITION_SOUND));
    check_resource_table(worm_ui_nuklear_compose_state().resource_picker.armed_resource_id == 0);
    point = action_point(fixture.harness, "ok", 1);
    check_no_resource_table(has_action(click(fixture.harness, point.first, point.second), WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS));
    point = action_point(fixture.harness, "unused", 1);
    const auto actions = click(fixture.harness, point.first, point.second);
    require_resource_table(has_action(actions, WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS));
    for (const auto& snapshot : actions) {
        const auto& action = snapshot.get();
        if (action.compose_action != WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS) continue;
        require_resource_table(action.compose_sound_resource_ids.size() == 1);

        check_resource_table(action.compose_sound_resource_ids[0].value == 12);
        check_resource_table(action.compose_input_project_id.value == fixture.harness.view().compose.project_id.value);
        check_resource_table(action.compose_input_document_revision == fixture.harness.view().compose.document_revision);
    }
}

TEST_CASE("Images table preserves hidden selection and disarms actual removed resources", "[event-harness][resource-table]") {
    image_fixture_t fixture;
    auto& state = worm_ui_nuklear_compose_state();
    (void)click_text(fixture.harness, "Used.png");
    check_resource_table(state.bga_catalog.bga_selection.primary == 21);
    check_resource_table(state.resource_picker.armed_resource_id == 0);
    (void)click_text(fixture.harness, "Used.png", true);
    check_resource_table(state.resource_picker.armed_resource_id == 21);
    (void)click_text(fixture.harness, "Lost.png", true);
    check_resource_table(state.resource_picker.armed_resource_id == 21);
    (void)click_text(fixture.harness, "Used.png");
    (void)click_text(fixture.harness, "Unused");
    auto& view = fixture.harness.view().compose;
    view.bga_rows.assign(fixture.rows.begin() + 1, fixture.rows.end()); view.bga_rows.resize(2);
    view.bga_matching_ids.assign(fixture.ids.begin() + 1, fixture.ids.end()); view.bga_total_matching = 2;
    (void)fixture.harness.frame();
    check_resource_table(state.bga_catalog.bga_selection.primary == 21);
    check_resource_table(state.resource_picker.armed_resource_id == 21);
    wormedit::ui::bga_dock_request_t request{};
    worm_ui_frontend_bga_dock_request(&request);
    check_resource_table(request.use_filter == wormedit::resource_use_filter_t::unused);
    ++view.document_revision;
    view.bga_all_resource_ids.assign(fixture.ids.begin() + 1, fixture.ids.end()); view.bga_all_resource_ids.resize(2);
    (void)fixture.harness.frame();
    check_resource_table(state.resource_picker.armed_resource_id == 0);
    check_resource_table(state.pointer_capture.draw_source_id == 0);
}

TEST_CASE("Images row deletion protects references and stamps the unused resource", "[event-harness][resource-table]") {
    image_fixture_t fixture;
    auto point = action_point(fixture.harness, "ok", 0);
    check_no_resource_table(has_action(click(fixture.harness, point.first, point.second), WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS));
    point = action_point(fixture.harness, "unused", 0);
    const auto actions = click(fixture.harness, point.first, point.second);
    require_resource_table(has_action(actions, WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS));
    for (const auto& snapshot : actions) {
        const auto& action = snapshot.get();
        if (action.compose_action != WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS) continue;
        require_resource_table(action.compose_sound_resource_ids.size() == 1);

        check_resource_table(action.compose_sound_resource_ids[0].value == 22);
        check_resource_table(action.compose_input_project_id.value == fixture.harness.view().compose.project_id.value);
        check_resource_table(action.compose_input_chart_id.value == fixture.harness.view().compose.chart_id.value);
        check_resource_table(action.compose_input_document_revision == fixture.harness.view().compose.document_revision);
    }
}
