#pragma once

#include <wormedit/audio/transport.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace wormedit::audio {

// One exact decoded source range participating in a one-shot audition. Layers
// start together at audition frame zero and retain independent source ranges.
struct audition_clip_t {
    audio_source_identity_t source;
    std::uint64_t source_start_frame{};
    std::uint64_t frame_count{};
    float gain{1.0F};
};

enum class audition_start_status_t : std::uint8_t {
    started,
    empty,
    too_many_layers,
    invalid_clip,
    transport_start_failed,
    transport_capacity_exceeded,
    transport_rejected_clip,
};

struct audition_start_result_t {
    audition_start_status_t status{audition_start_status_t::transport_start_failed};
    std::optional<session_token_t> token;
    std::size_t scheduled_layers{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == audition_start_status_t::started && token.has_value();
    }
};

// Control-thread adapter for resource and chart-object click previews. Input is
// validated before replacing a current audition. Once replacement begins, any
// scheduling failure stops the new generation so a partial layer set cannot
// remain audible.
class audition_transport_bridge_t {
  public:
    explicit audition_transport_bridge_t(transport_t& transport,
                                         std::size_t maximum_layers = 64)
        : transport_(transport), maximum_layers_(maximum_layers) {}

    [[nodiscard]] auto start(std::span<const audition_clip_t> clips)
        -> audition_start_result_t;
    [[nodiscard]] auto stop() noexcept -> transport_stop_result_t;

    [[nodiscard]] auto session_token() const noexcept -> std::optional<session_token_t> {
        return token_;
    }

  private:
    transport_t& transport_;
    std::size_t maximum_layers_{};
    std::optional<session_token_t> token_;
};

} // namespace wormedit::audio
