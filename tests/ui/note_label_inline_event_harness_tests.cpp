#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <source_location>
#include <string_view>

namespace {

void check_label(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}

void require_label(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    REQUIRE(condition);
}

auto key_down(SDL_Keycode key, SDL_Scancode scancode) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    event.key.key = key;
    event.key.scancode = scancode;
    return event;
}

auto text_input(const char* text) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = text;
    return event;
}

// The system layer selects the double-clicked note and raises this flag one frame later.
void request_editor(nuklear_event_harness_t& harness, const char* label) {
    auto& view = harness.view().compose;
    view.project_id = 31;
    view.chart_id = 7;
    view.document_revision = 12;
    view.selected_note_count = 1;
    view.selected_note_label_note_id = 55;
    view.selected_note_label = label;
    view.focus_note_label_editor = true;
    auto& state = worm_ui_nuklear_compose_state();
    state.note_label.note_label_anchor_x = 300.0f;
    state.note_label.note_label_anchor_y = 300.0f;
}

void open_editor(nuklear_event_harness_t& harness, const char* label) {
    (void)harness.frame();
    request_editor(harness, label);
    const auto flagged = harness.frame().get();
    require_label(flagged.compose_note_label_focus_consumed);
    harness.view().compose.focus_note_label_editor = false;
    (void)harness.frame();
    require_label(worm_ui_nuklear_compose_state().bookmark_ui.bookmark_rename.active);
}

} // namespace

TEST_CASE("note double-click opens a prefilled inline label editor and Enter applies it once",
          "[unit][frontend-state][event-harness][note-label-inline]") {
    nuklear_event_harness_t harness{1280, 900};
    require_label(harness.ready());
    open_editor(harness, "Kick");
    const auto& editor = worm_ui_nuklear_compose_state().bookmark_ui.bookmark_rename;
    check_label(editor.kind == inline_label_kind_t::note_label);
    check_label(std::string_view{editor.label} == "Kick");

    // Typing reaches the field, not the canvas.
    const auto typed = harness.frame(std::array{text_input("2")}).get();
    check_label(typed.compose_input_count == 0);
    const auto enter = harness.frame(std::array{key_down(SDLK_RETURN, SDL_SCANCODE_RETURN)}).get();
    check_label(enter.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL);
    // The field opens with its text selected, so typing replaces the prefilled label.
    check_label(std::string_view{enter.compose_note_label} == "2");
    check_label(enter.compose_input_chart_id == 7);
    check_label(enter.compose_input_document_revision == 12);
    check_label(!worm_ui_nuklear_compose_state().bookmark_ui.bookmark_rename.active);

    // The commit happens once.
    const auto after = harness.frame().get();
    check_label(after.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("an empty inline note label clears the label",
          "[unit][frontend-state][event-harness][note-label-inline]") {
    nuklear_event_harness_t harness{1280, 900};
    require_label(harness.ready());
    open_editor(harness, "");
    const auto enter = harness.frame(std::array{key_down(SDLK_RETURN, SDL_SCANCODE_RETURN)}).get();
    check_label(enter.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL);
}

TEST_CASE("Escape leaves the note label unchanged",
          "[unit][frontend-state][event-harness][note-label-inline]") {
    nuklear_event_harness_t harness{1280, 900};
    require_label(harness.ready());
    open_editor(harness, "Kick");
    (void)harness.frame(std::array{text_input("x")});
    const auto escape = harness.frame(std::array{key_down(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)}).get();
    check_label(escape.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_label(!worm_ui_nuklear_compose_state().bookmark_ui.bookmark_rename.active);
}

TEST_CASE("a stale document revision closes the inline note label editor without applying",
          "[unit][frontend-state][event-harness][note-label-inline]") {
    nuklear_event_harness_t harness{1280, 900};
    require_label(harness.ready());
    open_editor(harness, "Kick");
    harness.view().compose.document_revision += 1;
    const auto stale = harness.frame().get();
    check_label(stale.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_label(!worm_ui_nuklear_compose_state().bookmark_ui.bookmark_rename.active);
}

TEST_CASE("the inspector no longer carries a note label editor and names the search",
          "[unit][frontend-state][event-harness][note-label-inline]") {
    nuklear_event_harness_t harness{1280, 900};
    require_label(harness.ready());
    (void)harness.frame();
    const auto& text = harness.drawn_text();
    check_label(text.find("Label (") == std::string::npos);
    check_label(text.find("Find note label") != std::string::npos);
}
