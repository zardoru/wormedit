#pragma once

#include <wormedit/core/lane_profiles.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace wormedit {

// Channel observations retain the exact input spelling and order. The
// normalized fields are only aids for profile comparison; callers can always
// round-trip raw_spelling from the custom fallback.
enum class bms_raw_channel_kind_t {
    non_playable,
    visible_note,
    invisible_note,
    long_note,
    mine,
    unknown_or_reserved,
};

struct bms_raw_channel_t {
    std::size_t source_order{};
    std::string raw_spelling;
    std::optional<std::string> canonical_spelling;
    bms_raw_channel_kind_t kind{bms_raw_channel_kind_t::unknown_or_reserved};
};

// Kept alongside every detection result, including successful detections.
// Unmapped and future channel members therefore survive before any semantic
// lane decision is made.
struct bms_custom_channel_map_t {
    std::string profile_id{"bms-custom-raw"};
    std::vector<bms_raw_channel_t> channels;
};

enum class bms_detection_evidence_source_t {
    file_extension,
    player_header,
    octave_fp_header,
    used_channel,
    explicit_profile,
};

struct bms_profile_evidence_t {
    bms_detection_evidence_source_t source{bms_detection_evidence_source_t::used_channel};
    std::string message;
    double weight{};
};

struct bms_profile_detection_request_t {
    // May include or omit the leading dot. Matching is ASCII case-insensitive.
    std::string file_extension;
    std::optional<unsigned int> player_header;
    bool octave_fp_header{false};
    std::vector<std::string> used_channels;
    // An explicit project/user choice takes precedence only if every
    // recognized playable channel is representable by that profile.
    std::optional<std::string> explicit_profile_id;
};

struct bms_profile_candidate_t {
    lane_input_profile_t profile;
    double confidence{};
    bool maps_all_playable_channels{false};
    // At least one observed playable channel family covers this profile's
    // complete corresponding family after duplicate observations are removed.
    bool complete_channel_family_match{false};
    // Special dialects require their named header or an explicit choice even
    // when their raw channel occupancy happens to be a strong match.
    bool automatic_selection_eligible{true};
    std::vector<std::string> unmapped_playable_channels;
    std::vector<bms_profile_evidence_t> evidence;
};

enum class bms_profile_detection_status_t {
    selected,
    ambiguous,
    custom_required,
};

struct bms_profile_detection_result_t {
    bms_profile_detection_status_t status{bms_profile_detection_status_t::custom_required};
    double confidence{};
    std::optional<lane_input_profile_t> selected_profile;
    std::vector<bms_profile_candidate_t> candidates;
    bms_custom_channel_map_t custom_fallback;
    std::vector<bms_profile_evidence_t> evidence;
};

// Candidate confidence is comparative evidence, not a claim of file-format
// certainty. A unique complete primary family with matching player/header hints
// can select the ordinary standard profile while its auxiliary lanes are unused.
// Otherwise automatic selection requires a strong score and a clear margin. Sparse charts commonly remain
// ambiguous, while the raw fallback always preserves their original channels.
[[nodiscard]] auto detect_bms_lane_profile(const bms_profile_detection_request_t& request)
    -> bms_profile_detection_result_t;

} // namespace wormedit
