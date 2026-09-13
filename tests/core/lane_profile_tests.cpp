#include <wormedit/core/lane_profiles.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

using namespace wormedit;

namespace {

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

TEST_CASE("BMS 7K built-in profile declares seven keys, scratch, and number-row bindings",
          "[unit][core][lane-profile]") {
    const auto profile = make_bms_7k_profile();
    REQUIRE(profile.layout.lanes.size() == 8);
    CHECK(profile.layout.profile_id == "bme-7k-outside-scratch");
    CHECK(profile.layout.lanes.front().role == lane_role_t::scratch);
    CHECK(profile.layout.lanes.front().bms_channels.visible == "16");
    CHECK(profile.layout.lanes.front().bms_channels.invisible == "36");
    CHECK(profile.layout.lanes.front().bms_channels.long_note == "56");
    CHECK(profile.layout.lanes.front().bms_channels.mine == "D6");
    CHECK(profile.layout.lanes[1].bms_channels.visible == "11");
    CHECK(profile.layout.lanes.back().bms_channels.visible == "19");
    REQUIRE(profile.input.bindings.size() == 8);
    CHECK(profile.input.bindings.front().key == physical_key_t::digit_1);
    CHECK(profile.input.bindings.back().key == physical_key_t::digit_8);
    CHECK(validate_input_profile(profile.layout, profile.input).empty());

    const auto inside = make_bms_7k_profile(scratch_placement_t::inside);
    REQUIRE(inside.layout.lanes.size() == 8);
    CHECK(inside.layout.lanes.back().role == lane_role_t::scratch);
    CHECK(inside.layout.lanes.back().bms_channels.visible == "16");
}

TEST_CASE("BMS 14K uses the exact interleaved keyboard map in lane order",
          "[unit][core][lane-profile]") {
    const auto profile = make_bms_14k_profile();
    REQUIRE(profile.layout.lanes.size() == 16);
    REQUIRE(profile.input.bindings.size() == 16);
    CHECK(profile.layout.split_stage);
    CHECK(profile.layout.split_after_column == 8);

    constexpr std::array left_keys{
        physical_key_t::key_q, physical_key_t::key_a, physical_key_t::key_w, physical_key_t::key_s,
        physical_key_t::key_e, physical_key_t::key_d, physical_key_t::key_r, physical_key_t::key_f,
    };
    constexpr std::array right_keys{
        physical_key_t::key_h, physical_key_t::key_u, physical_key_t::key_j, physical_key_t::key_i,
        physical_key_t::key_k, physical_key_t::key_o, physical_key_t::key_l, physical_key_t::key_p,
    };
    for (std::size_t index = 0; index < 8; ++index) {
        CHECK(profile.input.bindings[index].key == left_keys[index]);
        CHECK(profile.input.bindings[index + 8].key == right_keys[index]);
        CHECK(profile.input.bindings[index].lane == profile.layout.lanes[index].id);
        CHECK(profile.input.bindings[index + 8].lane == profile.layout.lanes[index + 8].id);
    }
    CHECK(profile.layout.lanes.front().bms_channels.visible == "16");
    CHECK(profile.layout.lanes.back().bms_channels.visible == "26");
    CHECK(profile.layout.lanes.back().bms_channels.invisible == "46");
    CHECK(profile.layout.lanes.back().bms_channels.long_note == "66");
    CHECK(profile.layout.lanes.back().bms_channels.mine == "E6");
    CHECK(validate_input_profile(profile.layout, profile.input).empty());

    const auto inside_scratch =
        make_bms_14k_profile(scratch_placement_t::inside, scratch_placement_t::inside);
    REQUIRE(inside_scratch.layout.lanes.size() == 16);
    CHECK(inside_scratch.layout.lanes[7].role == lane_role_t::scratch);
    CHECK(inside_scratch.input.bindings[7].key == physical_key_t::key_f);
    CHECK(inside_scratch.layout.lanes[8].role == lane_role_t::scratch);
    CHECK(inside_scratch.input.bindings[8].key == physical_key_t::key_h);
}

TEST_CASE("number-row input profile applies only to the selected stage",
          "[unit][core][lane-profile]") {
    const auto layout = make_bms_14k_profile().layout;
    const auto left = make_number_row_input_profile(layout, 0);
    const auto right = make_number_row_input_profile(layout, 1, "right-number-row");
    REQUIRE(left);
    REQUIRE(right);
    REQUIRE(left->bindings.size() == 8);
    REQUIRE(right->bindings.size() == 8);
    CHECK(left->bindings.front().lane == layout.lanes.front().id);
    CHECK(right->bindings.front().lane == layout.lanes[8].id);
    CHECK(left->bindings.front().key == physical_key_t::digit_1);
    CHECK(right->bindings.back().key == physical_key_t::digit_8);
    CHECK(validate_input_profile(layout, *left).empty());
    CHECK(validate_input_profile(layout, *right).empty());
    CHECK_FALSE(make_number_row_input_profile(layout, 2));
    CHECK_FALSE(make_number_row_input_profile(make_bms_7k_profile().layout, 1));
}

TEST_CASE("BMSON generic 24K and 48K use a stage-focused reusable piano bank",
          "[unit][core][lane-profile][bmson]") {
    const auto profile24 = make_bmson_generic_24k_profile();
    REQUIRE(profile24.layout.lanes.size() == 24);
    REQUIRE(profile24.input.bindings.size() == 24);
    CHECK(profile24.layout.profile_id == "bmson-generic-24keys");
    CHECK(profile24.layout.bmson_generic_keys_only);
    CHECK(profile24.layout.lanes.front().display_order == 0);
    CHECK(profile24.layout.lanes.back().display_order == 23);
    CHECK(profile24.input.bindings[0].key == physical_key_t::key_z);
    CHECK(profile24.input.bindings[12].key == physical_key_t::key_q);
    CHECK(profile24.input.bindings[13].key == physical_key_t::digit_2);
    CHECK(profile24.input.bindings[23].key == physical_key_t::key_u);
    CHECK(validate_input_profile(profile24.layout, profile24.input).empty());

    const auto profile48 = make_bmson_generic_48k_profile();
    REQUIRE(profile48.layout.lanes.size() == 48);
    REQUIRE(profile48.input.bindings.size() == 48);
    CHECK(profile48.layout.profile_id == "bmson-generic-48keys");
    CHECK(profile48.layout.split_after_column == 24);
    CHECK(profile48.input.requires_focused_stage);
    CHECK(profile48.input.stage_count == 2);
    CHECK(profile48.input.bindings[0].key == profile48.input.bindings[24].key);
    CHECK(profile48.input.bindings[0].focused_stage == 0);
    CHECK(profile48.input.bindings[24].focused_stage == 1);
    CHECK(profile48.input.bindings[0].lane != profile48.input.bindings[24].lane);
    CHECK(validate_input_profile(profile48.layout, profile48.input).empty());
}

TEST_CASE("input profile validation catches duplicate and inaccessible bindings",
          "[unit][core][lane-profile][negative]") {
    const auto profile = make_bms_7k_profile();

    auto duplicate_key = profile.input;
    duplicate_key.bindings[1].key = duplicate_key.bindings[0].key;
    CHECK(has_code(validate_input_profile(profile.layout, duplicate_key),
                   diagnostic_code_t::duplicate_binding));

    auto unknown_lane = profile.input;
    unknown_lane.bindings[0].lane = lane_id_t{9999};
    CHECK(has_code(validate_input_profile(profile.layout, unknown_lane),
                   diagnostic_code_t::unknown_lane));

    auto wrong_focus_policy = make_bmson_generic_48k_profile().input;
    wrong_focus_policy.bindings[0].focused_stage.reset();
    CHECK(has_code(
        validate_input_profile(make_bmson_generic_48k_profile().layout, wrong_focus_policy),
        diagnostic_code_t::invalid_stage));

    auto out_of_range_stage = make_bmson_generic_48k_profile();
    out_of_range_stage.input.bindings[0].focused_stage = 2;
    CHECK(has_code(validate_input_profile(out_of_range_stage.layout, out_of_range_stage.input),
                   diagnostic_code_t::invalid_stage));

    auto wrong_lane_stage = make_bmson_generic_48k_profile();
    wrong_lane_stage.input.bindings[0].focused_stage = 1;
    CHECK(has_code(validate_input_profile(wrong_lane_stage.layout, wrong_lane_stage.input),
                   diagnostic_code_t::invalid_stage));
}
