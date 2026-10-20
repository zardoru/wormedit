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
    auto operator==(const lane_input_profile_t&) const -> bool = default;
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
[[nodiscard]] auto make_bms_5k_standard_profile(
    scratch_placement_t scratch = scratch_placement_t::outside) -> lane_input_profile_t;
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
// DP5: five playable keys and one scratch per side, without auxiliary lanes.
[[nodiscard]] auto
make_bms_10k_standard_profile(scratch_placement_t left_scratch = scratch_placement_t::outside,
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
// profiles. It is a special profile; detection selects it automatically only
// when complete scratchless 10-key channels and a compatible BMS player header
// provide unambiguous evidence.
// Derives each lane's key color from the layout's profile id and lane roles:
// BMS/BME keys alternate white/black per stage (scratch lanes excluded), PMS
// nine-button stages use the button colors, osu!mania uses symmetric key classes,
// generic piano banks follow note names, and everything else stays generic. Pedal, free-zone and scratch roles
// always map to their own class.
void assign_lane_key_colors(lane_layout_t& layout);

// Stable skin preference identity of the selected semantic layout. Scratch placement,
// lane IDs, source channels and the chart's file format do not affect known SP/DP keys.
// Unrecognized profile IDs remain distinct; an empty ID uses "custom".
[[nodiscard]] auto field_skin_layout_key(const lane_layout_t& layout) -> std::string;
// Common skin class names, independent of the original instrument's enum spelling.
// A playable special/red column keeps its authored role; only its visual class is red.
[[nodiscard]] auto field_skin_key_class(lane_key_color_t color) -> std::string_view;


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
[[nodiscard]] auto make_default_input_profile_for(const lane_layout_t& layout)
    -> std::optional<input_profile_t>;

// Column names and arrow glyphs of the StepMania step types that have a named
// layout (`pump-single`, `pump-double`, `dance-solo`), in file column order. The step type is
// matched ASCII case-insensitively; other types return an empty span.
struct stepmania_lane_spec_t {
    std::string_view name;
    lane_glyph_t glyph{lane_glyph_t::bar};
};
[[nodiscard]] auto stepmania_lane_specs(std::string_view step_type)
    -> std::span<const stepmania_lane_spec_t>;

// Full profile (layout with arrow glyphs and number-row bindings) of a StepMania step type that
// has a named layout; nullopt for other step types.
[[nodiscard]] auto make_stepmania_profile_for(std::string_view step_type)
    -> std::optional<lane_input_profile_t>;
// osu!mania key layout: `osu-mania-Nk`, lanes "Key 1"..."Key N", 1 to 18 keys.
[[nodiscard]] auto make_osu_mania_profile(std::uint32_t keys) -> std::optional<lane_input_profile_t>;
// SpecialStyle 12K/16K use DP-like split stages with playable outside columns.
[[nodiscard]] auto make_osu_mania_profile(std::uint32_t keys, bool special_style) -> std::optional<lane_input_profile_t>;

// pump-single: down-left, up-left, center, up-right, down-right.
[[nodiscard]] auto make_stepmania_pump_single_profile() -> lane_input_profile_t;
// dance-solo: left, up-left, down, up, up-right, right.
[[nodiscard]] auto make_stepmania_dance_solo_profile() -> lane_input_profile_t;

[[nodiscard]] auto make_bmson_generic_24k_profile() -> lane_input_profile_t;
[[nodiscard]] auto make_bmson_generic_48k_profile() -> lane_input_profile_t;

inline constexpr std::uint32_t minimum_osu_mania_keys = 1;
inline constexpr std::uint32_t maximum_osu_mania_keys = 18;

struct stepmania_step_type_descriptor_t {
    std::string_view id;
    std::uint32_t keys;
    bool default_for_key_count;
};
[[nodiscard]] auto stepmania_step_types() -> std::span<const stepmania_step_type_descriptor_t>;
[[nodiscard]] auto stepmania_key_count(std::string_view step_type) -> std::optional<std::uint32_t>;
[[nodiscard]] auto default_stepmania_step_type(std::size_t keys) -> std::string_view;

struct new_project_output_group_descriptor_t {
    std::string_view label;
    format_family_t family;
    std::string_view relative_output_path;
    std::optional<stepmania_export_policy_t> stepmania;
};
struct new_project_layout_descriptor_t {
    // The layout's profile with lane_keys bound left to right in display order. A layout without
    // lane_keys keeps its factory bindings; wider than ten lanes, that is none (D7).
    [[nodiscard]] auto profile() const -> lane_input_profile_t;
    // A shared output family enables its authoring tools; mixed/empty outputs stay native.
    [[nodiscard]] auto source_family() const -> format_family_t;

    std::string_view id;
    std::string_view label;
    lane_input_profile_t (*make_profile)();
    std::span<const new_project_output_group_descriptor_t> default_output_groups;
    std::string_view step_type;
    std::span<const physical_key_t> lane_keys{};
};
// Static descriptors and factories allocate only when a project is constructed.
[[nodiscard]] auto new_project_layouts() -> std::span<const new_project_layout_descriptor_t>;
[[nodiscard]] auto find_new_project_layout(std::string_view id) -> const new_project_layout_descriptor_t*;
[[nodiscard]] auto make_new_project_output_groups(const new_project_layout_descriptor_t& layout,
                                                 chart_id_t chart) -> std::vector<output_group_t>;

struct output_family_compatibility_t {
    bool compatible{};
    std::string_view reason;
};
[[nodiscard]] auto output_family_compatibility(const chart_document_t& chart,
                                               format_family_t family)
    -> output_family_compatibility_t;
[[nodiscard]] auto is_bms_lane_layout_compatible(const lane_layout_t& source,
                                                 const lane_layout_t& target) -> bool;
[[nodiscard]] auto suggested_output_groups(const chart_document_t& chart)
    -> std::span<const new_project_output_group_descriptor_t>;

} // namespace wormedit
