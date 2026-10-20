#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <array>

namespace {
auto click_hitsound(nuklear_event_harness_t& harness, int x, int y) -> wormedit::hitsound_change_t {
    const auto actions = wormedit::test::click_at(harness, x, y);
    const auto found = std::ranges::find_if(actions, [](const auto& a) { return a.get().compose_hitsound.has_value(); });
    REQUIRE(found != actions.end());
    CHECK(found->get().compose_input_chart_id == harness.view().compose.chart_id);
    return *found->get().compose_hitsound;
}
void open_sample_set_menu(nuklear_event_harness_t& harness) {
    const auto target = harness.drawn_text_center("S");
    REQUIRE(target);
    auto down = wormedit::test::mouse_button(true, target->first, target->second);
    down.button.button = SDL_BUTTON_RIGHT;
    const auto move = wormedit::test::mouse_motion(target->first, target->second);
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
    std::string_view label;
    wormedit::hitsound_field_t field;
    SECTION("mixed whistle") { label = "−"; field = wormedit::hitsound_field_t::whistle; }
    SECTION("normal set") { label = "N"; field = wormedit::hitsound_field_t::normal_set; }
    const auto target = harness.drawn_text_center(label);
    REQUIRE(target);
    const auto change = click_hitsound(harness, target->first, target->second);
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
