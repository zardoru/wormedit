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
    transport_status_input_t input{.status = "Ready", .artist = "Artist", .difficulty = "Chart", .playing = true};
    for (std::uint64_t frame = 0; frame < 144; ++frame) {
        input.seconds = frame / 144;
        text.refresh(input, frame * 1'000'000'000 / 144);
    }
    CHECK(text.refreshes() == 12);
    CHECK(text.status() == "Ready");
    CHECK(text.details() == "Artist · Chart");
    CHECK(text.elapsed() == "0:00.000");
}

TEST_CASE("Playback status authored values and user actions invalidate immediately", "[transport-status]") {
    transport_status_text_t text;
    transport_status_input_t input{.status = "Ready", .artist = "Artist", .difficulty = "Chart", .playing = true};
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
    transport_status_input_t input{.status = "Ready", .artist = "Artist", .difficulty = "Chart",
        .seconds = 80, .playing = true};
    REQUIRE(text.refresh(input, 100));
    CHECK(text.elapsed() == "1:20.000");
    input.seconds = 10;
    CHECK(text.refresh(input, 101));
    CHECK(text.elapsed() == "0:10.000");
    input.seconds = 30;
    CHECK(text.refresh(input, 102));
    check_stopped_status_frames(text, input);
}

TEST_CASE("Transport readouts retain milliseconds and distinguish chart coordinates", "[transport-status]") {
    transport_status_text_t text;
    transport_status_input_t input{.seconds = 61, .milliseconds = 7,
        .position = transport_position_view_t{.row = 192, .measure = 2, .beat = 1}};
    REQUIRE(text.refresh(input, 0));
    CHECK(text.elapsed() == "1:01.007");
    CHECK(text.row() == "192");
    CHECK(text.musical_position() == "2:1");
}

TEST_CASE("Transport readouts mark unavailable chart coordinates", "[transport-status]") {
    transport_status_text_t text;
    REQUIRE(text.refresh({.seconds = 61, .milliseconds = 999}, 0));
    CHECK(text.elapsed() == "1:01.999");
    CHECK(text.row() == "—");
    CHECK(text.musical_position() == "—");
}

TEST_CASE("Status text carries the other clock and the post-cut row for tooltips", "[transport-status]") {
    transport_status_text_t text;
    transport_status_input_t input{.status = "Ready", .seconds = 72, .milliseconds = 236,
        .alternate_available = true, .alternate_seconds = 50, .alternate_milliseconds = 10,
        .position = transport_position_view_t{.row = 960, .measure = 5, .beat = 1, .alternate_row = 400,
        .alternate_measure = 3}};
    text.refresh(input, 1);
    CHECK(text.elapsed() == "1:12.236");
    CHECK(text.alternate_elapsed() == "0:50.010");
    CHECK(text.alternate_row() == "400");
    CHECK(text.alternate_musical_position() == "3:1");
    input.alternate_available = false;
    input.position->alternate_row.reset();
    input.position->alternate_measure.reset();
    input.immediate = true;
    text.refresh(input, 2);
    CHECK(text.alternate_elapsed().empty());
    CHECK(text.alternate_row().empty());
    CHECK(text.alternate_musical_position().empty());
}
