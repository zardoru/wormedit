#include "nuklear_event_harness.h"
#include "ui/nuklear/metadata_state.h"

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <source_location>
#include <string_view>

namespace {
void expect_tag(bool condition, std::source_location location = std::source_location::current()) {
    INFO("Tag expectation at " << location.line());
    REQUIRE(condition);
}
auto tag_button(bool down, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.x = x;
    event.button.y = y;
    return event;
}
auto tag_motion(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.state = SDL_BUTTON_LMASK;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}
struct tag_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    std::array<const char*, 3> tags{"alpha", "beta", "gamma"};
    std::array<std::size_t, 3> bytes{5, 4, 5};
    tag_fixture_t() {
        auto& workspace = harness.view();
        workspace.compose.visible = false;
        workspace.active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        auto& view = workspace.metadata;
        view.visible = true;
        view.song_fields_editable = true;
        view.search_tags_editable = true;
        view.document_revision = 1;
        view.title = "Song"; view.title_bytes = 4;
        view.artist = "Artist"; view.artist_bytes = 6;
        view.search_tags = tags.data(); view.search_tag_bytes = bytes.data();
        view.search_tag_count = tags.size();
        (void)harness.frame();
        (void)harness.frame();
    }
    auto row(std::string_view number) -> std::pair<float, float> {
        for (const auto& text : harness.drawn_text_bounds()) {
            if (std::string_view{text.text} == number)
                return {text.x - 15, text.y + text.height / 2};
        }
        expect_tag(false);
        return {};
    }
    void begin() {
        const auto [x, y] = row("1");
        INFO("grip coordinate " << x << "," << y);
        const auto down = tag_button(true, x, y);
        const std::array events{tag_motion(x, y), down};
        (void)harness.frame(events);
        expect_tag(worm_ui_nuklear_metadata_state().tag_drag.source == 0);
    }
    auto drop() -> workspace_action_snapshot_t {
        const auto [x, y] = row("3");
        const auto motion = tag_motion(x + 80, y + 6);
        (void)harness.frame({&motion, 1});
        expect_tag(worm_ui_nuklear_metadata_state().tag_drag.insertion == 3);
        const auto up = tag_button(false, x + 80, y + 6);
        return harness.frame({&up, 1});
    }
    auto click(std::string_view text) -> workspace_action_snapshot_t {
        const auto position = harness.drawn_text_center(text);
        expect_tag(position.has_value());
        const auto down = tag_button(true, position->first, position->second);
        const std::array events{tag_motion(position->first, position->second), down};
        const auto pressed = harness.frame(events);
        const auto up = tag_button(false, position->first, position->second);
        (void)harness.frame({&up, 1});
        return pressed;
    }
};
}

TEST_CASE("Tag grips stage insertion reorder until Apply and Revert restores source order",
          "[frontend-state][event-harness][tag-grips]") {
    tag_fixture_t fixture;
    expect_tag(fixture.harness.drawn_text().find("Up") == std::string::npos);
    fixture.begin();
    expect_tag(fixture.drop().get().metadata_edit == WORM_UI_METADATA_EDIT_NONE);
    auto& state = worm_ui_nuklear_metadata_state();
    expect_tag(std::string_view{state.search_tags[2].data()} == "alpha");
    const auto applied = fixture.click("Apply");
    expect_tag(applied.get().metadata_edit == WORM_UI_METADATA_EDIT_SONG);
    expect_tag(applied.get().metadata_search_tag_count == 3);
    expect_tag(std::string_view{applied.get().metadata_search_tags[0]} == "beta");
    expect_tag(std::string_view{applied.get().metadata_search_tags[2]} == "alpha");
    (void)fixture.click("Revert");
    expect_tag(std::string_view{state.search_tags[0].data()} == "alpha");
}

TEST_CASE("Tag grips cancel outside releases and context or focus changes",
          "[frontend-state][event-harness][tag-grips]") {
    for (int transition = 0; transition < 6; ++transition) {
        tag_fixture_t fixture;
        fixture.begin();
        if (transition == 0) {
            const auto up = tag_button(false, 5, 850);
            const std::array events{tag_motion(5, 850), up};
            (void)fixture.harness.frame(events);
        } else if (transition == 1) {
            SDL_Event event{};
            event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            (void)fixture.harness.frame({&event, 1});
        } else if (transition == 2) {
            ++fixture.harness.view().metadata.document_revision;
            (void)fixture.harness.frame();
        } else if (transition == 3) {
            fixture.harness.view().metadata.visible = false;
            fixture.harness.view().compose.visible = true;
            (void)fixture.harness.frame();
        } else if (transition == 4) {
            fixture.harness.view().options.visible = true;
            (void)fixture.harness.frame();
        } else {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.type = SDL_EVENT_KEY_DOWN;
            event.key.down = true;
            event.key.key = SDLK_ESCAPE;
            event.key.scancode = SDL_SCANCODE_ESCAPE;
            (void)fixture.harness.frame({&event, 1});
        }
        const auto& state = worm_ui_nuklear_metadata_state();
        expect_tag(!state.tag_drag.source);
        expect_tag(std::string_view{state.search_tags[0].data()} == "alpha");
    }
}

TEST_CASE("Tag remove icon stages removal with a tooltip and read-only grips stay inert",
          "[frontend-state][event-harness][tag-grips]") {
    tag_fixture_t fixture;
    const auto apply = fixture.harness.drawn_text_center("Apply");
    expect_tag(apply.has_value());
    const auto row = fixture.row("2");
    const auto x = apply->first + 35;
    const auto motion = tag_motion(x, row.second);
    (void)fixture.harness.frame({&motion, 1});
    expect_tag(fixture.harness.drawn_text().find("Remove tag 2") != std::string::npos);
    const auto down = tag_button(true, x, row.second);
    const auto action = fixture.harness.frame({&down, 1});
    expect_tag(action.get().metadata_edit == WORM_UI_METADATA_EDIT_NONE);
    const auto up = tag_button(false, x, row.second);
    (void)fixture.harness.frame({&up, 1});
    auto& state = worm_ui_nuklear_metadata_state();
    expect_tag(state.search_tags.size() == 2);
    expect_tag(std::string_view{state.search_tags[1].data()} == "gamma");
    (void)fixture.click("Revert");
    expect_tag(state.search_tags.size() == 3);
    const auto grip = fixture.row("1");
    fixture.harness.view().metadata.search_tags_editable = false;
    (void)fixture.harness.frame();
    const std::array events{tag_motion(grip.first, grip.second),
        tag_button(true, grip.first, grip.second)};
    (void)fixture.harness.frame(events);
    expect_tag(!state.tag_drag.source);
}
