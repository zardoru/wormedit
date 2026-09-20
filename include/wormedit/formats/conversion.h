#pragma once

#include <wormedit/core/document.h>

#include <optional>
#include <string>

namespace wormedit {

enum class stepmania_profile_t {
    sm,
    ssc,
};

// Accepted/emitted warp syntax. This is independent of the target application.
enum class stepmania_timing_syntax_t {
    unspecified,
    native_warps,
    legacy_negative_bpm,
    legacy_negative_stop,
};

// Fixture-backed application/release subsets. A named profile claims only the
// fields and timing syntax covered by its pinned source audit.
enum class stepmania_implementation_profile_t {
    unspecified,
    stepmania_5_1,
    etterna_0_75_1,
};

// These are three independent conversion choices. Named implementation
// profiles further constrain the file/syntax pair; they do not replace it.
struct stepmania_export_policy_t {
    stepmania_profile_t file_profile{stepmania_profile_t::ssc};
    stepmania_timing_syntax_t timing_syntax{stepmania_timing_syntax_t::native_warps};
    stepmania_implementation_profile_t implementation_profile{
        stepmania_implementation_profile_t::unspecified};
};

struct conversion_request_t {
    format_family_t target{format_family_t::native_project};
    std::string target_profile;
    std::optional<stepmania_export_policy_t> stepmania;
    std::optional<chart_id_t> chart;
    warp_encoding_t warp_encoding{warp_encoding_t::native};
    branch_projection_t branches{branch_projection_t::preserve_tree};
    std::optional<text_encoding_t> encoding;
    std::optional<quantize_request_t> quantization;
    loss_policy_t loss_policy{loss_policy_t::reject};
    bool overwrite{false};
};

} // namespace wormedit
