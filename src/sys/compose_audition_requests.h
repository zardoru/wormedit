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
struct preview_audition_inputs_t {
    audio::preview_session_t& session;
    sdl_preview_output_t& output;
    runtime::chart_preview_t& state;
};

struct object_audition_inputs_t {
    audio::decoded_source_registry_t& sources;
    audio::object_audition_controller_t& controller;
    sdl_audition_output_t& output;
};

struct compose_audition_inputs_t {
    preview_audition_inputs_t preview;
    object_audition_inputs_t audition;
    const notification_sink_t& notifications;
};

// Plays one catalog sound from its start (Compose keysound listbox), pausing
// the chart preview first like every other audition.
void process_sound_audition_request(std::optional<resource_id_t>& request,
                                    document_history_t& history,
                                    const compose_audition_inputs_t& inputs);

void process_compose_audition_requests(compose_scene_cache_t& cache, document_history_t& history,
                                       const compose_audition_inputs_t& inputs);
#else
void process_sound_audition_request(std::optional<resource_id_t>& request,
                                    const notification_sink_t& notifications);
void process_compose_audition_requests(compose_scene_cache_t& cache, document_history_t& history,
                                       const notification_sink_t& notifications);
#endif

} // namespace wormedit::sys
