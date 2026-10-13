#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/editor_scene.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace wormedit;

namespace {

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

auto standard_5k_profile_valid(const lane_input_profile_t& profile, std::string_view id) -> bool {
    const auto& lanes = profile.layout.lanes;
    return profile.layout.profile_id == id && lanes.size() == 6 &&
           std::ranges::count(lanes, lane_role_t::playable, &lane_definition_t::role) == 5 &&
           std::ranges::count(lanes, lane_role_t::scratch, &lane_definition_t::role) == 1 &&
           std::ranges::none_of(lanes, [](const auto& lane) {
               return lane.role == lane_role_t::free_zone || lane.role == lane_role_t::foot_pedal;
           }) &&
           profile.input.bindings.size() == 6 &&
           profile.input.bindings[0].key == physical_key_t::digit_1 &&
           profile.input.bindings[5].key == physical_key_t::digit_6 &&
           validate_input_profile(profile.layout, profile.input).empty();
}

} // namespace

namespace {
template <typename T>
void check_bmson_condition(const T& condition) { CHECK(static_cast<bool>(condition)); }
template <typename T>
void require_bmson_condition(const T& condition) { REQUIRE(static_cast<bool>(condition)); }
} // namespace

TEST_CASE("built-in BMS profile registry is complete unique and frontend-neutral",
          "[unit][core][lane-profile][bms][registry]") {
    const auto profiles = built_in_bms_lane_profiles();
    REQUIRE(profiles.size() == 18);

    std::set<std::string> ids;
    for (const auto& descriptor : profiles) {
        const auto& profile = descriptor.profile;
        CAPTURE(profile.layout.profile_id);
        CHECK_FALSE(profile.layout.profile_id.empty());
        CHECK_FALSE(profile.layout.display_name.empty());
        CHECK(ids.insert(profile.layout.profile_id).second);
        CHECK_FALSE(profile.layout.lanes.empty());
        CHECK(validate_input_profile(profile.layout, profile.input).empty());
        if (descriptor.player_header_two_stage)
            CHECK(profile.layout.split_stage);
        if (descriptor.octave_fp_header)
            CHECK(profile.layout.profile_id == "bms-octave-foot-pedal");
    }

    CHECK(std::ranges::count_if(profiles, [](const auto& descriptor) {
        return descriptor.family == bms_lane_profile_family_t::bms_five_or_ten_key;
    }) == 6);
    CHECK(std::ranges::count_if(profiles, [](const auto& descriptor) {
              return descriptor.family == bms_lane_profile_family_t::bme_seven_or_fourteen_key;
          }) == 4);
    CHECK(std::ranges::count_if(profiles, [](const auto& descriptor) {
              return descriptor.family == bms_lane_profile_family_t::popn;
          }) == 4);
    CHECK(std::ranges::count_if(profiles, [](const auto& descriptor) {
              return descriptor.family == bms_lane_profile_family_t::special;
          }) == 4);
}

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

