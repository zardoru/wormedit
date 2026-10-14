#pragma once

#include <wormedit/audio/audition_bridge.h>
#include <wormedit/audio/source_registry.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace wormedit::audio {

inline constexpr std::size_t maximum_audition_pump_blocks = 128;
inline constexpr std::size_t maximum_audition_pump_clips = 64;

struct audition_pump_options_t {
    std::size_t lookahead_frames{maximum_read_frames};
    std::size_t maximum_blocks{maximum_audition_pump_blocks};
};

enum class audition_pump_status_t {
    ready,
    complete,
    stale_generation,
    invalid_request,
    capacity_exceeded,
    source_unavailable,
    decode_failed,
};

struct audition_pump_effect_t {
    audition_pump_status_t status{audition_pump_status_t::invalid_request};
    std::size_t requested_blocks{};
    std::size_t decoded_blocks{};
    std::size_t cached_blocks{};
    std::optional<block_decode_result_t> failure{};
    auto operator==(const audition_pump_effect_t&) const -> bool = default;
};
// Both alternatives retain counters and nested decoder details; expected owns
// the success rule while status distinguishes ready from complete.
using audition_pump_outcome_t = result_t<audition_pump_effect_t, audition_pump_effect_t>;

struct audition_pump_result_t {
    [[nodiscard]] static auto from(audition_pump_outcome_t&& outcome) -> audition_pump_result_t;

    audition_pump_status_t status{audition_pump_status_t::invalid_request};
    std::size_t requested_blocks{};
    std::size_t decoded_blocks{};
    std::size_t cached_blocks{};
    std::optional<block_decode_result_t> failure;
    auto operator==(const audition_pump_result_t&) const -> bool = default;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == audition_pump_status_t::ready ||
               status == audition_pump_status_t::complete;
    }
};

[[nodiscard]] auto prime_resident_audition_blocks(transport_t& transport, session_token_t token,
                                                  std::span<const audition_clip_t> clips)
    -> std::optional<audition_pump_effect_t>;

[[nodiscard]] auto
pump_audition_blocks(decoded_source_registry_t& sources, transport_t& transport,
                     session_token_t token, std::span<const audition_clip_t> clips,
                     std::uint64_t timeline_frame, audition_pump_options_t options = {})
    -> audition_pump_outcome_t;

// Checks the same bounded initial lookahead against immutable resident PCM only.
// Missing PCM returns nullopt; this function never opens or invokes a decoder.
[[nodiscard]] auto prime_resident_audition(transport_t& transport, session_token_t token,
                                           std::span<const audition_clip_t> clips)
    -> std::optional<audition_pump_result_t>;

// Control/worker-thread lookahead for an already scheduled one-shot. It plans
// every distinct cache key before decoding, so an over-cap request has no
// partial cache side effect. The transport generation gates every publication.
[[nodiscard]] auto
pump_audition_lookahead(decoded_source_registry_t& sources, transport_t& transport,
                        session_token_t token, std::span<const audition_clip_t> clips,
                        std::uint64_t timeline_frame, audition_pump_options_t options = {})
    -> audition_pump_result_t;

} // namespace wormedit::audio
