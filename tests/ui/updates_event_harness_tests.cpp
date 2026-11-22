#include "nuklear_event_harness.h"
#include "ui/nuklear/app/menu_bar.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <string_view>

namespace {
using kind_t = wormedit::ui::updates_request_t::kind_t;

auto click_text(nuklear_event_harness_t& harness, std::string_view text)
    -> std::array<workspace_action_snapshot_t, 2> {
    const auto location = harness.drawn_text_center(text);
    REQUIRE(location.has_value());
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = location->first;
    motion.motion.y = location->second;
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = true;
    button.button.clicks = 1;
    button.button.x = location->first;
    button.button.y = location->second;
    const std::array down{motion, button};
    auto pressed = harness.frame(down);
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.down = false;
    const std::array up{motion, button};
    return {pressed, harness.frame(up)};
}

auto answered(const std::array<workspace_action_snapshot_t, 2>& frames) -> const wormedit::ui::updates_request_t* {
    for (const auto& frame : frames)
        if (frame.get().options.updates.kind == kind_t::first_run_answer) return &frame.get().options.updates;
    return nullptr;
}
} // namespace

TEST_CASE("the first-run update question answers yes or no", "[frontend-state][event-harness][first-run]") {
    for (const auto& [label, choice] : {std::pair{"Check for updates", "yes"}, std::pair{"Not now", "no"}}) {
        nuklear_event_harness_t harness{1280, 900};
        REQUIRE(harness.ready());
        harness.view().updates.first_run_step = "updates";
        (void)harness.frame();
        (void)harness.frame(); // The dialog fits its content from the previous frame.
        CHECK(harness.drawn_text().find("Check for updates?") != std::string::npos);
        const auto frames = click_text(harness, label);
        const auto* request = answered(frames);
        REQUIRE(request != nullptr);
        CHECK(request->step == "updates");
        CHECK(request->choice == choice);
    }
}

TEST_CASE("no first-run question is drawn when none is pending", "[frontend-state][event-harness][first-run]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    (void)harness.frame();
    CHECK(harness.drawn_text().find("Check for updates?") == std::string::npos);
}

TEST_CASE("Help offers the update check only when the build can reach the network",
          "[frontend-state][event-harness][updates]") {
    using Action = wormedit::ui::workspace_menu_action_t;
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    const auto has = [](const auto& model, Action action) {
        return std::ranges::find(model, action, &wormedit::ui::workspace_menu_item_t::action) != model.end();
    };
    CHECK_FALSE(has(wormedit::ui::build_workspace_menu(harness.view(), false, false, false), Action::check_updates));

    harness.view().updates.check_available = true;
    auto model = wormedit::ui::build_workspace_menu(harness.view(), false, false, false);
    CHECK(has(model, Action::check_updates));
    CHECK_FALSE(has(model, Action::download_update));

    harness.view().updates.update_available = true;
    model = wormedit::ui::build_workspace_menu(harness.view(), false, false, false);
    CHECK(has(model, Action::download_update));
}
