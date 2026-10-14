#pragma once

#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/transport.h>
#include <wormedit/core/result.h>

#include <cstddef>
#include <cstdint>

namespace wormedit::audio {

inline constexpr std::size_t maximum_preview_pump_voices = 256;
inline constexpr std::size_t maximum_preview_pump_blocks = 512;

// Cache limits that hold two complete pump plans (the window being mixed and
// the next one). These are ceilings, not reservations.
[[nodiscard]] constexpr auto preview_block_cache_limits() noexcept -> block_cache_limits_t {
    constexpr auto maximum_frames = 2U * maximum_preview_pump_blocks * maximum_read_frames;
    constexpr auto maximum_bytes = maximum_frames * output_channel_count * sizeof(float);
    return {.maximum_bytes = maximum_bytes,
            .maximum_frames = maximum_frames,
            .maximum_bytes_per_asset = maximum_bytes,
            .maximum_frames_per_asset = maximum_frames};
}

struct preview_pump_options_t {
    std::size_t lookahead_frames{maximum_read_frames};
    std::size_t maximum_voices{maximum_preview_pump_voices};
    std::size_t maximum_blocks{maximum_preview_pump_blocks};
};

enum class preview_pump_status_t {
    ready,
    stale_generation,
    invalid_request,
    capacity_exceeded,
    source_unavailable,
    decode_failed,
};

struct preview_pump_effect_t {
    preview_pump_status_t status{preview_pump_status_t::invalid_request};
    std::size_t active_voices{};
    std::size_t overlapping_voices{};
    std::size_t requested_blocks{};
    std::size_t decoded_blocks{};
    std::size_t cached_blocks{};
    std::optional<block_decode_result_t> failure{};
    auto operator==(const preview_pump_effect_t&) const -> bool = default;
};
using preview_pump_outcome_t = result_t<preview_pump_effect_t, preview_pump_effect_t>;

struct preview_pump_result_t {
    [[nodiscard]] static auto from(preview_pump_outcome_t&& outcome) -> preview_pump_result_t;

    preview_pump_status_t status{preview_pump_status_t::invalid_request};
    std::size_t active_voices{};
    std::size_t overlapping_voices{};
    std::size_t requested_blocks{};
    std::size_t decoded_blocks{};
    std::size_t cached_blocks{};
    std::optional<block_decode_result_t> failure;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == preview_pump_status_t::ready;
    }
};

[[nodiscard]] auto pump_preview_blocks(decoded_source_registry_t& sources, transport_t& transport,
                                       session_token_t token, std::uint64_t timeline_frame,
                                       preview_pump_options_t options = {})
    -> preview_pump_outcome_t;

// Control/worker-thread lookahead for voices already materialized into a chart
// preview transport generation. The active voice snapshot and every distinct
// cache key are bounded before decoding begins, so capacity rejection has no
// partial cache side effect.
[[nodiscard]] auto pump_preview_lookahead(decoded_source_registry_t& sources,
                                          transport_t& transport, session_token_t token,
                                          std::uint64_t timeline_frame,
                                          preview_pump_options_t options = {})
    -> preview_pump_result_t;

} // namespace wormedit::audio
