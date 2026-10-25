#pragma once

#include <wormedit/core/export_cut_transform.h>
#include <wormedit/formats/slice_spans.h>

namespace wormedit {

// Fresh decoder geometry. With no authored cuts this performs no source I/O.
[[nodiscard]] auto prepare_export_cut_sources(const song_document_t& song,
    const std::filesystem::path& source_directory, std::stop_token stop = {})
    -> diagnosed_t<std::vector<export_cut_source_t>>;

// Writes generated float32 stereo WAVs to the caller's private transaction
// staging directory. Failure removes files created by this call only.
[[nodiscard]] auto render_export_cut_audio_plans(const song_document_t& song,
    std::span<const export_cut_audio_plan_t> plans, const export_cut_timeline_t& timeline,
    const slice_span_render_options_t& options) -> slice_span_render_payload_t;

// Removes the same rounded frame union from bounded interleaved stereo PCM.
// source_begin_frame locates the buffer on the original source-song clock.
[[nodiscard]] auto cut_preview_audio(std::span<const float> interleaved_stereo,
    std::uint64_t source_begin_frame, const export_cut_timeline_t& timeline,
    std::uint32_t sample_rate) -> diagnosed_t<std::vector<float>>;

} // namespace wormedit
