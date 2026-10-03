#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <utility>

namespace {

struct sdl_event_scope_t {
    SDL_Keymod previous{SDL_GetModState()};
    ~sdl_event_scope_t() {
        SDL_SetModState(previous);
        SDL_QuitSubSystem(SDL_INIT_EVENTS);
    }
};

auto key_event(SDL_EventType type, SDL_Keycode key, SDL_Scancode scancode, bool repeat = false)
    -> SDL_Event {
    SDL_Event event{};
    event.type = type;
    event.key.type = type;
    event.key.windowID = 0;
    event.key.key = key;
    event.key.scancode = scancode;
    event.key.down = type == SDL_EVENT_KEY_DOWN;
    event.key.repeat = repeat;
    return event;
}

auto button_event(bool down, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = 0;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    return event;
}

auto motion_event(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = 0;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

auto click_at(nuklear_event_harness_t& harness, float x, float y)
    -> std::vector<workspace_action_snapshot_t> {
    std::array<event_step_t, 2> script{};
    script[0].events = {motion_event(x, y), button_event(true, x, y)};
    script[1].events = {motion_event(x, y), button_event(false, x, y)};
    return harness.run_script(script);
}

auto delete_shortcut_has_compose_context(const WormUiWorkspaceAction& action) -> bool {
    return action.shortcut_event_count == 1 &&
           action.shortcut_events[0].hid_usage ==
               static_cast<std::uint16_t>(SDL_SCANCODE_DELETE) &&
           !action.shortcut_events[0].repeat && action.shortcut_window_focused &&
           !action.shortcut_text_entry_active && !action.shortcut_ime_composition_active;
}

} // namespace

TEST_CASE("Headless frontend routes a wheel event over Compose to viewport input",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0);
    REQUIRE(harness.frame().get().kind == WORM_UI_WORKSPACE_ACTION_NONE);

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.windowID = 0;
    wheel.wheel.x = 0.0f;
    wheel.wheel.y = 1.0f;
    wheel.wheel.mouse_x = 480.0f;
    wheel.wheel.mouse_y = 400.0f;
    const std::array events{wheel};
    const auto action = harness.frame(events);

    REQUIRE(action.get().compose_viewport_input_count == 1);
    CHECK(action.get().compose_viewport_inputs[0].kind ==
          WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL);
    CHECK(action.get().compose_viewport_inputs[0].delta_y != 0.0);
}

TEST_CASE("Headless frontend emits Delete shortcut press with focused Compose context",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());

    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto pressed = harness.frame(std::span{&press, 1});
    CHECK(delete_shortcut_has_compose_context(pressed.get()));
}

TEST_CASE("Headless frontend ignores Delete shortcut release",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    const auto release = key_event(SDL_EVENT_KEY_UP, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto released = harness.frame(std::span{&release, 1});
    CHECK(released.get().shortcut_event_count == 0);
}

TEST_CASE("Headless frontend marks repeated Delete shortcut press",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    const auto repeat = key_event(SDL_EVENT_KEY_DOWN, SDLK_DELETE, SDL_SCANCODE_DELETE, true);
    const auto repeated = harness.frame(std::span{&repeat, 1});
    REQUIRE(repeated.get().shortcut_event_count == 1);
    CHECK(repeated.get().shortcut_events[0].repeat);
}

TEST_CASE("Headless frontend tab click emits a workspace tab action",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());

    constexpr float tab_x = 80.0f;
    constexpr float tab_y = 54.0f;
    std::array<event_step_t, 2> script{};
    script[0].events.push_back(motion_event(tab_x, tab_y));
    script[0].events.push_back(button_event(true, tab_x, tab_y));
    script[1].events.push_back(motion_event(tab_x, tab_y));
    script[1].events.push_back(button_event(false, tab_x, tab_y));
    const auto actions = harness.run_script(script);

    REQUIRE(actions.size() == script.size());
    CHECK(actions[0].get().kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB);
    CHECK(actions[0].get().tab == WORM_UI_WORKSPACE_TAB_SETUP);
}

