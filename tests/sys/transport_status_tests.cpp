#include "ui/frontend/transport_status.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit::ui;

namespace {
void check_status_metadata_changes(transport_status_text_t& text, transport_status_input_t& input) {
    input.difficulty = "Another chart";
    CHECK(text.refresh(input, 104));
    input.artist = "Another artist";
    CHECK(text.refresh(input, 105));
    input.immediate = true;
    CHECK(text.refresh(input, 106));
}

void check_stopped_status_frames(transport_status_text_t& text, transport_status_input_t& input) {
    input.playing = false;
    CHECK(text.refresh(input, 103));
    CHECK(text.refresh(input, 104));
    CHECK(text.refresh(input, 105));
}
} // namespace

TEST_CASE("Playback status text refreshes at 12Hz while display frames stay at 144Hz", "[transport-status]") {
    transport_status_text_t text;
    transport_status_input_t input{"Ready", "Artist", "Chart", 0, 0, true, false};
    for (std::uint64_t frame = 0; frame < 144; ++frame) {
        input.seconds = frame / 144;
        text.refresh(input, frame * 1'000'000'000 / 144);
    }
    CHECK(text.refreshes() == 12);
    CHECK(text.status() == "Ready");
    CHECK(text.details() == "Artist · Chart");
    CHECK(text.elapsed() == "0:00");
}

TEST_CASE("Playback status authored values and user actions invalidate immediately", "[transport-status]") {
    transport_status_text_t text;
    transport_status_input_t input{"Ready", "Artist", "Chart", 0, 0, true, false};
    REQUIRE(text.refresh(input, 100));
    CHECK_FALSE(text.refresh(input, 101));
    input.status = "Changed document";
    CHECK(text.refresh(input, 102));
    input.selected = 4;
    CHECK(text.refresh(input, 103));
    CHECK(text.status() == "4 objects selected · Changed document");
    check_status_metadata_changes(text, input);
}

TEST_CASE("Playback status seeks and stopped frames keep immediate formatting", "[transport-status]") {
    transport_status_text_t text;
    transport_status_input_t input{"Ready", "Artist", "Chart", 0, 80, true, false};
    REQUIRE(text.refresh(input, 100));
    CHECK(text.elapsed() == "1:20");
    input.seconds = 10;
    CHECK(text.refresh(input, 101));
    CHECK(text.elapsed() == "0:10");
    input.seconds = 30;
    CHECK(text.refresh(input, 102));
    check_stopped_status_frames(text, input);
}
