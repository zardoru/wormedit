#pragma once

#include "sys/compose_view.h"

#include <wormedit/core/command.h>

#include <optional>
#include <string>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include "sys/chart_preview_state.h"
#include "sys/sdl_audition_output.h"
#include "sys/sdl_preview_output.h"
#include <wormedit/audio/object_audition.h>
#include <wormedit/audio/source_registry.h>
#endif

namespace wormedit::sys {

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
// Plays one catalog sound from its start (Compose keysound listbox), pausing
// the chart preview first like every other audition.
void process_sound_audition_request(
    std::optional<resource_id_t>& request, document_history_t& history,
    audio::decoded_source_registry_t& audition_sources,
    audio::preview_session_t& preview_session, sdl_preview_output_t& preview_output,
    runtime::chart_preview_t& preview_state, sdl_audition_output_t& audition_output,
    std::string& status);

void process_compose_audition_requests(
    compose_scene_cache_t& cache, document_history_t& history,
    audio::decoded_source_registry_t& audition_sources,
    audio::object_audition_controller_t& object_audition,
    audio::preview_session_t& preview_session, sdl_preview_output_t& preview_output,
    runtime::chart_preview_t& preview_state, sdl_audition_output_t& audition_output,
    std::string& status);
#else
void process_sound_audition_request(std::optional<resource_id_t>& request, std::string& status);
void process_compose_audition_requests(compose_scene_cache_t& cache,
                                       document_history_t& history, std::string& status);
#endif

} // namespace wormedit::sys
