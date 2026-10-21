#include "nuklear_event_harness.h"
#include "ui/nuklear/setup_panel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <source_location>

namespace {
void check_chart(bool condition, const std::source_location caller = std::source_location::current()) {
    INFO("assertion caller line " << caller.line());
    CHECK(condition);
}
void require_chart(bool condition, const std::source_location caller = std::source_location::current()) {
    INFO("assertion caller line " << caller.line());
    REQUIRE(condition);
}

struct chart_actions_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    chart_actions_fixture_t() {
        require_chart(harness.ready());
        auto& workspace = harness.view();
        workspace.active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
        workspace.compose.visible = false;
        workspace.metadata.visible = false;
        workspace.setup = {.visible = true, .song_id = {42}, .document_revision = 53,
            .session_generation = 17, .chart_count = 3};
        constexpr std::array names{"10", "20", "30"};
        for (std::size_t index = 0; index < names.size(); ++index)
            workspace.setup.charts.push_back({.chart_id = wormedit::chart_id_t{(index + 1) * 10},
                .name = names[index], .author = "Author", .rating = "Unset", .mode = "7K",
                .output_groups = "Unassigned", .length = "2 s", .diagnostic = ""});
        (void)harness.frame();
    }
    void click(const char* label) {
        const auto center = harness.drawn_text_center(label);
        require_chart(center.has_value());
        (void)wormedit::test::click_at(harness, center->first, center->second);
    }
    auto arrow(bool up, SDL_Keymod modifiers = SDL_KMOD_ALT, bool repeat = false)
        -> workspace_action_snapshot_t {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.type = SDL_EVENT_KEY_DOWN;
        event.key.down = true;
        event.key.repeat = repeat;
        event.key.key = up ? SDLK_UP : SDLK_DOWN;
        event.key.scancode = up ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN;
        return harness.frame(std::array{event}, modifiers);
    }
};
}

TEST_CASE("Project Alt arrows reorder the focused chart with the current stamp",
          "[frontend-state][event-harness][project-charts][chart-reorder]") {
    const bool up = GENERATE(false, true);
    chart_actions_fixture_t f;
    f.click("20");
    const auto frame = f.arrow(up);
    const auto& action = frame.get().project_chart;
    check_chart(action.kind == (up ? WORM_UI_PROJECT_CHART_UP : WORM_UI_PROJECT_CHART_DOWN));
    check_chart(action.chart_id == wormedit::chart_id_t{20});
    check_chart(action.document_revision == 53 && action.session_generation == 17);
    check_chart(frame.get().shortcut_events.empty());
    // Focus follows the stable chart identity after its new order is projected.
    std::swap(f.harness.view().setup.charts[1], f.harness.view().setup.charts[up ? 0 : 2]);
    ++f.harness.view().setup.document_revision;
    check_chart(f.arrow(up, SDL_KMOD_ALT, true).get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
    const auto back = f.arrow(!up);
    check_chart(back.get().project_chart.chart_id == wormedit::chart_id_t{20});
    check_chart(back.get().project_chart.document_revision == 54);
}

TEST_CASE("Project Alt arrows require row focus and exact modifiers",
          "[frontend-state][event-harness][project-charts][chart-reorder]") {
    chart_actions_fixture_t f;
    check_chart(f.arrow(true).get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
    f.click("20");
    const auto modifiers = GENERATE(SDL_KMOD_NONE, SDL_KMOD_CTRL,
        static_cast<SDL_Keymod>(SDL_KMOD_ALT | SDL_KMOD_SHIFT),
        static_cast<SDL_Keymod>(SDL_KMOD_ALT | SDL_KMOD_CTRL));
    check_chart(f.arrow(true, modifiers).get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
}

TEST_CASE("Project chart keyboard reorder is blocked by popups and lost row context",
          "[frontend-state][event-harness][project-charts][chart-reorder]") {
    const auto blocked = GENERATE(0, 1, 2, 3, 4, 5, 6);
    chart_actions_fixture_t f;
    f.click("20");
    auto& workspace = f.harness.view();
    switch (blocked) {
    case 0: workspace.options.visible = true; break;
    case 1: f.click("Set author…"); break;
    case 2: {
        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        (void)f.harness.frame(std::array{event});
        break;
    }
    case 3: ++workspace.setup.session_generation; break;
    case 4: workspace.setup.charts.erase(workspace.setup.charts.begin() + 1); break;
    case 5: workspace.active_tab = WORM_UI_WORKSPACE_TAB_METADATA; workspace.setup.visible = false; break;
    case 6: f.click("⋯"); break;
    }
    (void)f.harness.frame();
    check_chart(f.arrow(true).get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
}

TEST_CASE("Project chart menu contains only open duplicate and delete actions",
          "[frontend-state][event-harness][project-charts][chart-reorder]") {
    chart_actions_fixture_t f;
    f.click("20");
    f.click("⋯");
    (void)f.harness.frame();
    check_chart(f.harness.drawn_text_center("Open in Compose").has_value());
    check_chart(f.harness.drawn_text_center("Delete…").has_value());
    check_chart(!f.harness.drawn_text_center("Move up").has_value());
    check_chart(!f.harness.drawn_text_center("Move down").has_value());
}