TEST_CASE("standard BMS 5K maps five keys and scratch without an auxiliary lane",
          "[unit][core][lane-profile][bms][5k]") {
    CHECK(standard_5k_profile_valid(make_bms_5k_standard_profile(scratch_placement_t::outside),
                                    "bms-5k-outside-scratch"));
    CHECK(standard_5k_profile_valid(make_bms_5k_standard_profile(scratch_placement_t::inside),
                                    "bms-5k-inside-scratch"));
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

TEST_CASE("BMS, BME, PMS, and named compatibility profiles preserve memo channel maps",
          "[unit][core][lane-profile][bms]") {
    struct channel_case_t {
        std::string_view name;
        lane_input_profile_t profile;
        std::vector<std::string_view> channels;
    };
    const std::vector cases{
        channel_case_t{
            "BMS 5K free-zone", make_bms_5k_profile(), {"16", "11", "12", "13", "14", "15", "17"}},
        channel_case_t{"BMS 5K foot-pedal",
                       make_bms_5k_foot_pedal_profile(),
                       {"16", "11", "12", "13", "14", "15", "17"}},
        channel_case_t{
            "BMS DP5 without free zones",
            make_bms_10k_standard_profile(),
            {"16", "11", "12", "13", "14", "15", "21", "22", "23", "24", "25", "26"}},
        channel_case_t{
            "BMS 10K free-zone",
            make_bms_10k_free_zone_profile(),
            {"16", "11", "12", "13", "14", "15", "17", "21", "22", "23", "24", "25", "26", "27"}},
        channel_case_t{
            "BMS 10K foot-pedal",
            make_bms_10k_foot_pedal_profile(),
            {"16", "11", "12", "13", "14", "15", "17", "21", "22", "23", "24", "25", "26", "27"}},
        channel_case_t{"BME 7K foot-pedal",
                       make_bme_7k_foot_pedal_profile(),
                       {"16", "11", "12", "13", "14", "15", "18", "19", "17"}},
        channel_case_t{"BME 14K foot-pedal",
                       make_bme_14k_foot_pedal_profile(),
                       {"16", "11", "12", "13", "14", "15", "18", "19", "17", "21", "22", "23",
                        "24", "25", "28", "29", "26", "27"}},
        channel_case_t{"PMS 5-button", make_pms_5k_profile(), {"13", "14", "15", "22", "23"}},
        channel_case_t{"PMS standard 9-button",
                       make_pms_9k_profile(),
                       {"11", "12", "13", "14", "15", "22", "23", "24", "25"}},
        channel_case_t{"PMS BME-style 9-button",
                       make_pms_bme_style_9k_profile(),
                       {"11", "12", "13", "14", "15", "18", "19", "16", "17"}},
        channel_case_t{"PMS BME-style 18-button",
                       make_pms_bme_style_18k_profile(),
                       {"11", "12", "13", "14", "15", "18", "19", "16", "17", "21", "22", "23",
                        "24", "25", "28", "29", "26", "27"}},
        channel_case_t{"Double scratch / FPP",
                       make_bms_double_scratch_profile(),
                       {"16", "11", "12", "13", "14", "15", "18", "19", "26"}},
        channel_case_t{"Octave / foot-pedal",
                       make_bms_octave_fp_profile(),
                       {"16", "11", "12", "13", "14", "15", "18", "19", "22", "23", "24", "25",
                        "28", "29", "26", "21"}},
        channel_case_t{"Project2DX 7K",
                       make_project2dx_7k_profile(),
                       {"16", "11", "12", "13", "14", "15", "21", "22"}},
    };

    for (const auto& item : cases) {
        INFO(item.name);
        REQUIRE(item.profile.layout.lanes.size() == item.channels.size());
        for (std::size_t index = 0; index < item.channels.size(); ++index) {
            const auto& lane = item.profile.layout.lanes[index];
            REQUIRE(lane.bms_channels.visible);
            INFO(item.name << " lane " << index << " actual " << *lane.bms_channels.visible
                           << " expected " << item.channels[index]);
            CHECK(*lane.bms_channels.visible == item.channels[index]);
            const auto expected_order = static_cast<std::uint32_t>(std::ranges::count_if(
                item.profile.layout.lanes.begin(), item.profile.layout.lanes.begin() + index,
                [&lane](const auto& previous) { return previous.stage == lane.stage; }));
            CHECK(lane.display_order == expected_order);

            const auto channel = *lane.bms_channels.visible;
            auto invisible = channel;
            auto long_note = channel;
            auto mine = channel;
            invisible[0] = channel[0] == '1' ? '3' : '4';
            long_note[0] = channel[0] == '1' ? '5' : '6';
            mine[0] = channel[0] == '1' ? 'D' : 'E';
            REQUIRE(lane.bms_channels.invisible);
            REQUIRE(lane.bms_channels.long_note);
            REQUIRE(lane.bms_channels.mine);
            CHECK(*lane.bms_channels.invisible == invisible);
            CHECK(*lane.bms_channels.long_note == long_note);
            CHECK(*lane.bms_channels.mine == mine);
        }
        CHECK(validate_input_profile(item.profile.layout, item.profile.input).empty());
    }

    const auto ten_key = make_bms_10k_profile();
    CHECK(ten_key.layout.profile_id == make_bms_10k_free_zone_profile().layout.profile_id);
    CHECK(ten_key.layout.split_stage);
    CHECK(ten_key.layout.split_after_column == 7);
    CHECK(ten_key.layout.lanes[0].stage == stage_group_t::left);
    CHECK(ten_key.layout.lanes[7].stage == stage_group_t::right);
    CHECK(ten_key.layout.lanes[6].role == lane_role_t::free_zone);
    CHECK(ten_key.layout.lanes[13].role == lane_role_t::free_zone);
    REQUIRE(ten_key.input.bindings.size() == 12);
    CHECK(ten_key.input.bindings.front().key == physical_key_t::digit_1);
    CHECK(ten_key.input.bindings[6].key == physical_key_t::key_q);

    const auto five_key_pedal = make_bms_5k_foot_pedal_profile();
    CHECK(five_key_pedal.layout.lanes.back().role == lane_role_t::foot_pedal);
    CHECK(five_key_pedal.layout.lanes.back().bms_channels.visible == "17");
    REQUIRE(five_key_pedal.input.bindings.size() == 6);
    CHECK(make_bms_5k_profile().layout.profile_id ==
          make_bms_5k_free_zone_profile().layout.profile_id);

    const auto seven_key_pedal = make_bme_7k_foot_pedal_profile();
    CHECK(seven_key_pedal.layout.lanes.back().role == lane_role_t::foot_pedal);
    CHECK(seven_key_pedal.layout.lanes.back().bms_channels.visible == "17");
    REQUIRE(seven_key_pedal.input.bindings.size() == 8);

    const auto fourteen_key_pedals = make_bme_14k_foot_pedal_profile();
    CHECK(fourteen_key_pedals.layout.split_after_column == 9);
    CHECK(fourteen_key_pedals.layout.lanes[8].role == lane_role_t::foot_pedal);
    CHECK(fourteen_key_pedals.layout.lanes[17].role == lane_role_t::foot_pedal);
    REQUIRE(fourteen_key_pedals.input.bindings.size() == 16);

    const auto pms_standard = make_pms_9k_profile();
    CHECK(pms_standard.layout.lanes[5].stage == stage_group_t::left);
    CHECK(pms_standard.layout.lanes[5].bms_channels.visible == "22");
    CHECK(pms_standard.layout.lanes[5].role == lane_role_t::playable);

    const auto pms_eighteen = make_pms_bme_style_18k_profile();
    CHECK(pms_eighteen.layout.split_after_column == 9);
    CHECK(pms_eighteen.layout.lanes[8].stage == stage_group_t::left);
    CHECK(pms_eighteen.layout.lanes[9].stage == stage_group_t::right);
    CHECK(pms_eighteen.input.bindings[0].key == physical_key_t::digit_1);
    CHECK(pms_eighteen.input.bindings[9].key == physical_key_t::key_q);

    const auto double_scratch = make_bms_double_scratch_profile();
    CHECK(double_scratch.layout.lanes.front().role == lane_role_t::scratch);
    CHECK(double_scratch.layout.lanes.back().role == lane_role_t::scratch);

    const auto octave = make_bms_octave_fp_profile();
    CHECK(octave.layout.lanes.back().role == lane_role_t::foot_pedal);
    CHECK(octave.layout.lanes.back().stage == stage_group_t::left);
    CHECK(octave.layout.lanes.back().display_order == 15);
    CHECK(octave.input.profile_id == "bms-octave-fp-remap-required");
    CHECK(octave.input.bindings.empty());

    const auto project2dx = make_project2dx_7k_profile();
    CHECK(project2dx.layout.lanes[6].bms_channels.visible == "21");
    CHECK(project2dx.layout.lanes[6].role == lane_role_t::playable);
    CHECK(project2dx.layout.lanes[6].stage == stage_group_t::left);
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

namespace {
void check_default_lane_binding(const input_profile_t& profile, std::size_t index) {
    CHECK(profile.bindings[index].lane == lane_id_t{index + 1});
    const std::array keys{physical_key_t::digit_1, physical_key_t::digit_2, physical_key_t::digit_3,
        physical_key_t::digit_4, physical_key_t::digit_5, physical_key_t::digit_6,
        physical_key_t::digit_7, physical_key_t::digit_8, physical_key_t::digit_9, physical_key_t::digit_0};
    CHECK(profile.bindings[index].key == keys[index]);
}
void check_default_layout_count(std::uint32_t count) {
    lane_layout_t layout;
    for (std::uint32_t index = 0; index < count; ++index)
        layout.lanes.push_back({.id = lane_id_t{index + 1}, .name = "Key",
            .stage = index < count / 2 ? stage_group_t::left : stage_group_t::right,
            .display_order = index});
    const auto input = default_input_profile_for(layout);
    REQUIRE(input);
    CHECK(input->bindings.size() == count);
    CHECK(input->profile_id == "number-row-default");
    for (std::size_t index = 0; index < count; ++index) check_default_lane_binding(*input, index);
}
}
TEST_CASE("default input binds every single-stage lane despite semantic stage tags", "[lane-profile][default-input]") {
    for (const auto count : {4U, 7U, 10U}) check_default_layout_count(count);
}
TEST_CASE("default input rejects oversized single stages", "[lane-profile][default-input]") {
    auto layout = make_bms_octave_fp_profile().layout;
    layout.lanes.resize(11);
    CHECK_FALSE(default_input_profile_for(layout));
}
TEST_CASE("default input reuses split-stage digit and QWERTY rows", "[lane-profile][default-input]") {
    const auto profile = make_bms_10k_profile();
    const auto input = default_input_profile_for(profile.layout);
    REQUIRE(input);
    CHECK(input->bindings.size() == profile.input.bindings.size());
    CHECK(input->bindings.front().key == physical_key_t::digit_1);
    CHECK(input->bindings.back().key == physical_key_t::key_y);
    CHECK(input->stage_count == 2);
    CHECK(validate_input_profile(profile.layout, *input).empty());
}

TEST_CASE("double-play profiles are named DP5 and DP7 with outside scratches by default",
          "[unit][core][lane-profile][bms][double-play]") {
    const auto dp5 = make_bms_10k_free_zone_profile();
    const auto dp7 = make_bme_14k_profile();
    CHECK(dp5.layout.display_name.find("DP5") != std::string::npos);
    CHECK(dp7.layout.display_name.find("DP7") != std::string::npos);
    // Profile ids are stored in projects and must not change.
    CHECK(dp5.layout.profile_id == "bms-10k-left-outside-right-outside-free-zone");
    CHECK(dp7.layout.profile_id == "bme-14k-left-outside-right-outside");

    for (const auto& profile : {dp5, dp7}) {
        const auto& lanes = profile.layout.lanes;
        REQUIRE_FALSE(lanes.empty());
        CHECK(lanes.front().role == lane_role_t::scratch);
        CHECK(lanes.front().stage == stage_group_t::left);
        const auto scratches = std::ranges::count(lanes, lane_role_t::scratch, &lane_definition_t::role);
        CHECK(scratches == 2);
        const auto right_scratch = std::ranges::find_if(lanes, [](const auto& lane) {
            return lane.role == lane_role_t::scratch && lane.stage == stage_group_t::right;
        });
        REQUIRE(right_scratch != lanes.end());
        const auto right_keys = std::ranges::count_if(lanes, [&](const auto& lane) {
            return lane.role == lane_role_t::playable && lane.stage == stage_group_t::right &&
                   lane.display_order > right_scratch->display_order;
        });
        CHECK(right_keys == 0);
    }
}

namespace {
void check_standard_dp5_layout(scratch_placement_t left, scratch_placement_t right) {
    const auto profile = make_bms_10k_standard_profile(left, right);
    CHECK(profile.layout.lanes.size() == 12);
    CHECK(std::ranges::count(profile.layout.lanes, lane_role_t::playable,
                            &lane_definition_t::role) == 10);
    CHECK(std::ranges::count(profile.layout.lanes, lane_role_t::scratch,
                            &lane_definition_t::role) == 2);
    CHECK(profile.layout.split_after_column == 6);
    CHECK(profile.input.bindings.size() == 12);
    CHECK(validate_input_profile(profile.layout, profile.input).empty());
}
} // namespace

TEST_CASE("DP5 offers a twelve-lane layout without free zones",
          "[unit][core][lane-profile][dp5-standard]") {
    const auto profile = make_bms_10k_standard_profile();
    CHECK(profile.layout.profile_id == "bms-10k-left-outside-right-outside");
    CHECK(profile.layout.display_name == "BMS DP5 / 10K");
    for (const auto left : {scratch_placement_t::outside, scratch_placement_t::inside})
        for (const auto right : {scratch_placement_t::outside, scratch_placement_t::inside})
            check_standard_dp5_layout(left, right);
}

TEST_CASE("scratchless 10K has ten playable keys and keeps the 10K lane ids",
          "[unit][core][lane-profile][bms][double-play]") {
    const auto profile = make_bms_10k_scratchless_profile();
    const auto reference = make_bms_10k_free_zone_profile();
    CHECK(profile.layout.profile_id == "bms-10k-scratchless");
    REQUIRE(profile.layout.lanes.size() == 10);
    CHECK(profile.layout.split_stage);
    CHECK(std::ranges::all_of(profile.layout.lanes, [](const auto& lane) {
        return lane.role == lane_role_t::playable;
    }));
    for (const auto& lane : profile.layout.lanes) {
        const auto match = std::ranges::find(reference.layout.lanes, lane.id, &lane_definition_t::id);
        REQUIRE(match != reference.layout.lanes.end());
        CHECK(match->bms_channels.visible == lane.bms_channels.visible);
    }
    CHECK(validate_input_profile(profile.layout, profile.input).empty());
}

TEST_CASE("pump-single and dance-solo profiles carry arrow glyphs and number-row bindings",
          "[unit][core][lane-profile][stepmania]") {
    const auto pump = make_stepmania_pump_single_profile();
    REQUIRE(pump.layout.lanes.size() == 5);
    CHECK(pump.layout.profile_id == "stepmania:pump-single");
    CHECK(pump.layout.lanes[0].glyph == lane_glyph_t::arrow_down_left);
    CHECK(pump.layout.lanes[1].glyph == lane_glyph_t::arrow_up_left);
    CHECK(pump.layout.lanes[2].glyph == lane_glyph_t::arrow_center);
    CHECK(pump.layout.lanes[3].glyph == lane_glyph_t::arrow_up_right);
    CHECK(pump.layout.lanes[4].glyph == lane_glyph_t::arrow_down_right);
    REQUIRE(pump.input.bindings.size() == 5);
    CHECK(pump.input.bindings.front().key == physical_key_t::digit_1);
    CHECK(pump.input.bindings.back().key == physical_key_t::digit_5);
    CHECK(validate_input_profile(pump.layout, pump.input).empty());

    const auto solo = make_stepmania_dance_solo_profile();
    REQUIRE(solo.layout.lanes.size() == 6);
    CHECK(solo.layout.profile_id == "stepmania:dance-solo");
    CHECK(solo.layout.lanes[0].glyph == lane_glyph_t::arrow_left);
    CHECK(solo.layout.lanes[1].glyph == lane_glyph_t::arrow_up_left);
    CHECK(solo.layout.lanes[2].glyph == lane_glyph_t::arrow_down);
    CHECK(solo.layout.lanes[3].glyph == lane_glyph_t::arrow_up);
    CHECK(solo.layout.lanes[4].glyph == lane_glyph_t::arrow_up_right);
    CHECK(solo.layout.lanes[5].glyph == lane_glyph_t::arrow_right);
    REQUIRE(solo.input.bindings.size() == 6);
    CHECK(solo.input.bindings.back().key == physical_key_t::digit_6);
    CHECK(validate_input_profile(solo.layout, solo.input).empty());

    CHECK(stepmania_lane_specs("PUMP-Single").size() == 5);
    CHECK(stepmania_lane_specs("dance-single").empty());
    // Stored glyph values from before the diagonal glyphs keep their meaning.
    CHECK(static_cast<int>(lane_glyph_t::arrow_right) == 5);
    CHECK(static_cast<int>(lane_glyph_t::arrow_up_left) == 6);
}

namespace {

auto key_color_sequence(const lane_layout_t& layout, stage_group_t stage) -> std::vector<lane_key_color_t> {
    std::vector<const lane_definition_t*> lanes;
    for (const auto& lane : layout.lanes) {
        if (lane.stage == stage) lanes.push_back(&lane);
    }
    std::ranges::sort(lanes, {}, &lane_definition_t::display_order);
    std::vector<lane_key_color_t> colors;
    for (const auto* lane : lanes) colors.push_back(lane->key_color);
    return colors;
}

} // namespace

TEST_CASE("BME 7K key colors follow the instrument wherever the scratch sits",
          "[unit][core][lane-profile][key-color]") {
    using color = lane_key_color_t;
    const std::vector<color> keys{color::white, color::black, color::white, color::black,
                                  color::white, color::black, color::white};

    auto outside = key_color_sequence(make_bme_7k_profile(scratch_placement_t::outside).layout,
                                      stage_group_t::left);
    REQUIRE(outside.size() == 8);
    CHECK(outside.front() == color::scratch);
    CHECK(std::vector<color>(outside.begin() + 1, outside.end()) == keys);

    auto inside = key_color_sequence(make_bme_7k_profile(scratch_placement_t::inside).layout,
                                     stage_group_t::left);
    REQUIRE(inside.size() == 8);
    CHECK(inside.back() == color::scratch);
    CHECK(std::vector<color>(inside.begin(), inside.end() - 1) == keys);
}

TEST_CASE("pedals free zones PMS buttons and piano banks get their own classes",
          "[unit][core][lane-profile][key-color]") {
    using color = lane_key_color_t;
    const auto pedal = key_color_sequence(make_bme_7k_foot_pedal_profile().layout, stage_group_t::left);
    CHECK(std::ranges::count(pedal, color::foot_pedal) == 1);
    const auto free_zone = key_color_sequence(make_bms_5k_free_zone_profile().layout, stage_group_t::left);
    CHECK(std::ranges::count(free_zone, color::free_zone) == 1);

    const auto pms = key_color_sequence(make_pms_9k_profile().layout, stage_group_t::left);
    CHECK(pms == std::vector<color>{color::pms_white, color::pms_yellow, color::pms_green,
                                    color::pms_blue, color::pms_red, color::pms_blue,
                                    color::pms_green, color::pms_yellow, color::pms_white});
    CHECK(std::ranges::all_of(key_color_sequence(make_pms_5k_profile().layout, stage_group_t::left),
                              [](auto value) { return value == color::generic; }));

    const auto piano = key_color_sequence(make_bmson_generic_24k_profile().layout, stage_group_t::none);
    REQUIRE(piano.size() == 24);
    CHECK(piano[0] == color::white);
    CHECK(piano[1] == color::black);
    CHECK(piano[4] == color::white);
    CHECK(piano[5] == color::white);
    CHECK(piano[6] == color::black);
    CHECK(piano[12] == color::white);
    CHECK(piano[13] == color::black);

    const auto dance = make_stepmania_pump_single_profile().layout;
    CHECK(std::ranges::all_of(dance.lanes, [](const auto& lane) { return lane.key_color == color::generic; }));
}

TEST_CASE("BMSON beat and popn profiles match their BMS lane order and colors",
          "[unit][core][lane-profile][key-color]") {
    using color = lane_key_color_t;
    const auto verify = [](std::string profile_id, const lane_layout_t& expected) {
        lane_layout_t actual;
        actual.profile_id = std::move(profile_id);
        actual.lanes.reserve(expected.lanes.size());
        for (const auto& lane : expected.lanes) {
            actual.lanes.push_back({.id = lane.id,
                                    .role = lane.role,
                                    .stage = lane.stage,
                                    .display_order = lane.display_order});
        }
        assign_lane_key_colors(actual);
        auto by_order = [](const lane_layout_t& layout) {
            auto lanes = layout.lanes;
            std::ranges::stable_sort(lanes, {}, &lane_definition_t::display_order);
            return lanes;
        };
        const auto expected_order = by_order(expected);
        const auto actual_order = by_order(actual);
        require_bmson_condition(actual_order.size() == expected_order.size());
        for (std::size_t index = 0; index < expected_order.size(); ++index) {
            check_bmson_condition(actual_order[index].role == expected_order[index].role);
            check_bmson_condition(actual_order[index].stage == expected_order[index].stage);
            check_bmson_condition(actual_order[index].key_color == expected_order[index].key_color);
        }
    };
    verify("bmson-beat-5k", make_bms_5k_standard_profile().layout);
    verify("bmson-beat-7k", make_bms_7k_profile().layout);
    auto beat_10k_reference = make_bms_14k_profile().layout;
    lane_layout_t beat_10k;
    for (const auto stage : {stage_group_t::left, stage_group_t::right}) {
        std::vector<lane_definition_t> stage_lanes;
        for (const auto& lane : beat_10k_reference.lanes)
            if (lane.stage == stage)
                stage_lanes.push_back(lane);
        std::ranges::stable_sort(stage_lanes, {}, &lane_definition_t::display_order);
        if (stage == stage_group_t::left) {
            stage_lanes.resize(6);
        } else {
            const auto scratch = stage_lanes.back();
            stage_lanes.resize(5);
            stage_lanes.push_back(scratch);
        }
        for (std::size_t index = 0; index < stage_lanes.size(); ++index) {
            stage_lanes[index].display_order = static_cast<std::uint32_t>(index);
            beat_10k.lanes.push_back(stage_lanes[index]);
        }
    }
    verify("bmson-beat-10k", beat_10k);
    verify("bmson-beat-14k", make_bms_14k_profile().layout);
    verify("bmson-popn-9k", make_pms_9k_profile().layout);
}

TEST_CASE("layouts without stored colors derive them and unknown profiles stay generic",
          "[unit][core][lane-profile][key-color]") {
    auto layout = make_bme_7k_profile().layout;
    for (auto& lane : layout.lanes) lane.key_color = lane_key_color_t::generic;
    assign_lane_key_colors(layout);
    CHECK(std::ranges::count(layout.lanes, lane_key_color_t::white, &lane_definition_t::key_color) == 4);
    CHECK(std::ranges::count(layout.lanes, lane_key_color_t::black, &lane_definition_t::key_color) == 3);

    layout.profile_id = "custom-layout";
    assign_lane_key_colors(layout);
    CHECK(std::ranges::count(layout.lanes, lane_key_color_t::scratch, &lane_definition_t::key_color) == 1);
    CHECK(std::ranges::count(layout.lanes, lane_key_color_t::generic, &lane_definition_t::key_color) == 7);
}

namespace {

auto visual_playable_lanes(const lane_layout_t& layout)
    -> std::vector<const lane_definition_t*> {
    std::vector<const lane_definition_t*> lanes;
    for (const auto& lane : layout.lanes)
        if (lane.role == lane_role_t::playable) lanes.push_back(&lane);
    std::ranges::sort(lanes, [&layout](const auto* left, const auto* right) {
        if (layout.split_stage && left->stage != right->stage) return left->stage < right->stage;
        return std::pair{left->display_order, left->id.value} <
               std::pair{right->display_order, right->id.value};
    });
    return lanes;
}

auto scratchless_10k_colors_are_canonical_after_repair() -> bool {
    using color = lane_key_color_t;
    const std::vector<color> expected{color::white, color::black, color::white, color::black,
                                      color::pms_green, color::pms_yellow, color::black,
                                      color::white, color::black, color::white};
    const auto scratchless_profile = make_bms_10k_scratchless_profile();
    auto scratchless = scratchless_profile.layout;
    auto colors_in_order = [](const lane_layout_t& layout) {
        std::vector<color> colors;
        for (const auto* lane : visual_playable_lanes(layout)) colors.push_back(lane->key_color);
        return colors;
    };
    const auto initial_colors_match = colors_in_order(scratchless) == expected &&
        std::ranges::count_if(visual_playable_lanes(scratchless), [](const auto* lane) {
            return lane->stage == stage_group_t::left;
        }) == 5;
    const auto ids_and_orders = scratchless.lanes;
    for (auto& lane : scratchless.lanes) lane.key_color = color::generic;
    assign_lane_key_colors(scratchless);
    auto identity_preserved = true;
    for (std::size_t index = 0; index < scratchless.lanes.size(); ++index) {
        identity_preserved = identity_preserved && scratchless.lanes[index].id == ids_and_orders[index].id &&
            scratchless.lanes[index].stage == ids_and_orders[index].stage &&
            scratchless.lanes[index].display_order == ids_and_orders[index].display_order;
    }
    return initial_colors_match && identity_preserved && colors_in_order(scratchless) == expected;
}

} // namespace

TEST_CASE("osu!mania style 10K uses the canonical wbwbgybwbw key colors",
          "[unit][core][lane-profile][key-color]") {
    using color = lane_key_color_t;
    const std::vector<color> expected{color::white, color::black, color::white, color::black,
                                      color::pms_green, color::pms_yellow, color::black,
                                      color::white, color::black, color::white};
    CHECK(scratchless_10k_colors_are_canonical_after_repair());

    lane_layout_t osu;
    osu.profile_id = "osu-mania-10k";
    for (std::uint32_t column = 0; column < 10; ++column)
        osu.lanes.push_back({.id = lane_id_t{column + 1}, .display_order = column});
    assign_lane_key_colors(osu);
    std::vector<color> actual;
    for (const auto& lane : osu.lanes) actual.push_back(lane.key_color);
    CHECK(actual == expected);
}

TEST_CASE("BMS scratchless 10K colors follow rendered left-to-right lane geometry",
          "[unit][core][lane-profile][key-color][scene]") {
    const auto profile = make_bms_10k_scratchless_profile();
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960};
    chart.measures = {{.id = measure_id_t{1}}};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    chart.lane_layouts = {{.id = object_id_t{3}, .position = {measure_id_t{1}, 0},
                           .layout = profile.layout}};
    const auto built = build_editor_field_scene_index(chart, 1);
    REQUIRE(std::holds_alternative<editor_field_scene_index_t>(built));
    const editor_interaction_view_model_t view{
        .chart = chart.id, .active_lane = profile.layout.lanes.front().id,
        .cursor = {measure_id_t{1}, 0}, .active_snap_denominator = 4,
        .supported_snap_denominators = {4}, .document_revision = 1,
        .pixels_per_standard_measure = 192, .viewport_height_pixels = 640};
    const auto result = make_editor_field_scene(
        chart, std::get<editor_field_scene_index_t>(built), view, {.field_width_pixels = 640});
    REQUIRE(result);
    auto lanes = result.scene->lanes;
    std::ranges::sort(lanes, {}, [](const auto& lane) { return lane.bounds.left; });
    std::vector<lane_key_color_t> actual;
    for (const auto& lane : lanes) actual.push_back(lane.key_color);
    const std::vector expected{lane_key_color_t::white, lane_key_color_t::black,
        lane_key_color_t::white, lane_key_color_t::black, lane_key_color_t::pms_green,
        lane_key_color_t::pms_yellow, lane_key_color_t::black, lane_key_color_t::white,
        lane_key_color_t::black, lane_key_color_t::white};
    CHECK(actual == expected);
}

TEST_CASE("osu SpecialStyle 12K and 16K use outside scratch colors with playable columns",
          "[unit][core][lane-profile][key-color]") {
    for (const auto keys : {12U, 16U}) {
        CAPTURE(keys);
        auto special = make_osu_mania_profile(keys, true);
        REQUIRE(special);
        CHECK(special->layout.split_stage);
        CHECK(special->layout.split_after_column == keys / 2);
        CHECK(special->input.stage_count == 2);
        REQUIRE(special->input.bindings.size() == keys);
        for (std::uint32_t column = 0; column < keys; ++column) {
            const auto& lane = special->layout.lanes[column];
            CHECK(lane.id == lane_id_t{column + 1});
            CHECK(lane.role == lane_role_t::playable);
            CHECK(lane.stage == (column < keys / 2 ? stage_group_t::left : stage_group_t::right));
            const auto expected = column == 0 || column == keys - 1 ? lane_key_color_t::scratch
                : ((column < keys / 2 ? column - 1 : column - keys / 2) % 2 == 0
                    ? lane_key_color_t::white : lane_key_color_t::black);
            CHECK(lane.key_color == expected);
        }
        auto layout = special->layout;
        for (auto& lane : layout.lanes) lane.key_color = lane_key_color_t::generic;
        assign_lane_key_colors(layout);
        for (std::uint32_t column = 0; column < keys; ++column)
            CHECK(layout.lanes[column].key_color == special->layout.lanes[column].key_color);
        auto ordinary = make_osu_mania_profile(keys);
        REQUIRE(ordinary);
        CHECK_FALSE(ordinary->layout.split_stage);
        for (std::uint32_t column = 0; column < keys; ++column)
            CHECK(ordinary->layout.lanes[column].key_color ==
                  (std::min(column, keys - column - 1) % 2 == 0
                      ? lane_key_color_t::white : lane_key_color_t::black));
    }
    CHECK(make_osu_mania_profile(10, true)->layout.profile_id == "osu-mania-10k");
}

namespace {
void check_field_skin(bool value) { CHECK(value); }
auto skin_class_pattern(const lane_layout_t& layout) -> std::string {
    std::string result;
    for (const auto* lane : visual_playable_lanes(layout)) result += field_skin_key_class(lane->key_color).front();
    return result;
}
void check_osu_skin_classes(std::uint32_t keys, std::string_view expected) {
    auto profile = make_osu_mania_profile(keys);
    REQUIRE(profile);
    CHECK(skin_class_pattern(profile->layout) == expected);
    auto repaired = profile->layout;
    for (auto& lane : repaired.lanes) lane.key_color = lane_key_color_t::generic;
    assign_lane_key_colors(repaired);
    CHECK(skin_class_pattern(repaired) == expected);
    CHECK(std::ranges::all_of(repaired.lanes, [](const auto& lane) { return lane.role == lane_role_t::playable; }));
}
void check_layout_skin_key(const lane_input_profile_t& profile, std::string_view expected) {
    CHECK(field_skin_layout_key(profile.layout) == expected);
}
void check_bmson_skin_alias(lane_input_profile_t profile, const char* bmson_id, std::string_view expected) {
    profile.layout.profile_id = bmson_id;
    if (std::string_view(bmson_id) == "bmson-beat-10k" ||
        std::string_view(bmson_id) == "bmson-beat-14k") profile.layout.split_stage = false;
    for (auto& lane : profile.layout.lanes) {
        lane.id.value += 100000;
        lane.bms_channels.visible.reset();
    }
    CHECK(field_skin_layout_key(profile.layout) == expected);
}
}

TEST_CASE("Selected osu layouts assign stable semantic classes for every supported key count",
          "[core][lane-profile][key-color][field-skin-layout]") {
    constexpr std::array<std::string_view, 18> patterns{
        "y", "ww", "wyw", "wbbw", "wbybw", "wbwwbw", "wbwywbw", "wbwbbwbw", "wbwbybwbw",
        "wbwbgybwbw", "wbwbwywbwbw", "wbwbwbbwbwbw", "wbwbwbybwbwbw", "wbwbwbwwbwbwbw",
        "wbwbwbwywbwbwbw", "wbwbwbwbbwbwbwbw", "wbwbwbwbybwbwbwbw", "wbwbwbwbwwbwbwbwbw"};
    for (std::uint32_t keys = 1; keys <= patterns.size(); ++keys) check_osu_skin_classes(keys, patterns[keys - 1]);
}

TEST_CASE("Field skin classes share white blue green yellow red identities across instruments",
          "[core][lane-profile][field-skin-layout]") {
    using color = lane_key_color_t;
    check_field_skin(field_skin_key_class(color::white) == field_skin_key_class(color::pms_white));
    check_field_skin(field_skin_key_class(color::black) == field_skin_key_class(color::pms_blue));
    check_field_skin(field_skin_key_class(color::scratch) == field_skin_key_class(color::pms_red));
    check_field_skin(field_skin_key_class(color::pms_green) == "green");
    check_field_skin(field_skin_key_class(color::pms_yellow) == "yellow");
    check_field_skin(field_skin_key_class(color::foot_pedal) == "foot-pedal");
    check_field_skin(field_skin_key_class(color::free_zone) == "free-zone");
    check_field_skin(field_skin_key_class(static_cast<color>(999)) == "generic");
}

TEST_CASE("Beat skin layout identities normalize scratch geometry and BMS BMSON aliases",
          "[core][lane-profile][field-skin-layout]") {
    for (const auto scratch : {scratch_placement_t::inside, scratch_placement_t::outside}) {
        check_layout_skin_key(make_bms_5k_standard_profile(scratch), "bms-sp5");
        check_layout_skin_key(make_bms_7k_profile(scratch), "bms-sp7");
        for (const auto second : {scratch_placement_t::inside, scratch_placement_t::outside}) {
            check_layout_skin_key(make_bms_10k_standard_profile(scratch, second), "bms-dp5");
            check_layout_skin_key(make_bms_14k_profile(scratch, second), "bms-dp7");
        }
    }
    check_bmson_skin_alias(make_bms_5k_standard_profile(), "bmson-beat-5k", "bms-sp5");
    check_bmson_skin_alias(make_bms_7k_profile(), "bmson-beat-7k", "bms-sp7");
    check_bmson_skin_alias(make_bms_10k_standard_profile(), "bmson-beat-10k", "bms-dp5");
    check_bmson_skin_alias(make_bms_14k_profile(), "bmson-beat-14k", "bms-dp7");
}

TEST_CASE("Skin identity retains auxiliary and selected profile semantics with stable custom fallback",
          "[core][lane-profile][field-skin-layout]") {
    check_layout_skin_key(make_bms_5k_free_zone_profile(), "bms-sp5-free-zone");
    check_layout_skin_key(make_bms_5k_foot_pedal_profile(), "bms-sp5-foot-pedal");
    check_layout_skin_key(make_bms_10k_foot_pedal_profile(), "bms-dp5-foot-pedal");
    check_layout_skin_key(make_bms_10k_scratchless_profile(), "osu-mania-10k");
    check_layout_skin_key(make_pms_9k_profile(), "pms-9button");
    check_layout_skin_key(make_pms_bme_style_9k_profile(), "pms-9button");
    check_bmson_skin_alias(make_pms_9k_profile(), "bmson-popn-9k", "pms-9button");
    CHECK(field_skin_layout_key(make_osu_mania_profile(12, true)->layout) == "osu-mania-12k-specialstyle");
    auto custom = make_bms_7k_profile().layout;
    custom.profile_id = "custom-layout-one";
    CHECK(field_skin_layout_key(custom) == "custom-layout-one");
    custom.profile_id = "custom-layout-two";
    CHECK(field_skin_layout_key(custom) == "custom-layout-two");
    custom.profile_id.clear();
    CHECK(field_skin_layout_key(custom) == "custom");
}

TEST_CASE("Visual scratch override changes geometry without changing skin layout identity",
          "[core][lane-profile][field-skin-layout]") {
    const auto profile = make_bms_7k_profile();
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960};
    chart.measures = {{.id = measure_id_t{1}}};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}}};
    chart.lane_layouts = {{.id = object_id_t{3}, .position = {measure_id_t{1}, 0}, .layout = profile.layout}};
    const auto built = build_editor_field_scene_index(chart, 1);
    require_bmson_condition(std::holds_alternative<editor_field_scene_index_t>(built));
    const editor_interaction_view_model_t view{.chart = chart.id, .active_lane = profile.layout.lanes.front().id,
        .cursor = {measure_id_t{1}, 0}, .active_snap_denominator = 4, .supported_snap_denominators = {4},
        .document_revision = 1, .pixels_per_standard_measure = 192, .viewport_height_pixels = 640};
    const auto& index = std::get<editor_field_scene_index_t>(built);
    const auto left = make_editor_field_scene(chart, index, view,
        {.field_width_pixels = 640, .scratch_on_right = {false, std::nullopt}});
    const auto right = make_editor_field_scene(chart, index, view,
        {.field_width_pixels = 640, .scratch_on_right = {true, std::nullopt}});
    require_bmson_condition(left);
    require_bmson_condition(right);
    const auto scratch = [](const auto& scene) {
        return std::ranges::find(scene.lanes, lane_role_t::scratch, &editor_field_lane_geometry_t::role)->bounds.left;
    };
    check_field_skin(scratch(*left.scene) < scratch(*right.scene));
    check_field_skin(field_skin_layout_key(chart.lane_layouts.front().layout) == "bms-sp7");
}

