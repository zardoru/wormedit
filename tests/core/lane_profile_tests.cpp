#include <wormedit/core/lane_profiles.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace wormedit;

namespace {

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

TEST_CASE("built-in BMS profile registry is complete unique and frontend-neutral",
          "[unit][core][lane-profile][bms][registry]") {
    const auto profiles = built_in_bms_lane_profiles();
    REQUIRE(profiles.size() == 15);

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
          }) == 4);
    CHECK(std::ranges::count_if(profiles, [](const auto& descriptor) {
              return descriptor.family == bms_lane_profile_family_t::bme_seven_or_fourteen_key;
          }) == 4);
    CHECK(std::ranges::count_if(profiles, [](const auto& descriptor) {
              return descriptor.family == bms_lane_profile_family_t::popn;
          }) == 4);
    CHECK(std::ranges::count_if(profiles, [](const auto& descriptor) {
              return descriptor.family == bms_lane_profile_family_t::special;
          }) == 3);
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
