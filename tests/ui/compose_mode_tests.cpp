#include "ui/compose_mode.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
    CHECK(compose_effective_timing_lane_mask(mode, 0x05) == wormedit::default_timing_lane_mask);
}

TEST_CASE("Full mode never gains a timing-focus-only behavior", "[unit][frontend][compose-mode]") {
    const auto& full = compose_mode_profile(compose_mode_t::full);
    CHECK_FALSE(full.notes_dimmed);
    CHECK_FALSE(full.timing_lanes_forced_visible);
}

TEST_CASE("Optional sample columns keep independent visibility in both Compose modes", "[frontend][compose-mode][sample-controls]") {
    const auto volume = 1U << static_cast<unsigned>(wormedit::timing_lane_kind_t::volume);
    const auto set = 1U << static_cast<unsigned>(wormedit::timing_lane_kind_t::sample_set);
    const auto mode = GENERATE(compose_mode_t::full, compose_mode_t::timing_focus);
    const auto hidden = compose_effective_timing_lane_mask(mode, wormedit::default_timing_lane_mask);
    CHECK((hidden & wormedit::optional_timing_lane_mask) == 0);
    const auto volume_only = compose_effective_timing_lane_mask(mode, wormedit::default_timing_lane_mask | volume);
    CHECK((volume_only & wormedit::optional_timing_lane_mask) == volume);
    const auto both = compose_effective_timing_lane_mask(mode, wormedit::default_timing_lane_mask | volume | set);
    CHECK(both == wormedit::all_timing_lane_mask);
}
