#include "nuklear_event_harness.h"
#include "ui/nuklear/render/interface_scale.h"
#include <catch2/catch_test_macros.hpp>
#include <source_location>

namespace {
void check_display(bool value, const std::source_location caller = std::source_location::current()) {
    INFO("display options caller line " << caller.line());
    CHECK(value);
}
}

TEST_CASE("Scaled Appearance controls receive native mouse coordinates", "[event-harness][display-options]") {
    nuklear_event_harness_t harness{1600, 1100};
    auto& view = harness.view().options;
    view.visible = true;
    view.category = WORM_UI_OPTIONS_DISPLAY;
    view.revision = 71;
    REQUIRE(worm_ui_frontend_set_interface_scale(150));
    (void)harness.frame();
    const auto toggle = harness.drawn_text_center("Frame timing overlay");
    REQUIRE(toggle);
    bool changed = false;
    for (const auto& frame : wormedit::test::click_at(harness, toggle->first * 1.5f, toggle->second * 1.5f)) {
        const auto& action = frame.get().options;
        if (action.kind != WORM_UI_OPTIONS_EDIT_FIELD) continue;
        changed = true;
        check_display(action.field == WORM_UI_OPTIONS_FIELD_TIMING_OVERLAY);
        check_display(action.boolean);
        check_display(action.revision == 71);
    }
    check_display(changed);
    check_display(worm_ui_frontend_is_title_bar_drag_position(900, 40, 1600));
    check_display(!worm_ui_frontend_is_title_bar_drag_position(1550, 40, 1600));
    check_display(!worm_ui_frontend_set_interface_scale(99));
}

TEST_CASE("Interface scale handles automatic density and invalid requests", "[display-options]") {
    using wormedit::ui::nuklear::interface_scale_t;
    REQUIRE(interface_scale_t::from(0));
    CHECK(interface_scale_t::from(0)->percent == 100);
    CHECK(interface_scale_t::from(150)->logical_extent(1200) == 800);
    CHECK_FALSE(interface_scale_t::from(201));
}
