#pragma once
#include <wormedit/core/job_progress.h>
#include <wormedit/core/result.h>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string>

namespace wormedit::audio {
struct ogg_encode_options_t {
    float quality{0.4F};
    std::size_t maximum_output_bytes{16U * 1024U * 1024U};
    job_progress_t* progress{};
};
// Stereo interleaved 44.1 kHz PCM. Serial number and metadata are deterministic.
[[nodiscard]] auto encode_ogg_vorbis(std::span<const float> pcm, ogg_encode_options_t options = {},
                                    std::stop_token stop = {})
    -> result_t<std::string, error_t<failure_code_t>>;
[[nodiscard]] auto verify_ogg_vorbis(const std::filesystem::path& path, std::uint64_t expected_frames,
                                    std::stop_token stop = {}) -> result_t<void, error_t<failure_code_t>>;
}
