#pragma once
#include <wormedit/core/vocabulary.h>
#include "metadata_audio.h"
#include "workspace_types.h"
#include <cstdint>
#include <string>

// Session-level requests: transport, recovery, compare, close and open. Each is a window of its own.
namespace wormedit::ui {
/* Playback transport: seek target and rate. */
struct transport_request_t {
    uint64_t seek_frame;
    uint8_t rate_index;
    wormedit::audio::playback_rate_id_t rate_id;
    bool keep_pitch;
};
/* Drag of the audio offset handle, stamped with the chart and revision it started on. */
struct audio_offset_drag_request_t {
    WormUiAudioOffsetDragAction action;
    chart_id_t chart_id;
    uint64_t document_revision;
    uint32_t sample_rate;
    int64_t delta_frames;
};
/* Recovery list: the scan the choice was made against and the chosen row, candidate and destination. */
struct recovery_request_t {
    uint64_t scan_generation;
    size_t row_index;
    size_t candidate_index;
    std::string save_as_destination;
};
/* Compare tab: chart under comparison and the diagnostic row to jump to. */
struct compare_request_t {
    chart_id_t chart_id;
    uint64_t diagnostic_row;
    wormedit::measure_id_t diagnostic_measure;
    chart_id_t diagnostic_chart;
    double scroll_delta;
};
/* Answer to the close-project prompt. */
struct close_request_t {
    WormUiCloseDecision decision;
    song_id_t project_id;
    uint64_t revision;
};
/* Project to open or create. */
struct project_open_request_t {
    std::string path;
    // Unknown requests automatic detection.
    text_encoding_t encoding_id;
    std::string lane_profile_id;
    // Stable core layout identity.
    std::string layout_id;
};
/* Assets chosen in the audio and visual selectors. */
struct asset_selection_request_t {
    wormedit::asset_id_t audio_asset_id;
    wormedit::resource_id_t audio_resource_id;
    wormedit::asset_id_t visual_asset_id;
};
} // namespace wormedit::ui
