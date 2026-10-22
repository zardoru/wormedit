#pragma once
#include "sys/audio/project_audio_sources.h"

// Fixtures that install decoded bindings directly have already reconciled their
// authored inventory. Keep those immutable test identities when querying sys views.
inline auto retained_sources_for(const wormedit::song_document_t& document,
    wormedit::sys::project_audio_sources_t& sources) -> wormedit::sys::project_audio_sources_t& {
    sources.source_project = document.id;
    sources.reconciled_revision = document.revision;
    return sources;
}
