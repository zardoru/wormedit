#include "text_util.h"
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/preferences.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit {
auto bms_channel_map_t::from(std::string_view visible) -> bms_channel_map_t {
    auto channel = std::string(visible);
    auto invisible = channel;
    auto long_note = channel;
    auto mine = channel;
    invisible[0] = channel[0] == '1' ? '3' : '4';
    long_note[0] = channel[0] == '1' ? '5' : '6';
    mine[0] = channel[0] == '1' ? 'D' : 'E';
    return {
        .visible = std::move(channel),
        .invisible = std::move(invisible),
        .long_note = std::move(long_note),
        .mine = std::move(mine),
    };
}

auto lane_definition_t::from(std::uint64_t id, std::uint32_t column, stage_group_t stage)
    -> lane_definition_t {
    return {
        .id = lane_id_t{id},
        .name = "Key " + std::to_string(column + 1),
        .role = lane_role_t::playable,
        .stage = stage,
        .display_order = column,
        .glyph = lane_glyph_t::bar,
    };
}

namespace {

constexpr std::array number_row{
    physical_key_t::digit_1, physical_key_t::digit_2, physical_key_t::digit_3,
    physical_key_t::digit_4, physical_key_t::digit_5, physical_key_t::digit_6,
    physical_key_t::digit_7, physical_key_t::digit_8, physical_key_t::digit_9,
    physical_key_t::digit_0,
};

constexpr std::array bms_14k_left{
    physical_key_t::key_q, physical_key_t::key_a, physical_key_t::key_w, physical_key_t::key_s,
    physical_key_t::key_e, physical_key_t::key_d, physical_key_t::key_r, physical_key_t::key_f,
};

constexpr std::array bms_14k_right{
    physical_key_t::key_h, physical_key_t::key_u, physical_key_t::key_j, physical_key_t::key_i,
    physical_key_t::key_k, physical_key_t::key_o, physical_key_t::key_l, physical_key_t::key_p,
};

constexpr std::array qwerty_top_row{
    physical_key_t::key_q, physical_key_t::key_w, physical_key_t::key_e, physical_key_t::key_r,
    physical_key_t::key_t, physical_key_t::key_y, physical_key_t::key_u, physical_key_t::key_i,
    physical_key_t::key_o, physical_key_t::key_p,
};

constexpr std::array bmson_piano_keys{
    physical_key_t::key_z,   physical_key_t::key_s,   physical_key_t::key_x,
    physical_key_t::key_d,   physical_key_t::key_c,   physical_key_t::key_v,
    physical_key_t::key_g,   physical_key_t::key_b,   physical_key_t::key_h,
    physical_key_t::key_n,   physical_key_t::key_j,   physical_key_t::key_m,
    physical_key_t::key_q,   physical_key_t::digit_2, physical_key_t::key_w,
    physical_key_t::digit_3, physical_key_t::key_e,   physical_key_t::key_r,
    physical_key_t::digit_5, physical_key_t::key_t,   physical_key_t::digit_6,
    physical_key_t::key_y,   physical_key_t::digit_7, physical_key_t::key_u,
};

struct lane_spec_t {
    std::uint32_t id_offset{};
    std::string name;
    lane_role_t role{lane_role_t::playable};
    std::string_view visible_channel;
};

[[maybe_unused]] auto make_bms_channel_map(std::string_view visible) -> bms_channel_map_t {
    return bms_channel_map_t::from(visible);
}


auto lane_name(std::string_view stage_name, std::string_view role) -> std::string {
    std::string name(stage_name);
    name.push_back(' ');
    name.append(role);
    return name;
}

auto make_bms_stage(std::uint32_t stage_number, std::span<const lane_spec_t> ordered_lanes)
    -> std::vector<lane_definition_t> {
    const auto stage_name = stage_number == 0 ? std::string_view{"P1"} : std::string_view{"P2"};
    const auto lane_id_base = static_cast<std::uint64_t>(stage_number + 1) * 100;
    const auto stage = stage_number == 0 ? stage_group_t::left : stage_group_t::right;
    std::vector<lane_definition_t> result;
    result.reserve(ordered_lanes.size());
    for (const auto& spec : ordered_lanes) {
        result.push_back({
            .id = lane_id_t{lane_id_base + spec.id_offset},
            .name = lane_name(stage_name, spec.name),
            .role = spec.role,
            .stage = stage,
            .display_order = static_cast<std::uint32_t>(result.size()),
            .glyph = lane_glyph_t::bar,
            .bms_channels = bms_channel_map_t::from(spec.visible_channel),
        });
    }
    return result;
}

template <std::size_t KeyCount>
auto make_side_lanes(std::uint32_t stage_number, scratch_placement_t scratch,
                     const std::array<std::string_view, KeyCount>& key_channels,
                     std::string_view scratch_channel,
                     std::optional<std::pair<std::string_view, lane_role_t>> extra_lane = {})
    -> std::vector<lane_definition_t> {
    std::vector<lane_spec_t> keys;
    keys.reserve(KeyCount);
    for (std::size_t index = 0; index < KeyCount; ++index) {
        keys.push_back({
            .id_offset = static_cast<std::uint32_t>(index + 1),
            .name = "Key " + std::to_string(index + 1),
            .role = lane_role_t::playable,
            .visible_channel = key_channels[index],
        });
    }
    const auto scratch_spec = lane_spec_t{
        .id_offset = static_cast<std::uint32_t>(KeyCount + 1),
        .name = "Scratch",
        .role = lane_role_t::scratch,
        .visible_channel = scratch_channel,
    };
    std::vector<lane_spec_t> ordered;
    ordered.reserve(KeyCount + 2);
    const auto scratch_before_keys =
        (stage_number == 0 && scratch == scratch_placement_t::outside) ||
        (stage_number == 1 && scratch == scratch_placement_t::inside);
    if (scratch_before_keys) {
        ordered.push_back(scratch_spec);
        ordered.insert(ordered.end(), keys.begin(), keys.end());
    } else {
        ordered.insert(ordered.end(), keys.begin(), keys.end());
        ordered.push_back(scratch_spec);
    }
    if (extra_lane) {
        ordered.push_back({
            .id_offset = static_cast<std::uint32_t>(KeyCount + 2),
            .name = extra_lane->second == lane_role_t::foot_pedal ? "Foot Pedal" : "Free Zone",
            .role = extra_lane->second,
            .visible_channel = extra_lane->first,
        });
    }
    return make_bms_stage(stage_number, ordered);
}

auto make_number_row_bindings(const std::vector<lane_definition_t>& lanes, std::uint32_t stage)
    -> std::vector<lane_binding_t> {
    std::vector<const lane_definition_t*> ordered_lanes;
    for (const auto& lane : lanes) {
        const auto lane_stage = lane.stage == stage_group_t::right ? 1U : 0U;
        if (lane_stage == stage &&
            (lane.role == lane_role_t::playable || lane.role == lane_role_t::scratch)) {
            ordered_lanes.push_back(&lane);
        }
    }
    std::ranges::sort(ordered_lanes, [](const auto* left, const auto* right) {
        if (left->display_order != right->display_order) {
            return left->display_order < right->display_order;
        }
        return left->id.value < right->id.value;
    });
    std::vector<lane_binding_t> bindings;
    bindings.reserve(ordered_lanes.size());
    for (const auto* lane : ordered_lanes) {
        const auto key_index = bindings.size();
        if (key_index >= number_row.size()) {
            return {};
        }
        bindings.push_back({
            .key = number_row[key_index],
            .lane = lane->id,
        });
    }
    return bindings;
}

auto make_14k_input(const std::vector<lane_definition_t>& lanes) -> input_profile_t {
    input_profile_t profile{
        .profile_id = "bme-14k-interleaved",
        .stage_count = 2,
        .requires_focused_stage = false,
    };
    for (const auto& lane : lanes) {
        if (lane.role != lane_role_t::playable && lane.role != lane_role_t::scratch) {
            continue;
        }
        const auto index = lane.display_order % 8;
        const auto stage = lane.stage == stage_group_t::right ? 1U : 0U;
        profile.bindings.push_back({
            .key = stage == 0 ? bms_14k_left[index] : bms_14k_right[index],
            .lane = lane.id,
        });
    }
    return profile;
}

auto make_two_stage_row_input(const std::vector<lane_definition_t>& lanes, std::string profile_id)
    -> input_profile_t {
    input_profile_t profile{
        .profile_id = std::move(profile_id),
        .stage_count = 2,
        .requires_focused_stage = false,
    };
    const auto left = make_number_row_bindings(lanes, 0);
    const auto right_lanes = [&] {
        std::vector<lane_definition_t> right;
        std::ranges::copy_if(lanes, std::back_inserter(right),
                             [](const auto& lane) { return lane.stage == stage_group_t::right; });
        return right;
    }();
    const auto right = make_number_row_bindings(right_lanes, 1);
    profile.bindings.reserve(left.size() + right.size());
    for (std::size_t index = 0; index < left.size(); ++index) {
        profile.bindings.push_back({.key = number_row[index], .lane = left[index].lane});
    }
    for (std::size_t index = 0; index < right.size(); ++index) {
        profile.bindings.push_back({.key = qwerty_top_row[index], .lane = right[index].lane});
    }
    return profile;
}

auto make_single_stage_input(const std::vector<lane_definition_t>& lanes,
                             std::span<const physical_key_t> keys, std::string profile_id)
    -> input_profile_t {
    input_profile_t profile{
        .profile_id = std::move(profile_id),
        .stage_count = 1,
        .requires_focused_stage = false,
    };
    std::vector<const lane_definition_t*> ordered_lanes;
    for (const auto& lane : lanes) {
        if (lane.stage == stage_group_t::left &&
            (lane.role == lane_role_t::playable || lane.role == lane_role_t::scratch)) {
            ordered_lanes.push_back(&lane);
        }
    }
    std::ranges::sort(ordered_lanes, [](const auto* left, const auto* right) {
        return left->display_order < right->display_order;
    });
    if (ordered_lanes.size() > keys.size()) {
        return profile;
    }
    profile.bindings.reserve(ordered_lanes.size());
    for (std::size_t index = 0; index < ordered_lanes.size(); ++index) {
        profile.bindings.push_back({.key = keys[index], .lane = ordered_lanes[index]->id});
    }
    return profile;
}

auto make_profile(std::string profile_id, std::string display_name,
                  std::vector<lane_definition_t> lanes, input_profile_t input,
                  bool split_stage = false, std::optional<std::uint32_t> split_after = {})
    -> lane_input_profile_t {
    lane_input_profile_t profile{
        .layout =
            {
                .profile_id = std::move(profile_id),
                .display_name = std::move(display_name),
                .lanes = std::move(lanes),
                .split_stage = split_stage,
                .split_after_column = split_after,
            },
        .input = std::move(input),
    };
    assign_lane_key_colors(profile.layout);
    return profile;
}

auto placement_name(scratch_placement_t placement) -> std::string_view {
    return placement == scratch_placement_t::outside ? "outside" : "inside";
}

auto make_bms_10k(scratch_placement_t left_scratch, scratch_placement_t right_scratch,
                  std::optional<lane_role_t> auxiliary_role) -> lane_input_profile_t {
    constexpr std::array<std::string_view, 5> channels_left{"11", "12", "13", "14", "15"};
    constexpr std::array<std::string_view, 5> channels_right{"21", "22", "23", "24", "25"};
    auto left = make_side_lanes(0, left_scratch, channels_left, "16",
        auxiliary_role ? std::optional{std::pair{std::string_view{"17"}, *auxiliary_role}}
                       : std::nullopt);
    auto right = make_side_lanes(1, right_scratch, channels_right, "26",
        auxiliary_role ? std::optional{std::pair{std::string_view{"27"}, *auxiliary_role}}
                       : std::nullopt);
    const auto profile_id =
        std::string("bms-10k-left-") + std::string(placement_name(left_scratch)) + "-right-" +
        std::string(placement_name(right_scratch)) +
        (!auxiliary_role ? "" : auxiliary_role == lane_role_t::free_zone ? "-free-zone" : "-foot-pedal");
    const auto split_after = static_cast<std::uint32_t>(left.size());
    left.insert(left.end(), std::make_move_iterator(right.begin()),
                std::make_move_iterator(right.end()));
    auto input = make_two_stage_row_input(left, "bms-10k-stage-rows");
    const auto display_name =
        !auxiliary_role ? "BMS DP5 / 10K"
        : auxiliary_role == lane_role_t::free_zone ? "BMS DP5 / 10K (free zone)"
                                                 : "BMS DP5 / 10K (foot pedal)";
    return make_profile(profile_id, display_name, std::move(left), std::move(input), true,
                        split_after);
}

auto make_scratchless_stage(std::uint32_t stage_number,
                            const std::array<std::string_view, 5>& channels)
    -> std::vector<lane_definition_t> {
    std::vector<lane_spec_t> specs;
    specs.reserve(channels.size());
    for (std::size_t index = 0; index < channels.size(); ++index) {
        specs.push_back({
            .id_offset = static_cast<std::uint32_t>(index + 1),
            .name = "Key " + std::to_string(index + 1),
            .role = lane_role_t::playable,
            .visible_channel = channels[index],
        });
    }
    return make_bms_stage(stage_number, specs);
}

auto make_pms_profile(std::string profile_id, std::string display_name,
                      std::span<const std::string_view> channels, std::string input_id)
    -> lane_input_profile_t {
    std::vector<lane_spec_t> specs;
    specs.reserve(channels.size());
    for (std::size_t index = 0; index < channels.size(); ++index) {
        specs.push_back({
            .id_offset = static_cast<std::uint32_t>(index + 1),
            .name = "Button " + std::to_string(index + 1),
            .role = lane_role_t::playable,
            .visible_channel = channels[index],
        });
    }
    auto lanes = make_bms_stage(0, specs);
    auto bindings = make_number_row_bindings(lanes, 0);
    input_profile_t input{
        .profile_id = std::move(input_id),
        .bindings = std::move(bindings),
        .stage_count = 1,
        .requires_focused_stage = false,
    };
    return make_profile(std::move(profile_id), std::move(display_name), std::move(lanes),
                        std::move(input));
}

auto make_bme_7k_profile_impl(scratch_placement_t scratch, bool foot_pedal)
    -> lane_input_profile_t {
    constexpr std::array<std::string_view, 7> keys{"11", "12", "13", "14", "15", "18", "19"};
    auto lanes = make_side_lanes(
        0, scratch, keys, "16",
        foot_pedal ? std::optional{std::pair{std::string_view{"17"}, lane_role_t::foot_pedal}}
                   : std::nullopt);
    auto profile_id =
        std::string(scratch == scratch_placement_t::outside ? "bme-7k-outside-scratch"
                                                            : "bme-7k-inside-scratch");
    if (foot_pedal) {
        profile_id += "-foot-pedal";
    }
    auto input = input_profile_t{
        .profile_id = "number-row-single-stage",
        .bindings = make_number_row_bindings(lanes, 0),
        .stage_count = 1,
        .requires_focused_stage = false,
    };
    auto display_name =
        std::string(scratch == scratch_placement_t::outside ? "BME 7K (outside scratch"
                                                            : "BME 7K (inside scratch");
    display_name += foot_pedal ? ", foot pedal)" : ")";
    return make_profile(std::move(profile_id), std::move(display_name), std::move(lanes),
                        std::move(input));
}

auto make_bme_14k_profile_impl(scratch_placement_t left_scratch, scratch_placement_t right_scratch,
                               bool foot_pedal) -> lane_input_profile_t {
    constexpr std::array<std::string_view, 7> left_keys{"11", "12", "13", "14", "15", "18", "19"};
    constexpr std::array<std::string_view, 7> right_keys{"21", "22", "23", "24", "25", "28", "29"};
    const auto extra_left =
        foot_pedal ? std::optional{std::pair{std::string_view{"17"}, lane_role_t::foot_pedal}}
                   : std::nullopt;
    const auto extra_right =
        foot_pedal ? std::optional{std::pair{std::string_view{"27"}, lane_role_t::foot_pedal}}
                   : std::nullopt;
    auto left = make_side_lanes(0, left_scratch, left_keys, "16", extra_left);
    auto right = make_side_lanes(1, right_scratch, right_keys, "26", extra_right);
    const auto split_after = static_cast<std::uint32_t>(left.size());
    const auto profile_id =
        std::string("bme-14k-left-") + std::string(placement_name(left_scratch)) + "-right-" +
        std::string(placement_name(right_scratch)) + (foot_pedal ? "-foot-pedal" : "");
    left.insert(left.end(), std::make_move_iterator(right.begin()),
                std::make_move_iterator(right.end()));
    auto input = make_14k_input(left);
    return make_profile(profile_id, foot_pedal ? "BME DP7 / 14K (foot pedal)" : "BME DP7 / 14K",
                        std::move(left), std::move(input), true, split_after);
}

[[maybe_unused]] auto make_generic_lane(std::uint64_t id, std::uint32_t column, stage_group_t stage)
    -> lane_definition_t {
    return lane_definition_t::from(id, column, stage);
}


} // namespace

auto make_bms_7k_profile(scratch_placement_t scratch) -> lane_input_profile_t {
    return make_bme_7k_profile_impl(scratch, false);
}

auto make_bms_14k_profile(scratch_placement_t left_scratch, scratch_placement_t right_scratch)
    -> lane_input_profile_t {
    return make_bme_14k_profile_impl(left_scratch, right_scratch, false);
}

auto make_bms_5k_profile(scratch_placement_t scratch) -> lane_input_profile_t {
    return make_bms_5k_free_zone_profile(scratch);
}

auto make_bms_5k_standard_profile(scratch_placement_t scratch) -> lane_input_profile_t {
    constexpr std::array<std::string_view, 5> keys{"11", "12", "13", "14", "15"};
    auto lanes = make_side_lanes(0, scratch, keys, "16");
    auto input = input_profile_t{
        .profile_id = "number-row-single-stage",
        .bindings = make_number_row_bindings(lanes, 0),
        .stage_count = 1,
        .requires_focused_stage = false,
    };
    return make_profile(std::string("bms-5k-") + std::string(placement_name(scratch)) +
                            "-scratch",
                        "BMS 5K", std::move(lanes), std::move(input));
}

auto make_bms_5k_free_zone_profile(scratch_placement_t scratch) -> lane_input_profile_t {
    constexpr std::array<std::string_view, 5> keys{"11", "12", "13", "14", "15"};
    auto lanes = make_side_lanes(0, scratch, keys, "16",
                                 std::pair{std::string_view{"17"}, lane_role_t::free_zone});
    auto input = input_profile_t{
        .profile_id = "number-row-single-stage",
        .bindings = make_number_row_bindings(lanes, 0),
        .stage_count = 1,
        .requires_focused_stage = false,
    };
    return make_profile(std::string("bms-5k-") + std::string(placement_name(scratch)) +
                            "-scratch-free-zone",
                        "BMS 5K (free zone)", std::move(lanes), std::move(input));
}

auto make_bms_5k_foot_pedal_profile(scratch_placement_t scratch) -> lane_input_profile_t {
    constexpr std::array<std::string_view, 5> keys{"11", "12", "13", "14", "15"};
    auto lanes = make_side_lanes(0, scratch, keys, "16",
                                 std::pair{std::string_view{"17"}, lane_role_t::foot_pedal});
    auto input = input_profile_t{
        .profile_id = "number-row-single-stage",
        .bindings = make_number_row_bindings(lanes, 0),
        .stage_count = 1,
        .requires_focused_stage = false,
    };
    return make_profile(std::string("bms-5k-") + std::string(placement_name(scratch)) +
                            "-scratch-foot-pedal",
                        "BMS 5K (foot pedal)", std::move(lanes), std::move(input));
}

auto make_bms_10k_profile(scratch_placement_t left_scratch, scratch_placement_t right_scratch)
    -> lane_input_profile_t {
    return make_bms_10k_free_zone_profile(left_scratch, right_scratch);
}

auto make_bms_10k_free_zone_profile(scratch_placement_t left_scratch,
                                    scratch_placement_t right_scratch) -> lane_input_profile_t {
    return make_bms_10k(left_scratch, right_scratch, lane_role_t::free_zone);
}

auto make_bms_10k_standard_profile(scratch_placement_t left_scratch,
                                  scratch_placement_t right_scratch) -> lane_input_profile_t {
    return make_bms_10k(left_scratch, right_scratch, std::nullopt);
}

auto make_bms_10k_foot_pedal_profile(scratch_placement_t left_scratch,
                                     scratch_placement_t right_scratch) -> lane_input_profile_t {
    return make_bms_10k(left_scratch, right_scratch, lane_role_t::foot_pedal);
}

auto make_bms_10k_scratchless_profile() -> lane_input_profile_t {
    constexpr std::array<std::string_view, 5> channels_left{"11", "12", "13", "14", "15"};
    constexpr std::array<std::string_view, 5> channels_right{"21", "22", "23", "24", "25"};
    auto lanes = make_scratchless_stage(0, channels_left);
    const auto split_after = static_cast<std::uint32_t>(lanes.size());
    auto right = make_scratchless_stage(1, channels_right);
    lanes.insert(lanes.end(), std::make_move_iterator(right.begin()),
                 std::make_move_iterator(right.end()));
    auto input = make_two_stage_row_input(lanes, "bms-10k-scratchless-stage-rows");
    return make_profile("bms-10k-scratchless", "BMS 10K (no scratch)", std::move(lanes),
                        std::move(input), true, split_after);
}

auto make_bme_7k_profile(scratch_placement_t scratch) -> lane_input_profile_t {
    return make_bme_7k_profile_impl(scratch, false);
}

auto make_bme_7k_foot_pedal_profile(scratch_placement_t scratch) -> lane_input_profile_t {
    return make_bme_7k_profile_impl(scratch, true);
}

auto make_bme_14k_profile(scratch_placement_t left_scratch, scratch_placement_t right_scratch)
    -> lane_input_profile_t {
    return make_bme_14k_profile_impl(left_scratch, right_scratch, false);
}

auto make_bme_14k_foot_pedal_profile(scratch_placement_t left_scratch,
                                     scratch_placement_t right_scratch) -> lane_input_profile_t {
    return make_bme_14k_profile_impl(left_scratch, right_scratch, true);
}

auto make_pms_5k_profile() -> lane_input_profile_t {
    constexpr std::array<std::string_view, 5> channels{"13", "14", "15", "22", "23"};
    return make_pms_profile("pms-standard-5button", "PMS 5-button", channels,
                            "pms-5button-number-row");
}

auto make_pms_9k_profile() -> lane_input_profile_t {
    constexpr std::array<std::string_view, 9> channels{
        "11", "12", "13", "14", "15", "22", "23", "24", "25",
    };
    return make_pms_profile("pms-standard-9button", "PMS 9-button", channels,
                            "pms-9button-number-row");
}

auto make_pms_bme_style_9k_profile() -> lane_input_profile_t {
    constexpr std::array<std::string_view, 9> channels{
        "11", "12", "13", "14", "15", "18", "19", "16", "17",
    };
    return make_pms_profile("pms-bme-style-9button", "PMS 9-button (BME-style)", channels,
                            "pms-bme-style-9button-number-row");
}

auto make_pms_bme_style_18k_profile() -> lane_input_profile_t {
    constexpr std::array<std::string_view, 9> left_channels{
        "11", "12", "13", "14", "15", "18", "19", "16", "17",
    };
    constexpr std::array<std::string_view, 9> right_channels{
        "21", "22", "23", "24", "25", "28", "29", "26", "27",
    };
    auto make_buttons = [](std::uint32_t stage, const std::array<std::string_view, 9>& channels) {
        std::vector<lane_spec_t> specs;
        specs.reserve(channels.size());
        for (std::size_t index = 0; index < channels.size(); ++index) {
            specs.push_back({
                .id_offset = static_cast<std::uint32_t>(index + 1),
                .name = "Button " + std::to_string(index + 1),
                .role = lane_role_t::playable,
                .visible_channel = channels[index],
            });
        }
        return make_bms_stage(stage, specs);
    };
    auto left = make_buttons(0, left_channels);
    auto right = make_buttons(1, right_channels);
    left.insert(left.end(), std::make_move_iterator(right.begin()),
                std::make_move_iterator(right.end()));
    auto input = make_two_stage_row_input(left, "pms-bme-style-18button-stage-rows");
    return make_profile("pms-bme-style-18button", "PMS 18-button (BME-style)", std::move(left),
                        std::move(input), true, 9);
}

auto make_bms_double_scratch_profile() -> lane_input_profile_t {
    const std::array<lane_spec_t, 9> ordered{{
        {.id_offset = 8, .name = "Scratch", .role = lane_role_t::scratch, .visible_channel = "16"},
        {.id_offset = 1, .name = "Key 1", .visible_channel = "11"},
        {.id_offset = 2, .name = "Key 2", .visible_channel = "12"},
        {.id_offset = 3, .name = "Key 3", .visible_channel = "13"},
        {.id_offset = 4, .name = "Key 4", .visible_channel = "14"},
        {.id_offset = 5, .name = "Key 5", .visible_channel = "15"},
        {.id_offset = 6, .name = "Key 6", .visible_channel = "18"},
        {.id_offset = 7, .name = "Key 7", .visible_channel = "19"},
        {.id_offset = 9,
         .name = "Scratch 2",
         .role = lane_role_t::scratch,
         .visible_channel = "26"},
    }};
    auto lanes = make_bms_stage(0, ordered);
    const auto bindings = make_number_row_bindings(lanes, 0);
    input_profile_t input{
        .profile_id = "bms-double-scratch-number-row",
        .bindings = bindings,
        .stage_count = 1,
        .requires_focused_stage = false,
    };
    return make_profile("bms-double-scratch-fpp", "BMS Double Scratch / FPP", std::move(lanes),
                        std::move(input));
}

auto make_bms_octave_fp_profile() -> lane_input_profile_t {
    constexpr std::array<std::string_view, 13> key_channels{
        "11", "12", "13", "14", "15", "18", "19", "22", "23", "24", "25", "28", "29",
    };
    std::vector<lane_spec_t> ordered;
    ordered.reserve(16);
    ordered.push_back({.id_offset = 14,
                       .name = "Scratch",
                       .role = lane_role_t::scratch,
                       .visible_channel = "16"});
    for (std::size_t index = 0; index < key_channels.size(); ++index) {
        ordered.push_back({
            .id_offset = static_cast<std::uint32_t>(index + 1),
            .name = "Key " + std::to_string(index + 1),
            .role = lane_role_t::playable,
            .visible_channel = key_channels[index],
        });
    }
    ordered.push_back({.id_offset = 15,
                       .name = "Scratch 2",
                       .role = lane_role_t::scratch,
                       .visible_channel = "26"});
    ordered.push_back({.id_offset = 16,
                       .name = "Foot Pedal",
                       .role = lane_role_t::foot_pedal,
                       .visible_channel = "21"});
    auto lanes = make_bms_stage(0, ordered);
    // The single-stage default has ten number-row keys, fewer than OCT/FP's
    // fifteen scratch/key lanes. Leave the profile unbound until a banked
    // remapping policy is defined.
    auto input = make_single_stage_input(lanes, number_row, "bms-octave-fp-remap-required");
    return make_profile("bms-octave-foot-pedal", "BMS Octave / Foot Pedal", std::move(lanes),
                        std::move(input));
}

auto make_project2dx_7k_profile() -> lane_input_profile_t {
    const std::array<lane_spec_t, 8> ordered{{
        {.id_offset = 8, .name = "Scratch", .role = lane_role_t::scratch, .visible_channel = "16"},
        {.id_offset = 1, .name = "Key 1", .visible_channel = "11"},
        {.id_offset = 2, .name = "Key 2", .visible_channel = "12"},
        {.id_offset = 3, .name = "Key 3", .visible_channel = "13"},
        {.id_offset = 4, .name = "Key 4", .visible_channel = "14"},
        {.id_offset = 5, .name = "Key 5", .visible_channel = "15"},
        {.id_offset = 6, .name = "Key 6", .visible_channel = "21"},
        {.id_offset = 7, .name = "Key 7", .visible_channel = "22"},
    }};
    auto lanes = make_bms_stage(0, ordered);
    auto input = make_single_stage_input(lanes, number_row, "project2dx-number-row");
    return make_profile("project2dx-7k", "Project2DX 7K", std::move(lanes), std::move(input));
}

auto built_in_bms_lane_profiles() -> std::span<const bms_lane_profile_descriptor_t> {
    static const std::array profiles{
        bms_lane_profile_descriptor_t{make_bms_5k_standard_profile(),
                                      bms_lane_profile_family_t::bms_five_or_ten_key, false, false},
        bms_lane_profile_descriptor_t{make_bms_5k_free_zone_profile(),
                                      bms_lane_profile_family_t::bms_five_or_ten_key, false, false},
        bms_lane_profile_descriptor_t{make_bms_5k_foot_pedal_profile(),
                                      bms_lane_profile_family_t::bms_five_or_ten_key, false, false},
        bms_lane_profile_descriptor_t{make_bme_7k_profile(),
                                      bms_lane_profile_family_t::bme_seven_or_fourteen_key, false,
                                      false},
        bms_lane_profile_descriptor_t{make_bme_7k_foot_pedal_profile(),
                                      bms_lane_profile_family_t::bme_seven_or_fourteen_key, false,
                                      false},
        bms_lane_profile_descriptor_t{make_bms_10k_standard_profile(),
                                      bms_lane_profile_family_t::bms_five_or_ten_key, true, false},
        bms_lane_profile_descriptor_t{make_bms_10k_free_zone_profile(),
                                      bms_lane_profile_family_t::bms_five_or_ten_key, true, false},
        bms_lane_profile_descriptor_t{make_bms_10k_foot_pedal_profile(),
                                      bms_lane_profile_family_t::bms_five_or_ten_key, true, false},
        bms_lane_profile_descriptor_t{make_bme_14k_profile(),
                                      bms_lane_profile_family_t::bme_seven_or_fourteen_key, true,
                                      false},
        bms_lane_profile_descriptor_t{make_bme_14k_foot_pedal_profile(),
                                      bms_lane_profile_family_t::bme_seven_or_fourteen_key, true,
                                      false},
        bms_lane_profile_descriptor_t{make_pms_5k_profile(), bms_lane_profile_family_t::popn, false,
                                      false},
        bms_lane_profile_descriptor_t{make_pms_9k_profile(), bms_lane_profile_family_t::popn, false,
                                      false},
        bms_lane_profile_descriptor_t{make_pms_bme_style_9k_profile(),
                                      bms_lane_profile_family_t::popn, false, false},
        // Split layout is an editor presentation choice for this one-player mode.
        bms_lane_profile_descriptor_t{make_pms_bme_style_18k_profile(),
                                      bms_lane_profile_family_t::popn, false, false},
        // Chosen only explicitly: its channels are also a subset of every 10K profile.
        bms_lane_profile_descriptor_t{make_bms_10k_scratchless_profile(),
                                      bms_lane_profile_family_t::special, true, false},
        bms_lane_profile_descriptor_t{make_bms_double_scratch_profile(),
                                      bms_lane_profile_family_t::special, false, false},
        bms_lane_profile_descriptor_t{make_bms_octave_fp_profile(),
                                      bms_lane_profile_family_t::special, false, true},
        bms_lane_profile_descriptor_t{make_project2dx_7k_profile(),
                                      bms_lane_profile_family_t::special, false, false},
    };
    return profiles;
}

auto make_number_row_input_profile(const lane_layout_t& layout, std::uint32_t stage,
                                   std::string profile_id) -> std::optional<input_profile_t> {
    if (stage > 1 || (stage == 1 && !layout.split_stage)) {
        return std::nullopt;
    }
    const auto bindings = make_number_row_bindings(layout.lanes, stage);
    const auto stage_lane_count =
        static_cast<std::size_t>(std::ranges::count_if(layout.lanes, [stage](const auto& lane) {
            const auto lane_stage = lane.stage == stage_group_t::right ? 1U : 0U;
            return lane_stage == stage &&
                   (lane.role == lane_role_t::playable || lane.role == lane_role_t::scratch);
        }));
    if (stage_lane_count == 0 || stage_lane_count > number_row.size() ||
        bindings.size() != stage_lane_count) {
        return std::nullopt;
    }
    return input_profile_t{
        .profile_id = std::move(profile_id),
        .bindings = bindings,
        .stage_count = layout.split_stage ? 2U : 1U,
        .requires_focused_stage = false,
    };
}

auto make_default_input_profile_for(const lane_layout_t& layout) -> std::optional<input_profile_t> {
    if (layout.split_stage) {
        if (!make_number_row_input_profile(layout, 0) || !make_number_row_input_profile(layout, 1))
            return std::nullopt;
        return make_two_stage_row_input(layout.lanes, "split-stage-row-default");
    }
    auto single_stage = layout;
    for (auto& lane : single_stage.lanes) lane.stage = stage_group_t::left;
    return make_number_row_input_profile(single_stage, 0, "number-row-default");
}

auto lane_key_bindings_t::number_row(std::uint32_t count) -> lane_key_bindings_t {
    static_assert(maximum_default_lane_count == wormedit::number_row.size());
    lane_key_bindings_t result;
    if (count > maximum_default_lane_count) return result;
    for (const auto key : std::span{wormedit::number_row}.first(count)) result.keys.push_back(physical_key_hid_usage(key));
    return result;
}

namespace {

constexpr std::array<stepmania_lane_spec_t, 5> pump_single_specs{{
    {"Down Left", lane_glyph_t::arrow_down_left},
    {"Up Left", lane_glyph_t::arrow_up_left},
    {"Center", lane_glyph_t::arrow_center},
    {"Up Right", lane_glyph_t::arrow_up_right},
    {"Down Right", lane_glyph_t::arrow_down_right},
}};

constexpr std::array<stepmania_lane_spec_t, 6> dance_solo_specs{{
    {"Left", lane_glyph_t::arrow_left},
    {"Up Left", lane_glyph_t::arrow_up_left},
    {"Down", lane_glyph_t::arrow_down},
    {"Up", lane_glyph_t::arrow_up},
    {"Up Right", lane_glyph_t::arrow_up_right},
    {"Right", lane_glyph_t::arrow_right},
}};

constexpr std::array<stepmania_lane_spec_t, 10> pump_double_specs{{
    {"Down Left", lane_glyph_t::arrow_down_left},
    {"Up Left", lane_glyph_t::arrow_up_left},
    {"Center", lane_glyph_t::arrow_center},
    {"Up Right", lane_glyph_t::arrow_up_right},
    {"Down Right", lane_glyph_t::arrow_down_right},
    {"Down Left 2", lane_glyph_t::arrow_down_left},
    {"Up Left 2", lane_glyph_t::arrow_up_left},
    {"Center 2", lane_glyph_t::arrow_center},
    {"Up Right 2", lane_glyph_t::arrow_up_right},
    {"Down Right 2", lane_glyph_t::arrow_down_right},
}};

auto make_stepmania_profile(std::string_view step_type,
                            std::span<const stepmania_lane_spec_t> specs, std::string display_name)
    -> lane_input_profile_t {
    std::vector<lane_definition_t> lanes;
    lanes.reserve(specs.size());
    for (std::size_t index = 0; index < specs.size(); ++index) {
        lanes.push_back({.id = lane_id_t{index + 1},
                         .name = std::string(specs[index].name),
                         .role = lane_role_t::playable,
                         .stage = stage_group_t::left,
                         .display_order = static_cast<std::uint32_t>(index),
                         .glyph = specs[index].glyph});
    }
    input_profile_t input{
        .profile_id = "number-row-single-stage",
        .bindings = make_number_row_bindings(lanes, 0),
        .stage_count = 1,
        .requires_focused_stage = false,
    };
    return make_profile("stepmania:" + std::string(step_type), std::move(display_name),
                        std::move(lanes), std::move(input));
}

} // namespace

auto stepmania_lane_specs(std::string_view step_type) -> std::span<const stepmania_lane_spec_t> {
    const auto same = [&](std::string_view name) {
        return text_util::ascii_iequal(step_type, name);
    };
    if (same("pump-single"))
        return pump_single_specs;
    if (same("dance-solo"))
        return dance_solo_specs;
    if (same("pump-double"))
        return pump_double_specs;
    return {};
}

auto make_stepmania_pump_single_profile() -> lane_input_profile_t {
    return make_stepmania_profile("pump-single", pump_single_specs, "Pump 5K (pump-single)");
}

auto make_stepmania_layout(std::string_view step_type, std::uint32_t count) -> lane_layout_t {
    lane_layout_t layout;
    layout.profile_id = "stepmania:" + std::string(step_type);
    layout.display_name = step_type;
    static constexpr std::array<std::string_view, 10> dance_names{
        "Left", "Down", "Up", "Right", "Left 2", "Down 2", "Up 2", "Right 2", "Center", "Diagonal"};
    const auto folded_step_type = text_util::ascii_lowercase(step_type);
    const bool cardinal_dance = folded_step_type == "dance-single" ||
                                folded_step_type == "dance-double" ||
                                folded_step_type == "dance-couple";
    static constexpr std::array<lane_glyph_t, 4> dance_arrows{
        lane_glyph_t::arrow_left, lane_glyph_t::arrow_down,
        lane_glyph_t::arrow_up, lane_glyph_t::arrow_right};
    const auto named_specs = stepmania_lane_specs(folded_step_type);
    const bool named = named_specs.size() == count;
    for (std::uint32_t i = 0; i < count; ++i) {
        layout.lanes.push_back(
            {.id = lane_id_t{i + 1},
             .name = named ? std::string(named_specs[i].name)
                           : std::string(i < dance_names.size() ? dance_names[i] : "Column") +
                                 (i < dance_names.size() ? "" : " " + std::to_string(i + 1)),
             .role = lane_role_t::playable,
             .stage = count > 4 && i >= count / 2 ? stage_group_t::right : stage_group_t::left,
             .display_order = i,
             .glyph = cardinal_dance ? dance_arrows[i % dance_arrows.size()]
                       : named       ? named_specs[i].glyph
                                     : lane_glyph_t::bar});
    }
    layout.split_stage = count == 8 || count == 10;
    if (layout.split_stage)
        layout.split_after_column = count / 2;
    return layout;
}

auto make_stepmania_profile_for(std::string_view step_type) -> std::optional<lane_input_profile_t> {
    const auto specs = stepmania_lane_specs(step_type);
    if (!specs.empty()) return make_stepmania_profile(step_type, specs, std::string(step_type));
    const auto count=stepmania_key_count(step_type);
    if (!count) return std::nullopt;
    auto layout=make_stepmania_layout(step_type,*count);
    auto input=make_default_input_profile_for(layout);
    if (!input) return std::nullopt;
    return lane_input_profile_t{.layout=std::move(layout),.input=std::move(*input)};
}

auto make_osu_mania_profile(std::uint32_t keys) -> std::optional<lane_input_profile_t> {
    return make_osu_mania_profile(keys, false);
}

auto make_osu_mania_profile(std::uint32_t keys, bool special_style) -> std::optional<lane_input_profile_t> {
    if (keys < minimum_osu_mania_keys || keys > maximum_osu_mania_keys) return std::nullopt;
    const bool split = special_style && (keys == 12 || keys == 16);
    std::vector<lane_definition_t> lanes;
    lanes.reserve(keys);
    for (std::uint32_t index = 0; index < keys; ++index) {
        lanes.push_back({.id = lane_id_t{index + 1},
                         .name = "Key " + std::to_string(index + 1),
                         .role = lane_role_t::playable,
                         .stage = split && index >= keys / 2 ? stage_group_t::right : stage_group_t::left,
                         .display_order = index});
    }
    // The number row binds at most ten lanes; wider layouts start unbound.
    auto input = make_number_row_input_profile(lane_layout_t{.lanes = lanes})
                     .value_or(input_profile_t{.profile_id = "unbound-" + std::to_string(keys) + "k"});
    const auto id = "osu-mania-" + std::to_string(keys) + "k" +
                    (split ? "-specialstyle" : "");
    if (split) input = make_two_stage_row_input(lanes, id + "-stage-rows");
    return make_profile(id, std::to_string(keys) + "K", std::move(lanes), std::move(input),
                        split, split ? std::optional{keys / 2} : std::nullopt);
}

auto make_stepmania_dance_solo_profile() -> lane_input_profile_t {
    return make_stepmania_profile("dance-solo", dance_solo_specs, "Solo 6K (dance-solo)");
}

auto make_bmson_generic_24k_profile() -> lane_input_profile_t {
    lane_input_profile_t result{
        .layout =
            {
                .profile_id = "bmson-generic-24keys",
                .display_name = "BMSON Generic 24K",
                .bmson_generic_keys_only = true,
            },
        .input =
            {
                .profile_id = "bmson-generic-24keys-piano",
                .stage_count = 1,
            },
    };
    result.layout.lanes.reserve(bmson_piano_keys.size());
    result.input.bindings.reserve(bmson_piano_keys.size());
    for (std::size_t index = 0; index < bmson_piano_keys.size(); ++index) {
        const auto lane =
            lane_definition_t::from(index + 1, static_cast<std::uint32_t>(index), stage_group_t::none);
        result.layout.lanes.push_back(lane);
        result.input.bindings.push_back({.key = bmson_piano_keys[index], .lane = lane.id});
    }
    assign_lane_key_colors(result.layout);
    return result;
}

auto make_bmson_generic_48k_profile() -> lane_input_profile_t {
    lane_input_profile_t result{
        .layout =
            {
                .profile_id = "bmson-generic-48keys",
                .display_name = "BMSON Generic 48K (focused 24-key stages)",
                .split_stage = true,
                .split_after_column = 24,
                .bmson_generic_keys_only = true,
            },
        .input =
            {
                .profile_id = "bmson-generic-48keys-stage-bank",
                .stage_count = 2,
                .requires_focused_stage = true,
            },
    };
    result.layout.lanes.reserve(48);
    result.input.bindings.reserve(48);
    for (std::size_t index = 0; index < 48; ++index) {
        const auto stage = index < 24 ? 0U : 1U;
        const auto local_column = static_cast<std::uint32_t>(index % 24);
        const auto lane =
            lane_definition_t::from(index + 1, static_cast<std::uint32_t>(index),
                              stage == 0 ? stage_group_t::left : stage_group_t::right);
        result.layout.lanes.push_back(lane);
        result.input.bindings.push_back({
            .key = bmson_piano_keys[local_column],
            .lane = lane.id,
            .focused_stage = stage,
        });
    }
    assign_lane_key_colors(result.layout);
    return result;
}

namespace {

constexpr std::array pms_button_colors{
    lane_key_color_t::pms_white, lane_key_color_t::pms_yellow, lane_key_color_t::pms_green,
    lane_key_color_t::pms_blue,  lane_key_color_t::pms_red,    lane_key_color_t::pms_blue,
    lane_key_color_t::pms_green, lane_key_color_t::pms_yellow, lane_key_color_t::pms_white,
};

// osu!mania's canonical 10K coloring: wbwbg ybwbw across the whole field.
constexpr std::array osu_10k_colors{
    lane_key_color_t::white,      lane_key_color_t::black,      lane_key_color_t::white,
    lane_key_color_t::black,      lane_key_color_t::pms_green,  lane_key_color_t::pms_yellow,
    lane_key_color_t::black,      lane_key_color_t::white,      lane_key_color_t::black,
    lane_key_color_t::white,
};

auto is_osu_style_10k(std::string_view profile) -> bool {
    return profile == "osu-mania-10k" || profile == "bms-10k-scratchless";
}

void color_osu_style_10k(lane_layout_t& layout) {
    std::vector<lane_definition_t*> lanes;
    for (auto& lane : layout.lanes) {
        if (lane.role == lane_role_t::playable) lanes.push_back(&lane);
    }
    std::ranges::stable_sort(lanes, [&layout](const auto* left, const auto* right) {
        if (layout.split_stage && left->stage != right->stage)
            return left->stage < right->stage;
        return std::pair{left->display_order, left->id.value} <
               std::pair{right->display_order, right->id.value};
    });
    if (lanes.size() != osu_10k_colors.size()) return;
    for (std::size_t index = 0; index < lanes.size(); ++index)
        lanes[index]->key_color = osu_10k_colors[index];
}

auto role_key_color(lane_role_t role) -> std::optional<lane_key_color_t> {
    switch (role) {
    case lane_role_t::scratch:
        return lane_key_color_t::scratch;
    case lane_role_t::foot_pedal:
        return lane_key_color_t::foot_pedal;
    case lane_role_t::free_zone:
        return lane_key_color_t::free_zone;
    default:
        return std::nullopt;
    }
}

auto piano_key_color(std::uint32_t display_order) -> lane_key_color_t {
    switch (display_order % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return lane_key_color_t::black;
    default:
        return lane_key_color_t::white;
    }
}

// Playable lanes of one stage in display order.
auto playable_lanes_of_stage(lane_layout_t& layout, stage_group_t stage)
    -> std::vector<lane_definition_t*> {
    std::vector<lane_definition_t*> lanes;
    for (auto& lane : layout.lanes) {
        if (lane.stage == stage && lane.role == lane_role_t::playable) lanes.push_back(&lane);
    }
    std::ranges::stable_sort(lanes, {}, &lane_definition_t::display_order);
    return lanes;
}

void color_osu_special_stage(lane_layout_t& layout, stage_group_t stage) {
    const auto lanes = playable_lanes_of_stage(layout, stage);
    if (lanes.empty()) return;
    const auto scratch = stage == stage_group_t::left ? 0U : lanes.size() - 1;
    std::size_t key = 0;
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        if (index == scratch) lanes[index]->key_color = lane_key_color_t::scratch;
        else lanes[index]->key_color = key++ % 2 == 0 ? lane_key_color_t::white
                                                     : lane_key_color_t::black;
    }
}

// Default osu!mania sprites alternate by distance from the nearest stage edge;
// an odd stage uses the special/yellow sprite in its center. See upstream:
// osu.Game.Rulesets.Mania/Skinning/Legacy/LegacyManiaColumnElement.cs and
// osu.Game.Rulesets.Mania/Beatmaps/StageDefinition.cs in ppy/osu.
void color_osu_stage(lane_layout_t& layout, stage_group_t stage) {
    const auto lanes = playable_lanes_of_stage(layout, stage);
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        const auto edge_distance = std::min(index, lanes.size() - index - 1);
        lanes[index]->key_color = lanes.size() % 2 == 1 && index == lanes.size() / 2
            ? lane_key_color_t::pms_yellow
            : edge_distance % 2 == 0 ? lane_key_color_t::white : lane_key_color_t::black;
    }
}