namespace {
void check_bmson_dp_scratch_pick(const chart_document_t& chart, editor_interaction_view_model_t view,
                               lane_id_t scratch, double x) {
    view.scratch_on_right = {false, false};
    const auto indexed = build_editor_hit_test_index(chart, 1);
    require_bmson_condition(std::holds_alternative<editor_hit_test_index_t>(indexed));
    const auto y = editor_position_y_pixels(chart, view, {measure_id_t{1}, 240});
    require_bmson_condition(y);
    const auto hit = hit_test_editor(chart, std::get<editor_hit_test_index_t>(indexed), view,
        {.x_pixels = x, .y_pixels = *y, .field_width_pixels = 640});
    require_bmson_condition(hit);
    check_field_skin(hit->lane == scratch);
    check_field_skin(hit->note == object_id_t{4});
}
}

TEST_CASE("BMSON DP without a gap keeps each left scratch in its own stage and pick column",
          "[core][lane-profile][field-skin-layout][scratch-presentation]") {
    auto profile = make_bms_10k_standard_profile();
    profile.layout.profile_id = "bmson-beat-10k";
    profile.layout.split_stage = false;
    profile.layout.split_after_column.reset();
    const auto right_scratch = std::ranges::find_if(profile.layout.lanes, [](const auto& lane) {
        return lane.stage == stage_group_t::right && lane.role == lane_role_t::scratch;
    })->id;
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960};
    chart.measures = {{.id = measure_id_t{1}, .notes = {{.id = object_id_t{4},
        .position = {measure_id_t{1}, 240}, .lane = right_scratch}}}};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}}};
    chart.lane_layouts = {{.id = object_id_t{3}, .position = {measure_id_t{1}, 0}, .layout = profile.layout}};
    const auto built = build_editor_field_scene_index(chart, 1);
    require_bmson_condition(std::holds_alternative<editor_field_scene_index_t>(built));
    const editor_interaction_view_model_t view{.chart = chart.id, .active_lane = right_scratch,
        .cursor = {measure_id_t{1}, 0}, .active_snap_denominator = 4, .supported_snap_denominators = {4},
        .document_revision = 1, .pixels_per_standard_measure = 192, .viewport_height_pixels = 640};
    const auto scene = make_editor_field_scene(chart, std::get<editor_field_scene_index_t>(built), view,
        {.field_width_pixels = 640, .scratch_on_right = {false, false}});
    require_bmson_condition(scene);
    const auto& lanes = scene.scene->lanes;
    require_bmson_condition(lanes.size() == 12);
    check_field_skin(lanes[0].role == lane_role_t::scratch && lanes[0].stage == stage_group_t::left);
    check_field_skin(lanes[6].lane == right_scratch && lanes[6].stage == stage_group_t::right);
    check_field_skin(std::ranges::all_of(std::span{lanes}.subspan(1, 5), [](const auto& lane) {
        return lane.role == lane_role_t::playable && lane.stage == stage_group_t::left;
    }));
    check_field_skin(std::ranges::all_of(std::span{lanes}.subspan(7, 5), [](const auto& lane) {
        return lane.role == lane_role_t::playable && lane.stage == stage_group_t::right;
    }));
    check_field_skin(field_skin_layout_key(chart.lane_layouts.front().layout) == "bms-dp5");
    check_bmson_dp_scratch_pick(chart, view, right_scratch, (lanes[6].bounds.left + lanes[6].bounds.right) / 2);
}

