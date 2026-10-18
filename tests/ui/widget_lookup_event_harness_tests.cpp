#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

TEST_CASE("Icon snapshots require exactly one visible tooltip match",
          "[frontend-state][event-harness][widget-lookup]") {
    using namespace wormedit::ui::testing;
    icon_widget_snapshot_t snapshot;
    CHECK_THROWS_AS(snapshot.center_of("missing"), std::runtime_error);
    const SDL_FRect clip{.x = 0, .y = 0, .w = 100, .h = 100};
    snapshot.append("same", {.x = 0, .y = 0, .w = 10, .h = 10}, clip);
    snapshot.append("same", {.x = 20, .y = 0, .w = 10, .h = 10}, clip);
    CHECK_THROWS_AS(snapshot.center_of("same"), std::runtime_error);
}

TEST_CASE("Icon snapshots locate clipped bounds and exclude invisible cells",
          "[frontend-state][event-harness][widget-lookup]") {
    using namespace wormedit::ui::testing;
    icon_widget_snapshot_t snapshot;
    const SDL_FRect clip{.x = 0, .y = 0, .w = 100, .h = 100};
    snapshot.append("part", {.x = -10, .y = -10, .w = 20, .h = 20}, clip);
    CHECK(snapshot.center_of("part") == std::pair{5.0f, 5.0f});
    snapshot.append("outside", {.x = 110, .y = 0, .w = 10, .h = 10}, clip);
    CHECK_THROWS_AS(snapshot.center_of("outside"), std::runtime_error);
}

TEST_CASE("Harness widget lookup refreshes conditional native icons every frame",
          "[frontend-state][event-harness][widget-lookup]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    constexpr auto tooltip =
        "Moved bar lines keep other objects in place (click to carry later content)";
    harness.view().compose.measure_lane.visible = true;
    (void)harness.frame();
    CHECK(harness.widget_center(tooltip).first > 0.0f);
    harness.view().compose.measure_lane.visible = false;
    (void)harness.frame();
    CHECK_THROWS_AS(harness.widget_center(tooltip), std::runtime_error);
}
