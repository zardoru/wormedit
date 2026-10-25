#include "sys/app/display_health.h"
#include "sys/app/frontend_event_batch.h"
#include <catch2/catch_test_macros.hpp>

using namespace wormedit::sys;

TEST_CASE("Display health reports one stall and its recovery with an injected clock", "[sys][display-health]") {
    display_health_t health;
    CHECK_FALSE(health.observe(100, 0, frame_phase_t::events));
    const auto stalled = health.observe(2'000'000'001, 1, frame_phase_t::render_present);
    REQUIRE(stalled);
    CHECK(stalled->kind == display_notice_kind_t::stalled);
    CHECK(stalled->phase == frame_phase_t::render_present);
    CHECK_FALSE(health.observe(3'000'000'000, 1, frame_phase_t::render_present));
    const auto recovered = health.observe(3'000'000'001, 3'000'000'000, frame_phase_t::events);
    REQUIRE(recovered);
}

TEST_CASE("Display health rejects clock rollback and rearms after recovery", "[sys][display-health]") {
    display_health_t health;
    CHECK_FALSE(health.observe(10, 20, frame_phase_t::render));
    REQUIRE(health.observe(2'000'000'001, 1, frame_phase_t::render_present));
    const auto recovered = health.observe(3'000'000'001, 3'000'000'000, frame_phase_t::events);
    REQUIRE(recovered);
    CHECK(recovered->kind == display_notice_kind_t::resumed);
    CHECK_FALSE(health.observe(3'000'000'001, 3'000'000'000, frame_phase_t::events));
    CHECK(health.observe(5'000'000'000, 3'000'000'000, frame_phase_t::render_present));
}

namespace {
void expect_one_fps_throttling(const display_notice_t& notice) {
    CHECK(notice.kind == display_notice_kind_t::throttled);
    CHECK(notice.duration_ns == 1'000'000'000);
}
}

TEST_CASE("Ten consecutive slow frames produce one throttling notice", "[sys][display-health]") {
    display_health_t health;
    for (int index = 0; index < 9; ++index) CHECK_FALSE(health.record_frame(1'000'000'000));
    const auto slow = health.record_frame(1'000'000'000);
    REQUIRE(slow);
    expect_one_fps_throttling(*slow);
    CHECK_FALSE(health.record_frame(1'000'000'000));
    CHECK_FALSE(health.record_frame(5'000'000));
}

TEST_CASE("Wayland preference preserves explicit video-driver overrides", "[sys][display-health]") {
    CHECK(preferred_video_drivers("wayland-1", "") == "wayland,x11");
    CHECK_FALSE(preferred_video_drivers("wayland-1", "x11"));
    CHECK_FALSE(preferred_video_drivers("wayland-1", "wayland"));
    CHECK_FALSE(preferred_video_drivers("", ""));
    CHECK_FALSE(preferred_video_drivers("", "dummy"));
}

namespace {
auto key_edge(SDL_EventType type, SDL_Scancode scan) -> SDL_Event {
    SDL_Event event{};
    event.type = type;
    event.key.scancode = scan;
    return event;
}
auto mouse_edge(SDL_EventType type, Uint8 button) -> SDL_Event {
    SDL_Event event{};
    event.type = type;
    event.button.button = button;
    return event;
}
}

TEST_CASE("A fast key pair retains its release for the next input frame", "[sys][display-health][input-batch]") {
    frontend_event_batch_t batch;
    batch.begin();
    CHECK(batch.admit(key_edge(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE)));
    CHECK_FALSE(batch.admit(key_edge(SDL_EVENT_KEY_UP, SDL_SCANCODE_SPACE)));
    batch.begin();
    const auto pending = batch.pending();
    REQUIRE(pending);
    CHECK(pending->type == SDL_EVENT_KEY_UP);
    CHECK(batch.admit(*pending));
    CHECK_FALSE(batch.pending());
}

TEST_CASE("Button edges preserve order across frames without delaying unrelated controls", "[sys][display-health][input-batch]") {
    frontend_event_batch_t batch;
    batch.begin();
    CHECK(batch.admit(mouse_edge(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT)));
    CHECK(batch.admit(mouse_edge(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT)));
    CHECK_FALSE(batch.admit(mouse_edge(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT)));
    batch.begin();
    const auto pending = batch.pending();
    REQUIRE(pending);
    REQUIRE(batch.admit(*pending));
    CHECK_FALSE(batch.admit(mouse_edge(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT)));
}
