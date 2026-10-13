#pragma once
#include <wormedit/audio/session_render.h>

#include <wormedit/audio/audition_pump.h>
#include <wormedit/audio/playhead_snapshot.h>

#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace wormedit::audio {

enum class audition_session_start_status_t {
    started,
    transport_failed,
    prime_failed,
};

struct audition_session_start_result_t {
    audition_session_start_status_t status{audition_session_start_status_t::transport_failed};
    std::optional<session_token_t> token;
    std::optional<audition_start_result_t> transport;
    std::optional<audition_pump_result_t> prime;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == audition_session_start_status_t::started && token.has_value();
    }
};

enum class audition_session_render_status_t {
    mixed,
    priming,
    complete,
    stopped,
    invalid_buffer,
    stale_generation,
    underrun,
    transport_failed,
};

using audition_session_render_result_t = session_render_result_t<audition_session_render_status_t>;

// Owns one active click-preview timeline above the transport bridge. start()
// checks resident PCM before publishing; streamed priming remains pending until
// pump() performs decoder work off the caller/realtime path. render() only
// mixes cache-resident blocks and advances a monotonic output-frame cursor.
class audition_session_t {
  public:
    audition_session_t(decoded_source_registry_t& sources, transport_t& transport,
                       audition_transport_bridge_t& bridge)
        : sources_(sources), transport_(transport), bridge_(bridge) {}

    [[nodiscard]] auto start(std::span<const audition_clip_t> clips)
        -> audition_session_start_result_t;
    [[nodiscard]] auto pump(audition_pump_options_t options = {}) -> audition_pump_result_t;
    [[nodiscard]] auto render(std::span<float> interleaved_stereo)
        -> audition_session_render_result_t;
    [[nodiscard]] auto stop() noexcept -> transport_stop_result_t;

    // Lock-free reads of a snapshot published by start/render/stop.
    [[nodiscard]] auto playhead() const noexcept -> playhead_state_t;
    [[nodiscard]] auto is_active() const -> bool;
    // Compatibility for the coordinated sys/UI migration.
    inline auto active() const -> bool { return is_active(); }
    [[nodiscard]] auto timeline_frame() const -> std::uint64_t;

  private:
    friend struct audition_session_test_access_t;
    struct pump_state_t {
        std::optional<session_token_t> token;
        std::vector<audition_clip_t> clips;
        std::uint64_t timeline_frame{};
    };
    [[nodiscard]] auto snapshot_pump_state() const -> pump_state_t;
    [[nodiscard]] auto complete_pump(session_token_t token, audition_pump_result_t result)
        -> audition_pump_result_t;
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
