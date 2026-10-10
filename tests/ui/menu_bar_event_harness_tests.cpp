#include "nuklear_event_harness.h"
#include "wormedit/config.h"
#include "sys/menu_command_projection.h"
#include "ui/nuklear/menu_bar.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <string_view>
#include <source_location>

namespace {
void check_menu(bool condition, const std::source_location location = std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}
void require_menu(bool condition, const std::source_location location = std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    REQUIRE(condition);
}

auto click_menu(nuklear_event_harness_t& harness, std::string_view text)
    -> std::array<workspace_action_snapshot_t, 2> {
    const auto location = harness.drawn_text_center(text);
    require_menu(location.has_value());
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

auto item(const wormedit::ui::workspace_menu_t& model, wormedit::ui::workspace_menu_action_t action)
    -> const wormedit::ui::workspace_menu_item_t& {
    const auto found = std::ranges::find(model, action, &wormedit::ui::workspace_menu_item_t::action);
    require_menu(found != model.end());
    return *found;
}

void check_dispatch(std::string_view menu, std::string_view label, std::string_view command) {
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    int invocations = 0;
    const auto registry = wormedit::make_default_editor_command_registry([&](const auto& invocation) {
        check_menu(invocation.command.value == command);
        ++invocations;
        return wormedit::command_invocation_result_t{true, {}};
    });
    wormedit::sys::project_menu_commands(registry, harness.view());
    (void)harness.frame();
    (void)click_menu(harness, menu);
    (void)harness.frame();
    const auto frames = click_menu(harness, label);
    for (const auto& frame : frames) {
        const auto& action = frame.get();
        if (!action.menu_command_id[0]) continue;
        check_menu(std::string_view{action.menu_command_id} == command);
        check_menu(action.clipboard == WORM_UI_CLIPBOARD_NONE);
        check_menu(action.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
        const auto dispatched = wormedit::invoke_command(registry, {action.menu_command_id},
            {.workspace_active = true, .active_tab = wormedit::workspace_tab_t::compose,
             .visible_tabs = {wormedit::workspace_tab_t::compose}});
        check_menu(dispatched.status == wormedit::command_dispatch_status_t::invoked);
    }
    check_menu(invocations == 1);
}
}

TEST_CASE("Menu commands use one registry invocation across editing chart and playback",
          "[frontend-state][event-harness][menu-bar]") {
    SECTION("Undo") { check_dispatch("Edit", "Undo", "history.undo"); }
    SECTION("Redo") { check_dispatch("Edit", "Redo", "history.redo"); }
    SECTION("Delete") { check_dispatch("Edit", "Delete", "edit.delete"); }
    SECTION("Select all") { check_dispatch("Edit", "Select all", "selection.select_all"); }
    SECTION("Invert") { check_dispatch("Edit", "Invert selection", "selection.invert"); }
    SECTION("Clear") { check_dispatch("Edit", "Clear selection", "selection.clear"); }
    SECTION("New chart") { check_dispatch("Chart", "New chart", "document.new_chart"); }
    SECTION("BPM") { check_dispatch("Chart", "Insert BPM change", "timing.insert_bpm"); }
    SECTION("SV") { check_dispatch("Chart", "Insert scroll velocity", "timing.insert_sv"); }
    SECTION("Play") { check_dispatch("Playback", "Play from cursor / Pause", "transport.play_pause"); }
    SECTION("Start") { check_dispatch("Playback", "Play from start", "transport.play_from_start"); }
    SECTION("Stop") { check_dispatch("Playback", "Stop", "transport.stop"); }
    SECTION("External") { check_dispatch("Playback", "External play from here", "transport.external_play_here"); }
}

TEST_CASE("Menu projections follow remaps unbinding platform modifiers and registry state",
          "[frontend-state][event-harness][menu-bar]") {
    using namespace wormedit;
    auto registry = make_default_editor_command_registry({});
    (void)remap_command(registry, {"history.undo"}, {{shortcut_key_t::u, shortcut_modifier_t::primary | shortcut_modifier_t::shift}});
    (void)remap_command(registry, {"history.redo"}, {});
    (void)update_command_state(registry, {"history.undo"}, false, std::nullopt);
    (void)update_command_state(registry, {"timing.toggle_lock"}, true, true);
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    sys::project_menu_commands(registry, harness.view(), shortcut_primary_modifier_t::control);
    auto model = ui::build_workspace_menu(harness.view(), false, false, false);
    using Action = ui::workspace_menu_action_t;
    check_menu(std::string_view{item(model, Action::undo).shortcut} == "Ctrl+Shift+U");
    check_menu(!(item(model, Action::undo).enabled));
    check_menu(std::string_view{item(model, Action::redo).shortcut}.empty());
    check_menu(item(model, Action::timing_lock).checked);
    sys::project_menu_commands(registry, harness.view(), shortcut_primary_modifier_t::meta);
    model = ui::build_workspace_menu(harness.view(), false, false, false);
    check_menu(std::string_view{item(model, Action::undo).shortcut} == "Shift+⌘+U");
    (void)harness.frame();
    (void)click_menu(harness, "Edit");
    (void)harness.frame();
    check_menu(harness.drawn_text().find("Shift+⌘+U") != std::string::npos);
    const auto disabled = click_menu(harness, "Undo");
    for (const auto& frame : disabled) check_menu(frame.get().menu_command_id[0] == '\0');
}

TEST_CASE("Menu groups preserve existing actions and block missing or modal commands",
          "[frontend-state][event-harness][menu-bar]") {
    using namespace wormedit;
    using Action = ui::workspace_menu_action_t;
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    sys::project_menu_commands(make_default_editor_command_registry({}), harness.view());
    const auto model = ui::build_workspace_menu(harness.view(), false, false, true);
    for (const auto action : {Action::open, Action::save_as, Action::close, Action::options,
        Action::copy, Action::cut, Action::paste, Action::select_same_sound, Action::move_to_bgm,
        Action::reset_view, Action::tap_timing, Action::timing_settings, Action::notifications,
        Action::preview, Action::external_preferences, Action::external_start, Action::external_here,
        Action::external_stop, Action::about}) check_menu(item(model, action).title != nullptr);
    check_menu(item(model, Action::frame_timing).checked);
    const auto blocked = ui::build_workspace_menu(harness.view(), true, false, false);
    check_menu(std::ranges::none_of(blocked, &ui::workspace_menu_item_t::enabled));
    const auto text_owner = ui::build_workspace_menu(harness.view(), false, true, false);
    check_menu(!(item(text_owner, Action::undo).enabled));
    check_menu(!(item(text_owner, Action::play_start).enabled));
    harness.view().menu_command_count = 0;
    const auto missing = ui::build_workspace_menu(harness.view(), false, false, false);
    check_menu(!(item(missing, Action::undo).enabled));
}

TEST_CASE("About shows the build and releases modal input on Escape",
          "[frontend-state][event-harness][menu-bar]") {
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    (void)harness.frame();
    for (const auto* title : wormedit::ui::workspace_menu_titles)
        check_menu(harness.drawn_text_center(title).has_value());
    (void)click_menu(harness, "Help");
    (void)harness.frame();
    (void)click_menu(harness, "About WormEdit");
    (void)harness.frame();
    check_menu(harness.drawn_text().find("WormEdit " WORMEDIT_VERSION) != std::string::npos);
    check_menu(harness.drawn_text().find("Build test") != std::string::npos);
    SDL_Event escape{};
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.key = SDLK_ESCAPE;
    escape.key.scancode = SDL_SCANCODE_ESCAPE;
    escape.key.down = true;
    const auto closed = harness.frame(std::span{&escape, 1});
    check_menu(closed.get().compose_input_count == 0);
    escape.type = SDL_EVENT_KEY_UP;
    escape.key.down = false;
    (void)harness.frame(std::span{&escape, 1});
    (void)harness.frame();
    check_menu(harness.drawn_text().find("Build test") == std::string::npos);
}

TEST_CASE("All six menu titles switch without dispatching the covered menu",
          "[frontend-state][event-harness][menu-bar]") {
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    wormedit::sys::project_menu_commands(wormedit::make_default_editor_command_registry({}), harness.view());
    (void)harness.frame();
    constexpr std::array labels{"Open chart or project...", "Undo", "Player preview", "New chart",
                                "Play from cursor / Pause", "About WormEdit"};
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const auto frames = click_menu(harness, wormedit::ui::workspace_menu_titles[i]);
        for (const auto& frame : frames) check_menu(frame.get().menu_command_id[0] == '\0');
        (void)harness.frame();
        check_menu(harness.drawn_text_center(labels[i]).has_value());
    }
}
