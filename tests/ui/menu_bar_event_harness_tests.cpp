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
        check_menu(action.kind == WORM_UI_WORKSPACE_ACTION_NONE);
        check_menu(action.compose_input_count == 0);
        check_menu(action.project_chart.kind == WORM_UI_PROJECT_CHART_NONE);
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
    const auto separator = std::ranges::find(model, Action::none, &ui::workspace_menu_item_t::action);
    require_menu(separator != model.end());
    require_menu(separator != model.begin());
    require_menu(separator + 1 != model.end());
    check_menu((separator - 1)->action == Action::metronome);
    check_menu((separator + 1)->action == Action::external_preferences);
    check_menu(!separator->enabled);
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

TEST_CASE("Help shortcut sheet follows remaps and releases modal lane input on Escape",
          "[frontend-state][event-harness][menu-bar][shortcut-sheet]") {
    using namespace wormedit;
    nuklear_event_harness_t harness{800, 560};
    require_menu(harness.ready());
    auto registry = make_default_editor_command_registry({});
    (void)remap_command(registry, {"view.open_layers"},
        {{shortcut_key_t::u, shortcut_modifier_t::primary | shortcut_modifier_t::shift}});
    (void)remap_command(registry, {"view.toggle_sounds"}, {});
    sys::project_menu_commands(registry, harness.view(), shortcut_primary_modifier_t::meta);
    const WormUiComposeLaneKeyBinding binding{.scancode = SDL_SCANCODE_1, .lane_id = 1};
    harness.view().compose.lane_key_bindings = &binding;
    harness.view().compose.lane_key_binding_count = 1;
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
    check_menu(blocked.get().compose_input_count == 0);
    check_menu(blocked.get().shortcut_event_count == 0);
    key.type = SDL_EVENT_KEY_UP;
    key.key.down = false;
    (void)harness.frame(std::span{&key, 1});
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.down = true;
    key.key.key = SDLK_ESCAPE;
    key.key.scancode = SDL_SCANCODE_ESCAPE;
    check_menu(harness.frame(std::span{&key, 1}).get().compose_input_count == 0);
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
    check_menu(resumed.get().compose_input_count == 1);
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
    WormUiProjectChartRow row{.chart_id = 10, .name = "Covered difficulty", .author = "Author",
        .rating = "Unset", .mode = "7K", .output_groups = "Unassigned", .length = "2 s", .diagnostic = ""};
    auto& view = harness.view();
    view.compose.visible = false;
    view.active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
    view.setup = {.visible = true, .song_id = 1, .document_revision = 1, .session_generation = 1,
        .charts = &row, .chart_row_count = 1, .chart_count = 1,
        .stepmania_policy_valid = true, .stepmania_policy_status = "Ready"};
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
    wormedit::sys::project_menu_commands(wormedit::make_default_editor_command_registry({}), harness.view());
    (void)harness.frame();
    (void)click_menu(harness, "File");
    (void)harness.frame();
    const auto target = harness.drawn_text_center("New project");
    require_menu(target.has_value());
    int invocations = 0;
    for (const auto& frame : click_menu(harness, "New project")) {
        invocations += std::string_view{frame.get().menu_command_id} == "document.new_project";
        check_menu(frame.get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
        check_menu(frame.get().compose_input_count == 0);
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
    view.recent_file_count = 2;
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
        check_menu(opened->get().compose_input_count == 0);
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
    harness.view().recent_file_count = labels.size();
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
    check_dispatch("File", "Export…", "document.export_output_group");
}
