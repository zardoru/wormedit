#pragma once

#include <wormedit/audio/playhead_snapshot.h>
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
    loop_boundary,
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
    priming,
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

struct preview_loop_region_t {
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};
    auto operator==(const preview_loop_region_t&) const -> bool = default;
};

// Owns one chart-play generation and its normalized sample-frame timeline.
// start()/seek() enqueue initial work without decoding; pump() runs on the decode
// worker (or explicitly in headless owners). render() performs only the
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
    // Output producers can require their owned generation; stale work emits
    // silence and leaves a replacement session untouched.
    [[nodiscard]] auto render(std::span<float> interleaved_stereo,
        std::optional<std::uint64_t> expected_generation = {})
        -> preview_session_render_result_t;
    [[nodiscard]] auto stop(std::optional<std::uint64_t> expected_generation = {}) noexcept -> transport_stop_result_t;

    // Rendering clamps at end_frame and never seeks. An output refill worker can
    // prepare the next iteration while already queued PCM continues playing.
    // The expected chart-play generation prevents a stale wrap after seek/Stop.
    [[nodiscard]] auto restart_loop(std::uint64_t expected_generation)
        -> preview_session_start_result_t;
    // An output owner publishes its audible clock independently of the producer
    // cursor. A replacement generation automatically invalidates this estimate.
    // The output owner serializes these publications.
    void set_output_playhead(std::uint64_t generation, std::uint64_t frame) noexcept;
    [[nodiscard]] auto set_loop_region(std::optional<preview_loop_region_t> region) -> bool;
    [[nodiscard]] auto loop_region() const -> std::optional<preview_loop_region_t>;
    // Whether a loop region is set, without the session mutex (which rendering holds for a
    // whole mix), for per-frame UI queries.
    [[nodiscard]] auto has_loop_region() const noexcept -> bool {
        return loop_region_set_.load(std::memory_order_acquire);
    }

    void set_metronome_enabled(bool enabled);
    [[nodiscard]] auto set_layer_tick_frames(std::span<const std::uint64_t> frames) -> bool;
    [[nodiscard]] auto metronome_enabled() const -> bool;
    [[nodiscard]] auto set_metronome_volume(std::uint8_t volume_percent) noexcept -> bool;
    [[nodiscard]] auto metronome_volume() const noexcept -> std::uint8_t {
        return bridge_.metronome_volume();
    }
    [[nodiscard]] auto set_metronome_count_in(std::uint8_t measures) noexcept -> bool;
    [[nodiscard]] auto metronome_count_in() const noexcept -> std::uint8_t {
        return metronome_count_in_measures_.load(std::memory_order_acquire);
    }
    [[nodiscard]] auto set_assist_grid(bool active,
        std::optional<preview_assist_click_grid_t> grid) -> bool;
    // active() and timeline_frame() read a lock-free snapshot published by the control
    // and render sides; they never wait on the session mutex.
    [[nodiscard]] auto playhead() const noexcept -> playhead_state_t;
    [[nodiscard]] auto active() const -> bool;
    // Audible source-frame estimate; scheduling continues on the unadjusted
    // internal source clock. Headless sessions have zero output latency.
    [[nodiscard]] auto timeline_frame() const -> std::uint64_t;
    void set_output_latency_frames(std::uint64_t frames) noexcept {
        output_latency_frames_.store(frames, std::memory_order_release);
    }

  private:
    [[nodiscard]] auto prime_started(preview_bridge_start_result_t started,
                                     std::uint64_t timeline_frame, preview_pump_options_t options, bool loop_restart = false)
        -> preview_session_start_result_t;
    void clear_state() noexcept;
    void publish_playhead() noexcept;

    decoded_source_registry_t& sources_;
    transport_t& transport_;
    preview_transport_bridge_t& bridge_;
    playhead_snapshot_t playhead_;
    playhead_snapshot_t output_playhead_;
    std::uint64_t generation_{};
    std::atomic<std::uint64_t> loop_restart_generation_{};
    mutable std::mutex mutex_;
    std::optional<session_token_t> token_;
    bool prime_pending_{};
    preview_pump_options_t prime_options_{};
    std::uint64_t timeline_frame_{};
    std::uint64_t start_frame_{};
    std::uint64_t minimum_end_frame_{};
    std::optional<preview_loop_region_t> loop_region_;
    // Mirrors loop_region_.has_value(); written under the mutex, read lock-free.
    std::atomic<bool> loop_region_set_{};
    std::atomic<std::uint64_t> output_latency_frames_{};
    std::atomic<std::uint8_t> metronome_count_in_measures_{};
};

} // namespace wormedit::audio
