#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/export_cut.h>
#include <span>

namespace wormedit {

inline constexpr std::uint32_t export_cut_render_sample_rate = 48'000;

// Fresh decoder evidence supplied by the I/O owner. The pure transformation never opens files.
struct export_cut_source_t {
    asset_id_t asset;
    std::uint32_t native_sample_rate{};
    std::uint64_t native_frame_count{};
    std::uint64_t output_frame_count{};
    std::filesystem::path source_path;
    std::uint32_t bitrate_bits_per_second{};
    auto operator==(const export_cut_source_t&) const -> bool = default;
};
struct export_cut_audio_plan_t {
    asset_id_t asset;
    std::string filename;
    export_cut_source_t source;
    std::uint64_t output_frames{};
    export_audio_settings_t encoding;
    auto operator==(const export_cut_audio_plan_t&) const -> bool = default;
};
struct export_cut_location_t {
    std::optional<chart_id_t> chart;
    std::optional<object_id_t> object;
    auto operator==(const export_cut_location_t&) const -> bool = default;
};
using export_cut_diagnostic_t = format_diagnostic_t<diagnostic_code_t, export_cut_location_t>;
// A detached export document and its generated audio. Source cuts and authored data are untouched.
struct export_cut_transform_t {
    [[nodiscard]] static auto from(const song_document_t& source, std::span<const chart_id_t> charts,
                                   std::span<const export_cut_source_t> sources = {},
                                   export_audio_settings_t audio = {})
        -> diagnosed_t<std::optional<export_cut_transform_t>, std::vector<export_cut_diagnostic_t>>;
    song_document_t document;
    std::vector<export_cut_audio_plan_t> audio_plans;
};

using export_cut_transform_outcome_t =
    diagnosed_t<std::optional<export_cut_transform_t>, std::vector<export_cut_diagnostic_t>>;

} // namespace wormedit
