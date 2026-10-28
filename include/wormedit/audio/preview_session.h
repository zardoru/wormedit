#pragma once
#include <wormedit/audio/session_render.h>

#include <wormedit/audio/playhead_snapshot.h>
#include <wormedit/audio/preview_bridge.h>
#include <wormedit/audio/preview_pump.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <span>
#include <type_traits>

namespace wormedit::audio {

struct preview_session_start_detail_t {
    std::optional<preview_bridge_start_outcome_t> bridge{};
    std::optional<preview_bridge_pump_effect_t> materialize{};
    std::optional<preview_pump_effect_t> prime{};
    auto operator==(const preview_session_start_detail_t&) const -> bool = default;
};
struct started_preview_session_t {
    session_token_t token;
    preview_session_start_detail_t detail;
    auto operator==(const started_preview_session_t&) const -> bool = default;
};
struct preview_session_start_error_t {
    failure_code_t code{failure_code_t::transport_failed};
    preview_session_start_detail_t detail{};
    // Synchronous priming can fail after the session has already started.
    std::optional<session_token_t> token{};
    auto operator==(const preview_session_start_error_t&) const -> bool = default;
};
using preview_session_start_outcome_t =
    result_t<started_preview_session_t, preview_session_start_error_t>;

enum class preview_session_pump_phase_t : std::uint8_t { ready = 0, loop_boundary = 2 };

static_assert(std::is_trivially_copyable_v<operation_state_t<preview_session_pump_phase_t>>);

struct preview_session_pump_effect_t {
    operation_state_t<preview_session_pump_phase_t> status{failure_code_t::not_started};
    std::optional<preview_bridge_pump_effect_t> materialize{};
    std::optional<preview_pump_effect_t> decode{};
    [[nodiscard]] auto has_ready_decode() const -> bool {
        return decode.has_value() && !decode->status.has_failure();
    }
    auto operator==(const preview_session_pump_effect_t&) const -> bool = default;
};

struct preview_loop_region_t {
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};
    auto operator==(const preview_loop_region_t&) const -> bool = default;
};

// Owns one chart-play generation and its normalized sample-frame timeline.
// start_session()/seek_session() enqueue initial work without decoding; pump_session() runs on the decode
// worker (or explicitly in headless owners). render_frames() performs only the
// cache-resident transport mix. The immutable schedule must outlive the active
// session because preview_transport_bridge_t retains its queue storage.
class preview_session_t {
  public:
    preview_session_t(decoded_source_registry_t& sources, transport_t& transport,
                      preview_transport_bridge_t& bridge)
        : sources_(sources), transport_(transport), bridge_(bridge) {}

    [[nodiscard]] auto
    start_session(const preview_schedule_t& schedule,
                  std::span<const preview_object_source_binding_t> objects,
                  std::span<const preview_metronome_source_binding_t> metronome = {},
                  std::uint64_t seek_frame = 0, preview_pump_options_t options = {},
                  std::uint64_t minimum_end_frame = 0) -> preview_session_start_outcome_t;
    [[nodiscard]] auto seek_session(std::uint64_t sample_frame, preview_pump_options_t options = {})
        -> preview_session_start_outcome_t;
    [[nodiscard]] auto restart_session_loop(std::uint64_t expected_generation)
        -> preview_session_start_outcome_t;
    [[nodiscard]] auto pump_session(preview_pump_options_t options = {})
        -> preview_session_pump_effect_t;
    // Output producers can require their owned generation; stale work emits
    // silence and leaves a replacement session untouched.
    [[nodiscard]] auto render_frames(std::span<float> interleaved_stereo,
        std::optional<std::uint64_t> expected_generation = {}) -> session_render_effect_t;
    [[nodiscard]] auto stop(std::optional<std::uint64_t> expected_generation = {}) noexcept
        -> transport_stop_result_t;

    // Rendering clamps at end_frame and never seeks. An output refill worker can
    // prepare the next iteration while already queued PCM continues playing.
    // The expected chart-play generation prevents a stale wrap after seek/Stop.
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
    void set_muted_mix_groups(std::span<const std::uint64_t> groups);
    void set_hitsounds_enabled(bool enabled) noexcept;
    [[nodiscard]] auto set_preview_mix(const preview_mix_t& mix) -> bool {
        return transport_.set_preview_mix(mix);
    }
    [[nodiscard]] auto is_hitsounds_enabled() const noexcept -> bool {
        return transport_.is_hitsounds_enabled();
    }
    [[nodiscard]] auto set_layer_tick_frames(std::span<const std::uint64_t> frames) -> bool;
    [[nodiscard]] auto is_metronome_enabled() const -> bool;

    [[nodiscard]] auto set_metronome_volume(std::uint8_t volume_percent) noexcept -> bool;
    [[nodiscard]] auto set_metronome_accent_volume(std::uint8_t volume_percent) noexcept -> bool {
        return bridge_.set_metronome_accent_volume(volume_percent);
    }
    [[nodiscard]] auto metronome_volume() const noexcept -> std::uint8_t {
        return bridge_.metronome_volume();
    }
    [[nodiscard]] auto set_metronome_count_in(std::uint8_t measures) noexcept -> bool;
    [[nodiscard]] auto metronome_count_in() const noexcept -> std::uint8_t {
        return metronome_count_in_measures_.load(std::memory_order_acquire);
    }
    [[nodiscard]] auto set_assist_grid(bool active,
        std::optional<preview_assist_click_grid_t> grid) -> bool;
    // is_active() and timeline_frame() read a lock-free snapshot published by the control
    // and render sides; they never wait on the session mutex.
    [[nodiscard]] auto playhead() const noexcept -> playhead_state_t;
    [[nodiscard]] auto is_active() const -> bool;
    // Audible source-frame estimate; scheduling continues on the unadjusted
    // internal source clock. Headless sessions have zero output latency.
    [[nodiscard]] auto timeline_frame() const -> std::uint64_t;
    void set_output_latency_frames(std::uint64_t frames) noexcept {
        output_latency_frames_.store(frames, std::memory_order_release);
    }

  private:
    [[nodiscard]] auto prime_started(preview_bridge_start_outcome_t started,
                                     std::uint64_t timeline_frame, preview_pump_options_t options, bool loop_restart = false)
        -> preview_session_start_outcome_t;
    [[nodiscard]] auto mix_render_frames(std::uint64_t first_frame,
                                         std::span<float> interleaved_stereo)
        -> transport_mix_result_t;
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
