#pragma once

#include <wormedit/core/document.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace wormedit {

enum class scratch_placement_t {
    outside,
    inside,
};

// A built-in profile keeps the semantic column layout and its default physical
// keyboard bindings together. Callers can copy and remap the input profile
// without changing lane identities or the source-format channel map.
struct lane_input_profile_t {
    lane_layout_t layout;
    input_profile_t input;
};

enum class bms_lane_profile_family_t {
    bms_five_or_ten_key,
    bme_seven_or_fourteen_key,
    popn,
    special,
};

// Canonical frontend-neutral catalog metadata. player_header_two_stage is
// intentionally separate from layout.split_stage: PMS 18-button is rendered
// as two groups but remains one #PLAYER-compatible mode.
struct bms_lane_profile_descriptor_t {
    lane_input_profile_t profile;
    bms_lane_profile_family_t family{bms_lane_profile_family_t::special};
    bool player_header_two_stage{};
    bool octave_fp_header{};
};

[[nodiscard]] auto make_bms_7k_profile(scratch_placement_t scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;
[[nodiscard]] auto
make_bms_14k_profile(scratch_placement_t left_scratch = scratch_placement_t::outside,
                     scratch_placement_t right_scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;

// BMS 5K uses channel 17 as a free-zone lane by default. The explicitly named
// foot-pedal variant assigns the same raw channel a different semantic role.
[[nodiscard]] auto make_bms_5k_profile(scratch_placement_t scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;
[[nodiscard]] auto
make_bms_5k_free_zone_profile(scratch_placement_t scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;
[[nodiscard]] auto
make_bms_5k_foot_pedal_profile(scratch_placement_t scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;
[[nodiscard]] auto
make_bms_10k_profile(scratch_placement_t left_scratch = scratch_placement_t::outside,
                     scratch_placement_t right_scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;
[[nodiscard]] auto
make_bms_10k_free_zone_profile(scratch_placement_t left_scratch = scratch_placement_t::outside,
                               scratch_placement_t right_scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;
[[nodiscard]] auto
make_bms_10k_foot_pedal_profile(scratch_placement_t left_scratch = scratch_placement_t::outside,
                                scratch_placement_t right_scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;

// BME 7K/14K profiles retain the existing BMS-named factories above for
// source compatibility. The optional foot-pedal channels are separate lanes
// and are not included in the default keyboard bindings.
[[nodiscard]] auto make_bme_7k_profile(scratch_placement_t scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;
[[nodiscard]] auto
make_bme_7k_foot_pedal_profile(scratch_placement_t scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;
[[nodiscard]] auto
make_bme_14k_profile(scratch_placement_t left_scratch = scratch_placement_t::outside,
                     scratch_placement_t right_scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;
[[nodiscard]] auto
make_bme_14k_foot_pedal_profile(scratch_placement_t left_scratch = scratch_placement_t::outside,
                                scratch_placement_t right_scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;

[[nodiscard]] auto make_pms_5k_profile() -> lane_input_profile_t;
[[nodiscard]] auto make_pms_9k_profile() -> lane_input_profile_t;
[[nodiscard]] auto make_pms_bme_style_9k_profile() -> lane_input_profile_t;
[[nodiscard]] auto make_pms_bme_style_18k_profile() -> lane_input_profile_t;

[[nodiscard]] auto make_bms_double_scratch_profile() -> lane_input_profile_t;
// OCT/FP has fifteen scratch/key lanes in one stage; its default input remains
// unbound until a keyboard bank policy is specified.
[[nodiscard]] auto make_bms_octave_fp_profile() -> lane_input_profile_t;
[[nodiscard]] auto make_project2dx_7k_profile() -> lane_input_profile_t;

// Stable process-lifetime catalog used by detection and frontend profile
// pickers. Callers must treat the returned profiles as immutable.
[[nodiscard]] auto built_in_bms_lane_profiles() -> std::span<const bms_lane_profile_descriptor_t>;

// The number row (1 through 0) is applied in display order to playable and
// scratch lanes in one stage. The operation fails if that stage has more than
// ten bindable lanes or does not exist in the supplied layout.
[[nodiscard]] auto make_number_row_input_profile(const lane_layout_t& layout,
                                                 std::uint32_t stage = 0,
                                                 std::string profile_id = "number-row")
    -> std::optional<input_profile_t>;

[[nodiscard]] auto make_bmson_generic_24k_profile() -> lane_input_profile_t;
[[nodiscard]] auto make_bmson_generic_48k_profile() -> lane_input_profile_t;

} // namespace wormedit
