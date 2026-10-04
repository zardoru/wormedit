#pragma once

#include <wormedit/core/tap_tempo.h>
#include <wormedit/core/tap_timing_apply.h>
#include <wormedit/audio/preview_bridge.h>

#include <cstdint>
#include <optional>

namespace wormedit::sys {

inline constexpr std::uint64_t maximum_tap_event_age_ns = 2'000'000'000;
// The application supplies the audible music position on its normalized source
// clock. This snapshot is not a historical device-position record: event-time
// interpolation is valid only within one unchanged epoch and playback rate.
struct tap_timing_clock_t {
    std::uint64_t chart{};
    std::uint64_t revision{};
    std::uint64_t asset{};
    std::uint64_t epoch{};
    std::uint64_t epoch_begin_ns{};
    std::uint64_t observed_ns{};
    std::optional<std::uint64_t> source_frame;
    std::uint32_t sample_rate{44'100};
    std::uint16_t beats_per_measure{4};
    rational_t playback_rate{1, 1};
    bool playing{};
    bool keep_pitch{};
};

// Checked rational interpolation, rounded to the nearest source frame. Events
// before the epoch, after the snapshot, older than two seconds, or before source
// frame zero have no usable position.
[[nodiscard]] auto tap_source_frame_at_event(const tap_timing_clock_t& clock,
                                              std::uint64_t event_ns)
    -> std::optional<std::uint64_t>;

enum class tap_timing_result_t { accepted, ignored, stale, not_playing, unavailable, closed };
struct tap_timing_state_t {
    bool visible{};
    bool playing{};
    bool source_available{};
    std::uint16_t beats_per_measure{4};
    std::uint16_t active_segment{};
    bool downbeat_flash{};
    std::uint64_t tap_count{};
    std::uint64_t tap_capacity{16};
    std::optional<tap_tempo_estimate_t> estimate;
    const char* status{};
};

struct tap_timing_assist_publication_t {
    bool active{};
    std::uint64_t epoch{};
    std::optional<audio::preview_assist_click_grid_t> grid;
    auto operator==(const tap_timing_assist_publication_t&) const -> bool = default;
};

class tap_timing_runtime_t {
  public:
    void set_visible(bool visible) { visible_ = visible; }
    void toggle() { visible_ = !visible_; }
    [[nodiscard]] auto visible() const -> bool { return visible_; }
    // Returns false for a snapshot older than the previously observed clock.
    auto observe(const tap_timing_clock_t& clock) -> bool;
    void reset();
    auto tap(std::uint64_t event_ns, const tap_timing_clock_t& clock) -> tap_timing_result_t;
    [[nodiscard]] auto state() const -> tap_timing_state_t;
    [[nodiscard]] auto assist_publication() const -> tap_timing_assist_publication_t;
    void set_bpm_choice(tap_bpm_choice_t choice);
    [[nodiscard]] auto bpm_choice() const -> tap_bpm_choice_t { return choice_; }
    void prepare_apply(const song_document_t& document, bool timing_locked);
    [[nodiscard]] auto apply_proposal() const
        -> const std::optional<tap_timing_apply_proposal_t>& { return proposal_; }
    auto apply(document_history_t& history, const tap_timing_clock_t& clock,
               std::uint64_t expected_revision, bool timing_locked)
        -> document_command_result_t;

  private:
    void bind(const tap_timing_clock_t& clock, bool changed);
    auto observe_clock(const tap_timing_clock_t& clock) -> bool;
    bool visible_{};
    std::optional<tap_timing_clock_t> clock_;
    tap_tempo_estimator_t estimator_;
    std::uint64_t valid_begin_ns_{};
    const char* status_{};
    tap_bpm_choice_t choice_{tap_bpm_choice_t::raw};
    std::optional<tap_timing_apply_proposal_t> proposal_;
    std::optional<tap_timing_clock_t> proposal_clock_;
    std::optional<tap_tempo_estimate_t> proposal_estimate_;
};

} // namespace wormedit::sys
