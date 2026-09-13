#pragma once

#include <wormedit/core/document.h>

#include <cstdint>
#include <optional>
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

[[nodiscard]] auto make_bms_7k_profile(scratch_placement_t scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;
[[nodiscard]] auto
make_bms_14k_profile(scratch_placement_t left_scratch = scratch_placement_t::outside,
                     scratch_placement_t right_scratch = scratch_placement_t::outside)
    -> lane_input_profile_t;

// The number row (1 through 0) is applied in display order to one stage. The
// operation fails if the selected stage has more than ten lanes or does not
// exist in the supplied layout.
[[nodiscard]] auto make_number_row_input_profile(const lane_layout_t& layout,
                                                 std::uint32_t stage = 0,
                                                 std::string profile_id = "number-row")
    -> std::optional<input_profile_t>;

[[nodiscard]] auto make_bmson_generic_24k_profile() -> lane_input_profile_t;
[[nodiscard]] auto make_bmson_generic_48k_profile() -> lane_input_profile_t;

} // namespace wormedit
