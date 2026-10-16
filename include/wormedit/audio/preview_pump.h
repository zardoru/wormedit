#pragma once

#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/transport.h>
#include <wormedit/core/operation_state.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

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


static_assert(sizeof(operation_state_t<std::monostate>) <= sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<operation_state_t<std::monostate>>);

struct preview_pump_effect_t {
    operation_state_t<std::monostate> status{failure_code_t::invalid_request};
    std::size_t active_voices{};
    std::size_t overlapping_voices{};
    std::size_t requested_blocks{};
    std::size_t decoded_blocks{};
    std::size_t cached_blocks{};
    std::optional<block_decode_result_t> failure{};
    auto operator==(const preview_pump_effect_t&) const -> bool = default;
};
using preview_pump_outcome_t = result_t<preview_pump_effect_t, preview_pump_effect_t>;

[[nodiscard]] auto pump_preview_blocks(decoded_source_registry_t& sources, transport_t& transport,
                                       session_token_t token, std::uint64_t timeline_frame,
                                       preview_pump_options_t options = {})
    -> preview_pump_outcome_t;

// Control/worker-thread lookahead for voices already materialized into a chart
// preview transport generation. The active voice snapshot and every distinct
// cache key are bounded before decoding begins, so capacity rejection has no
// partial cache side effect.
} // namespace wormedit::audio
