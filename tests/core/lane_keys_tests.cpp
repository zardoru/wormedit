#include <wormedit/core/lane_keys.h>
#include <wormedit/core/lane_profiles.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>

using namespace wormedit;

namespace {
void expect_default_keys(bool value) { CHECK(value); }
void check_lane_key(const input_profile_t& profile, lane_id_t lane, physical_key_t key) {
    const auto binding = std::ranges::find(profile.bindings, lane, &lane_binding_t::lane);
    REQUIRE(binding != profile.bindings.end());
    CHECK(binding->key == key);
}

TEST_CASE("Default lane keys assign DFJK by display order and leave unmatched layouts alone", "[core][default-lane-keys]") {
    auto profile = *make_osu_mania_profile(4);
    std::ranges::reverse(profile.layout.lanes);
    default_lane_keys_t defaults{.bindings = {{4, lane_key_bindings_t::dfjk()}}};
    REQUIRE(defaults.is_valid());
    const auto resolved = defaults.profile_for(profile.layout, profile.input);
    expect_default_keys(validate_input_profile(profile.layout, resolved).empty());
    const auto lanes = profile.layout.lanes_in_stage_order();
    check_lane_key(resolved, lanes[0].id, physical_key_t::key_d);
    check_lane_key(resolved, lanes[1].id, physical_key_t::key_f);
    check_lane_key(resolved, lanes[2].id, physical_key_t::key_j);
    check_lane_key(resolved, lanes[3].id, physical_key_t::key_k);
    const auto keys = lane_key_bindings_t::from(profile.layout, resolved);
    expect_default_keys(keys == lane_key_bindings_t::dfjk());
    const auto other = make_bms_7k_profile();
    expect_default_keys(defaults.profile_for(other.layout, other.input) == other.input);
    expect_default_keys(defaults.keys_for(3) == lane_key_bindings_t::number_row(3));
    defaults.bindings[4].keys[1] = defaults.bindings[4].keys[0];
    expect_default_keys(defaults.conflicting_key() == physical_key_hid_usage(physical_key_t::key_d));
    expect_default_keys(!defaults.is_valid());
}

TEST_CASE("Malformed conflicting default lane keys retain program defaults", "[core][default-lane-keys]") {
    const auto parsed = preferences_parse_effect_t::from(
        R"({"default_lane_keys":[{"count":4,"keys":[7,7,13,14]}],"general":{"autosave_minutes":8}})");
    expect_default_keys(parsed.succeeded && parsed.preferences.general.autosave_minutes == 8);
    expect_default_keys(parsed.preferences.default_lane_keys.bindings.empty());
    expect_default_keys(!parsed.warnings.empty());
}
}

TEST_CASE("Personal lane keys survive preference persistence as physical usages", "[core][lane-keys]") {
    auto preferences = program_preferences_t::with_defaults();
    preferences.lane_keys.layouts["custom"] = {.keys = {0x04, 0, 0x27}};
    const auto bytes = serialize_program_preferences(preferences);
    REQUIRE(bytes);
    const auto parsed = preferences_parse_effect_t::from(*bytes);
    CHECK(parsed.succeeded);
    CHECK(parsed.warnings.empty());
    CHECK(parsed.preferences == preferences);
    preferences.lane_keys.layouts["bad"] = {.keys = {0x2c}};
    CHECK_FALSE(serialize_program_preferences(preferences));
}

TEST_CASE("Malformed lane keys are ignored without losing other preferences", "[core][lane-keys]") {
    const auto parsed = preferences_parse_effect_t::from(
        R"({"lane_keys":{"bad":[44],"good":[4,0,39]},"general":{"autosave_minutes":8}})");
    CHECK(parsed.succeeded);
    CHECK(parsed.preferences.general.autosave_minutes == 8);
    CHECK_FALSE(parsed.preferences.lane_keys.layouts.contains("bad"));
    CHECK(parsed.preferences.lane_keys.layouts.at("good").keys == std::vector<std::uint16_t>{4, 0, 39});
    CHECK_FALSE(parsed.warnings.empty());
}

