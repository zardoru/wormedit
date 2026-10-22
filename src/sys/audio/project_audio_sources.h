#pragma once

#include "sys/audio/audio_source_registration.h"
#include "sys/audio/metronome.h"
#include <wormedit/audio/hitsound_resolver.h>
#include <wormedit/audio/audition_resolver.h>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
namespace wormedit::sys {
class sdl_audition_output_t;
class sdl_preview_output_t;
namespace runtime { class waveform_analysis_t; struct chart_preview_t; }

// Control-thread owner. Documents are passed at query boundaries, never retained.
struct project_audio_sources_t {
    audio::decoded_source_registry_t sources{};
    std::optional<song_id_t> source_project{};
    std::vector<audition_asset_entry_t> asset_inventory{};
    std::uint64_t generation{};
    std::optional<std::uint64_t> reconciled_revision{};
    std::filesystem::path asset_base{};
    metronome_t* metronome{};
    audio::hitsound_resolver_t hitsounds;
    [[nodiscard]] auto resolve_hitsounds(const song_document_t&, const chart_document_t&,
                                        std::optional<object_id_t> object = {})
        -> std::vector<audio::hitsound_binding_t>;
    [[nodiscard]] auto resolve_preview(const song_document_t&, const chart_document_t&,
        const preview_schedule_t&) -> audio::preview_bindings_resolve_outcome_t;
    [[nodiscard]] auto metronome_bindings() const -> std::span<const audio::preview_metronome_source_binding_t> {
        return metronome ? metronome->bindings() : std::span<const audio::preview_metronome_source_binding_t>{};
    }
    task_scheduler_t* probe_scheduler{};
    resident_sample_loader_t* resident_loader{};
    const notification_sink_t* notifications{};
#if defined(WORMEDIT_HAS_PROJECT_AUDIO_OUTPUTS)
    sdl_audition_output_t* audition_output{};
    sdl_preview_output_t* preview_output{};
    runtime::waveform_analysis_t* waveform{};
    runtime::chart_preview_t* preview{};
#endif

    [[nodiscard]] auto registry() -> audio::decoded_source_registry_t& { return sources; }
    [[nodiscard]] auto registry() const -> const audio::decoded_source_registry_t& { return sources; }
    [[nodiscard]] auto reconcile(const song_document_t&, const std::filesystem::path&, bool stop_playback = true)
        -> std::optional<std::size_t>;
    [[nodiscard]] auto reconcile_if_needed(const song_document_t&) -> std::optional<std::size_t>;
    void bindings(const song_document_t&, std::vector<audio::decoded_source_binding_t>& result);
    [[nodiscard]] auto bindings(const song_document_t&) -> std::vector<audio::decoded_source_binding_t>;
    [[nodiscard]] auto binding(const song_document_t&, asset_id_t) -> std::optional<audio::decoded_source_binding_t>;
    void reset_dependents(bool stop_playback);
    void ensure_current(const song_document_t&);
};
} // namespace wormedit::sys
#endif
