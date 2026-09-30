#pragma once

#include <wormedit/audio/preview_bridge.h>
#include <wormedit/audio/preview_session.h>
#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/audition_session.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/workspace.h>

#include "sys/sdl_audition_output.h"
#include "sys/sdl_preview_output.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::sys::runtime {

struct chart_preview_t {
    std::optional<preview_schedule_t> schedule;
    std::optional<timing_index_t> timing;
    std::vector<audio::preview_object_source_binding_t> bindings;
    std::optional<chart_id_t> chart;
    std::uint64_t revision{};
    std::optional<asset_id_t> music_asset;
    std::uint64_t resume_frame{};
    std::uint64_t end_frame{};
    std::size_t skipped_unavailable_audio_objects{};
    bool paused{};

    [[nodiscard]] bool reusable(chart_id_t chart_id, std::uint64_t document_revision,
                                std::optional<asset_id_t> selected_music_asset) const;
    [[nodiscard]] bool matches_selection(const song_document_t& document,
                                         const workspace_controller_t& workspace) const;
    [[nodiscard]] auto compile_if_needed(
        const chart_document_t& selected_chart, const song_document_t& document,
        const audio::decoded_source_registry_t& sources,
        const std::vector<audio::preview_metronome_source_binding_t>& metronome_bindings)
        -> std::optional<std::string>;
    [[nodiscard]] std::uint64_t current_frame(const audio::preview_session_t& session) const;
    [[nodiscard]] auto play_pause(
        const chart_document_t* selected_chart, const song_document_t& document,
        const audio::decoded_source_registry_t& sources,
        const std::vector<audio::preview_metronome_source_binding_t>& metronome_bindings,
        sdl_audition_output_t& audition_output,
        audio::preview_session_t& preview_session, sdl_preview_output_t& preview_output,
        std::optional<local_position_t> start_cursor = std::nullopt,
        bool from_beginning = false)
        -> std::string;
    [[nodiscard]] auto seek(
        std::uint64_t frame, const song_document_t& document,
        const workspace_controller_t& workspace,
        sdl_audition_output_t& audition_output, audio::preview_session_t& preview_session,
        sdl_preview_output_t& preview_output) -> std::string;
    [[nodiscard]] auto audio_milliseconds(
        const song_document_t& document, const workspace_controller_t& workspace,
        const audio::preview_session_t& session) const -> std::optional<std::int64_t>;
    [[nodiscard]] auto seek_audio_milliseconds(
        std::int64_t milliseconds, const song_document_t& document,
        const workspace_controller_t& workspace,
        sdl_audition_output_t& audition_output, audio::preview_session_t& preview_session,
        sdl_preview_output_t& preview_output) -> std::string;
    void stop(sdl_audition_output_t& audition_output, sdl_preview_output_t& preview_output);
    void pause_at(std::uint64_t frame);
    void reset_transport();
    void invalidate();
};

} // namespace wormedit::sys::runtime
