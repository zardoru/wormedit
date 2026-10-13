#pragma once

#include <wormedit/audio/transport.h>
#include <wormedit/core/timing.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace wormedit::audio {

struct preview_voice_source_t {
    audio_source_identity_t source;
    std::uint64_t source_start_frame{};
    std::uint64_t frame_count{};
    float gain{1.0F};
    std::uint64_t mix_group{};
};

struct preview_object_source_binding_t {
    object_id_t object;
    // One chart event may expand to several exact BMSON slice/layer voices.
    // They are scheduled atomically at the event's timeline frame.
    std::vector<preview_voice_source_t> voices;
};

struct preview_metronome_source_binding_t {
    metronome_accent_t accent{metronome_accent_t::regular};
    preview_voice_source_t voice;
};

inline constexpr std::size_t maximum_assist_clicks = 32;
// Estimate on the normalized music source clock; the bridge maps it onto its timeline.
struct preview_assist_click_grid_t {
    double bpm{};
    double phase_source_frame{};
    std::uint32_t sample_rate{44'100};
    std::uint16_t beats_per_measure{4};
    rational_t playback_rate{1, 1};
    auto operator==(const preview_assist_click_grid_t&) const -> bool = default;
};

enum class preview_bridge_start_status_t : std::uint8_t {
    started,
    not_started,
    unsupported_sample_rate,
    invalid_schedule,
    invalid_source_binding,
    duplicate_source_binding,
    source_binding_capacity_exceeded,
    transport_start_failed,
};

struct preview_bridge_start_result_t {
    preview_bridge_start_status_t status{preview_bridge_start_status_t::transport_start_failed};
    std::optional<session_token_t> token;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == preview_bridge_start_status_t::started && token.has_value();
    }
};

enum class preview_bridge_pump_status_t : std::uint8_t {
    ready,
    not_started,
    stale_generation,
    capacity_limited,
    non_monotonic_window,
    voice_id_exhausted,
    transport_rejected_voice,
};

struct preview_bridge_pump_result_t {
    preview_bridge_pump_status_t status{preview_bridge_pump_status_t::not_started};
    std::size_t scheduled_events{};
    std::size_t missing_sources{};
};

// Control-thread bridge from immutable core preview queues to the bounded
// transport. The schedule must outlive the active bridge session. Call
// materialize_until() away from the audio callback and refill after callbacks
// retire voices; a capacity-limited result leaves the first unqueued event at
// its queue head for the next refill.
class preview_transport_bridge_t {
  public:
    explicit preview_transport_bridge_t(transport_t& transport,
                                        std::size_t maximum_object_bindings = 200'000,
                                        std::size_t maximum_total_voices = 200'000,
                                        std::size_t maximum_layers_per_object = 64)
        : transport_(transport), maximum_object_bindings_(maximum_object_bindings),
          maximum_total_voices_(maximum_total_voices),
          maximum_layers_per_object_(maximum_layers_per_object) {}

    [[nodiscard]] auto start(const preview_schedule_t& schedule,
                             std::span<const preview_object_source_binding_t> objects,
                             std::span<const preview_metronome_source_binding_t> metronome = {},
                             std::uint64_t seek_frame = 0) -> preview_bridge_start_result_t;
    [[nodiscard]] auto seek(std::uint64_t sample_frame) -> preview_bridge_start_result_t;
    [[nodiscard]] auto restart() -> preview_bridge_start_result_t {
        return seek(0);
    }
    [[nodiscard]] auto stop() noexcept -> transport_stop_result_t;
    // Control-thread setter: re-enable from the current scheduling cursor, never missed beats.
    void set_metronome_enabled(bool enabled, std::uint64_t current_frame);
    // Sorted unique source frames; independent of metronome/grid assist. Control thread only.
    [[nodiscard]] auto set_layer_tick_frames(std::span<const std::uint64_t> frames,
                                             std::uint64_t current_frame) -> bool;
    [[nodiscard]] auto set_metronome_volume(std::uint8_t volume_percent) noexcept -> bool;
    [[nodiscard]] auto metronome_volume() const noexcept -> std::uint8_t {
        return transport_.click_preview_volume();
    }
    [[nodiscard]] auto is_metronome_enabled() const noexcept -> bool {
        return metronome_enabled_.load(std::memory_order_acquire);
    }


    // Replaces predictions still held by the bridge. Already handed voices survive
    // estimate updates; new predictions begin after the last handed frame.
    [[nodiscard]] auto set_assist_grid(bool active,
        std::optional<preview_assist_click_grid_t> grid, std::uint64_t current_frame) -> bool;

    // Queues events with sample_frame < exclusive_end_frame. This method may
    // allocate while preparing voices, so it belongs on a control thread.
    [[nodiscard]] auto materialize_until(std::uint64_t exclusive_end_frame,
                                         std::uint64_t current_frame = 0)
        -> preview_bridge_pump_result_t;

    [[nodiscard]] auto session_token() const noexcept -> std::optional<session_token_t> {
        return token_;
    }
    [[nodiscard]] auto is_exhausted() const noexcept -> bool;
    // Compatibility for the coordinated sys/UI migration.
    inline auto exhausted() const noexcept -> bool { return is_exhausted(); }

  private:
    [[nodiscard]] auto begin_session(std::uint64_t seek_frame) -> preview_bridge_start_result_t;

    void record_assist_handoff(bool assist, std::uint64_t frame);
    void refill_assist_clicks(std::uint64_t exclusive_end_frame, std::uint64_t current_frame);
    transport_t& transport_;
    std::size_t maximum_object_bindings_{};
    std::size_t maximum_total_voices_{};
    std::size_t maximum_layers_per_object_{};
    const preview_schedule_t* schedule_{};
    std::vector<preview_object_source_binding_t> objects_;
    std::vector<preview_object_source_binding_t> resumed_objects_;
    std::vector<preview_audio_event_t> resumed_playable_;
    std::vector<preview_audio_event_t> resumed_bgm_;
    std::vector<preview_metronome_source_binding_t> metronome_;
    preview_queue_head_t playable_head_;
    preview_queue_head_t bgm_head_;
    preview_queue_head_t resumed_playable_head_;
    preview_queue_head_t resumed_bgm_head_;
    preview_queue_head_t metronome_head_;
    bool music_pending_{};
    std::atomic<bool> metronome_enabled_{true};
    std::vector<std::uint64_t> layer_tick_frames_;
    std::size_t layer_tick_head_{};
    bool assist_active_{};
    std::optional<preview_assist_click_grid_t> assist_grid_;
    std::array<preview_metronome_event_t, maximum_assist_clicks> assist_clicks_{};
    std::size_t assist_count_{};
    std::size_t assist_head_{};
    std::uint64_t assist_begin_frame_{};
    std::optional<std::uint64_t> last_assist_handed_frame_;
    std::uint64_t music_timeline_frame_{};
    std::uint64_t music_source_frame_{};
    std::uint64_t music_frame_count_{};
    std::optional<session_token_t> token_;
    std::uint64_t next_voice_id_{1};
    std::uint64_t last_materialized_end_{};
};

} // namespace wormedit::audio
