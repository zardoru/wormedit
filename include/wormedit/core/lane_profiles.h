#pragma once

#include <wormedit/core/document.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

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

// Ten playable keys (5 per player, channels 11-15 and 21-25) with no scratch or
// auxiliary lane, for osu!mania-style charts. Lane ids match the other 10K
// profiles. It is a special profile: detection never selects it on its own.
[[nodiscard]] auto make_bms_10k_scratchless_profile() -> lane_input_profile_t;

// BMS "10K" is beatmania double play with five keys per side (DP5), and "14K"
// is double play with seven (DP7). Display names say both; profile ids stay.
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

// Bind all playable/scratch lanes in display order. Single-stage layouts use
// digits regardless of semantic stage tags; split layouts use digits and QWERTY.
[[nodiscard]] auto default_input_profile_for(const lane_layout_t& layout)
    -> std::optional<input_profile_t>;

// Column names and arrow glyphs of the StepMania step types that have a named
// layout (`pump-single`, `dance-solo`), in file column order. The step type is
// matched ASCII case-insensitively; other types return an empty span.
struct stepmania_lane_spec_t {
    std::string_view name;
    lane_glyph_t glyph{lane_glyph_t::bar};
};
[[nodiscard]] auto stepmania_lane_specs(std::string_view step_type)
    -> std::span<const stepmania_lane_spec_t>;

// pump-single: down-left, up-left, center, up-right, down-right.
[[nodiscard]] auto make_stepmania_pump_single_profile() -> lane_input_profile_t;
// dance-solo: left, up-left, down, up, up-right, right.
[[nodiscard]] auto make_stepmania_dance_solo_profile() -> lane_input_profile_t;

[[nodiscard]] auto make_bmson_generic_24k_profile() -> lane_input_profile_t;
[[nodiscard]] auto make_bmson_generic_48k_profile() -> lane_input_profile_t;

} // namespace wormedit
