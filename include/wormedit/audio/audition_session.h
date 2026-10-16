#pragma once
#include <wormedit/audio/session_render.h>

#include <wormedit/audio/audition_pump.h>
#include <wormedit/audio/playhead_snapshot.h>
#include <wormedit/core/result.h>

#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace wormedit::audio {

struct started_audition_session_t {
    session_token_t token;
    audition_start_outcome_t transport{
        std::unexpected(failure_code_t::transport_failed)};
    std::optional<audition_pump_outcome_t> prime;
    auto operator==(const started_audition_session_t&) const -> bool = default;
};
struct audition_session_start_error_t {
    failure_code_t code{failure_code_t::transport_failed};
    audition_start_outcome_t transport{
        std::unexpected(failure_code_t::transport_failed)};
    auto operator==(const audition_session_start_error_t&) const -> bool = default;
};
using audition_session_start_outcome_t =
    result_t<started_audition_session_t, audition_session_start_error_t>;

// Owns one active click-preview timeline above the transport bridge. start_session()
// checks resident PCM before publishing; streamed priming remains pending until
// pump() performs decoder work off the caller/realtime path. render_frames() only
// mixes cache-resident blocks and advances a monotonic output-frame cursor.
class audition_session_t {
  public:
    audition_session_t(decoded_source_registry_t& sources, transport_t& transport,
                       audition_transport_bridge_t& bridge)
        : sources_(sources), transport_(transport), bridge_(bridge) {}

    [[nodiscard]] auto start_session(std::span<const audition_clip_t> clips)
        -> audition_session_start_outcome_t;
    [[nodiscard]] auto pump(audition_pump_options_t options = {}) -> audition_pump_outcome_t;
    [[nodiscard]] auto render_frames(std::span<float> interleaved_stereo)
        -> render_outcome_t;
    [[nodiscard]] auto stop() noexcept -> transport_stop_result_t;

    // Lock-free reads of a snapshot published by session operations.
    [[nodiscard]] auto playhead() const noexcept -> playhead_state_t;
    [[nodiscard]] auto is_active() const -> bool;
    [[nodiscard]] auto timeline_frame() const -> std::uint64_t;

  private:
    friend struct audition_session_test_access_t;
    struct pump_state_t {
        std::optional<session_token_t> token;
        std::vector<audition_clip_t> clips;
        std::uint64_t timeline_frame{};
    };
    [[nodiscard]] auto snapshot_pump_state() const -> pump_state_t;
    [[nodiscard]] auto complete_pump(session_token_t token, audition_pump_outcome_t result)
        -> audition_pump_outcome_t;
    void clear_state() noexcept;
    void publish_playhead() noexcept;

    decoded_source_registry_t& sources_;
    transport_t& transport_;
    audition_transport_bridge_t& bridge_;
    playhead_snapshot_t playhead_;
    std::uint64_t generation_{};
    mutable std::mutex mutex_;
    bool prime_pending_{};
    std::optional<session_token_t> token_;
    std::vector<audition_clip_t> clips_;
    std::uint64_t timeline_frame_{};
    std::uint64_t duration_frames_{};
};

} // namespace wormedit::audio