void color_stage(lane_layout_t& layout, stage_group_t stage, std::string_view profile) {
    const auto lanes = playable_lanes_of_stage(layout, stage);
    const auto popn = profile.starts_with("pms-") || profile.starts_with("bmson-popn-");
    const auto popn_nine = popn && lanes.size() == pms_button_colors.size();
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        if (profile.starts_with("bmson-generic-")) {
            lanes[index]->key_color = piano_key_color(lanes[index]->display_order);
        } else if (popn_nine) {
            lanes[index]->key_color = pms_button_colors[index];
        } else if (profile.starts_with("bms-") || profile.starts_with("bme-") ||
                   profile.starts_with("bmson-beat-")) {
            lanes[index]->key_color =
                index % 2 == 0 ? lane_key_color_t::white : lane_key_color_t::black;
        }
    }
}

} // namespace

void assign_lane_key_colors(lane_layout_t& layout) {
    for (auto& lane : layout.lanes) {
        lane.key_color = role_key_color(lane.role).value_or(lane_key_color_t::generic);
    }
    if (layout.profile_id == "osu-mania-12k-specialstyle" ||
        layout.profile_id == "osu-mania-16k-specialstyle") {
        color_osu_special_stage(layout, stage_group_t::left);
        color_osu_special_stage(layout, stage_group_t::right);
        return;
    }
    if (is_osu_style_10k(layout.profile_id)) {
        color_osu_style_10k(layout);
        return;
    }
    for (const auto stage : {stage_group_t::none, stage_group_t::left, stage_group_t::right}) {
        if (layout.profile_id.starts_with("osu-mania-")) color_osu_stage(layout, stage);
        else color_stage(layout, stage, layout.profile_id);
    }
}

