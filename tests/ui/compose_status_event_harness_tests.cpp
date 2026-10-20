#include "nuklear_event_harness.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

TEST_CASE("Compose status shows the canonical selection count without double counting notes",
          "[frontend-state][event-harness][selection-status]") {
    const auto width = GENERATE(800, 1280);
    nuklear_event_harness_t harness{width, 900};
    auto& view = harness.view().compose;
    view.selected_note_count = 2;
    view.selected_bgm_event_count = 1;
    view.selected_object_count = 4; // Includes the three sound objects plus a timing event.
    (void)harness.frame();
    CHECK(harness.drawn_text().find("4 objects selected") != std::string::npos);
    view.selected_object_count = 0; // Legacy note/BGM-only projection fallback.
    (void)harness.frame();
    CHECK(harness.drawn_text().find("3 objects selected") != std::string::npos);
    view.selected_note_count = view.selected_bgm_event_count = 0;
    (void)harness.frame();
    CHECK(harness.drawn_text().find("objects selected") == std::string::npos);
}
