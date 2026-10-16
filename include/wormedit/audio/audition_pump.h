#pragma once

#include <wormedit/audio/audition_bridge.h>
#include <wormedit/audio/source_registry.h>
#include <wormedit/core/operation_state.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace wormedit::audio {

inline constexpr std::size_t maximum_audition_pump_blocks = 128;
inline constexpr std::size_t maximum_audition_pump_clips = 64;

struct audition_pump_options_t {
    std::size_t lookahead_frames{maximum_read_frames};
    std::size_t maximum_blocks{maximum_audition_pump_blocks};
};

enum class audition_pump_phase_t : std::uint8_t { ready, complete };

static_assert(sizeof(operation_state_t<audition_pump_phase_t>) <= sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<operation_state_t<audition_pump_phase_t>>);

struct audition_pump_effect_t {
    operation_state_t<audition_pump_phase_t> status{failure_code_t::invalid_request};
    std::size_t requested_blocks{};
    std::size_t decoded_blocks{};
    std::size_t cached_blocks{};
    std::optional<block_decode_result_t> failure{};
    auto operator==(const audition_pump_effect_t&) const -> bool = default;
};
// Both alternatives retain counters and nested decoder details; expected owns
// the success rule while status distinguishes ready from complete.
using audition_pump_outcome_t = result_t<audition_pump_effect_t, audition_pump_effect_t>;

// Checks the same bounded initial lookahead against immutable resident PCM only.
// Missing PCM returns nullopt; this function never opens or invokes a decoder.
[[nodiscard]] auto prime_resident_audition_blocks(transport_t& transport, session_token_t token,
                                                  std::span<const audition_clip_t> clips)
    -> std::optional<audition_pump_effect_t>;

// Control/worker-thread lookahead for an already scheduled one-shot. It plans
// every distinct cache key before decoding, so an over-cap request has no
// partial cache side effect. The transport generation gates every publication.
[[nodiscard]] auto
pump_audition_blocks(decoded_source_registry_t& sources, transport_t& transport,
                     session_token_t token, std::span<const audition_clip_t> clips,
                     std::uint64_t timeline_frame, audition_pump_options_t options = {})
    -> audition_pump_outcome_t;

} // namespace wormedit::audio