namespace {

auto is_beat_skin_family(std::string_view profile) -> bool {
    return profile.starts_with("bms-5k-") || profile.starts_with("bms-10k-") ||
           profile.starts_with("bme-7k-") || profile.starts_with("bme-14k-") ||
           profile.starts_with("bmson-beat-");
}

auto is_canonical_beat_stages(const lane_layout_t& layout, std::ptrdiff_t keys_per_stage, bool double_play) -> bool {
    if (!double_play) return true;
    for (const auto stage : {stage_group_t::left, stage_group_t::right}) {
        const auto count = [&](lane_role_t role) {
            return std::ranges::count_if(layout.lanes, [=](const auto& lane) { return lane.stage == stage && lane.role == role; });
        };
        if (count(lane_role_t::playable) != keys_per_stage || count(lane_role_t::scratch) != 1) return false;
    }
    return true;
}

auto beat_skin_layout_key(const lane_layout_t& layout) -> std::string {
    const auto count = [&](lane_role_t role) { return std::ranges::count(layout.lanes, role, &lane_definition_t::role); };
    // BMSON beat-DP imports carry two semantic stage groups without split_stage.
    const auto double_play = layout.split_stage || layout.profile_id == "bmson-beat-10k" ||
                             layout.profile_id == "bmson-beat-14k";
    const auto stages = double_play ? 2 : 1;
    const auto playable = count(lane_role_t::playable);
    if (count(lane_role_t::scratch) != stages || (playable != 5 * stages && playable != 7 * stages)) return {};
    if (!is_canonical_beat_stages(layout, playable / stages, double_play)) return {};
    auto result = std::string(double_play ? "bms-dp" : "bms-sp") + std::to_string(playable / stages);
    if (count(lane_role_t::free_zone)) result += "-free-zone";
    if (count(lane_role_t::foot_pedal)) result += "-foot-pedal";
    return result;
}

} // namespace

