#pragma once
// What the audio dock (and the metadata audio section that embeds it) may write of the frame.
// Deliberately not actions.h: audio_ui_window_of lives in audio_ui_window_of.h for the
// composition root. Member names match workspace_action_t so the panel code reads the same.
#include "ui/frontend/audio_slicer.h"
#include "ui/frontend/compose_request.h"
#include "ui/frontend/metadata_edit.h"
#include "ui/frontend/project.h"
#include "ui/frontend/request_stamp.h"
#include "ui/frontend/session_requests.h"
#include "ui/frontend/sound_requests.h"
#include "ui/frontend/timing_links.h"
#include <optional>

namespace wormedit::ui {
/* - asset_selection, sound_properties, sound_usage, sound_import, audio_slicer: the dock's own requests.
   - audio_offset_drag: the waveform offset handle.
   - compose, stamp: resource audition/copy requests the dock raises through Compose.
   - kind: shell request (audio navigation) checked as "busy".
   - metadata, timing_link: only the metadata audio section (music import, audio offset). */
struct audio_ui_window_t {
    asset_selection_request_t& asset_selection;
    audio_offset_drag_request_t& audio_offset_drag;
    std::optional<audio_slicer_action_t>& audio_slicer;
    compose_request_t& compose;
    WormUiWorkspaceActionKind& kind;
    sound_properties_request_t& sound_properties;
    sound_usage_locate_request_t& sound_usage;
    sound_import_request_t& sound_import;
    request_stamp_t& stamp;
    metadata_edit_request_t& metadata;
    timing_link_action_t& timing_link;
};
} // namespace wormedit::ui