TEST_CASE("Personal lane keys resolve by display order across chart identities", "[core][lane-keys]") {
    auto profile = make_bms_7k_profile();
    auto keys = lane_key_bindings_t::from(profile.layout, profile.input);
    keys.keys[0] = 0x04;
    lane_key_preferences_t preferences{.layouts = {{profile.layout.profile_id, keys}}};
    lane_key_profile_cache_t cache;
    const auto resolved = cache.resolve(preferences, profile.layout, profile.input);
    CHECK(resolved.bindings.front().key == physical_key_t::key_a);
    std::ranges::reverse(profile.layout.lanes);
    for (auto& lane : profile.layout.lanes) lane.id.value += 1000;
    for (auto& binding : profile.input.bindings) binding.lane.value += 1000;
    const auto& second = cache.resolve(preferences, profile.layout, profile.input);
    CHECK(second.bindings.front().key == physical_key_t::key_a);
    CHECK(second.bindings.front().lane.value == resolved.bindings.front().lane.value + 1000);
    const auto* storage = second.bindings.data();
    CHECK(cache.resolve(preferences, profile.layout, profile.input).bindings.data() == storage);
    CHECK(profile.input.bindings.front().key != physical_key_t::key_a);
}

TEST_CASE("Duplicate or mismatched personal keys fall back to authored bindings", "[core][lane-keys]") {
    const auto profile = make_bms_7k_profile();
    auto keys = lane_key_bindings_t::from(profile.layout, profile.input);
    keys.keys[1] = keys.keys[0];
    CHECK_FALSE(keys.profile_for(profile.layout, profile.input));
    lane_key_preferences_t preferences{.layouts = {{profile.layout.profile_id, keys}}};
    lane_key_profile_cache_t cache;
    CHECK(cache.resolve(preferences, profile.layout, profile.input) == profile.input);
    preferences.layouts.begin()->second.keys.pop_back();
    CHECK(cache.resolve(preferences, profile.layout, profile.input) == profile.input);
    preferences.layouts.clear();
    CHECK(cache.resolve(preferences, profile.layout, profile.input) == profile.input);
}

TEST_CASE("Lane-key presets share catalog defaults and the DP interleaved factory", "[core][lane-keys]") {
    const auto* descriptor = find_new_project_layout("bme-14k-left-outside-right-outside");
    REQUIRE(descriptor);
    const auto profile = descriptor->profile();
    const auto defaults = lane_key_bindings_t::preset(profile.layout, profile.input, false);
    const auto interleaved = lane_key_bindings_t::preset(profile.layout, profile.input, true);
    CHECK(defaults == lane_key_bindings_t::from(profile.layout, profile.input));
    const auto factory = make_bms_14k_profile();
    CHECK(interleaved == lane_key_bindings_t::from(factory.layout, factory.input));
    CHECK(defaults != interleaved);
    CHECK(defaults.profile_for(profile.layout, profile.input).has_value());
}

TEST_CASE("DP7 lane-key presets match the stage cards rather than interleaving equal display columns",
          "[core][lane-keys]") {
    const auto profile = make_bms_14k_profile();
    const auto defaults = lane_key_bindings_t::preset(profile.layout, profile.input, false);
    const auto interleaved = lane_key_bindings_t::preset(profile.layout, profile.input, true);
    CHECK(lane_key_bindings_t::has_interleaved_preset(profile.layout));
    const auto wormedit = defaults.profile_for(profile.layout, profile.input);
    const auto ibmsc = interleaved.profile_for(profile.layout, profile.input);
    REQUIRE(wormedit);
    REQUIRE(ibmsc);
    check_lane_key(*wormedit, lane_id_t{108}, physical_key_t::digit_1);
    check_lane_key(*wormedit, lane_id_t{101}, physical_key_t::key_q);
    check_lane_key(*wormedit, lane_id_t{102}, physical_key_t::digit_2);
    check_lane_key(*wormedit, lane_id_t{201}, physical_key_t::key_y);
    check_lane_key(*wormedit, lane_id_t{208}, physical_key_t::digit_0);
    check_lane_key(*ibmsc, lane_id_t{108}, physical_key_t::key_q);
    check_lane_key(*ibmsc, lane_id_t{101}, physical_key_t::key_a);
    check_lane_key(*ibmsc, lane_id_t{201}, physical_key_t::key_h);
    check_lane_key(*ibmsc, lane_id_t{208}, physical_key_t::key_p);
}

TEST_CASE("Interleaved presets retain factory bindings for every DP7 scratch placement", "[core][lane-keys]") {
    const auto left = GENERATE(scratch_placement_t::outside, scratch_placement_t::inside);
    const auto right = GENERATE(scratch_placement_t::outside, scratch_placement_t::inside);
    const auto profile = make_bms_14k_profile(left, right);
    CHECK(lane_key_bindings_t::has_interleaved_preset(profile.layout));
    const auto keys = lane_key_bindings_t::preset(profile.layout, profile.input, true);
    const auto applied = keys.profile_for(profile.layout, profile.input);
    REQUIRE(applied);
    CHECK(*applied == profile.input);
}