namespace {
void check_catalog(bool condition) { REQUIRE(condition); }
void check_project_layout(const new_project_layout_descriptor_t& entry) {
    const auto profile = entry.make_profile();
    check_catalog(profile.layout.profile_id == entry.id);
    check_catalog(!entry.label.empty());
    check_catalog(!profile.layout.lanes.empty());
    check_catalog(find_new_project_layout(entry.id) == &entry);
    if (profile.layout.lanes.size() > 10) check_catalog(profile.input.bindings.empty());
    const auto groups = make_new_project_output_groups(entry, chart_id_t{42});
    check_catalog(groups.size() == entry.default_output_groups.size());
    for (const auto& group : groups) {
        check_catalog(group.charts == std::vector{chart_id_t{42}});
        if (group.family == format_family_t::stepmania)
            check_catalog(stepmania_key_count(entry.step_type) == profile.layout.lanes.size());
    }
}
}

TEST_CASE("New Project catalog covers all core profiles and unbinds wide layouts", "[core][catalog][D7]") {
    std::set<std::string_view> ids;
    for (const auto& entry : new_project_layouts()) {
        check_catalog(ids.insert(entry.id).second);
        check_project_layout(entry);
    }
    for (std::uint32_t keys = minimum_osu_mania_keys; keys <= maximum_osu_mania_keys; ++keys) {
        const auto id = "osu-mania-" + std::to_string(keys) + "k";
        check_catalog(find_new_project_layout(id) != nullptr);
    }
    for (const auto& built_in : built_in_bms_lane_profiles())
        check_catalog(find_new_project_layout(built_in.profile.layout.profile_id) != nullptr);
    check_catalog(find_new_project_layout("stepmania:pump-double") != nullptr);
    check_catalog(find_new_project_layout("unknown") == nullptr);
}

TEST_CASE("StepMania catalog keeps supported step types and default key counts", "[core][catalog]") {
    for (const auto& entry : stepmania_step_types()) {
        check_catalog(stepmania_key_count(entry.id) == entry.keys);
        if (entry.default_for_key_count)
            check_catalog(default_stepmania_step_type(entry.keys) == entry.id);
    }
    check_catalog(stepmania_key_count("DaNcE-SiNgLe") == 4);
    check_catalog(!stepmania_key_count("unknown"));
    check_catalog(default_stepmania_step_type(9).empty());
}
