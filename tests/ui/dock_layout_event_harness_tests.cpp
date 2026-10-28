#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/dock.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <algorithm>
using namespace wormedit;
namespace {
constexpr auto inspector_tip = "Inspector panel (Shift-click to add; drag to reorder)";
constexpr auto sounds_tip = "Sounds panel (Shift-click to add; drag to reorder)";
constexpr auto images_tip = "Images panel (Shift-click to add; drag to reorder)";
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
    f.layout().set_side_by_side(GENERATE(false, true));
    f.click(sounds_tip, SDL_KMOD_SHIFT);
    const auto expected_width = f.layout().is_side_by_side() ? 370 : 420;
    const auto [x, y] = f.press("Resize dock width");
    CHECK(worm_ui_frontend_dock_layout_is_editing());
    (void)f.harness.frame(std::array{test::mouse_motion(x - 100, y)});
    CHECK(f.layout().width() == expected_width);
    const auto released = f.harness.frame(std::array{test::mouse_button(false, x - 100, y)});
    CHECK_FALSE(worm_ui_frontend_dock_layout_is_editing());
    CHECK(worm_ui_frontend_dock_layout().width() == expected_width);
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
    const std::array available{true, true, true, true, true, true, true};
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

TEST_CASE("Right dock strip switches a pair to full-height side-by-side columns", "[event-harness][dock-layout]") {
    dock_fixture_t f;
    f.click(sounds_tip, SDL_KMOD_SHIFT);
    f.click("Side-by-side panels");
    const auto geometry = ui::dock_geometry_t::from(1280, 700, f.layout(), {true, true, true, true, true, true, true});
    REQUIRE(geometry.is_side_by_side());
    CHECK(f.layout().is_side_by_side());
    CHECK(geometry.panes[0]->bounds.top == geometry.panes[1]->bounds.top);
    CHECK(geometry.panes[0]->bounds.bottom == geometry.panes[1]->bounds.bottom);
    f.click("Side-by-side panels");
    CHECK_FALSE(f.layout().is_side_by_side());
}
TEST_CASE("Side-by-side dock geometry preserves minimum column widths", "[event-harness][dock-layout]") {
    ui_layout_preferences_t layout;
    layout.set_side_by_side(true);
    layout.set_split(0.2f);
    const auto width = GENERATE(1000.0f, 1280.0f);
    const auto geometry = ui::dock_geometry_t::from(width, 700, layout, {true, true, true, true, true, true, true});
    REQUIRE(geometry.is_side_by_side());
    CHECK(geometry.panes[0]->bounds.right - geometry.panes[0]->bounds.left >= dock_minimum_width);
    CHECK(geometry.panes[1]->bounds.right - geometry.panes[1]->bounds.left >= dock_minimum_width);
    CHECK(geometry.panes[0]->bounds.bottom == geometry.panes[1]->bounds.bottom);
}
TEST_CASE("Narrow windows temporarily stack columns and restore them when widened", "[event-harness][dock-layout]") {
    ui_layout_preferences_t layout;
    layout.set_side_by_side(true);
    const std::array available{true, true, true, true, true, true, true};
    const auto narrow = ui::dock_geometry_t::from(700, 700, layout, available);
    REQUIRE(narrow.panes[1]);
    CHECK_FALSE(narrow.is_side_by_side());
    CHECK(layout.is_side_by_side());
    CHECK(ui::dock_geometry_t::from(1280, 700, layout, available).is_side_by_side());
}
TEST_CASE("Column divider drags start from the rendered split and publish on release", "[event-harness][dock-layout]") {
    dock_fixture_t f;
    f.click(sounds_tip, SDL_KMOD_SHIFT);
    f.layout().set_split(0.2f);
    f.click("Side-by-side panels");
    const auto [x, y] = f.press("Resize dock split");
    CHECK(worm_ui_frontend_dock_layout_is_editing());
    (void)f.harness.frame(std::array{test::mouse_motion(x + 20, y)});
    CHECK(f.layout().split() > 0.375f);
    const auto release = f.harness.frame(std::array{test::mouse_button(false, x + 20, y)});
    CHECK_FALSE(worm_ui_frontend_dock_layout_is_editing());
    CHECK(worm_ui_frontend_dock_layout().is_side_by_side());
    CHECK(release.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}
TEST_CASE("Close confirmation blocks the dock arrangement toggle", "[event-harness][dock-layout]") {
    dock_fixture_t f;
    f.harness.view().close_confirmation.visible = true;
    (void)f.harness.frame();
    f.click("Side-by-side panels");
    CHECK_FALSE(f.layout().is_side_by_side());
}

TEST_CASE("Side-by-side columns leave space for the left BMS dock", "[event-harness][dock-layout]") {
    ui_layout_preferences_t layout;
    layout.set_side_by_side(true);
    const std::array available{true, true, true, true, true, true, true};
    const auto narrow = ui::dock_geometry_t::from(800, 700, layout, available, 268);
    CHECK_FALSE(narrow.is_side_by_side());
    const auto wide = ui::dock_geometry_t::from(1280, 700, layout, available, 328);
    REQUIRE(wide.is_side_by_side());
    CHECK(wide.panes[0]->bounds.left >= 328 + 200);
    CHECK(layout.is_side_by_side());
}

TEST_CASE("Docks shrink before the field drops below its minimum width", "[event-harness][dock-layout]") {
    ui_layout_preferences_t layout;
    layout.set_width(400);
    const std::array available{true, true, true, true, true, true, true};
    const auto geometry = ui::dock_geometry_t::from(1000, 700, layout, available, 268);
    REQUIRE(geometry.panes[0]);
    const auto field = 1000 - 268 - geometry.reserved_width;
    CHECK(field >= 400);
    CHECK(geometry.panes[0]->bounds.right - geometry.panes[0]->bounds.left >= dock_minimum_width);
    // Too narrow for the dock beside the left panel: the dock reports no panes so the caller can drop the branch panel.
    CHECK_FALSE(ui::dock_geometry_t::from(900, 700, layout, available, 268).panes[0]);
    CHECK(ui::dock_geometry_t::from(900, 700, layout, available).panes[0]);
}

TEST_CASE("Compact docks preserve the field without overwriting preferred panels", "[event-harness][dock-layout]") {
    ui_layout_preferences_t layout;
    const auto saved = layout;
    const std::array available{true, true, true, true, true, true, true};
    const auto compact = ui::dock_geometry_t::from(640, 240, layout, available);
    CHECK_FALSE(compact.panes[0]);
    CHECK_FALSE(compact.panes[1]);
    CHECK(compact.reserved_width < 50);
    CHECK(layout == saved);
    const auto restored = ui::dock_geometry_t::from(1280, 700, layout, available);
    REQUIRE(restored.panes[0]);
    REQUIRE(restored.panes[1]);
}

TEST_CASE("Compact dock can show an explicitly selected sole panel", "[event-harness][dock-layout]") {
    ui_layout_preferences_t layout;
    layout.activate(dock_panel_t::sounds, false);
    const auto geometry = ui::dock_geometry_t::from(640, 120, layout, {true, true, true, true, true, true, true});
    CHECK(geometry.is_visible(dock_panel_t::sounds));
    CHECK_FALSE(geometry.panes[1]);
}

TEST_CASE("Compact Compose keeps the complete toolbar inside the viewport", "[event-harness][dock-layout]") {
    nuklear_event_harness_t harness{640, 360};
    REQUIRE(harness.ready());
    (void)harness.frame();
    for (const auto* tip : {"Zoom out", "Reset view", "Scroll BGM columns right", "Play sound on click"}) {
        const auto [x, y] = harness.widget_center(tip);
        CHECK((x > 0 && x < 640));
        CHECK((y > 0 && y < 360));
    }
}

TEST_CASE("Compact toolbar leaves useful chart height at the maximum UI scale", "[event-harness][dock-layout]") {
    nuklear_event_harness_t harness{640, 360};
    REQUIRE(harness.ready());
    (void)harness.frame();
    wormedit::ui::testing_rect_view_t viewport{};
    REQUIRE(worm_ui_frontend_compose_viewport_for_testing(&viewport));
    CHECK(viewport.height >= 100);
}
namespace {
constexpr auto mixer_tip = "Mixer panel (Shift-click to add; drag to reorder)";
auto requested_mix(const auto& frames) -> std::optional<preview_mix_t> {
    for (const auto& frame : frames)
        if (frame.get().preview_mix) return frame.get().preview_mix;
    return std::nullopt;
}
struct mixer_fixture_t : dock_fixture_t {
    mixer_fixture_t() {
        harness.view().compose.preview_mix_available = true;
        (void)harness.frame();
        click(mixer_tip);
    }
    // Clicks one control and publishes the requested mix back as the next view, like the app.
    auto toggle(std::string_view tip) -> std::optional<preview_mix_t> {
        const auto mix = requested_mix(harness.click_widget(tip));
        if (mix) harness.view().compose.preview_mix = *mix;
        (void)harness.frame();
        return mix;
    }
    auto reset_is_whole_default_mix() -> bool {
        const auto point = harness.drawn_text_center("Reset");
        if (!point) return false;
        const auto [x, y] = *point;
        const std::array frames{
            harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)}),
            harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(false, x, y)})};
        const auto mix = requested_mix(frames);
        return mix && mix->is_default() && std::ranges::all_of(frames, [](const auto& frame) {
            return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE;
        });
    }
};
}
TEST_CASE("Mixer dock requests category mute, additive solo and reset as whole mixes", "[event-harness][dock-layout][preview-mix]") {
    mixer_fixture_t f;
    REQUIRE(f.layout().is_visible(dock_panel_t::mixer));
    CHECK(f.harness.drawn_text().find("Playback only") != std::string::npos);
    preview_mix_t expected;
    expected.bus(mix_bus_t::bgm).muted = true;
    CHECK(f.toggle("Mute BGM") == expected);
    expected.bus(mix_bus_t::keysounds).solo = true;
    CHECK(f.toggle("Solo Keysounds (additive)") == expected);
    CHECK(f.reset_is_whole_default_mix());
}
TEST_CASE("Mixer dock button stays unavailable without live preview output", "[event-harness][dock-layout][preview-mix]") {
    dock_fixture_t f;
    f.harness.view().compose.preview_mix_available = false;
    (void)f.harness.frame();
    f.click(mixer_tip);
    CHECK_FALSE(f.layout().is_visible(dock_panel_t::mixer));
}
