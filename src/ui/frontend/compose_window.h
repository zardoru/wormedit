#pragma once
#include "compose_request.h"
#include "frame_input.h"
#include "session_requests.h"
#include "request_stamp.h"
#include "sound_requests.h"

namespace wormedit::ui {
/* What sys/compose reads of a frame: the Compose request, the stamp it was built against, and the
   sound requests that Compose dispatches. A dispatcher that takes this can't see (or write) any other
   domain's request. Built from the frame action by compose_window_of (actions.h). */
struct compose_window_t {
    const compose_request_t& compose;
    const request_stamp_t& stamp;
    const sound_import_request_t& sound_import;
    const sound_properties_request_t& sound_properties;
};
/* What sys/compose input dispatch reads and writes of a frame: the sampled input, the Compose request it
   may raise, its stamp, and the audio-offset drag it can start (a waveform handle drag is Compose input
   that moves the audio offset). */
struct compose_input_window_t {
    frame_input_t& input;
    compose_request_t& compose;
    request_stamp_t& stamp;
    audio_offset_drag_request_t& audio_offset_drag;
};
} // namespace wormedit::ui
