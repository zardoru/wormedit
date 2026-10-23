#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/dock.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
using namespace wormedit;
namespace {
constexpr auto inspector_tip = "Inspector panel (Shift-click to stack; drag to reorder)";
constexpr auto sounds_tip = "Sounds panel (Shift-click to stack; drag to reorder)";
constexpr auto images_tip = "Images panel (Shift-click to stack; drag to reorder)";
struct dock_fixture_t {
    nuklear_event_harness_t harness{1280, 1000};
    dock_fixture_t() {
        REQUIRE(harness.ready());
        harness.view().compose.keysounds_available = true;
        worm_ui_nuklear_compose_state().dock_layout.layout.activate(dock_panel_t::inspector, false);
        (void)harness.frame();
    }
    auto& layout() { return worm_ui_nuklear_compose_state().dock_layout.layout; }
    void click(std::string_view tip, SDL_Keymod modifiers = SDL_KMOD_NONE) {
        const auto [x, y] = harness.widget_center(tip);
        (void)harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)}, modifiers);
        (void)harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(false, x, y)}, modifiers);
        (void)harness.frame();
    }
    auto press(std::string_view tip) -> std::pair<float, float> {
        const auto point = harness.widget_center(tip);
        (void)harness.frame(std::array{test::mouse_motion(point.first, point.second),
                                     test::mouse_button(true, point.first, point.second)});
        return point;
    }
};
}
TEST_CASE("Dock strip chooses solo panels stacks with Shift and hides the last panel", "[event-harness][dock-layout]") {
    dock_fixture_t f;
    f.click(sounds_tip, SDL_KMOD_SHIFT);
    CHECK(f.layout().is_visible(dock_panel_t::inspector));
    CHECK(f.layout().is_visible(dock_panel_t::sounds));
    f.click(images_tip);
    CHECK(f.layout().is_visible(dock_panel_t::images));
    CHECK_FALSE(f.layout().is_visible(dock_panel_t::inspector));
    f.click(images_tip);
    CHECK_FALSE(f.layout().visible_panels()[0]);
    CHECK(f.harness.widget_center(inspector_tip).first > 0);
}
TEST_CASE("Dock strip dragging reorders both stacked panes without chart actions", "[event-harness][dock-layout]") {
    dock_fixture_t f;
    f.click(sounds_tip, SDL_KMOD_SHIFT);
    const auto end = f.harness.widget_center(inspector_tip);
    (void)f.press(sounds_tip);
    (void)f.harness.frame(std::array{test::mouse_motion(end.first, end.second)});
    const auto release = f.harness.frame(std::array{test::mouse_button(false, end.first, end.second)});
    CHECK(f.layout().order().front() == dock_panel_t::sounds);
    CHECK(f.layout().visible_panels()[0] == dock_panel_t::sounds);
    CHECK(release.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK(release.get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
}
TEST_CASE("Dock resize captures defer preference publication until release", "[event-harness][dock-layout]") {
    dock_fixture_t f;
    const auto [x, y] = f.press("Resize dock width");
    CHECK(worm_ui_frontend_dock_layout_is_editing());
    (void)f.harness.frame(std::array{test::mouse_motion(x - 100, y)});
    CHECK(f.layout().width() == 420);
    const auto released = f.harness.frame(std::array{test::mouse_button(false, x - 100, y)});
    CHECK_FALSE(worm_ui_frontend_dock_layout_is_editing());
    CHECK(worm_ui_frontend_dock_layout().width() == 420);
    CHECK(released.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}
TEST_CASE("Dock split captures cancel on focus loss without a late panel action", "[event-harness][dock-layout]") {
    dock_fixture_t f;
    f.click(sounds_tip, SDL_KMOD_SHIFT);
    const auto [x, y] = f.press("Resize dock split");
    (void)f.harness.frame(std::array{test::mouse_motion(x, y + 60)});
    CHECK(f.layout().split() > 0.5f);
    SDL_Event lost{};
    lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    (void)f.harness.frame(std::span{&lost, 1});
    CHECK_FALSE(worm_ui_frontend_dock_layout_is_editing());
    const auto released = f.harness.frame(std::array{test::mouse_button(false, x, y + 60)});
    CHECK(released.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}
TEST_CASE("Dock panes share one horizontal footprint and retain a usable narrow canvas", "[event-harness][dock-layout]") {
    ui_layout_preferences_t layout;
    const std::array available{true, true, true, true, true, true};
    const auto wide = ui::dock_geometry_t::from(1280, 700, layout, available);
    REQUIRE(wide.panes[0]);
    REQUIRE(wide.panes[1]);
    CHECK(wide.panes[0]->bounds.left == wide.panes[1]->bounds.left);
    CHECK(wide.panes[0]->bounds.bottom < wide.panes[1]->bounds.top);
    const auto narrow = ui::dock_geometry_t::from(480, 700, layout, available);
    CHECK_FALSE(narrow.panes[0]);
    CHECK(narrow.reserved_width < 50);
}
TEST_CASE("Markers dock activates existing bookmark navigation with a stamped target", "[event-harness][dock-layout]") {
    dock_fixture_t f;
    f.layout().activate(dock_panel_t::markers, false);
    auto& view = f.harness.view().compose;
    view.bookmark_chooser = {.project_id = view.project_id, .chart_id = view.chart_id,
        .document_revision = view.document_revision,
        .rows = {{.bookmark_id = object_id_t{77}, .display_label = "Chorus marker", .navigation_available = true}},
        .total_count = 1};
    (void)f.harness.frame();
    const auto point = f.harness.drawn_text_center("Chorus marker");
    REQUIRE(point);
    const auto [x, y] = *point;
    const std::array frames{
        f.harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)}),
        f.harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(false, x, y)})};
    const auto action = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK;
    });
    REQUIRE(action != frames.end());
    CHECK(action->get().compose_bookmark_id == object_id_t{77});
    CHECK(action->get().compose_input_chart_id == view.chart_id);
    CHECK(action->get().compose_input_document_revision == view.document_revision);
    CHECK(f.layout().is_visible(dock_panel_t::markers));
}
TEST_CASE("Minimum width Sounds dock keeps its final toolbar control reachable", "[event-harness][dock-layout]") {
    dock_fixture_t f;
    f.layout().activate(dock_panel_t::sounds, false);
    f.layout().set_width(dock_minimum_width);
    f.harness.view().compose.keysound_ids_available = true;
    (void)f.harness.frame();
    constexpr auto layers_tip = "Layers: give the selected sounds a semantic layer, so notes painted from them join it";
    const auto last = f.harness.widget_center(layers_tip);
    CHECK(last.first < f.harness.widget_center(sounds_tip).first - 16);
    const auto frames = f.harness.click_widget(layers_tip);
    CHECK(worm_ui_nuklear_compose_state().dock_layout.sounds_layers_open);
    CHECK(std::ranges::all_of(frames, [](const auto& frame) {
        return frame.get().kind == WORM_UI_WORKSPACE_ACTION_NONE &&
               frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE;
    }));
}