TEST_CASE("Headless frontend suppresses Delete shortcut while new-project wizard is open",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());

    REQUIRE(worm_ui_frontend_request_new_project());
    static_cast<void>(harness.frame());
    const auto delete_key = key_event(SDL_EVENT_KEY_DOWN, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto modal_key_action = harness.frame(std::span{&delete_key, 1});
    CHECK(modal_key_action.get().shortcut_event_count == 0);
}

TEST_CASE("Headless frontend new-project wizard cancel closes and permits reopening",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    REQUIRE(worm_ui_frontend_request_new_project());
    static_cast<void>(harness.frame());

    // The wizard is centered at (230,258), 500x204. Its last row begins at y=366;
    // the button centers are in the two 242px columns at y=406.
    constexpr float button_y = 406.0f;
    const auto cancel_actions = click_at(harness, 350.0f, button_y);
    CHECK(cancel_actions[0].get().kind == WORM_UI_WORKSPACE_ACTION_NONE);
    CHECK(worm_ui_frontend_request_new_project());
}

TEST_CASE("Headless frontend new-project wizard creates the default 7K layout",
          "[unit][frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());
    REQUIRE(worm_ui_frontend_request_new_project());
    static_cast<void>(harness.frame());

    const auto create_actions = click_at(harness, 610.0f, 406.0f);
    CHECK(create_actions[0].get().kind == WORM_UI_WORKSPACE_ACTION_NEW_PROJECT);
    CHECK(create_actions[0].get().new_project_layout_index == 3);
}

namespace {

void check_wheel_zoom(const workspace_action_snapshot_t& snapshot, bool zoom) {
    const auto& action = snapshot.get();
    REQUIRE(action.compose_viewport_input_count == 1);
    CHECK(action.compose_viewport_inputs[0].zoom_modifier == zoom);
    CHECK(action.compose_viewport_inputs[0].delta_y != 0.0);
}

void check_modifier_fixture_isolation() {
    CHECK(SDL_GetModState() == SDL_KMOD_ALT);
    nuklear_event_harness_t next;
    REQUIRE(next.ready());
    static_cast<void>(next.frame());
    CHECK(SDL_GetModState() == SDL_KMOD_ALT);
}

void check_snapshot_tags(const WormUiWorkspaceAction& action) {
    REQUIRE(action.metadata_search_tag_count == 3);
    CHECK(std::string{action.metadata_search_tags[0]} == "original tag");
    CHECK(std::string{action.metadata_search_tags[1]}.empty());
    CHECK(action.metadata_search_tags[2] == nullptr);
}

} // namespace

TEST_CASE("Headless frontend isolates scripted Ctrl wheel zoom and release",
          "[unit][frontend-state][frontend][event-harness]") {
    REQUIRE(SDL_Init(SDL_INIT_EVENTS));
    const sdl_event_scope_t restore;
    SDL_SetModState(SDL_KMOD_ALT);
    {
        nuklear_event_harness_t harness;
        REQUIRE(harness.ready());
        static_cast<void>(harness.frame());
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.y = 1.0f;
        wheel.wheel.mouse_x = 480.0f;
        wheel.wheel.mouse_y = 400.0f;
        const std::array<event_step_t, 4> script{{
            {{key_event(SDL_EVENT_KEY_DOWN, SDLK_LCTRL, SDL_SCANCODE_LCTRL)}, SDL_KMOD_LCTRL},
            {{wheel}, SDL_KMOD_LCTRL},
            {{key_event(SDL_EVENT_KEY_UP, SDLK_LCTRL, SDL_SCANCODE_LCTRL)}, SDL_KMOD_NONE},
            {{wheel}, SDL_KMOD_NONE},
        }};
        const auto actions = harness.run_script(script);
        check_wheel_zoom(actions[1], true);
        check_wheel_zoom(actions[3], false);
        CHECK(SDL_GetModState() == SDL_KMOD_ALT);
        const auto normal = harness.frame(std::span{&wheel, 1});
        check_wheel_zoom(normal, false);
        CHECK(SDL_GetModState() == SDL_KMOD_ALT);
    }
    check_modifier_fixture_isolation();
}

