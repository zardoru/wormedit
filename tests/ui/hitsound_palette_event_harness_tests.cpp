#include "ui/nuklear/compose/compose_panel.h"
#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/chrome/chrome.h"
#include "wormedit/nuklear_config.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <memory>

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

TEST_CASE("Icon recording distinguishes a complete button from its visible intersection",
          "[frontend-state][palette-layout]") {
    wormedit::ui::testing::icon_widget_snapshot_t widgets;
    widgets.append("right clipped", {8, 8, 30, 30}, {8, 8, 28, 30});
    widgets.append("bottom clipped", {8, 8, 30, 30}, {8, 8, 30, 29});
    widgets.append("complete", {8, 8, 30, 30}, {8, 8, 30, 30});
    CHECK_FALSE(widgets.is_fully_visible("right clipped"));
    CHECK_FALSE(widgets.is_fully_visible("bottom clipped"));
    CHECK(widgets.is_fully_visible("complete"));
    CHECK(widgets.center_of("right clipped").first == 22);
}

TEST_CASE("Compose palette contains complete note-kind and hitsound buttons at different paddings",
          "[frontend-state][event-harness][hitsound][palette-layout]") {
    const auto padding = GENERATE(0.0f, 3.0f, 4.0f, 8.0f);
    const auto hitsounds = GENERATE(false, true);
    CAPTURE(padding, hitsounds);
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    view.note_kind_palette_visible = true;
    view.allowed_note_kind_mask = 0x7f;
    const std::array samples{wormedit::osu_hitsample_t{}};
    view.hitsounds = wormedit::ui::hitsound_palette_view_t::from(hitsounds, samples);

    // Render the production Compose panel with a local style so the test can vary
    // group padding without changing the frontend's global context.
    nk_user_font font{};
    font.height = 15;
    font.width = [](nk_handle, float, const char*, int length) { return length * 8.0f; };
    nk_context context{};
    REQUIRE(nk_init_default(&context, &font));
    const std::unique_ptr<nk_context, decltype(&nk_free)> cleanup{&context, nk_free};
    wormedit::ui::chrome::apply_theme(&context);
    context.style.window.group_padding = nk_vec2(padding, 4);
    wormedit::ui::testing::icon_widget_snapshot_t widgets;
    const wormedit::ui::testing::icon_widget_recording_scope_t recording{widgets};
    wormedit::ui::workspace_action_t action{};
    nk_input_begin(&context);
    nk_input_end(&context);
    if (nk_begin(&context, "Palette layout", nk_rect(0, 0, 1280, 900), NK_WINDOW_NO_SCROLLBAR)) {
        worm_ui_nuklear_render_compose_panel(&context, &view, &harness.view().timing,
            &action, true, false, nullptr, 0, 0);
    }
    nk_end(&context);

    for (const auto& descriptor : view.note_palette.entries) {
        CAPTURE(descriptor.label);
        CHECK(widgets.is_fully_visible(descriptor.label));
    }
    if (hitsounds) {
        for (const auto* tooltip : {"Whistle (W): off; click toggles", "Finish (E): off; click toggles",
                "Clap (R): off; click toggles", "Normal sample set: Auto; click cycles, right-click chooses",
                "Addition sample set: Auto; click cycles, right-click chooses"}) {
            CAPTURE(tooltip);
            CHECK(widgets.is_fully_visible(tooltip));
        }
    }
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

TEST_CASE("Draw mode places a sample-set control from the armed toolbar bank",
          "[frontend-state][event-harness][sample-controls]") {
    using namespace wormedit;
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    view.viewport_width_pixels = 900;
    view.viewport_height_pixels = 600;
    view.playable_field_left_pixels = 126;
    view.playable_field_right_pixels = 850;
    view.timing_lane_visibility_mask = 1U << static_cast<unsigned>(timing_lane_kind_t::sample_set);
    view.note_paint_mode_active = true;
    const std::array samples{osu_hitsample_t{.normal_set = osu_sample_set_t::drum}};
    view.hitsounds = ui::hitsound_palette_view_t::from(true, samples);
    (void)harness.frame();
    ui::testing_rect_view_t viewport{};
    REQUIRE(worm_ui_frontend_compose_viewport_for_testing(&viewport));
    const auto x = viewport.x + 84.0f / 900 * viewport.width;
    const auto y = viewport.y + 200.0f / 600 * viewport.height;
    const auto frames = test::click_at(harness, x, y);
    const auto inserted = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().timing_stage_action == ui::timing_stage_action_t::quick_insert;
    });
    REQUIRE(inserted != frames.end());
    CHECK(inserted->get().timing_lane == timing_lane_kind_t::sample_set);
    CHECK(inserted->get().timing_primary_numerator_text == "3");
    CHECK(inserted->get().timing_chart_id == view.chart_id);
}
