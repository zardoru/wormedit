#pragma once

#include <wormedit/core/document.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit::sys {

[[nodiscard]] auto new_session_project_id() -> song_id_t;
[[nodiscard]] auto empty_session_document(song_id_t session_id) -> song_document_t;
[[nodiscard]] auto initial_document(song_id_t project_id) -> song_document_t;
[[nodiscard]] auto initial_document(song_id_t project_id, std::uint32_t lane_count)
    -> std::optional<song_document_t>;

[[nodiscard]] auto initial_document(song_id_t project_id, std::string_view layout_id)
    -> std::optional<song_document_t>;

// The source path may be absolute while the project has not been saved. The
// chosen lane count is the number of playable columns, with no scratch lane.
[[nodiscard]] auto audio_project_document(song_id_t project_id,
                                          std::filesystem::path audio_source_path,
                                          std::string title, std::string artist,
                                          std::uint32_t lane_count,
                                          audio_stream_info_t stream = {})
    -> std::optional<song_document_t>;

// Stable catalog identity preserves scratch/stage/PMS variants as well as key count.
[[nodiscard]] auto audio_project_document(song_id_t project_id,
                                          std::filesystem::path audio_source_path,
                                          std::string title, std::string artist,
                                          std::string_view layout_id,
                                          audio_stream_info_t stream = {})
    -> std::optional<song_document_t>;

} // namespace wormedit::sys
