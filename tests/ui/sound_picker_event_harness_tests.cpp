#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>

namespace {
void require_picker(bool value) { REQUIRE(value); }
void check_picker(bool value) { CHECK(value); }
auto key(SDL_Keycode code, SDL_Scancode scan, bool down = true) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = code;
    event.key.scancode = scan;
    event.key.down = down;
    return event;
}
void prepare(nuklear_event_harness_t& harness) {
    require_picker(harness.ready());
    auto& view = harness.view().compose;
    view.sound_picker = {};
    view.sound_picker.ready = true;
    view.sound_picker.rows[0] = {41, "01 · Kick  (#41)", false};
    view.sound_picker.rows[1] = {42, "02 · Snare  (#42)", false};
    view.sound_picker.row_count = view.sound_picker.total_count = 2;
    static_cast<void>(harness.frame());
    require_picker(worm_ui_frontend_open_sound_picker(1, 1, 1));
    static_cast<void>(harness.frame());
}
auto press(nuklear_event_harness_t& harness, SDL_Keycode code, SDL_Scancode scan)
    -> workspace_action_snapshot_t {
    auto event = key(code, scan);
    return harness.frame(std::span{&event, 1});
}
void check_source(const WormUiWorkspaceAction& action, std::uint64_t id) {
    require_picker(action.compose_input_count == 1);
    check_picker(action.compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_DRAW_SOURCE);
    check_picker(action.compose_inputs[0].resource_id == id);
    check_picker(action.compose_inputs[0].resource_role == 1);
    check_picker(action.compose_input_project_id == 1);
    check_picker(action.compose_input_chart_id == 1);
    check_picker(action.compose_input_document_revision == 1);
    check_picker(action.shortcut_event_count == 0);
}
void type(nuklear_event_harness_t& harness, const char* text) {
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = text;
    static_cast<void>(harness.frame(std::span{&event, 1}));
}
}

TEST_CASE("Go to sound keyboard selection arms through Draw source without shortcut leakage",
          "[frontend-state][event-harness][sound-picker]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    require_picker(harness.drawn_text().find("Go to sound") != std::string::npos);
    const auto moved = press(harness, SDLK_DOWN, SDL_SCANCODE_DOWN);
    check_picker(moved.get().shortcut_event_count == 0);
    const auto selected = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_source(selected.get(), 42);
    const auto& state = worm_ui_nuklear_compose_state();
    check_picker(state.pointer_capture.draw_source_id == 42);
    check_picker(state.resource_picker.armed_resource_id == 42);
    check_picker(state.keysound_catalog.keysound_selection.primary == 42);
    check_picker(!(state.sound_picker_ui.sound_picker.open));
    check_picker(state.sound_picker_ui.sound_picker.release_pending);
    const auto held = press(harness, SDLK_SPACE, SDL_SCANCODE_SPACE);
    check_picker(held.get().shortcut_event_count == 0);
    const std::array releases{key(SDLK_DOWN, SDL_SCANCODE_DOWN, false),
        key(SDLK_RETURN, SDL_SCANCODE_RETURN, false), key(SDLK_SPACE, SDL_SCANCODE_SPACE, false)};
    static_cast<void>(harness.frame(releases));
    static_cast<void>(harness.frame());
    const auto resumed = press(harness, SDLK_SPACE, SDL_SCANCODE_SPACE);
    check_picker(resumed.get().shortcut_event_count == 1);
}

TEST_CASE("Go to sound typing requests full-catalog search and rejects stale query results",
          "[frontend-state][event-harness][sound-picker]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    type(harness, "snr");
    WormUiKeysoundDockRequest request{};
    worm_ui_frontend_keysound_dock_request(&request);
    check_picker(request.sound_picker_open);
    check_picker(std::string{request.sound_picker_query} == "snr");
    check_picker(harness.drawn_text().find("Searching") != std::string::npos);
    auto stale = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_picker(stale.get().compose_input_count == 0);
    auto release = key(SDLK_RETURN, SDL_SCANCODE_RETURN, false);
    static_cast<void>(harness.frame(std::span{&release, 1}));
    auto& result = harness.view().compose.sound_picker;
    std::strcpy(result.query, "snr");
    result.rows[0] = result.rows[1];
    result.row_count = result.total_count = 1;
    static_cast<void>(harness.frame());
    const auto selected = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_source(selected.get(), 42);
}

