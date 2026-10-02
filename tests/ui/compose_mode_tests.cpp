#include "ui/compose_mode.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit::ui;

TEST_CASE("Full mode keeps every Compose capability", "[unit][frontend][compose-mode]") {
    constexpr auto mode = compose_mode_t::full;
    STATIC_REQUIRE_FALSE(compose_notes_dimmed(mode));
    STATIC_REQUIRE(compose_notes_editable(mode));
    STATIC_REQUIRE(compose_note_palette_visible(mode));
    STATIC_REQUIRE(compose_inspector_available(mode));
    STATIC_REQUIRE_FALSE(compose_ctrl_click_inserts_matching_bpm(mode));
    STATIC_REQUIRE_FALSE(compose_alt_click_inserts_anchor(mode));
    CHECK(compose_effective_timing_lane_mask(mode, 0x05) == 0x05);
}

TEST_CASE("Timing focus mutes notes and forces timing lanes", "[unit][frontend][compose-mode]") {
    constexpr auto mode = compose_mode_t::timing_focus;
    STATIC_REQUIRE(compose_notes_dimmed(mode));
    STATIC_REQUIRE_FALSE(compose_notes_editable(mode));
    STATIC_REQUIRE_FALSE(compose_note_palette_visible(mode));
    STATIC_REQUIRE_FALSE(compose_inspector_available(mode));
    STATIC_REQUIRE(compose_ctrl_click_inserts_matching_bpm(mode));
    STATIC_REQUIRE(compose_alt_click_inserts_anchor(mode));
    CHECK(compose_effective_timing_lane_mask(mode, 0x05) == 0xff);
}

TEST_CASE("Full mode never gains a timing-focus-only behavior", "[unit][frontend][compose-mode]") {
    const auto& full = compose_mode_profile(compose_mode_t::full);
    CHECK_FALSE(full.notes_dimmed);
    CHECK_FALSE(full.timing_lanes_forced_visible);
}
