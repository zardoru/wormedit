#pragma once

#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/transport.h>

#include <cstddef>
#include <cstdint>

namespace wormedit::audio {

inline constexpr std::size_t maximum_preview_pump_voices = 256;
inline constexpr std::size_t maximum_preview_pump_blocks = 512;

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

struct preview_pump_result_t {
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
