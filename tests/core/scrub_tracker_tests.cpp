#include <wormedit/core/scrub_tracker.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

namespace {

// Drags the position at a constant audio speed for `seconds` at 60 updates per second.
struct drag_t {
    scrub_tracker_t tracker;
    double now{10.0};
    double position{5.0};
    scrub_output_t last;

    auto step(double speed, bool scrolled, std::optional<int> fingers = std::nullopt,
              bool was_playing = false) -> scrub_output_t {
        now += 1.0 / 60.0;
        position += speed / 60.0;
        last = tracker.update({.now_seconds = now, .position_seconds = position, .scrolled = scrolled,
                               .fingers = fingers, .was_playing = was_playing});
        return last;
    }
};

} // namespace

TEST_CASE("core.scrub_tracker idle without scroll input", "[scrub]") {
    drag_t drag;
    for (int i = 0; i < 10; ++i) {
        const auto out = drag.step(1.0, false);
        REQUIRE(out.phase == scrub_phase_t::idle);
        REQUIRE_FALSE(out.began);
    }
}

TEST_CASE("core.scrub_tracker follows a steady drag speed, forwards and backwards", "[scrub]") {
    for (const auto speed : {1.0, -1.0, 2.5, -2.5}) {
        drag_t drag;
        auto first = drag.step(speed, true);
        REQUIRE(first.began);
        REQUIRE(first.anchor_seconds == Catch::Approx(drag.position));
        for (int i = 0; i < 60; ++i) drag.step(speed, true);
        REQUIRE(drag.last.phase == scrub_phase_t::moving);
        REQUIRE(drag.last.speed == Catch::Approx(speed).epsilon(0.02));
    }
}

TEST_CASE("core.scrub_tracker clamps the speed", "[scrub]") {
    drag_t drag;
    for (int i = 0; i < 120; ++i) drag.step(50.0, true);
    REQUIRE(drag.last.speed == Catch::Approx(drag.tracker.settings().maximum_speed).epsilon(0.02));
}

TEST_CASE("core.scrub_tracker ends once input stops when fingers are unknown", "[scrub]") {
    drag_t drag;
    for (int i = 0; i < 30; ++i) drag.step(1.0, true);
    bool ended = false;
    int updates = 0;
    while (!ended && updates++ < 30) ended = drag.step(0.0, false).ended;
    REQUIRE(ended);
    // idle_seconds is 0.1 s: about six updates at 60 Hz.
    REQUIRE(updates <= 8);
    REQUIRE(drag.tracker.phase() == scrub_phase_t::idle);
}

TEST_CASE("core.scrub_tracker holds the record while fingers rest and ends when they lift", "[scrub]") {
    drag_t drag;
    for (int i = 0; i < 30; ++i) drag.step(1.0, true, 2);
    for (int i = 0; i < 40; ++i) drag.step(0.0, false, 2);
    REQUIRE(drag.last.phase == scrub_phase_t::held);
    REQUIRE(drag.last.speed == 0.0);
    REQUIRE_FALSE(drag.last.ended);
    // Scrolling again picks the needle back up.
    for (int i = 0; i < 20; ++i) drag.step(-1.0, true, 2);
    REQUIRE(drag.last.phase == scrub_phase_t::moving);
    REQUIRE(drag.last.speed < -0.5);
    // Fingers up with no motion ends the gesture.
    const auto out = drag.step(0.0, false, 0);
    REQUIRE(out.phase == scrub_phase_t::moving);
    bool ended = false;
    for (int i = 0; i < 10 && !ended; ++i) ended = drag.step(0.0, false, 0).ended;
    REQUIRE(ended);
}

TEST_CASE("core.scrub_tracker slow drift sits in the dead zone", "[scrub]") {
    drag_t drag;
    for (int i = 0; i < 60; ++i) drag.step(0.01, true);
    REQUIRE(drag.last.speed == 0.0);
}

TEST_CASE("core.scrub_tracker resumes interrupted playback after the delay", "[scrub]") {
    drag_t drag;
    drag.step(1.0, true, std::nullopt, true);
    for (int i = 0; i < 30; ++i) drag.step(1.0, true);
    scrub_output_t end;
    for (int i = 0; i < 20 && !end.ended; ++i) end = drag.step(0.0, false);
    REQUIRE(end.ended);
    const auto stopped_at = drag.position;
    bool resumed = false;
    double waited = 0.0;
    while (!resumed && waited < 1.0) {
        const auto out = drag.step(0.0, false);
        waited += 1.0 / 60.0;
        if (out.resume_playback) {
            resumed = true;
            REQUIRE(out.resume_seconds == Catch::Approx(stopped_at).margin(0.2));
        }
    }
    REQUIRE(resumed);
    REQUIRE(waited >= 0.1);
    REQUIRE(waited <= 0.25);
    // Only once.
    REQUIRE_FALSE(drag.step(0.0, false).resume_playback);
}

TEST_CASE("core.scrub_tracker a stopped transport does not resume", "[scrub]") {
    drag_t drag;
    for (int i = 0; i < 20; ++i) drag.step(1.0, true);
    for (int i = 0; i < 60; ++i) REQUIRE_FALSE(drag.step(0.0, false).resume_playback);
}

TEST_CASE("core.scrub_tracker a quick second gesture keeps the pending resume", "[scrub]") {
    drag_t drag;
    drag.step(1.0, true, std::nullopt, true);
    for (int i = 0; i < 20; ++i) drag.step(1.0, true);
    for (int i = 0; i < 8; ++i) drag.step(0.0, false);
    REQUIRE(drag.tracker.phase() == scrub_phase_t::idle);
    // Playback has not resumed yet; scroll again.
    drag.step(1.0, true);
    for (int i = 0; i < 20; ++i) drag.step(1.0, true);
    bool resumed = false;
    for (int i = 0; i < 60; ++i) resumed = drag.step(0.0, false).resume_playback || resumed;
    REQUIRE(resumed);
}

TEST_CASE("core.scrub_tracker cancel returns to idle without resuming", "[scrub]") {
    drag_t drag;
    drag.step(1.0, true, std::nullopt, true);
    drag.tracker.cancel();
    for (int i = 0; i < 60; ++i) REQUIRE_FALSE(drag.step(0.0, false).resume_playback);
    REQUIRE(drag.tracker.phase() == scrub_phase_t::idle);
}