TEST_CASE("Go to sound Escape and context loss leave the armed source unchanged",
          "[frontend-state][event-harness][sound-picker]") {
    for (int cause = 0; cause < 5; ++cause) {
        INFO("cause " << cause);
        nuklear_event_harness_t harness{1280, 800};
        prepare(harness);
        auto& state = worm_ui_nuklear_compose_state();
        state.pointer_capture.draw_source_id = state.resource_picker.armed_resource_id = 90;
        workspace_action_snapshot_t action{{}};
        if (cause == 0) action = press(harness, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        if (cause == 1) {
            ++harness.view().compose.document_revision;
            action = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
        }
        if (cause == 2) {
            ++harness.view().compose.chart_id;
            action = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
        }
        if (cause == 3) {
            SDL_Event event{};
            event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            action = harness.frame(std::span{&event, 1});
        }
        if (cause == 4) {
            harness.view().active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
            action = harness.frame();
        }
        check_picker(!(state.sound_picker_ui.sound_picker.open));
        check_picker(state.pointer_capture.draw_source_id == 90);
        check_picker(state.resource_picker.armed_resource_id == 90);
        if (cause == 3) {
            require_picker(action.get().compose_input_count == 1);
            check_picker(action.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
        } else check_picker(action.get().compose_input_count == 0);
        check_picker(action.get().shortcut_event_count == 0);
    }
}

TEST_CASE("Go to sound missing results cannot arm and an empty search explains itself",
          "[frontend-state][event-harness][sound-picker]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    auto& view = harness.view().compose.sound_picker;
    view.rows[0].missing = true;
    const auto selected = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_picker(selected.get().compose_input_count == 0);
    check_picker(worm_ui_nuklear_compose_state().sound_picker_ui.sound_picker.open);
    view.row_count = view.total_count = 0;
    static_cast<void>(harness.frame());
    check_picker(harness.drawn_text().find("No matching sounds") != std::string::npos);
}

TEST_CASE("Go to sound mouse activation and outside click consume pointer input",
          "[frontend-state][event-harness][sound-picker]") {
    for (const bool outside : {false, true}) {
        nuklear_event_harness_t harness{1280, 800};
        prepare(harness);
        const auto target = harness.drawn_text_center("02 · Snare  (#42)");
        require_picker(target.has_value());
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.x = outside ? 50 : target->first;
        motion.motion.y = outside ? 150 : target->second;
        SDL_Event button{};
        button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        button.button.button = SDL_BUTTON_LEFT;
        button.button.down = true;
        button.button.x = motion.motion.x;
        button.button.y = motion.motion.y;
        const std::array events{motion, button};
        const auto action = harness.frame(events);
        if (!outside) check_source(action.get(), 42);
        else check_picker(action.get().compose_input_count == 0);
        check_picker(!(worm_ui_nuklear_compose_state().sound_picker_ui.sound_picker.open));
    }
}

TEST_CASE("Go to sound keyboard keeps later ranked results visible in a short window",
          "[frontend-state][event-harness][sound-picker]") {
    nuklear_event_harness_t harness{960, 480};
    prepare(harness);
    std::array<std::string, 20> labels;
    auto& results = harness.view().compose.sound_picker;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        labels[index] = "Sound " + std::to_string(index);
        results.rows[index] = {100 + index, labels[index].c_str(), false};
    }
    results.row_count = results.total_count = 20;
    static_cast<void>(harness.frame());
    for (int index = 0; index < 19; ++index) {
        static_cast<void>(press(harness, SDLK_DOWN, SDL_SCANCODE_DOWN));
        auto release = key(SDLK_DOWN, SDL_SCANCODE_DOWN, false);
        static_cast<void>(harness.frame(std::span{&release, 1}));
    }
    check_picker(harness.drawn_text().find("Sound 19") != std::string::npos);
    const auto action = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_source(action.get(), 119);
}

TEST_CASE("Go to sound IME confirmation cannot arm an old result", "[frontend-state][event-harness][sound-picker]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    SDL_Event editing{};
    editing.type = SDL_EVENT_TEXT_EDITING;
    editing.edit.type = SDL_EVENT_TEXT_EDITING;
    editing.edit.text = "ki";
    static_cast<void>(harness.frame(std::span{&editing, 1}));
    const auto composing = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_picker(composing.get().compose_input_count == 0);
    check_picker(worm_ui_nuklear_compose_state().sound_picker_ui.sound_picker.open);
    auto release = key(SDLK_RETURN, SDL_SCANCODE_RETURN, false);
    static_cast<void>(harness.frame(std::span{&release, 1}));
    editing.edit.text = "";
    static_cast<void>(harness.frame(std::span{&editing, 1}));
    const auto selected = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_source(selected.get(), 41);
}

namespace {
auto sound_picker_glyph_width(nk_handle, float, const char*, int length) -> float {
    return static_cast<float>(length) * 8.0f;
}

struct sound_picker_host_fixture_t {
    nk_user_font font{};
    nk_context context{};
    worm_ui_nuklear_compose_state_t compose{};
    WormUiComposeView view{.project_id = 1, .chart_id = 1, .document_revision = 1};
    WormUiWorkspaceAction action{};

    sound_picker_host_fixture_t() {
        font.height = 15.0f;
        font.width = sound_picker_glyph_width;
        require_picker(nk_init_default(&context, &font));
        nk_input_begin(&context);
        nk_input_end(&context);
        require_picker(nk_begin(&context, "Sound picker host", nk_rect(0, 0, 1280, 800),
                                NK_WINDOW_NO_SCROLLBAR));
        require_picker(nk_popup_begin(&context, NK_POPUP_STATIC, "Other popup",
            NK_WINDOW_BORDER, nk_rect(10, 10, 100, 100)));
        nk_popup_end(&context);
    }
    ~sound_picker_host_fixture_t() { nk_end(&context); nk_free(&context); }
    sound_picker_host_fixture_t(const sound_picker_host_fixture_t&) = delete;
    auto operator=(const sound_picker_host_fixture_t&) -> sound_picker_host_fixture_t& = delete;
};
} // namespace

TEST_CASE("Go to sound begin failure clears focus only when it cancels an open picker",
          "[frontend-state][event-harness][sound-picker][picker-lifecycle]") {
    for (const bool open : {false, true}) {
        sound_picker_host_fixture_t fixture;
        auto& state = fixture.compose.sound_picker_ui.sound_picker;
        open_compose_sound_picker(state, 1, 1, 1);
        state.open = open;
        state.close_pending = true;
        auto* host = fixture.context.current;
        render_compose_sound_picker(&fixture.context, fixture.view,
                                    fixture.action, fixture.compose, true);
        check_picker(!state.open);
        check_picker(!state.close_pending);
        check_picker(state.release_pending == open);
        check_picker(state.focus_pending == !open);
        check_picker(fixture.context.current == host);
    }
}
