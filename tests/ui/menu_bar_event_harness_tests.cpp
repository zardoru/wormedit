#include "nuklear_event_harness.h"
#include "wormedit/config.h"
#include "sys/commands/menu_command_view.h"
#include "sys/app/frame_timing.h"
#include "ui/nuklear/app/menu_bar.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <source_location>
#include <cstring>

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
    INFO("Menu label " << text << ", tab " << harness.view().active_tab);
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

void check_dispatch(std::string_view menu, std::string_view label, std::string_view command,
                    WormUiWorkspaceTab tab = WORM_UI_WORKSPACE_TAB_COMPOSE) {
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    harness.view().active_tab = tab;
    harness.view().compose.visible = tab == WORM_UI_WORKSPACE_TAB_COMPOSE;
    harness.view().setup.visible = tab == WORM_UI_WORKSPACE_TAB_SETUP;
    harness.view().metadata.visible = tab == WORM_UI_WORKSPACE_TAB_METADATA;
    int invocations = 0;
    const auto registry = wormedit::command_registry_t::from([&](const auto& invocation) {
        check_menu(invocation.command.value == command);
        ++invocations;
        return wormedit::dispatch_outcome_t{true, {}};
    });
    wormedit::sys::command_shortcut_text_t menu_shortcuts_0;
    wormedit::sys::build_menu_commands_view(registry, harness.view(), menu_shortcuts_0);
    (void)harness.frame();
    (void)click_menu(harness, menu);
    (void)harness.frame();
    const auto frames = click_menu(harness, label);
    for (const auto& frame : frames) {
        const auto& action = frame.get();
        if (!action.menu_command_id[0]) continue;
        check_menu(std::string_view{action.menu_command_id} == command);
        check_menu(action.kind == WORM_UI_WORKSPACE_ACTION_NONE);
        check_menu(action.compose_inputs.size() == 0);
        check_menu(action.project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
        check_menu(action.clipboard == WORM_UI_CLIPBOARD_NONE);
        check_menu(action.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
        const auto dispatched = wormedit::invoke_command(registry, {action.menu_command_id},
            {.workspace_active = true, .active_tab = static_cast<wormedit::workspace_tab_t>(tab),
             .visible_tabs = {static_cast<wormedit::workspace_tab_t>(tab)}});
        check_menu(dispatched.value.status == wormedit::command_dispatch_status_t::invoked);
    }
    check_menu(invocations == 1);
}
}

TEST_CASE("Menu commands use one registry invocation across editing chart and playback",
          "[frontend-state][event-harness][menu-bar]") {
    SECTION("New project") { check_dispatch("File", "New project", "document.new_project"); }
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
    SECTION("Hitsound monitoring") {
        check_dispatch("Playback", "Play Hitsounds on Playback", "audio.toggle_playback_audition");
    }
    SECTION("External") { check_dispatch("Playback", "External play from here", "transport.external_play_here"); }
}

TEST_CASE("Menu projections follow remaps unbinding platform modifiers and registry state",
          "[frontend-state][event-harness][menu-bar]") {
    using namespace wormedit;
    auto registry = command_registry_t::from({});
    (void)remap_command(registry, {"history.undo"}, {{shortcut_key_t::u, shortcut_modifier_t::primary | shortcut_modifier_t::shift}});
    (void)remap_command(registry, {"history.redo"}, {});
    (void)update_command_state(registry, {"history.undo"}, false, std::nullopt);
    (void)update_command_state(registry, {"timing.toggle_lock"}, true, true);
    (void)update_command_state(registry, {"audio.toggle_playback_audition"}, true, true);
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    wormedit::sys::command_shortcut_text_t menu_shortcuts_1;
    sys::build_menu_commands_view(registry, harness.view(), menu_shortcuts_1, shortcut_primary_modifier_t::control);
    auto model = ui::build_workspace_menu(harness.view(), false, false, false);
    using Action = ui::workspace_menu_action_t;
    check_menu(std::string_view{item(model, Action::undo).shortcut} == "Ctrl+Shift+U");
    check_menu(!(item(model, Action::undo).enabled));
    check_menu(std::string_view{item(model, Action::redo).shortcut}.empty());
    check_menu(item(model, Action::timing_lock).checked);
    check_menu(item(model, Action::playback_audition).checked);
    wormedit::sys::command_shortcut_text_t menu_shortcuts_2;
    sys::build_menu_commands_view(registry, harness.view(), menu_shortcuts_2, shortcut_primary_modifier_t::meta);
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
    wormedit::sys::command_shortcut_text_t menu_shortcuts_3;
    sys::build_menu_commands_view(command_registry_t::from({}), harness.view(), menu_shortcuts_3);
    const auto model = ui::build_workspace_menu(harness.view(), false, false, true);
    for (const auto action : {Action::open, Action::save_as, Action::close, Action::options,
        Action::copy, Action::cut, Action::paste, Action::select_same_sound, Action::move_to_bgm,
        Action::reset_view, Action::tap_timing, Action::timing_settings, Action::notifications,
        Action::preview, Action::external_preferences, Action::external_start, Action::external_here,
        Action::external_stop, Action::manual, Action::about}) check_menu(item(model, action).title != nullptr);
    check_menu(item(model, Action::frame_timing).checked);
    const auto separator = std::ranges::find(model, Action::none, &ui::workspace_menu_item_t::action);
    require_menu(separator != model.end());
    require_menu(separator != model.begin());
    require_menu(separator + 1 != model.end());
    check_menu((separator - 1)->action == Action::playback_audition);
    check_menu((separator + 1)->action == Action::external_preferences);
    check_menu(!separator->enabled);
    const auto blocked = ui::build_workspace_menu(harness.view(), true, false, false);
    check_menu(std::ranges::none_of(blocked, &ui::workspace_menu_item_t::enabled));
    const auto text_owner = ui::build_workspace_menu(harness.view(), false, true, false);
    check_menu(!(item(text_owner, Action::undo).enabled));
    check_menu(!(item(text_owner, Action::play_start).enabled));
    harness.view().menu_commands.resize(0);
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
    check_menu(harness.drawn_text().find("zardoru") != std::string::npos);
    check_menu(harness.drawn_text().find("github.com/zardoru/wormedit") != std::string::npos);
    check_menu(harness.drawn_text().find("Claude and Codex") != std::string::npos);
    check_menu(harness.drawn_text().find("The Unlicense") != std::string::npos);
    SDL_Event escape{};
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.key = SDLK_ESCAPE;
    escape.key.scancode = SDL_SCANCODE_ESCAPE;
    escape.key.down = true;
    const auto closed = harness.frame(std::span{&escape, 1});
    check_menu(closed.get().compose_inputs.size() == 0);
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
    wormedit::sys::command_shortcut_text_t menu_shortcuts_4;
    wormedit::sys::build_menu_commands_view(wormedit::command_registry_t::from({}), harness.view(), menu_shortcuts_4);
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

TEST_CASE("Help shortcut sheet follows remaps and releases modal lane input on Escape",
          "[frontend-state][event-harness][menu-bar][shortcut-sheet]") {
    using namespace wormedit;
    nuklear_event_harness_t harness{800, 560};
    require_menu(harness.ready());
    auto registry = command_registry_t::from({});
    (void)remap_command(registry, {"view.open_layers"},
        {{shortcut_key_t::u, shortcut_modifier_t::primary | shortcut_modifier_t::shift}});
    (void)remap_command(registry, {"view.toggle_sounds"}, {});
    wormedit::sys::command_shortcut_text_t menu_shortcuts_5;
    sys::build_menu_commands_view(registry, harness.view(), menu_shortcuts_5, shortcut_primary_modifier_t::meta);
    const wormedit::ui::compose_lane_key_binding_t binding{.scancode = SDL_SCANCODE_1, .lane_id = {1}};
    harness.view().compose.lane_key_bindings = {binding};
    harness.view().compose.lane_key_bindings.resize(1);
    harness.view().compose.note_paint_mode_active = true;
    (void)harness.frame();
    (void)click_menu(harness, "Help");
    (void)harness.frame();
    (void)click_menu(harness, "Shortcut cheat sheet");
    (void)harness.frame();
    (void)harness.frame();
    const auto text = harness.drawn_text();
    check_menu(text.find("Show Layers") != std::string::npos);
    check_menu(text.find("Shift+⌘+U") != std::string::npos);
    check_menu(text.find("Unbound") != std::string::npos);
    check_menu(text.find("Timing lock") != std::string::npos);
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.key = SDLK_1;
    key.key.scancode = SDL_SCANCODE_1;
    key.key.down = true;
    const auto blocked = harness.frame(std::span{&key, 1});
    check_menu(blocked.get().compose_inputs.size() == 0);
    check_menu(blocked.get().shortcut_events.size() == 0);
    key.type = SDL_EVENT_KEY_UP;
    key.key.down = false;
    (void)harness.frame(std::span{&key, 1});
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.down = true;
    key.key.key = SDLK_ESCAPE;
    key.key.scancode = SDL_SCANCODE_ESCAPE;
    check_menu(harness.frame(std::span{&key, 1}).get().compose_inputs.size() == 0);
    key.type = SDL_EVENT_KEY_UP;
    key.key.down = false;
    (void)harness.frame(std::span{&key, 1});
    (void)harness.frame();
    check_menu(harness.drawn_text().find("Show Layers") == std::string::npos);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.down = true;
    key.key.key = SDLK_1;
    key.key.scancode = SDL_SCANCODE_1;
    const auto resumed = harness.frame(std::span{&key, 1});
    check_menu(resumed.get().compose_inputs.size() == 1);
    check_menu(resumed.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
}

namespace {
auto menu_pointer(nuklear_event_harness_t& harness, float x, float y, int edge)
    -> workspace_action_snapshot_t {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    SDL_Event button{};
    button.type = edge > 0 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = edge > 0;
    button.button.x = x;
    button.button.y = y;
    const std::array events{motion, button};
    return harness.frame(events);
}
}

TEST_CASE("Menu dismissal captures the held pointer and release above a Project chart row",
          "[frontend-state][event-harness][menu-bar][menu-capture]") {
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    wormedit::ui::project_chart_row_view_t row{.chart_id = wormedit::chart_id_t{10}, .name = "Covered difficulty", .author = "Author",
        .rating = "Unset", .mode = "7K", .output_groups = "Unassigned", .length = "2 s", .diagnostic = ""};
    auto& view = harness.view();
    view.compose.visible = false;
    view.active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
    view.setup = {.visible = true, .song_id = wormedit::song_id_t{1}, .document_revision = 1, .session_generation = 1,
        .charts = {row}, .chart_count = 1};
    (void)harness.frame();
    const auto target = harness.drawn_text_center(row.name);
    require_menu(target.has_value());
    (void)click_menu(harness, "Help");
    (void)harness.frame();
    const auto press = menu_pointer(harness, target->first, target->second, 1);
    check_menu(press.get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
    check_menu(harness.frame().get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    const auto release = menu_pointer(harness, target->first, target->second, 0);
    check_menu(release.get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
    (void)harness.frame();
    check_menu(harness.drawn_text().find("1 selected") == std::string::npos);
    (void)click_menu(harness, row.name);
    (void)harness.frame();
    check_menu(harness.drawn_text().find("1 selected") != std::string::npos);
}

TEST_CASE("Menu dismissal cannot activate the landing New project button",
          "[frontend-state][event-harness][menu-bar][menu-capture]") {
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    harness.view().project_loaded = false;
    harness.view().compose.visible = false;
    (void)harness.frame();
    const auto target = harness.drawn_text_center("New project");
    require_menu(target.has_value());
    (void)click_menu(harness, "Help");
    (void)harness.frame();
    (void)menu_pointer(harness, target->first, target->second, 1);
    (void)harness.frame();
    (void)menu_pointer(harness, target->first, target->second, 0);
    (void)harness.frame();
    check_menu(harness.drawn_text().find("Create a new project") == std::string::npos);
    (void)click_menu(harness, "New project");
    (void)harness.frame();
    check_menu(harness.drawn_text().find("Create a new project") != std::string::npos);
}

TEST_CASE("Menu selection and its release cannot switch the covered Compose workspace tab",
          "[frontend-state][event-harness][menu-bar][menu-capture]") {
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    wormedit::sys::command_shortcut_text_t menu_shortcuts_6;
    wormedit::sys::build_menu_commands_view(wormedit::command_registry_t::from({}), harness.view(), menu_shortcuts_6);
    (void)harness.frame();
    (void)click_menu(harness, "File");
    (void)harness.frame();
    const auto target = harness.drawn_text_center("New project");
    require_menu(target.has_value());
    int invocations = 0;
    for (const auto& frame : click_menu(harness, "New project")) {
        invocations += std::string_view{frame.get().menu_command_id} == "document.new_project";
        check_menu(frame.get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
        check_menu(frame.get().compose_inputs.size() == 0);
    }
    check_menu(invocations == 1);
    (void)harness.frame();
    const auto project = harness.drawn_text_center("Project");
    require_menu(project.has_value());
    bool switched = false;
    for (const auto& frame : click_menu(harness, "Project"))
        switched |= frame.get().kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB;
    check_menu(switched);
}

TEST_CASE("Recent menu and startup entries dispatch guarded normal file opens",
          "[frontend-state][event-harness][menu-bar][recent-files]") {
    nuklear_event_harness_t harness{1280, 900};
    require_menu(harness.ready());
    auto& view = harness.view();
    view.recent_files.resize(2);
    view.recent_files[0] = {"chart.bms", "/tmp/music/chart.bms"};
    view.recent_files[1] = {"project.wormedit", "/tmp/project.wormedit"};
    SECTION("File menu") {
        (void)harness.frame();
        (void)click_menu(harness, "File");
        (void)harness.frame();
        const auto frames = click_menu(harness, "chart.bms");
        const auto opened = std::ranges::find_if(frames, [](const auto& frame) {
            return frame.get().kind == WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
        });
        require_menu(opened != frames.end());
        check_menu(std::string_view{opened->get().project_path} == "/tmp/music/chart.bms");
        check_menu(opened->get().compose_inputs.size() == 0);
        check_menu(opened->get().project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
        (void)harness.frame();
        check_menu(!harness.drawn_text_center("chart.bms").has_value());
    }
    SECTION("Startup bounded controls and recent list") {
        view.project_loaded = false;
        view.compose.visible = false;
        (void)harness.frame();
        const auto button = harness.drawn_text_center("New project");
        require_menu(button.has_value());
        check_menu(button->first > 400 && button->first < 900);
        const auto frames = click_menu(harness, "project.wormedit");
        const auto opened = std::ranges::find_if(frames, [](const auto& frame) {
            return frame.get().kind == WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
        });
        require_menu(opened != frames.end());
        check_menu(std::string_view{opened->get().project_path} == "/tmp/project.wormedit");
    }
    SECTION("Modal ownership disables recent entries") {
        view.options.visible = true;
        const auto model = wormedit::ui::build_workspace_menu(view, true, false, false);
        check_menu(!item(model, wormedit::ui::workspace_menu_action_t::recent_first).enabled);
    }
}

TEST_CASE("File recent history scrolls within a small window and opens the last entry",
          "[frontend-state][event-harness][menu-bar][recent-files]") {
    nuklear_event_harness_t harness{800, 500};
    require_menu(harness.ready());
    std::array<std::string, 100> labels;
    std::array<std::string, 100> paths;
    harness.view().recent_files.resize(labels.size());
    for (std::size_t index = 0; index < labels.size(); ++index) {
        labels[index] = "recent-" + std::to_string(index) + ".wormedit";
        paths[index] = "/tmp/" + labels[index];
        harness.view().recent_files[index] = {labels[index].c_str(), paths[index].c_str()};
    }
    (void)harness.frame();
    (void)click_menu(harness, "File");
    (void)harness.frame();
    const auto first = harness.drawn_text_center(labels.front());
    require_menu(first.has_value());
    check_menu(!harness.drawn_text_center(labels.back()).has_value());
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = first->first;
    motion.motion.y = first->second;
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = -1000;
    const std::array events{motion, wheel};
    (void)harness.frame(events);
    (void)harness.frame();
    const auto last = harness.drawn_text_center(labels.back());
    require_menu(last.has_value());
    check_menu(last->second > 0 && last->second < 500);
    const auto frames = click_menu(harness, labels.back());
    const auto opened = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().kind == WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
    });
    require_menu(opened != frames.end());
    check_menu(std::string_view{opened->get().project_path} == paths.back());
}

TEST_CASE("File Export dispatches the shared command", "[frontend-state][event-harness][menu-bar]") {
    const auto tab = GENERATE(WORM_UI_WORKSPACE_TAB_SETUP, WORM_UI_WORKSPACE_TAB_METADATA,
        WORM_UI_WORKSPACE_TAB_COMPOSE, WORM_UI_WORKSPACE_TAB_AUDIO_SLICES,
        WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE);
    check_dispatch("File", "Export…", "document.export_output_group", tab);
}

TEST_CASE("Menu shortcuts retain long text and reuse the shared cache after remaps",
          "[frontend-state][menu-bar][steady-state]") {
    using namespace wormedit;
    auto registry = command_registry_t::from({});
    const auto command = std::ranges::find_if(registry.commands, [](const auto& entry) {
        return entry.definition.id.value == "history.undo";
    });
    require_menu(command != registry.commands.end());
    ui::workspace_view_t view{};
    sys::command_shortcut_text_t shortcuts;
    for (const auto extra : {shortcut_modifiers_t{},
                             static_cast<shortcut_modifiers_t>(shortcut_modifier_t::meta)}) {
        command->current_shortcuts = {
            {.key = shortcut_key_t::backspace,
             .modifiers = static_cast<shortcut_modifiers_t>(shortcut_modifier_t::control |
                 shortcut_modifier_t::shift | shortcut_modifier_t::alt | extra)},
            {.key = shortcut_key_t::z, .modifiers = 0}};
        sys::build_menu_commands_view(registry, view, shortcuts, shortcut_primary_modifier_t::control);
        const auto row = std::ranges::find_if(view.menu_commands, [](const auto& entry) {
            return entry.id == "history.undo";
        });
        require_menu(row != view.menu_commands.end());
        const auto expected = extra == 0 ? "Ctrl+Shift+Alt+Backspace"
            : "Ctrl+Shift+Alt+Meta+Backspace";
        check_menu(row->shortcut == expected);
        const auto command_index = static_cast<std::size_t>(command - registry.commands.begin());
        check_menu(std::strcmp(shortcuts.first_text(command_index).data(), expected) == 0);
        check_menu(std::string_view{shortcuts.text(command_index)} == std::string{expected} + ", Z");
        const auto before = sys::frame_heap_allocation_snapshot();
        sys::build_menu_commands_view(registry, view, shortcuts, shortcut_primary_modifier_t::control);
        const auto after = sys::frame_heap_allocation_snapshot();
        check_menu(after.count == before.count);
        check_menu(row->shortcut == expected);
    }
}