TEST_CASE("Action snapshots own metadata payloads across copies moves and destruction",
          "[unit][frontend-state][frontend][event-harness]") {
    using text_member_t = const char* WormUiWorkspaceAction::*;
    constexpr std::array<text_member_t, 11> fields{
        &WormUiWorkspaceAction::metadata_chart_rating_numerator_text,
        &WormUiWorkspaceAction::metadata_chart_rating_denominator_text,
        &WormUiWorkspaceAction::metadata_audio_offset_numerator_text,
        &WormUiWorkspaceAction::metadata_audio_offset_denominator_text,
        &WormUiWorkspaceAction::metadata_title,
        &WormUiWorkspaceAction::metadata_artist,
        &WormUiWorkspaceAction::metadata_chart_name,
        &WormUiWorkspaceAction::metadata_chart_author,
        &WormUiWorkspaceAction::metadata_format_name,
        &WormUiWorkspaceAction::metadata_format_value,
        &WormUiWorkspaceAction::metadata_output_path,
    };
    const auto make_snapshot = [&] {
        WormUiWorkspaceAction action{};
        std::array<std::string, 11> strings;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            strings[i] = std::to_string(i) + std::string(100, 'x');
            action.*fields[i] = strings[i].c_str();
        }
        std::string tag = "original tag";
        const char* tags[]{tag.c_str(), "", nullptr};
        action.metadata_search_tags = tags;
        action.metadata_search_tag_count = 3;
        action.compose_viewport_input_count = 1;
        action.compose_viewport_inputs[0].zoom_modifier = true;
        auto snapshot = workspace_action_snapshot_t{action};
        strings.fill("overwritten");
        tag.assign("changed");
        return snapshot;
    };
    auto original = make_snapshot();
    auto copy = original;
    auto moved = std::move(copy);
    auto assigned = workspace_action_snapshot_t{{}};
    assigned = moved;
    auto move_assigned = workspace_action_snapshot_t{{}};
    move_assigned = std::move(assigned);
    std::vector<workspace_action_snapshot_t> history;
    history.push_back(std::move(move_assigned));
    original = workspace_action_snapshot_t{{}};
    moved = workspace_action_snapshot_t{{}};
    for (int i = 0; i < 32; ++i) history.push_back(workspace_action_snapshot_t{{}});
    const auto& action = history.front().get();
    for (std::size_t i = 0; i < fields.size(); ++i)
        CHECK(std::string{action.*fields[i]} == std::to_string(i) + std::string(100, 'x'));
    check_snapshot_tags(action);
    CHECK(action.compose_viewport_inputs[0].zoom_modifier);
    CHECK(history.back().get().metadata_title == nullptr);
    CHECK(history.back().get().metadata_search_tags == nullptr);
}

namespace {

auto edit_metadata_title(nuklear_event_harness_t& harness, const char* value)
    -> std::vector<workspace_action_snapshot_t> {
    // At 960x720 the title field is (287,135,631,26); Apply is (822,224,96,23).
    static_cast<void>(click_at(harness, 450.0f, 148.0f));
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    // These ASCII fixture strings use one codepoint per backend text event.
    for (const char* next = value; *next; ++next) {
        const char codepoint[]{*next, '\0'};
        text.text.text = codepoint;
        static_cast<void>(harness.frame(std::span{&text, 1}));
    }
    auto actions = click_at(harness, 870.0f, 235.0f);
    REQUIRE(actions[0].get().metadata_edit == WORM_UI_METADATA_EDIT_SONG);
    return actions;
}

} // namespace

