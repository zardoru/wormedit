#pragma once

#include <wormedit/audio/preview_bridge.h>
#include <wormedit/audio/preview_pump.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <span>

namespace wormedit::audio {

enum class preview_session_start_status_t {
    started,
    bridge_failed,
    materialize_failed,
    prime_failed,
};

struct preview_session_start_result_t {
    preview_session_start_status_t status{preview_session_start_status_t::bridge_failed};
    std::optional<session_token_t> token;
    std::optional<preview_bridge_start_result_t> bridge;
    std::optional<preview_bridge_pump_result_t> materialize;
    std::optional<preview_pump_result_t> prime;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == preview_session_start_status_t::started && token.has_value();
    }
};

enum class preview_session_pump_status_t {
    ready,
    not_started,
    invalid_timeline,
    materialize_failed,
    decode_failed,
};

struct preview_session_pump_result_t {
    preview_session_pump_status_t status{preview_session_pump_status_t::not_started};
    std::optional<preview_bridge_pump_result_t> materialize;
    std::optional<preview_pump_result_t> decode;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == preview_session_pump_status_t::ready;
    }
};

enum class preview_session_render_status_t {
    mixed,
    complete,
    stopped,
    invalid_buffer,
    stale_generation,
    underrun,
    transport_failed,
};

struct preview_session_render_result_t {
    preview_session_render_status_t status{preview_session_render_status_t::stopped};
    std::uint64_t first_timeline_frame{};
    transport_mix_result_t mix;
};

// Owns one chart-play generation and its monotonic normalized timeline.
// start()/seek()/pump() run on the control thread; render() performs only the
// cache-resident transport mix. The immutable schedule must outlive the active
// session because preview_transport_bridge_t retains its queue storage.
class preview_session_t {
  public:
    preview_session_t(decoded_source_registry_t& sources, transport_t& transport,
                      preview_transport_bridge_t& bridge)
        : sources_(sources), transport_(transport), bridge_(bridge) {}

    [[nodiscard]] auto start(const preview_schedule_t& schedule,
                             std::span<const preview_object_source_binding_t> objects,
                             std::span<const preview_metronome_source_binding_t> metronome = {},
                             std::uint64_t seek_frame = 0, preview_pump_options_t options = {},
                             std::uint64_t minimum_end_frame = 0)
        -> preview_session_start_result_t;
    [[nodiscard]] auto seek(std::uint64_t sample_frame, preview_pump_options_t options = {})
        -> preview_session_start_result_t;
    [[nodiscard]] auto pump(preview_pump_options_t options = {}) -> preview_session_pump_result_t;
    [[nodiscard]] auto render(std::span<float> interleaved_stereo)
        -> preview_session_render_result_t;
    [[nodiscard]] auto stop() noexcept -> transport_stop_result_t;

    void set_metronome_enabled(bool enabled);
    [[nodiscard]] auto metronome_enabled() const -> bool;
    [[nodiscard]] auto set_assist_grid(bool active,
        std::optional<preview_assist_click_grid_t> grid) -> bool;
    [[nodiscard]] auto active() const -> bool;
    // Audible source-frame estimate; scheduling continues on the unadjusted
    // internal source clock. Headless sessions have zero output latency.
    [[nodiscard]] auto timeline_frame() const -> std::uint64_t;
    void set_output_latency_frames(std::uint64_t frames) noexcept {
        output_latency_frames_.store(frames, std::memory_order_release);
    }

  private:
    [[nodiscard]] auto prime_started(preview_bridge_start_result_t started,
                                     std::uint64_t timeline_frame, preview_pump_options_t options)
        -> preview_session_start_result_t;
    void clear_state() noexcept;

    decoded_source_registry_t& sources_;
    transport_t& transport_;
    preview_transport_bridge_t& bridge_;
    mutable std::mutex mutex_;
    std::optional<session_token_t> token_;
    std::uint64_t timeline_frame_{};
    std::uint64_t start_frame_{};
    std::uint64_t minimum_end_frame_{};
    std::atomic<std::uint64_t> output_latency_frames_{};
};

} // namespace wormedit::audio
