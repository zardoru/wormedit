#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace wormedit::audio {

inline constexpr std::uint32_t output_sample_rate = 44'100;
inline constexpr std::uint32_t output_channel_count = 2;
inline constexpr std::size_t maximum_read_frames = 16'384;

struct stream_info_t {
    std::uint32_t source_sample_rate{};
    std::uint32_t source_channel_count{};
    std::uint64_t source_frame_count{};
    std::uint32_t output_sample_rate{wormedit::audio::output_sample_rate};
    std::uint32_t output_channel_count{wormedit::audio::output_channel_count};
    std::uint64_t output_frame_count{};
    bool seekable{};
};

enum class read_status_t {
    success,
    end_of_stream,
    not_open,
    seek_error,
    request_too_large,
    insufficient_output_space,
    decode_error,
};

struct read_result_t {
    read_status_t status{read_status_t::not_open};
    std::size_t frames_read{};
};

class decoder_t {
  public:
    decoder_t();
    ~decoder_t();

    decoder_t(const decoder_t&) = delete;
    auto operator=(const decoder_t&) -> decoder_t& = delete;
    decoder_t(decoder_t&&) noexcept;
    auto operator=(decoder_t&&) noexcept -> decoder_t&;

    [[nodiscard]] auto open(const std::filesystem::path& path, std::string* error_message = nullptr)
        -> bool;
    // Owns a normalized 44.1 kHz stereo PCM copy for generated internal
    // sources such as metronome clicks.
    [[nodiscard]] auto open_pcm(std::span<const float> interleaved_stereo,
                                std::string* error_message = nullptr) -> bool;
    void close() noexcept;

    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto info() const noexcept -> std::optional<stream_info_t>;
    [[nodiscard]] auto seek(std::uint64_t output_frame) noexcept -> bool;
    [[nodiscard]] auto read(std::span<float> interleaved_stereo,
                            std::size_t requested_frames) noexcept -> read_result_t;
    // Seeks and reads under one decoder lock so independent background jobs
    // cannot interleave positions on a shared decoder instance.
    [[nodiscard]] auto read_at(std::uint64_t output_frame, std::span<float> interleaved_stereo,
                               std::size_t requested_frames) noexcept -> read_result_t;

  private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;
};

} // namespace wormedit::audio