auto field_skin_layout_key(const lane_layout_t& layout) -> std::string {
    const auto& profile = layout.profile_id;
    if (profile == "bms-10k-scratchless") return "osu-mania-10k";
    if (is_beat_skin_family(profile)) {
        const auto beat = beat_skin_layout_key(layout);
        if (!beat.empty()) return beat;
    }
    if (profile.starts_with("pms-") || profile.starts_with("bmson-popn-")) {
        const auto keys = std::ranges::count(layout.lanes, lane_role_t::playable, &lane_definition_t::role);
        if (keys == 5 || keys == 9 || keys == 18) return "pms-" + std::to_string(keys) + "button";
    }
    return profile.empty() ? "custom" : profile;
}

auto field_skin_key_class(lane_key_color_t color) -> std::string_view {
    switch (color) {
    case lane_key_color_t::white:
    case lane_key_color_t::pms_white: return "white";
    case lane_key_color_t::black:
    case lane_key_color_t::pms_blue: return "blue";
    case lane_key_color_t::pms_green: return "green";
    case lane_key_color_t::pms_yellow: return "yellow";
    case lane_key_color_t::scratch:
    case lane_key_color_t::pms_red: return "red";
    case lane_key_color_t::foot_pedal: return "foot-pedal";
    case lane_key_color_t::free_zone: return "free-zone";
    default: return "generic";
    }
}