TEST_CASE("Headless metadata text history survives later compose and frontend shutdown",
          "[unit][frontend-state][frontend][event-harness]") {
    std::vector<workspace_action_snapshot_t> history;
    {
        nuklear_event_harness_t harness;
        REQUIRE(harness.ready());
        auto& view = harness.view();
        view.compose.visible = false;
        view.active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        view.metadata.visible = true;
        view.metadata.song_fields_editable = true;
        view.metadata.search_tags_editable = true;
        view.metadata.title = "";
        view.metadata.artist = "Artist";
        view.metadata.artist_bytes = 6;
        static_cast<void>(harness.frame());
        history = edit_metadata_title(harness, "First");
        REQUIRE(std::string{history[0].get().metadata_title} == "First");
        // Simulate the next projected view, then edit the same frontend buffer.
        view.metadata.title = "First";
        view.metadata.title_bytes = 5;
        ++view.metadata.document_revision;
        static_cast<void>(harness.frame());
        const auto later = edit_metadata_title(harness, "Second");
        CHECK(std::string{later[0].get().metadata_title} == "FirstSecond");
        CHECK(std::string{history[0].get().metadata_title} == "First");
    }
    auto copied = history;
    history.clear();
    auto moved = std::move(copied);
    CHECK(std::string{moved[0].get().metadata_title} == "First");
    CHECK(std::string{moved[0].get().metadata_artist} == "Artist");
}

namespace {

constexpr std::array project_chart_text_fields{
    &WormUiProjectChartAction::author,
    &WormUiProjectChartAction::rating_numerator,
    &WormUiProjectChartAction::rating_denominator,
};

auto project_chart_snapshot_from_local_buffers() -> workspace_action_snapshot_t {
    WormUiWorkspaceAction source{};
    std::array<std::string, 3> strings{"Chart author", std::string(100, '9'), "7"};
    for (std::size_t i = 0; i < strings.size(); ++i)
        source.project_chart.*project_chart_text_fields[i] = strings[i].c_str();
    auto snapshot = workspace_action_snapshot_t{source};
    for (auto& value : strings) value[0] = '!';
    // Check while the source still lives: the shallow-copy negative control
    // fails safely here before any borrowed pointer could become dangling.
    for (std::size_t i = 0; i < strings.size(); ++i)
        REQUIRE((snapshot.get().project_chart.*project_chart_text_fields[i])[0] != '!');
    return snapshot;
}

void check_project_chart_snapshot(const workspace_action_snapshot_t& snapshot) {
    const auto& chart = snapshot.get().project_chart;
    CHECK(std::string{chart.author} == "Chart author");
    CHECK(std::string{chart.rating_numerator} == std::string(100, '9'));
    CHECK(std::string{chart.rating_denominator} == "7");
}

void check_project_chart_null_and_empty() {
    for (const auto member : project_chart_text_fields) {
        WormUiWorkspaceAction source{};
        const workspace_action_snapshot_t absent{source};
        CHECK(absent.get().project_chart.*member == nullptr);
        char empty[]{'\0', '\0'};
        source.project_chart.*member = empty;
        const workspace_action_snapshot_t present{source};
        empty[0] = 'x';
        REQUIRE(present.get().project_chart.*member != nullptr);
        CHECK(std::string{present.get().project_chart.*member}.empty());
    }
}

} // namespace

TEST_CASE("Action snapshots own nested project chart text through source destruction",
          "[unit][frontend-state][frontend][event-harness]") {
    auto original = project_chart_snapshot_from_local_buffers();
    auto copied = original;
    auto moved = std::move(copied);
    auto assigned = workspace_action_snapshot_t{{}};
    assigned = moved;
    auto move_assigned = workspace_action_snapshot_t{{}};
    move_assigned = std::move(assigned);
    std::vector<workspace_action_snapshot_t> history;
    history.push_back(std::move(move_assigned));
    original = workspace_action_snapshot_t{{}};
    moved = workspace_action_snapshot_t{{}};
    for (int i = 0; i < 32; ++i) history.push_back(workspace_action_snapshot_t{{}});
    check_project_chart_snapshot(history.front());
    check_project_chart_null_and_empty();
}
