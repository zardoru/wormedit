#include <wormedit/core/lane_profiles.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
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

struct bms_stage_lanes_t {
    std::vector<lane_definition_t> lanes;
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

auto make_bms_stage(std::uint32_t stage_number, scratch_placement_t scratch,
                    std::array<std::string_view, 7> key_channels, std::string_view scratch_channel)
    -> bms_stage_lanes_t {
    const auto stage_name = stage_number == 0 ? std::string_view{"P1"} : std::string_view{"P2"};
    const auto lane_id_base = static_cast<std::uint64_t>(stage_number + 1) * 100;
    std::vector<lane_definition_t> keys;
    keys.reserve(key_channels.size());
    for (std::size_t key_index = 0; key_index < key_channels.size(); ++key_index) {
        const auto lane_id = lane_id_t{lane_id_base + key_index + 1};
        keys.push_back({
            .id = lane_id,
            .name = lane_name(stage_name, "Key " + std::to_string(key_index + 1)),
            .role = lane_role_t::playable,
            .stage = stage_number == 0 ? stage_group_t::left : stage_group_t::right,
            .display_order = 0,
            .glyph = lane_glyph_t::bar,
            .bms_channels = make_bms_channel_map(key_channels[key_index]),
        });
    }

    const auto scratch_lane = lane_definition_t{
        .id = lane_id_t{lane_id_base + 8},
        .name = lane_name(stage_name, "Scratch"),
        .role = lane_role_t::scratch,
        .stage = stage_number == 0 ? stage_group_t::left : stage_group_t::right,
        .display_order = 0,
        .glyph = lane_glyph_t::bar,
        .bms_channels = make_bms_channel_map(scratch_channel),
    };

    bms_stage_lanes_t result;
    result.lanes.reserve(8);
    auto push_lane = [&](lane_definition_t lane) {
        lane.display_order = static_cast<std::uint32_t>(result.lanes.size());
        result.lanes.push_back(std::move(lane));
    };
    const auto scratch_before_keys =
        (stage_number == 0 && scratch == scratch_placement_t::outside) ||
        (stage_number == 1 && scratch == scratch_placement_t::inside);
    if (scratch_before_keys) {
        push_lane(scratch_lane);
        for (auto& key : keys) {
            push_lane(std::move(key));
        }
    } else {
        for (auto& key : keys) {
            push_lane(std::move(key));
        }
        push_lane(scratch_lane);
    }
    return result;
}

auto make_number_row_bindings(const std::vector<lane_definition_t>& lanes, std::uint32_t stage)
    -> std::vector<lane_binding_t> {
    std::vector<lane_binding_t> bindings;
    for (const auto& lane : lanes) {
        const auto lane_stage = lane.stage == stage_group_t::right ? 1U : 0U;
        if (lane_stage != stage ||
            (lane.role != lane_role_t::playable && lane.role != lane_role_t::scratch)) {
            continue;
        }
        const auto key_index = bindings.size();
        if (key_index >= number_row.size()) {
            return {};
        }
        bindings.push_back({
            .key = number_row[key_index],
            .lane = lane.id,
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
        const auto index = lane.display_order % 8;
        const auto stage = lane.stage == stage_group_t::right ? 1U : 0U;
        profile.bindings.push_back({
            .key = stage == 0 ? bms_14k_left[index] : bms_14k_right[index],
            .lane = lane.id,
        });
    }
    return profile;
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
    auto stage = make_bms_stage(0, scratch, {"11", "12", "13", "14", "15", "18", "19"}, "16");
    lane_input_profile_t result{
        .layout =
            {
                .profile_id = scratch == scratch_placement_t::outside ? "bme-7k-outside-scratch"
                                                                      : "bme-7k-inside-scratch",
                .display_name = scratch == scratch_placement_t::outside ? "BME 7K (outside scratch)"
                                                                        : "BME 7K (inside scratch)",
                .lanes = std::move(stage.lanes),
            },
    };
    result.input.profile_id = "number-row-single-stage";
    result.input.bindings = make_number_row_bindings(result.layout.lanes, 0);
    result.input.stage_count = 1;
    return result;
}

auto make_bms_14k_profile(scratch_placement_t left_scratch, scratch_placement_t right_scratch)
    -> lane_input_profile_t {
    auto left = make_bms_stage(0, left_scratch, {"11", "12", "13", "14", "15", "18", "19"}, "16");
    auto right = make_bms_stage(1, right_scratch, {"21", "22", "23", "24", "25", "28", "29"}, "26");
    const auto profile_id = std::string("bme-14k-left-") +
                            (left_scratch == scratch_placement_t::outside ? "outside" : "inside") +
                            "-right-" +
                            (right_scratch == scratch_placement_t::outside ? "outside" : "inside");
    lane_input_profile_t result{
        .layout =
            {
                .profile_id = profile_id,
                .display_name = "BME 14K",
                .lanes = std::move(left.lanes),
                .split_stage = true,
                .split_after_column = 8,
            },
    };
    result.layout.lanes.insert(result.layout.lanes.end(),
                               std::make_move_iterator(right.lanes.begin()),
                               std::make_move_iterator(right.lanes.end()));
    result.input = make_14k_input(result.layout.lanes);
    return result;
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
    return result;
}

} // namespace wormedit
