#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace {
auto make_group(std::size_t index, const char* name) -> wormedit::ui::project_output_group_view_t {
    return {.document_index = index, .name = name, .family = wormedit::format_family_t::bms,
            .path = "song/normal.bme", .chart_ids = {wormedit::chart_id_t{2}},
            .preview_status = "Preview not run"};
}

struct export_menu_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    export_menu_fixture_t() {
        REQUIRE(harness.ready());
        auto& view = harness.view();
        view.active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
        view.compose.visible = false;
        view.setup.visible = true;
        view.setup.song_id = wormedit::song_id_t{1};
        view.setup.document_revision = 1;
        view.setup.project_has_native_path = true;
        view.setup.charts = {{.chart_id = wormedit::chart_id_t{2}, .name = "Normal", .author = "Author",
            .rating = "4", .mode = "BME 7K", .output_groups = "BMS", .length = "2 s", .diagnostic = ""}};
        view.setup.chart_count = 1;
        view.setup.output_groups = {make_group(0, "First"), make_group(1, "Second"), make_group(2, "Third")};
        (void)harness.frame();
    }
    // The topmost (or leftmost) drawn copy of a label; card labels repeat per card.
    [[nodiscard]] auto first_center(std::string_view text, bool leftmost = false) const
        -> std::pair<float, float> {
        std::pair<float, float> found{0.0f, 0.0f};
        float best = std::numeric_limits<float>::max();
        for (const auto& drawn : harness.drawn_text_bounds()) {
            const auto key = leftmost ? drawn.x : drawn.y;
            if (drawn.text != text || key >= best) continue;
            best = key;
            found = {drawn.x + drawn.width / 2.0f, drawn.y + drawn.height / 2.0f};
        }
        REQUIRE(best != std::numeric_limits<float>::max());
        return found;
    }
    void open_first_menu() {
        const auto dots = first_center("⋯");
        (void)wormedit::test::click_at(harness, dots.first, dots.second);
    }
    // The second card's copy of a repeated card label.
    [[nodiscard]] auto second_center_y(std::string_view text) const -> float {
        float first = std::numeric_limits<float>::max();
        float second = first;
        for (const auto& drawn : harness.drawn_text_bounds()) {
            if (drawn.text != text) continue;
            const auto center = drawn.y + drawn.height / 2.0f;
            second = std::min(second, std::max(first, center));
            first = std::min(first, center);
        }
        return second;
    }
    [[nodiscard]] auto menu_drawn() const -> bool {
        return harness.drawn_text_center("Show plan").has_value();
    }
};
} // namespace

TEST_CASE("Export target menu stays drawn while the pointer rests on its button",
          "[frontend-state][event-harness][export-target-menu]") {
    export_menu_fixture_t f;
    f.open_first_menu();
    // The button's tooltip used to take the window's only popup slot every other frame.
    for (int frame = 0; frame < 4; ++frame) {
        (void)f.harness.frame();
        INFO("frame " << frame);
        CHECK(f.menu_drawn());
        CHECK(f.harness.drawn_text().find("Export target actions") == std::string::npos);
    }
}

TEST_CASE("Export target menu rows do not click the card underneath",
          "[frontend-state][event-harness][export-target-menu]") {
    export_menu_fixture_t f;
    const auto second_export_y = f.second_center_y("Export");
    const auto export_x = f.first_center("Export").first;
    f.open_first_menu();
    (void)f.harness.frame();
    REQUIRE(f.menu_drawn());
    // The menu's Preview row lies over the second card's Export button.
    const auto row = f.first_center("Preview", true);
    REQUIRE(std::abs(row.second - second_export_y) < 14.0f);
    const auto frames = wormedit::test::click_at(f.harness, export_x, row.second);
    for (const auto& frame : frames) CHECK(frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    CHECK(std::ranges::any_of(frames, [](const auto& frame) {
        return frame.get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW;
    }));
}
