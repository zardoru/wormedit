#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <algorithm>

TEST_CASE("Measures dock captures an owned exact ratio with its selected rows", "[event-harness][measure-length]") {
    nuklear_event_harness_t harness{1280, 1000};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    view.measure_rows = {{.measure_index = 0, .number = 1}, {.measure_index = 5, .number = 6}};
    view.measure_row_ids = {0, 5};
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.layout.activate(wormedit::dock_panel_t::measures, false);
    state.measures_dock.measure_selection.selected = {0, 5};
    std::snprintf(state.measures_dock.measures_length_numerator.data(), 32, "3");
    std::snprintf(state.measures_dock.measures_length_denominator.data(), 32, "2");
    (void)harness.frame();
    const auto set = harness.drawn_text_center("Set");
    REQUIRE(set);
    const auto frames = wormedit::test::click_at(harness, set->first, set->second);
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURES_LENGTH;
    });
    REQUIRE(found != frames.end());
    CHECK(found->get().compose_measure_ids == std::vector<std::uint64_t>{0, 5});
    CHECK(found->get().compose_measure_length_numerator_text == "3");
    std::snprintf(state.measures_dock.measures_length_numerator.data(), 32, "9");
    CHECK(found->get().compose_measure_length_numerator_text == "3");
}
