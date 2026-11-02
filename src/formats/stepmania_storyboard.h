#pragma once

#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/osbx_osb_export.h>
#include <wormedit/core/vocabulary.h>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

// The caller owns song timing, prepared image receipts and atomic publication.
// This file-free emitter owns only native actor text and per-object losses.
struct stepmania_storyboard_options_t {
    static constexpr std::int64_t maximum_music_time_ms = 86'400'000;
    osbx_osb_span_t playback_span;
    // Activation must precede the playback span. SSC integration converts this
    // music time to an unambiguous song beat; this emitter never guesses a BPM.
    std::int64_t activation_time_ms{};
    storyboard_conditional_layer_t conditional_layer{storyboard_conditional_layer_t::pass};
    stepmania_implementation_profile_t implementation_profile{stepmania_implementation_profile_t::stepmania_5_1};
    std::string relative_directory{"wormedit-storyboard"};
    std::span<const osbx_osb_image_size_t> image_sizes;
    std::size_t maximum_output_bytes{8U * 1024U * 1024U};
    std::size_t maximum_actor_frames{100'000};
    std::size_t maximum_scene_storage_bytes{64U * 1024U * 1024U};
    std::size_t maximum_work{8'000'000};
};

struct stepmania_storyboard_file_t {
    std::filesystem::path relative_path;
    // Hook folder (SM5) or XML file (NotITG); empty for a sidecar with no hook.
    std::string actor_reference;
    bool foreground{};
    std::string bytes;
    std::vector<std::string> images;
};

struct stepmania_storyboard_export_t {
    std::vector<stepmania_storyboard_file_t> files;
    std::vector<osbx_osb_report_t> report;
};
using stepmania_storyboard_outcome_t = diagnosed_t<stepmania_storyboard_export_t,
    std::vector<osbx_osb_diagnostic_t>>;

// Absolute song-time SM5 Lua5.1 or conservative NotITG XML/Lua5.0 declarations,
// no filesystem access or Lua VM
// in the exporter. Prepared Lua links resolve through the existing core scope.
// Unsupported objects are omitted and reported; the caller must still emit its
// gameplay chart. No unverified target-engine runtime certificate is implied.
[[nodiscard]] auto emit_stepmania_storyboard(const osbx_document_t&,
    stepmania_storyboard_options_t) -> stepmania_storyboard_outcome_t;

} // namespace wormedit
