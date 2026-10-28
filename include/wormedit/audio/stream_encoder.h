#pragma once
#include <wormedit/core/export_audio.h>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>

namespace wormedit::audio {
// Encodes bounded chunks of stereo PCM without retaining the complete song.
// The caller owns exclusive staging and removes incomplete output on failure.
class stream_encoder_t {
  public:
    stream_encoder_t();
    ~stream_encoder_t();
    [[nodiscard]] auto open(const std::filesystem::path&, export_audio_settings_t,
        std::uint32_t sample_rate) -> bool;
    [[nodiscard]] auto write(std::span<const float>, std::stop_token) -> bool;
    [[nodiscard]] auto finish() -> bool;
    [[nodiscard]] static auto source_bitrate(const std::filesystem::path&) -> std::uint32_t;
    [[nodiscard]] static auto can_encode(export_audio_codec_t) -> bool;
  private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;
};
}
