#pragma once

#include <wormedit/core/document.h>

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {

struct audio_project_import_result_t {
    std::filesystem::path source_path;
    std::string title;
    std::string artist;
    audio_stream_info_t stream;
    std::optional<double> duration_seconds;
    std::string error;
    bool valid{};

    [[nodiscard]] explicit operator bool() const noexcept { return valid; }
};

// Inspects an MP3 or Ogg Vorbis selected as the starting music for a new
// project. With FFmpeg, embedded TITLE/ARTIST tags take precedence over the
// filename. Without FFmpeg, inspection succeeds with empty title/artist fields.
[[nodiscard]] auto inspect_audio_for_project(const std::filesystem::path& source_path)
    -> audio_project_import_result_t;

} // namespace wormedit::sys
