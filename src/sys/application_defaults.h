#pragma once

#include <wormedit/core/document.h>

namespace wormedit::sys {

[[nodiscard]] auto new_session_project_id() -> song_id_t;
[[nodiscard]] auto initial_document(song_id_t project_id) -> song_document_t;

} // namespace wormedit::sys