namespace {
constexpr std::array step_types{
    stepmania_step_type_descriptor_t{"dance-threepanel", 3, true},
    stepmania_step_type_descriptor_t{"dance-single", 4, true},
    stepmania_step_type_descriptor_t{"pump-single", 5, true},
    stepmania_step_type_descriptor_t{"dance-solo", 6, true},
    stepmania_step_type_descriptor_t{"kb7", 7, true},
    stepmania_step_type_descriptor_t{"dance-double", 8, true},
    stepmania_step_type_descriptor_t{"pump-double", 10, true},
    stepmania_step_type_descriptor_t{"techno-single", 4, false},
    stepmania_step_type_descriptor_t{"dance-couple", 8, false},
    stepmania_step_type_descriptor_t{"dance-routine", 8, false},
    stepmania_step_type_descriptor_t{"techno-double", 8, false},
    stepmania_step_type_descriptor_t{"ez2-single", 5, false},
    stepmania_step_type_descriptor_t{"pump-couple", 10, false},
    stepmania_step_type_descriptor_t{"pump-routine", 10, false},
    stepmania_step_type_descriptor_t{"ez2-double", 10, false},
    stepmania_step_type_descriptor_t{"pump-halfdouble", 6, false},
    stepmania_step_type_descriptor_t{"ez2-real", 7, false},
    stepmania_step_type_descriptor_t{"kb4", 4, false},
    stepmania_step_type_descriptor_t{"kb5", 5, false},
    stepmania_step_type_descriptor_t{"kb6", 6, false},
    stepmania_step_type_descriptor_t{"para-single", 5, false},
};
constexpr std::array mania_outputs{
    new_project_output_group_descriptor_t{"osu!mania", format_family_t::osu_mania, "exports/osu",
                                          std::nullopt},
};
constexpr std::array mania_step_outputs{
    mania_outputs[0],
    new_project_output_group_descriptor_t{"StepMania", format_family_t::stepmania,
                                          "exports/stepmania/song.ssc",
                                          stepmania_export_policy_t{}},
};
constexpr std::array bms_outputs{
    new_project_output_group_descriptor_t{"BMS", format_family_t::bms, "exports/bms", std::nullopt},
};
constexpr std::array bmson_outputs{
    new_project_output_group_descriptor_t{"BMSON", format_family_t::bmson, "exports/bmson",
                                          std::nullopt},
};
constexpr std::array step_outputs{mania_step_outputs[1]};

// D7 applies to new-project defaults only; detection/profile factories keep their bindings.
void unbind_wide_project_profile(lane_input_profile_t& profile) {
    if (profile.layout.lanes.size() > 10) profile.input.bindings.clear();
}
template <auto Factory>
auto make_catalog_profile() -> lane_input_profile_t {
    return Factory();
}

constexpr auto catalog_default_step_type(std::size_t keys) -> std::string_view {
    for (const auto& entry : step_types)
        if (entry.keys == keys && entry.default_for_key_count) return entry.id;
    return {};
}
constexpr auto catalog_mania_outputs(std::size_t keys)
    -> std::span<const new_project_output_group_descriptor_t> {
    if (!catalog_default_step_type(keys).empty()) return mania_step_outputs;
    return mania_outputs;
}

template <std::uint32_t Keys, bool Special = false>
auto make_catalog_mania_profile() -> lane_input_profile_t {
    static_assert(Keys >= minimum_osu_mania_keys && Keys <= maximum_osu_mania_keys);
    return *make_osu_mania_profile(Keys, Special);
}
template <auto Factory, scratch_placement_t Scratch>
auto make_catalog_single_scratch_profile() -> lane_input_profile_t {
    return Factory(Scratch);
}
template <auto Factory, scratch_placement_t Left, scratch_placement_t Right>
auto make_catalog_double_scratch_profile() -> lane_input_profile_t {
    return Factory(Left, Right);
}

auto make_catalog_pump_double_profile() -> lane_input_profile_t {
    return *make_stepmania_profile_for("pump-double");
}

// New-project lane keys for layouts wider than the number row (owner, 2026-10-18), left to right.
// A right scratch outside its stage takes the key after the stage; inside, the key before it.
using enum physical_key_t;
constexpr std::array dp5_outer_keys{digit_1, key_q, digit_2, key_w, digit_3, key_e,
                                    key_u, digit_8, key_i, digit_9, key_o, digit_0};
constexpr std::array dp5_inner_right_keys{digit_1, key_q, digit_2, key_w, digit_3, key_e,
                                          key_y, key_u, digit_8, key_i, digit_9, key_o};
constexpr std::array dp7_keys{digit_1, key_q, digit_2, key_w, digit_3, key_e, digit_4, key_r,
                              key_y, digit_7, key_u, digit_8, key_i, digit_9, key_o, digit_0};
constexpr std::array mania_12k_keys{digit_1, key_q, digit_2, key_w, digit_3, key_e,
                                    key_y, digit_7, key_u, digit_8, key_i, digit_9};
constexpr std::array mania_18k_keys{digit_1, digit_2, digit_3, digit_4, key_q, key_w, key_e, key_r, key_f,
                                    digit_7, digit_8, digit_9, digit_0, key_y, key_u, key_i, key_o, key_h};

constexpr std::array project_layouts{
    new_project_layout_descriptor_t{"osu-mania-1k", "1K", &make_catalog_mania_profile<1>, catalog_mania_outputs(1), catalog_default_step_type(1), {}, "osu!mania", "1 key", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-2k", "2K", &make_catalog_mania_profile<2>, catalog_mania_outputs(2), catalog_default_step_type(2), {}, "osu!mania", "2 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-3k", "3K", &make_catalog_mania_profile<3>, catalog_mania_outputs(3), catalog_default_step_type(3), {}, "osu!mania", "3 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-4k", "4K", &make_catalog_mania_profile<4>, catalog_mania_outputs(4), catalog_default_step_type(4), {}, "osu!mania", "4 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-5k", "5K", &make_catalog_mania_profile<5>, catalog_mania_outputs(5), catalog_default_step_type(5), {}, "osu!mania", "5 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-6k", "6K", &make_catalog_mania_profile<6>, catalog_mania_outputs(6), catalog_default_step_type(6), {}, "osu!mania", "6 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-7k", "7K", &make_catalog_mania_profile<7>, catalog_mania_outputs(7), catalog_default_step_type(7), {}, "osu!mania", "7 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-8k", "8K", &make_catalog_mania_profile<8>, catalog_mania_outputs(8), catalog_default_step_type(8), {}, "osu!mania", "8 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-9k", "9K", &make_catalog_mania_profile<9>, catalog_mania_outputs(9), catalog_default_step_type(9), {}, "osu!mania", "9 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-10k", "10K", &make_catalog_mania_profile<10>, catalog_mania_outputs(10), catalog_default_step_type(10), {}, "osu!mania", "10 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-11k", "11K", &make_catalog_mania_profile<11>, catalog_mania_outputs(11), catalog_default_step_type(11), {}, "osu!mania", "11 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-12k", "12K", &make_catalog_mania_profile<12>, catalog_mania_outputs(12), catalog_default_step_type(12), mania_12k_keys, "osu!mania", "12 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-13k", "13K", &make_catalog_mania_profile<13>, catalog_mania_outputs(13), catalog_default_step_type(13), {}, "osu!mania", "13 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-14k", "14K", &make_catalog_mania_profile<14>, catalog_mania_outputs(14), catalog_default_step_type(14), {}, "osu!mania", "14 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-15k", "15K", &make_catalog_mania_profile<15>, catalog_mania_outputs(15), catalog_default_step_type(15), {}, "osu!mania", "15 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-16k", "16K", &make_catalog_mania_profile<16>, catalog_mania_outputs(16), catalog_default_step_type(16), dp7_keys, "osu!mania", "16 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-17k", "17K", &make_catalog_mania_profile<17>, catalog_mania_outputs(17), catalog_default_step_type(17), {}, "osu!mania", "17 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-18k", "18K", &make_catalog_mania_profile<18>, catalog_mania_outputs(18), catalog_default_step_type(18), mania_18k_keys, "osu!mania", "18 keys", "Standard"},
    new_project_layout_descriptor_t{"osu-mania-12k-specialstyle", "12K SpecialStyle", &make_catalog_mania_profile<12, true>, mania_outputs, "", dp5_outer_keys, "osu!mania", "12 keys", "SpecialStyle"},
    new_project_layout_descriptor_t{"osu-mania-16k-specialstyle", "16K SpecialStyle", &make_catalog_mania_profile<16, true>, mania_outputs, "", dp7_keys, "osu!mania", "16 keys", "SpecialStyle"},
    new_project_layout_descriptor_t{"bms-5k-outside-scratch", "BMS 5K (outside scratch)", &make_catalog_single_scratch_profile<make_bms_5k_standard_profile, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "BMS 5K", "Outside scratch"},
    new_project_layout_descriptor_t{"bms-5k-inside-scratch", "BMS 5K (inside scratch)", &make_catalog_single_scratch_profile<make_bms_5k_standard_profile, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "BMS 5K", "Inside scratch"},
    new_project_layout_descriptor_t{"bms-5k-outside-scratch-free-zone", "BMS 5K (free zone) (outside scratch)", &make_catalog_single_scratch_profile<make_bms_5k_free_zone_profile, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "BMS 5K", "Outside + free zone"},
    new_project_layout_descriptor_t{"bms-5k-inside-scratch-free-zone", "BMS 5K (free zone) (inside scratch)", &make_catalog_single_scratch_profile<make_bms_5k_free_zone_profile, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "BMS 5K", "Inside + free zone"},
    new_project_layout_descriptor_t{"bms-5k-outside-scratch-foot-pedal", "BMS 5K (foot pedal) (outside scratch)", &make_catalog_single_scratch_profile<make_bms_5k_foot_pedal_profile, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "BMS 5K", "Outside + pedal"},
    new_project_layout_descriptor_t{"bms-5k-inside-scratch-foot-pedal", "BMS 5K (foot pedal) (inside scratch)", &make_catalog_single_scratch_profile<make_bms_5k_foot_pedal_profile, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "BMS 5K", "Inside + pedal"},
    new_project_layout_descriptor_t{"bme-7k-outside-scratch", "BME 7K (outside scratch)", &make_catalog_single_scratch_profile<make_bme_7k_profile, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "BME 7K", "Outside scratch"},
    new_project_layout_descriptor_t{"bme-7k-inside-scratch", "BME 7K (inside scratch)", &make_catalog_single_scratch_profile<make_bme_7k_profile, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "BME 7K", "Inside scratch"},
    new_project_layout_descriptor_t{"bme-7k-outside-scratch-foot-pedal", "BME 7K (foot pedal) (outside scratch)", &make_catalog_single_scratch_profile<make_bme_7k_foot_pedal_profile, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "BME 7K", "Outside + pedal"},
    new_project_layout_descriptor_t{"bme-7k-inside-scratch-foot-pedal", "BME 7K (foot pedal) (inside scratch)", &make_catalog_single_scratch_profile<make_bme_7k_foot_pedal_profile, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "BME 7K", "Inside + pedal"},
    new_project_layout_descriptor_t{"bms-10k-left-outside-right-outside", "BMS DP5 / 10K (outside/outside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_standard_profile, scratch_placement_t::outside, scratch_placement_t::outside>, bms_outputs, "", dp5_outer_keys, "BMS / BME", "DP5 / 10K", "O/O scratch"},
    new_project_layout_descriptor_t{"bms-10k-left-outside-right-inside", "BMS DP5 / 10K (outside/inside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_standard_profile, scratch_placement_t::outside, scratch_placement_t::inside>, bms_outputs, "", dp5_inner_right_keys, "BMS / BME", "DP5 / 10K", "O/I scratch"},
    new_project_layout_descriptor_t{"bms-10k-left-inside-right-outside", "BMS DP5 / 10K (inside/outside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_standard_profile, scratch_placement_t::inside, scratch_placement_t::outside>, bms_outputs, "", dp5_outer_keys, "BMS / BME", "DP5 / 10K", "I/O scratch"},
    new_project_layout_descriptor_t{"bms-10k-left-inside-right-inside", "BMS DP5 / 10K (inside/inside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_standard_profile, scratch_placement_t::inside, scratch_placement_t::inside>, bms_outputs, "", dp5_inner_right_keys, "BMS / BME", "DP5 / 10K", "I/I scratch"},
    new_project_layout_descriptor_t{"bms-10k-left-outside-right-outside-free-zone", "BMS DP5 / 10K (free zone) (outside/outside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_free_zone_profile, scratch_placement_t::outside, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "DP5 / 10K", "O/O + free"},
    new_project_layout_descriptor_t{"bms-10k-left-outside-right-inside-free-zone", "BMS DP5 / 10K (free zone) (outside/inside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_free_zone_profile, scratch_placement_t::outside, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "DP5 / 10K", "O/I + free"},
    new_project_layout_descriptor_t{"bms-10k-left-inside-right-outside-free-zone", "BMS DP5 / 10K (free zone) (inside/outside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_free_zone_profile, scratch_placement_t::inside, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "DP5 / 10K", "I/O + free"},
    new_project_layout_descriptor_t{"bms-10k-left-inside-right-inside-free-zone", "BMS DP5 / 10K (free zone) (inside/inside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_free_zone_profile, scratch_placement_t::inside, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "DP5 / 10K", "I/I + free"},
    new_project_layout_descriptor_t{"bms-10k-left-outside-right-outside-foot-pedal", "BMS DP5 / 10K (foot pedal) (outside/outside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_foot_pedal_profile, scratch_placement_t::outside, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "DP5 / 10K", "O/O + pedal"},
    new_project_layout_descriptor_t{"bms-10k-left-outside-right-inside-foot-pedal", "BMS DP5 / 10K (foot pedal) (outside/inside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_foot_pedal_profile, scratch_placement_t::outside, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "DP5 / 10K", "O/I + pedal"},
    new_project_layout_descriptor_t{"bms-10k-left-inside-right-outside-foot-pedal", "BMS DP5 / 10K (foot pedal) (inside/outside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_foot_pedal_profile, scratch_placement_t::inside, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "DP5 / 10K", "I/O + pedal"},
    new_project_layout_descriptor_t{"bms-10k-left-inside-right-inside-foot-pedal", "BMS DP5 / 10K (foot pedal) (inside/inside scratch)", &make_catalog_double_scratch_profile<make_bms_10k_foot_pedal_profile, scratch_placement_t::inside, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "DP5 / 10K", "I/I + pedal"},
    new_project_layout_descriptor_t{"bme-14k-left-outside-right-outside", "BME DP7 / 14K (outside/outside scratch)", &make_catalog_double_scratch_profile<make_bme_14k_profile, scratch_placement_t::outside, scratch_placement_t::outside>, bms_outputs, "", dp7_keys, "BMS / BME", "DP7 / 14K", "O/O scratch"},
    new_project_layout_descriptor_t{"bme-14k-left-outside-right-inside", "BME DP7 / 14K (outside/inside scratch)", &make_catalog_double_scratch_profile<make_bme_14k_profile, scratch_placement_t::outside, scratch_placement_t::inside>, bms_outputs, "", dp7_keys, "BMS / BME", "DP7 / 14K", "O/I scratch"},
    new_project_layout_descriptor_t{"bme-14k-left-inside-right-outside", "BME DP7 / 14K (inside/outside scratch)", &make_catalog_double_scratch_profile<make_bme_14k_profile, scratch_placement_t::inside, scratch_placement_t::outside>, bms_outputs, "", dp7_keys, "BMS / BME", "DP7 / 14K", "I/O scratch"},
    new_project_layout_descriptor_t{"bme-14k-left-inside-right-inside", "BME DP7 / 14K (inside/inside scratch)", &make_catalog_double_scratch_profile<make_bme_14k_profile, scratch_placement_t::inside, scratch_placement_t::inside>, bms_outputs, "", dp7_keys, "BMS / BME", "DP7 / 14K", "I/I scratch"},
    new_project_layout_descriptor_t{"bme-14k-left-outside-right-outside-foot-pedal", "BME DP7 / 14K (foot pedal) (outside/outside scratch)", &make_catalog_double_scratch_profile<make_bme_14k_foot_pedal_profile, scratch_placement_t::outside, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "DP7 / 14K", "O/O + pedal"},
    new_project_layout_descriptor_t{"bme-14k-left-outside-right-inside-foot-pedal", "BME DP7 / 14K (foot pedal) (outside/inside scratch)", &make_catalog_double_scratch_profile<make_bme_14k_foot_pedal_profile, scratch_placement_t::outside, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "DP7 / 14K", "O/I + pedal"},
    new_project_layout_descriptor_t{"bme-14k-left-inside-right-outside-foot-pedal", "BME DP7 / 14K (foot pedal) (inside/outside scratch)", &make_catalog_double_scratch_profile<make_bme_14k_foot_pedal_profile, scratch_placement_t::inside, scratch_placement_t::outside>, bms_outputs, "", {}, "BMS / BME", "DP7 / 14K", "I/O + pedal"},
    new_project_layout_descriptor_t{"bme-14k-left-inside-right-inside-foot-pedal", "BME DP7 / 14K (foot pedal) (inside/inside scratch)", &make_catalog_double_scratch_profile<make_bme_14k_foot_pedal_profile, scratch_placement_t::inside, scratch_placement_t::inside>, bms_outputs, "", {}, "BMS / BME", "DP7 / 14K", "I/I + pedal"},
    new_project_layout_descriptor_t{"pms-standard-5button", "PMS 5-button", &make_catalog_profile<make_pms_5k_profile>, bms_outputs, "", {}, "PMS", "5 buttons", "Standard"},
    new_project_layout_descriptor_t{"pms-standard-9button", "PMS 9-button", &make_catalog_profile<make_pms_9k_profile>, bms_outputs, "", {}, "PMS", "9 buttons", "Standard"},
    new_project_layout_descriptor_t{"pms-bme-style-9button", "PMS 9-button (BME-style)", &make_catalog_profile<make_pms_bme_style_9k_profile>, bms_outputs, "", {}, "PMS", "9 buttons", "BME-style"},
    new_project_layout_descriptor_t{"pms-bme-style-18button", "PMS 18-button (BME-style)", &make_catalog_profile<make_pms_bme_style_18k_profile>, bms_outputs, "", {}, "PMS", "18 buttons", "BME-style"},
    new_project_layout_descriptor_t{"bms-10k-scratchless", "BMS 10K (no scratch)", &make_catalog_profile<make_bms_10k_scratchless_profile>, bms_outputs, "", {}, "BMS / BME", "10K no scratch", "Standard"},
    new_project_layout_descriptor_t{"bms-double-scratch-fpp", "BMS Double Scratch / FPP", &make_catalog_profile<make_bms_double_scratch_profile>, bms_outputs, "", {}, "BMS / BME", "Double scratch", "FPP"},
    new_project_layout_descriptor_t{"bms-octave-foot-pedal", "BMS Octave / Foot Pedal", &make_catalog_profile<make_bms_octave_fp_profile>, bms_outputs, "", {}, "BMS / BME", "Octave", "Foot pedal"},
    new_project_layout_descriptor_t{"project2dx-7k", "Project2DX 7K", &make_catalog_profile<make_project2dx_7k_profile>, bms_outputs, "", {}, "BMS / BME", "Project2DX 7K", "Standard"},
    new_project_layout_descriptor_t{"stepmania:pump-single", "Pump 5K (pump-single)", &make_catalog_profile<make_stepmania_pump_single_profile>, step_outputs, "pump-single", {}, "StepMania", "Pump 5K", "Single"},
    new_project_layout_descriptor_t{"stepmania:pump-double", "Pump 10K (pump-double)", &make_catalog_pump_double_profile, step_outputs, "pump-double", {}, "StepMania", "Pump 10K", "Double"},
    new_project_layout_descriptor_t{"stepmania:dance-solo", "Solo 6K (dance-solo)", &make_catalog_profile<make_stepmania_dance_solo_profile>, step_outputs, "dance-solo", {}, "StepMania", "Solo 6K", "Solo"},
    new_project_layout_descriptor_t{"bmson-generic-24keys", "BMSON generic 24K", &make_catalog_profile<make_bmson_generic_24k_profile>, bmson_outputs, "", {}, "BMSON", "24 keys", "Generic"},
    new_project_layout_descriptor_t{"bmson-generic-48keys", "BMSON generic 48K", &make_catalog_profile<make_bmson_generic_48k_profile>, bmson_outputs, "", {}, "BMSON", "48 keys", "Generic"},
};
} // namespace

auto stepmania_step_types() -> std::span<const stepmania_step_type_descriptor_t> { return step_types; }
auto stepmania_key_count(std::string_view step_type) -> std::optional<std::uint32_t> {
    for (const auto& entry : step_types)
        if (text_util::ascii_iequal(entry.id, step_type)) return entry.keys;
    return std::nullopt;
}
auto default_stepmania_step_type(std::size_t keys) -> std::string_view {
    return catalog_default_step_type(keys);
}
auto new_project_layouts() -> std::span<const new_project_layout_descriptor_t> { return project_layouts; }
auto new_project_layout_descriptor_t::source_family() const -> format_family_t {
    if (default_output_groups.empty()) return format_family_t::native_project;
    const auto family = default_output_groups.front().family;
    for (const auto& group : default_output_groups)
        if (group.family != family) return format_family_t::native_project;
    return family;
}
auto new_project_layout_descriptor_t::profile() const -> lane_input_profile_t {
    auto result = make_profile();
    unbind_wide_project_profile(result);
    if (lane_keys.empty()) return result;
    const auto lanes = result.layout.lanes_in_stage_order();
    result.input.bindings.clear();
    result.input.requires_focused_stage = false;
    for (std::size_t index = 0; index < lanes.size() && index < lane_keys.size(); ++index)
        result.input.bindings.push_back({.key = lane_keys[index], .lane = lanes[index].id});
    return result;
}
auto new_project_layout_descriptor_t::preview_lane_roles() const -> std::vector<lane_role_t> {
    auto lanes = profile().layout.lanes;
    std::ranges::sort(lanes, {}, &lane_definition_t::display_order);
    std::vector<lane_role_t> result;
    result.reserve(lanes.size());
    for (const auto& lane : lanes) result.push_back(lane.role);
    return result;
}
auto find_new_project_layout(std::string_view id) -> const new_project_layout_descriptor_t* {
    for (const auto& entry : project_layouts)
        if (entry.id == id) return &entry;
    return nullptr;
}
auto make_new_project_output_groups(const new_project_layout_descriptor_t& layout, chart_id_t chart)
    -> std::vector<output_group_t> {
    std::vector<output_group_t> groups;
    groups.reserve(layout.default_output_groups.size());
    for (const auto& entry : layout.default_output_groups)
        groups.push_back({.name = std::string(entry.label), .family = entry.family,
                          .charts = {chart},
                          .destination_kind = entry.family == format_family_t::stepmania ||
                                                      entry.family == format_family_t::native_project
                                                  ? output_destination_kind_t::file
                                                  : output_destination_kind_t::directory,
                          .relative_output_path = std::string(entry.relative_output_path),
                          .stepmania = entry.stepmania});
    return groups;
}

namespace {
auto has_stable_layout(const chart_document_t& chart) -> bool {
    if (chart.lane_layouts.empty()) return false;
    const auto& first = chart.lane_layouts.front().layout;
    return std::ranges::all_of(chart.lane_layouts,
                               [&](const auto& segment) { return first == segment.layout; });
}
} // namespace

auto is_bms_lane_layout_compatible(const lane_layout_t& source, const lane_layout_t& target) -> bool {
    if (source.lanes.size() != target.lanes.size()) return false;
    return std::ranges::all_of(source.lanes, [&](const auto& lane) {
        const auto matching = [&](const auto& other) {
            return other.role == lane.role && other.stage == lane.stage;
        };
        return std::ranges::count_if(source.lanes, matching) ==
               std::ranges::count_if(target.lanes, matching);
    });
}

auto output_family_compatibility(const chart_document_t& chart, format_family_t family)
    -> output_family_compatibility_t {
    if (family == format_family_t::native_project) return {true, {}};
    if (chart.lane_layouts.empty()) return {false, "The chart has no lane layout."};
    const auto& layout = chart.lane_layouts.front().layout;
    if (family == format_family_t::bms) {
        const auto compatible = std::ranges::all_of(chart.lane_layouts, [&](const auto& segment) {
            return std::ranges::any_of(built_in_bms_lane_profiles(), [&](const auto& candidate) {
                return is_bms_lane_layout_compatible(segment.layout, candidate.profile.layout);
            });
        });
        return compatible ? output_family_compatibility_t{true, {}}
                          : output_family_compatibility_t{
                                false, "The lane layout has no compatible BMS channel profile."};
    }
    if (!has_stable_layout(chart))
        return {false, "This format requires one stable lane layout for the whole chart."};
    if (family == format_family_t::osu_mania) {
        const auto playable = std::ranges::all_of(layout.lanes, [](const auto& lane) {
            return lane.role == lane_role_t::playable;
        });
        return playable && !layout.lanes.empty() && layout.lanes.size() <= 18
                   ? output_family_compatibility_t{true, {}}
                   : output_family_compatibility_t{false, "osu!mania requires 1–18 playable lanes."};
    }
    if (family == format_family_t::stepmania)
        return !default_stepmania_step_type(layout.lanes.size()).empty()
                   ? output_family_compatibility_t{true, {}}
                   : output_family_compatibility_t{
                         false, "No truthful StepMania step type exists for this lane count."};
    if (family == format_family_t::bmson)
        return layout.lanes.size() <= 48
                   ? output_family_compatibility_t{true, {}}
                   : output_family_compatibility_t{false, "BMSON supports at most 48 mapped lanes."};
    return {false, "This output format is not supported."};
}

auto suggested_output_groups(const chart_document_t& chart)
    -> std::span<const new_project_output_group_descriptor_t> {
    if (chart.lane_layouts.empty()) return {};
    const auto& profile = chart.lane_layouts.front().layout.profile_id;
    const auto found = std::ranges::find(project_layouts, profile,
                                         &new_project_layout_descriptor_t::id);
    return found == project_layouts.end() ? std::span<const new_project_output_group_descriptor_t>{}
                                          : found->default_output_groups;
}

} // namespace wormedit
