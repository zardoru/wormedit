#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {

struct audio_project_import_result_t {
    bool succeeded{};
    [[nodiscard]] auto is_success() const noexcept -> bool { return succeeded; }

    std::filesystem::path source_path;
    std::string title;
    std::string artist;
    audio_stream_info_t stream;
    std::optional<double> duration_seconds;
    std::string error;
};

// Inspects an MP3 or Ogg Vorbis selected as the starting music for a new
// project. With FFmpeg, embedded TITLE/ARTIST tags take precedence over the
// filename. Without FFmpeg, inspection succeeds with empty title/artist fields.
[[nodiscard]] auto inspect_audio_for_project(const std::filesystem::path& source_path)
    -> audio_project_import_result_t;

} // namespace wormedit::sys
