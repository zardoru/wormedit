#pragma once

#include <wormedit/audio/preview_bridge.h>
#include <wormedit/audio/preview_session.h>
#include "sys/audio/project_audio_sources.h"
#include <wormedit/audio/audition_session.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/workspace.h>
#include <wormedit/core/notifications.h>

#include "sys/audio/sdl_audition_output.h"
#include "sys/audio/sdl_preview_output.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <tuple>

namespace wormedit::sys::runtime {

struct preview_feedback_t {
    notification_severity_t severity{notification_severity_t::info};
    bool accepted{};
};

// Assign source-lane mix identity without changing source ranges or authored data.
void assign_preview_bgm_mix_groups(const chart_document_t& chart,
    std::span<audio::preview_object_source_binding_t> bindings);

struct chart_preview_t {
    struct compiled_t {
        std::optional<preview_schedule_t> schedule;
        std::optional<timing_index_t> timing;
        std::vector<audio::preview_object_source_binding_t> bindings;
        std::vector<audio::preview_metronome_source_binding_t> retained_metronome_bindings;
        std::uint64_t end_frame{};
        std::size_t skipped_unavailable_audio_objects{};
    };
    using source_key_t = std::tuple<chart_id_t, std::uint64_t, std::optional<asset_id_t>, std::uint8_t>;
    keyed_cache_t<source_key_t, compiled_t> compiled;
    [[nodiscard]] auto chart() const -> std::optional<chart_id_t> {
        return compiled.has_value() ? std::optional{std::get<0>(*compiled.key())} : std::nullopt;
    }
    [[nodiscard]] auto revision() const -> std::uint64_t {
        return compiled.has_value() ? std::get<1>(*compiled.key()) : 0;
    }
    [[nodiscard]] auto music_asset() const -> std::optional<asset_id_t> {
        return compiled.has_value() ? std::get<2>(*compiled.key()) : std::nullopt;
    }
    std::uint64_t resume_frame{};
    std::int32_t display_latency_ms{};
    [[nodiscard]] auto display_frame(const audio::preview_session_t&) const -> std::uint64_t;
    bool paused{};
    std::optional<chart_id_t> bgm_mix_chart;
    std::vector<std::uint64_t> bgm_muted_lanes;
    void set_bgm_mix(chart_id_t selected, std::span<const std::uint64_t> muted,
                     audio::preview_session_t& session);
    std::optional<chart_id_t> layer_assist_chart;
    std::vector<layer_id_t> layer_assist_layers;
    [[nodiscard]] auto layer_assist_frames() const -> std::vector<std::uint64_t>;
    [[nodiscard]] auto set_layer_assist(chart_id_t selected, std::span<const layer_id_t> layers,
                                        audio::preview_session_t& session) -> bool;
    metronome_t* metronome{};
    [[nodiscard]] auto count_in_measures() const -> std::uint8_t {
        return metronome ? metronome->settings.count_in : 0;
    }
    const sdl_preview_output_t* output_observer{};
    preview_feedback_t feedback;

    auto report(notification_severity_t severity, bool accepted, std::string message) -> std::string;

    [[nodiscard]] bool preserve_bookmark_revision(chart_id_t chart_id,
                                                  std::uint64_t previous_revision,
                                                  std::uint64_t next_revision) noexcept;
    [[nodiscard]] bool matches_selection(const song_document_t& document,
                                         const workspace_controller_t& workspace) const;
    [[nodiscard]] auto compile_if_needed(
        const chart_document_t& selected_chart, const song_document_t& document,
        project_audio_sources_t& sources)
        -> std::optional<std::string>;
    [[nodiscard]] bool playing(const audio::preview_session_t& session) const;
    [[nodiscard]] std::uint64_t current_frame(const audio::preview_session_t& session) const;
    [[nodiscard]] auto play_pause(
        const chart_document_t* selected_chart, const song_document_t& document,
        project_audio_sources_t& sources,
        sdl_audition_output_t& audition_output,
        audio::preview_session_t& preview_session, sdl_preview_output_t& preview_output,
        std::optional<local_position_t> start_cursor = std::nullopt,
        bool from_beginning = false, std::optional<std::uint64_t> exact_start_frame = std::nullopt,
        double start_cursor_subrow = 0.0,
        const result_t<measure_index_t, diagnostic_list_t>* geometry = nullptr)
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

  private:
    struct compilation_request_t {
        const chart_document_t& chart;
        const song_document_t& document;
        project_audio_sources_t& sources;
        std::span<const audio::preview_metronome_source_binding_t> metronome_bindings;
        std::optional<std::pair<resource_id_t, asset_id_t>> selected_music;
    };
    [[nodiscard]] auto build_compiled(const compilation_request_t& request, compiled_t& data)
        -> std::optional<std::string>;
};

} // namespace wormedit::sys::runtime
