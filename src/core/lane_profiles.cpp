#include <wormedit/core/lane_profiles.h>

#include <algorithm>
#include <array>
#include <cctype>
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

auto make_bms_channel_map(std::string_view visible) -> bms_channel_map_t {
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
            .bms_channels = make_bms_channel_map(spec.visible_channel),
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
                  lane_role_t auxiliary_role) -> lane_input_profile_t {
    constexpr std::array<std::string_view, 5> channels_left{"11", "12", "13", "14", "15"};
    constexpr std::array<std::string_view, 5> channels_right{"21", "22", "23", "24", "25"};
    auto left = make_side_lanes(0, left_scratch, channels_left, "16",
                                std::pair{std::string_view{"17"}, auxiliary_role});
    auto right = make_side_lanes(1, right_scratch, channels_right, "26",
                                 std::pair{std::string_view{"27"}, auxiliary_role});
    const auto profile_id =
        std::string("bms-10k-left-") + std::string(placement_name(left_scratch)) + "-right-" +
        std::string(placement_name(right_scratch)) +
        (auxiliary_role == lane_role_t::free_zone ? "-free-zone" : "-foot-pedal");
    const auto split_after = static_cast<std::uint32_t>(left.size());
    left.insert(left.end(), std::make_move_iterator(right.begin()),
                std::make_move_iterator(right.end()));
    auto input = make_two_stage_row_input(left, "bms-10k-stage-rows");
    const auto display_name =
        auxiliary_role == lane_role_t::free_zone ? "BMS DP5 / 10K (free zone)"
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

auto make_generic_lane(std::uint64_t id, std::uint32_t column, stage_group_t stage)
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

auto default_input_profile_for(const lane_layout_t& layout) -> std::optional<input_profile_t> {
    if (layout.split_stage) {
        if (!make_number_row_input_profile(layout, 0) || !make_number_row_input_profile(layout, 1))
            return std::nullopt;
        return make_two_stage_row_input(layout.lanes, "split-stage-row-default");
    }
    auto single_stage = layout;
    for (auto& lane : single_stage.lanes) lane.stage = stage_group_t::left;
    return make_number_row_input_profile(single_stage, 0, "number-row-default");
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
        return std::ranges::equal(step_type, name, [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) ==
                   std::tolower(static_cast<unsigned char>(right));
        });
    };
    if (same("pump-single"))
        return pump_single_specs;
    if (same("dance-solo"))
        return dance_solo_specs;
    return {};
}

auto make_stepmania_pump_single_profile() -> lane_input_profile_t {
    return make_stepmania_profile("pump-single", pump_single_specs, "Pump 5K (pump-single)");
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
            make_generic_lane(index + 1, static_cast<std::uint32_t>(index), stage_group_t::none);
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
            make_generic_lane(index + 1, static_cast<std::uint32_t>(index),
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

void color_stage(lane_layout_t& layout, stage_group_t stage, std::string_view profile) {
    const auto lanes = playable_lanes_of_stage(layout, stage);
    const auto popn = profile.starts_with("pms-");
    const auto popn_nine = popn && lanes.size() == pms_button_colors.size();
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        if (profile.starts_with("bmson-generic-")) {
            lanes[index]->key_color = piano_key_color(lanes[index]->display_order);
        } else if (popn_nine) {
            lanes[index]->key_color = pms_button_colors[index];
        } else if (profile.starts_with("bms-") || profile.starts_with("bme-")) {
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
    for (const auto stage : {stage_group_t::none, stage_group_t::left, stage_group_t::right}) {
        color_stage(layout, stage, layout.profile_id);
    }
}

} // namespace wormedit
