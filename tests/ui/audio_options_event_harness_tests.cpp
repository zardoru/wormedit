#include "nuklear_event_harness.h"
#include <catch2/catch_test_macros.hpp>
#include <source_location>

namespace {
void check_audio_page(bool value, const std::source_location caller = std::source_location::current()) {
    INFO("audio page caller line " << caller.line());
    CHECK(value);
}
}
TEST_CASE("Playback audio Options exposes output routing and refresh without applying the draft",
          "[frontend-state][event-harness][audio-options]") {
    nuklear_event_harness_t harness{1280, 1000};
    auto& view = harness.view().options;
    view.visible = true;
    view.category = WORM_UI_OPTIONS_AUDIO;
    view.revision = 51;
    view.audio_devices = {"Test speakers", "Test headphones"};
    (void)harness.frame();
    check_audio_page(harness.drawn_text_center("Output device").has_value());
    const auto refresh = harness.drawn_text_center("Refresh devices");
    REQUIRE(refresh);
    bool refreshed = false;
    for (const auto& frame : wormedit::test::click_at(harness, refresh->first, refresh->second)) {
        if (frame.get().options.kind == WORM_UI_OPTIONS_NONE) continue;
        refreshed = true;
        check_audio_page(frame.get().options.kind == WORM_UI_OPTIONS_REFRESH_AUDIO_DEVICES);
        check_audio_page(frame.get().options.revision == 51);
    }
    check_audio_page(refreshed);
}

TEST_CASE("Playback audio Options selects a named device and reports deferred output changes",
          "[frontend-state][event-harness][audio-options]") {
    nuklear_event_harness_t harness{1280, 1000};
    auto& view = harness.view().options;
    view.visible = true;
    view.category = WORM_UI_OPTIONS_AUDIO;
    view.revision = 52;
    view.audio_devices = {"Test speakers"};
    view.audio_output_pending = true;
    (void)harness.frame();
    check_audio_page(harness.drawn_text_center("Output changes apply after Stop.").has_value());
    const auto picker = harness.drawn_text_center("System default");
    REQUIRE(picker);
    (void)wormedit::test::click_at(harness, picker->first, picker->second);
    const auto device = harness.drawn_text_center("Test speakers");
    REQUIRE(device);
    bool selected = false;
    for (const auto& frame : wormedit::test::click_at(harness, device->first, device->second)) {
        if (frame.get().options.kind != WORM_UI_OPTIONS_EDIT_FIELD) continue;
        selected = true;
        check_audio_page(frame.get().options.field == WORM_UI_OPTIONS_FIELD_OUTPUT_DEVICE);
        check_audio_page(frame.get().options.text == "Test speakers");
    }
    check_audio_page(selected);
}
