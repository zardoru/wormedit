#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <array>

namespace {
auto click_hitsound(nuklear_event_harness_t& harness, int x, int y) -> wormedit::hitsound_change_t {
    const auto actions = wormedit::test::click_at(harness, x, y);
    const auto found = std::ranges::find_if(actions, [](const auto& a) { return a.get().compose_hitsound.has_value(); });
    REQUIRE(found != actions.end());
    CHECK(found->get().compose_input_chart_id.value == harness.view().compose.chart_id.value);
    return *found->get().compose_hitsound;
}
void open_sample_set_menu(nuklear_event_harness_t& harness) {
    const auto target = harness.widget_center("Normal sample set: Soft; click cycles, right-click chooses");
    auto down = wormedit::test::mouse_button(true, target.first, target.second);
    down.button.button = SDL_BUTTON_RIGHT;
    const auto move = wormedit::test::mouse_motion(target.first, target.second);
    const std::array events{move, down};
    (void)harness.frame(events);
    down.type = SDL_EVENT_MOUSE_BUTTON_UP; down.button.down = false;
    (void)harness.frame(std::span{&down, 1});
    REQUIRE(worm_ui_nuklear_compose_state().hitsound_menu.menu.open);
}
}

TEST_CASE("Hitsound palette emits mixed toggles and sample set cycling without note-kind tiles",
          "[frontend-state][event-harness][hitsound]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    view.note_kind_palette_visible = false;
    view.lane_key_legend_visible = false;
    const std::array samples{wormedit::osu_hitsample_t{.normal_set = wormedit::osu_sample_set_t::normal},
        wormedit::osu_hitsample_t{.whistle = true, .normal_set = wormedit::osu_sample_set_t::normal}};
    view.hitsounds = wormedit::ui::hitsound_palette_view_t::from(true, samples);
    (void)harness.frame();
    std::string_view tooltip;
    wormedit::hitsound_field_t field;
    SECTION("mixed whistle") { tooltip = "Whistle (W): mixed; click toggles"; field = wormedit::hitsound_field_t::whistle; }
    SECTION("normal set") { tooltip = "Normal sample set: Normal; click cycles, right-click chooses"; field = wormedit::hitsound_field_t::normal_set; }
    const auto target = harness.widget_center(tooltip);
    const auto change = click_hitsound(harness, target.first, target.second);
    CHECK(change.field == field);
    CHECK_FALSE(change.value);
}

TEST_CASE("Hitsound icons expose addition toggle states and both sample-set selectors",
          "[frontend-state][event-harness][hitsound]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    view.note_kind_palette_visible = false;
    view.lane_key_legend_visible = false;
    const std::array samples{wormedit::osu_hitsample_t{.finish = true,
        .normal_set = wormedit::osu_sample_set_t::soft, .addition_set = wormedit::osu_sample_set_t::drum}};
    view.hitsounds = wormedit::ui::hitsound_palette_view_t::from(true, samples);
    (void)harness.frame();
    std::string_view tooltip;
    wormedit::hitsound_field_t field;
    SECTION("off whistle") { tooltip = "Whistle (W): off; click toggles"; field = wormedit::hitsound_field_t::whistle; }
    SECTION("on finish") { tooltip = "Finish (E): on; click toggles"; field = wormedit::hitsound_field_t::finish; }
    SECTION("off clap") { tooltip = "Clap (R): off; click toggles"; field = wormedit::hitsound_field_t::clap; }
    SECTION("addition set") { tooltip = "Addition sample set: Drum; click cycles, right-click chooses"; field = wormedit::hitsound_field_t::addition_set; }
    const auto target = harness.widget_center(tooltip);
    const auto change = click_hitsound(harness, target.first, target.second);
    CHECK(change.field == field);
    CHECK_FALSE(change.value);
}

TEST_CASE("Hitsound sample set context menu picks a value and closes when occluded",
          "[frontend-state][event-harness][hitsound]") {
    nuklear_event_harness_t harness{1280, 900};
    auto& view = harness.view().compose;
    view.lane_key_legend_visible = false;
    const std::array samples{wormedit::osu_hitsample_t{.normal_set = wormedit::osu_sample_set_t::soft}};
    view.hitsounds = wormedit::ui::hitsound_palette_view_t::from(true, samples);
    (void)harness.frame();
    open_sample_set_menu(harness);
    SECTION("choose") {
        const auto drum = harness.drawn_text_center("Drum");
        INFO(harness.drawn_text());
        REQUIRE(drum);
        const auto change = click_hitsound(harness, drum->first, drum->second);
        CHECK(change.value == 3);
    }
    SECTION("Options") { harness.view().options.visible = true; (void)harness.frame(); }
    CHECK_FALSE(worm_ui_nuklear_compose_state().hitsound_menu.menu.open);
}

TEST_CASE("Note hitsound details retain custom fields and explain inherited sets and gain",
          "[frontend-state][hitsound]") {
    const wormedit::osu_hitsample_t sample{.whistle = true, .finish = true, .clap = true,
        .addition_set = wormedit::osu_sample_set_t::drum, .filename = "custom-hit.wav"};
    const auto note = wormedit::ui::hitsound_note_view_t::from(sample,
        {.normal_set = wormedit::osu_sample_set_t::soft, .custom_index = 7, .volume = 65}, sample.filename);
    const auto details = note.details();
    REQUIRE(details.size() == 4);
    CHECK(details[0] == "Hitsounds · normal set: Auto → Soft · addition set: Drum");
    CHECK(details[1] == "Additions: whistle finish clap");
    CHECK(details[2] == "Custom index: 7 (inherited) · volume: 65% (inherited)");
    CHECK(details[3] == "Custom sample: custom-hit.wav");
}
