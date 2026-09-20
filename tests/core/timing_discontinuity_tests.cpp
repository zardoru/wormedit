#include <wormedit/core/timing_discontinuity.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

TEST_CASE("timing discontinuity profiles resolve explicitly or from source family",
          "[unit][core][timing][discontinuity]") {
    CHECK(resolve_timing_discontinuity_profile(format_family_t::native_project) ==
          timing_discontinuity_profile_t::canonical);
    CHECK(resolve_timing_discontinuity_profile(format_family_t::bms) ==
          timing_discontinuity_profile_t::canonical);
    CHECK(resolve_timing_discontinuity_profile(format_family_t::bmson) ==
          timing_discontinuity_profile_t::bmson_1_0);
    CHECK(resolve_timing_discontinuity_profile(
              format_family_t::bmson, timing_discontinuity_profile_t::canonical) ==
          timing_discontinuity_profile_t::canonical);
    CHECK_FALSE(valid_timing_discontinuity_profile(
        static_cast<timing_discontinuity_profile_t>(255)));
}

TEST_CASE("BMSON discontinuities activate presentation before BPM and stop",
          "[unit][core][timing][discontinuity][bmson]") {
    const auto profile = timing_discontinuity_profile_t::bmson_1_0;
    CHECK(timing_discontinuity_order(profile, preview_discontinuity_kind_t::playable) <
          timing_discontinuity_order(profile, preview_discontinuity_kind_t::bpm));
    CHECK(timing_discontinuity_order(profile, preview_discontinuity_kind_t::bga) <
          timing_discontinuity_order(profile, preview_discontinuity_kind_t::bpm));
    CHECK(timing_discontinuity_order(profile, preview_discontinuity_kind_t::bpm) <
          timing_discontinuity_order(profile, preview_discontinuity_kind_t::stop));
}

TEST_CASE("canonical discontinuities process BPM before delay event and stop phases",
          "[unit][core][timing][discontinuity]") {
    const auto profile = timing_discontinuity_profile_t::canonical;
    CHECK(timing_discontinuity_order(profile, preview_discontinuity_kind_t::bpm) <
          timing_discontinuity_order(profile, preview_discontinuity_kind_t::delay));
    CHECK(timing_discontinuity_order(profile, preview_discontinuity_kind_t::delay) <
          timing_discontinuity_order(profile, preview_discontinuity_kind_t::playable));
    CHECK(timing_discontinuity_order(profile, preview_discontinuity_kind_t::playable) <
          timing_discontinuity_order(profile, preview_discontinuity_kind_t::stop));
}
