#pragma once

#include <wormedit/audio/aligned_waveform.h>
#include <wormedit/core/sound_slice.h>

namespace wormedit::audio {

struct channel_waveform_source_geometry_t {
    std::uint32_t sample_rate{};
    std::optional<std::uint64_t> total_frames;
};

// Projects one retained BMSON sound channel into the chart viewport. Source
// peaks stay in source-frame space; the exact channel cuts select the frames
// that may appear and timing maps them to musical rows.
[[nodiscard]] auto
build_channel_waveform_view(const waveform_t& waveform, const chart_document_t& chart,
                         const sound_channel_t& channel, const timing_index_t& timing,
                         aligned_waveform_request_t request,
                         channel_waveform_source_geometry_t source) -> aligned_waveform_result_t;

// Accepts a revision-bound slice derivation so interactive scrolling and zoom
// need only reproject peaks, not rebuild all channel cuts.
[[nodiscard]] auto
build_channel_waveform_view(const waveform_t& waveform, const sound_channel_t& channel,
                         const timing_index_t& timing, aligned_waveform_request_t request,
                         const sound_slice_result_t& slices,
                         channel_waveform_source_geometry_t source) -> aligned_waveform_result_t;

// Display path: shared forward clock and bounded row/slice pyramid reads.
// The timing_index_t overloads remain the exact inverse reference path.
[[nodiscard]] auto build_channel_waveform_view(const waveform_t& waveform,
    const sound_channel_t& channel, const display_row_table_t& table,
    aligned_waveform_request_t request, const sound_slice_result_t& slices,
    channel_waveform_source_geometry_t source) -> aligned_waveform_result_t;

} // namespace wormedit::audio
