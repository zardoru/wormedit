#include "sys/tap_timing_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>

namespace wormedit::sys {
namespace {
auto clock_configuration_valid(const tap_timing_clock_t& clock) -> bool {
    return clock.sample_rate != 0 && clock.beats_per_measure != 0 &&
        clock.beats_per_measure <= maximum_tap_beats_per_measure &&
        clock.playback_rate.numerator != 0 && clock.playback_rate.denominator != 0;
}
auto clock_binding(const tap_timing_clock_t& clock) {
    return std::tuple{clock.chart, clock.revision, clock.asset, clock.epoch,
        clock.sample_rate, clock.beats_per_measure, clock.playback_rate, clock.keep_pitch};
}
template <std::size_t Count>
auto checked_product(const std::array<std::uint64_t, Count>& factors)
    -> std::optional<std::uint64_t> {
    std::uint64_t result{1};
    for (const auto factor : factors) {
        if (factor != 0 && result > std::numeric_limits<std::uint64_t>::max() / factor)
            return std::nullopt;
        result *= factor;
    }
    return result;
}
auto elapsed_source_frames(std::uint64_t elapsed_ns, const tap_timing_clock_t& clock)
    -> std::optional<std::uint64_t> {
    std::array<std::uint64_t, 3> numerator{elapsed_ns, clock.sample_rate, clock.playback_rate.numerator};
    std::array<std::uint64_t, 2> denominator{1'000'000'000, clock.playback_rate.denominator};
    for (auto& top : numerator) {
        for (auto& bottom : denominator) {
            const auto common = std::gcd(top, bottom);
            top /= common;
            bottom /= common;
        }
    }
    const auto top = checked_product(numerator);
    const auto bottom = checked_product(denominator);
    if (!top || !bottom || *bottom == 0) return std::nullopt;
    const auto whole = *top / *bottom;
    const auto remainder = *top % *bottom;
    const auto round_up = remainder >= *bottom - remainder;
    if (round_up && whole == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    return whole + static_cast<std::uint64_t>(round_up);
}
void populate_beat_animation(tap_timing_state_t& state, const tap_timing_clock_t& clock) {
    if (!state.estimate || !clock.playing || !clock.source_frame) return;
    const auto& estimate = *state.estimate;
    const auto period = static_cast<long double>(clock.sample_rate) * 60.0L / estimate.bpm;
    const auto beat = (static_cast<long double>(*clock.source_frame) - estimate.phase_frame) / period;
    const auto whole = std::floor(beat);
    auto segment = std::fmod(whole, state.beats_per_measure);
    if (segment < 0) segment += state.beats_per_measure;
    state.active_segment = static_cast<std::uint16_t>(segment);
    state.downbeat_flash = state.active_segment == 0 && beat - whole < 0.18L;
}
} // namespace

auto tap_source_frame_at_event(const tap_timing_clock_t& clock, std::uint64_t event_ns)
    -> std::optional<std::uint64_t> {
    if (!clock.playing || !clock.source_frame || !clock_configuration_valid(clock) ||
        event_ns < clock.epoch_begin_ns || event_ns > clock.observed_ns ||
        clock.observed_ns - event_ns > maximum_tap_event_age_ns)
        return std::nullopt;
    const auto elapsed = elapsed_source_frames(clock.observed_ns - event_ns, clock);
    if (!elapsed || *elapsed > *clock.source_frame) return std::nullopt;
    return *clock.source_frame - *elapsed;
}
void tap_timing_runtime_t::bind(const tap_timing_clock_t& clock, bool changed) {
    proposal_.reset();
    proposal_clock_.reset();
    estimator_ = tap_tempo_estimator_t{{clock.sample_rate, clock.beats_per_measure}};
    valid_begin_ns_ = changed ? std::max(clock.epoch_begin_ns, clock.observed_ns) : clock.epoch_begin_ns;
    status_ = nullptr;
    last_tap_ns_.reset();
}
auto tap_timing_runtime_t::observe_clock(const tap_timing_clock_t& clock) -> bool {
    if (clock_ && clock.observed_ns < clock_->observed_ns) return false;
    const auto changed = clock_ && (clock_binding(*clock_) != clock_binding(clock) ||
        (clock.source_frame && clock_->source_frame && *clock.source_frame < *clock_->source_frame));
    if (!clock_ || changed) bind(clock, changed);
    if (!clock.playing || !clock.source_frame || !clock_configuration_valid(clock)) {
        estimator_.reset();
        last_tap_ns_.reset();
        valid_begin_ns_ = clock.observed_ns;
    }
    clock_ = clock;
    return true;
}
auto tap_timing_runtime_t::observe(const tap_timing_clock_t& clock) -> bool {
    if (!observe_clock(clock)) return false;
    if (clock.playing && clock.source_frame) (void)estimator_.expire(*clock.source_frame);
    return true;
}
void tap_timing_runtime_t::reset() {
    proposal_.reset();
    proposal_clock_.reset();
    estimator_.reset();
    valid_begin_ns_ = clock_ ? clock_->observed_ns : 0;
    status_ = nullptr;
    last_tap_ns_.reset();
}
auto tap_timing_runtime_t::tap(std::uint64_t event_ns, const tap_timing_clock_t& clock)
    -> tap_timing_result_t {
    if (!visible_) return tap_timing_result_t::closed;
    if (!observe_clock(clock)) return tap_timing_result_t::stale;
    if (!clock.playing) {
        status_ = "Play the song, then tap on the beat";
        return tap_timing_result_t::not_playing;
    }
    if (event_ns < valid_begin_ns_) {
        status_ = "Tap discarded because the transport clock changed.";
        return tap_timing_result_t::stale;
    }
    const auto frame = tap_source_frame_at_event(clock, event_ns);
    if (!frame) {
        status_ = "A current music position is unavailable for this tap.";
        return tap_timing_result_t::unavailable;
    }
    const auto result = estimator_.tap(*frame);
    if (result == tap_tempo_result_t::ignored || result == tap_tempo_result_t::invalid_configuration) {
        status_ = "Tap ignored; keep tapping once per beat.";
        return tap_timing_result_t::ignored;
    }
    status_ = nullptr;
    last_tap_ns_ = event_ns;
    tap_segment_ = estimator_.tap_count() == 1 ? 0 :
        static_cast<std::uint16_t>((tap_segment_ + 1) % clock.beats_per_measure);
    return tap_timing_result_t::accepted;
}
auto tap_timing_runtime_t::state() const -> tap_timing_state_t {
    tap_timing_state_t result;
    result.visible = visible_;
    result.estimate = estimator_.estimate();
    result.tap_count = estimator_.tap_count();
    result.status = status_ ? std::string_view{status_} : std::string_view{};
    result.timing_locked = timing_locked_;
    result.tap_segment = tap_segment_;
    if (!clock_) return result;
    result.playing = clock_->playing;
    result.tap_flash = last_tap_ns_ && clock_->playing &&
        clock_->observed_ns >= *last_tap_ns_ && clock_->observed_ns - *last_tap_ns_ < 120'000'000;
    result.source_available = clock_->source_frame.has_value() && clock_configuration_valid(*clock_);
    result.beats_per_measure = clock_configuration_valid(*clock_) ? clock_->beats_per_measure : 4;
    result.tap_capacity = 4 * result.beats_per_measure;
    populate_beat_animation(result, *clock_);
    return result;
}

void tap_timing_runtime_t::set_bpm_choice(tap_bpm_choice_t choice) {
    if (choice_ == choice) return;
    choice_ = choice;
    proposal_.reset();
    proposal_clock_.reset();
}

void tap_timing_runtime_t::prepare_apply(const song_document_t& document, bool timing_locked) {
    timing_locked_ = timing_locked;
    const auto estimate = estimator_.estimate();
    if (!visible_ || !clock_ || !clock_->playing || !clock_->source_frame || !estimate ||
        clock_->revision != document.revision || timing_locked) {
        proposal_.reset();
        proposal_clock_.reset();
        return;
    }
    if (proposal_ && proposal_clock_ && proposal_estimate_ &&
        clock_binding(*proposal_clock_) == clock_binding(*clock_) &&
        std::tie(proposal_estimate_->bpm, proposal_estimate_->phase_frame, proposal_estimate_->stable) ==
        std::tie(estimate->bpm, estimate->phase_frame, estimate->stable)) return;
    proposal_.reset();
    proposal_clock_.reset();
    auto proposed = tap_timing_apply_proposal_t::propose(document, {clock_->chart}, *estimate,
        clock_->sample_rate, clock_->beats_per_measure, choice_, timing_locked);
    if (!proposed.ok() || !proposed.value || !make_tap_timing_preview(document, *proposed.value)) return;
    proposal_ = std::move(proposed.value);
    proposal_clock_ = clock_;
    proposal_estimate_ = estimate;
}

auto tap_timing_runtime_t::apply(document_history_t& history, const tap_timing_clock_t& clock,
    std::uint64_t expected_revision, bool timing_locked) -> document_command_result_t {
    const auto captured = proposal_;
    const auto bound = proposal_clock_;
    const auto matching = bound && clock_binding(*bound) == clock_binding(clock);
    const auto observed = observe(clock);
    const auto estimate = estimator_.estimate();
    if (!observed || !visible_ || !captured || !matching || !clock.playing ||
        !clock.source_frame || !estimate || !estimate->stable) {
        proposal_.reset();
        status_ = "Tap result expired; play and tap again before Apply.";
        return {.revision = history.document.revision};
    }
    if (!captured->stamp.document.is_revision_current(expected_revision)) {
        proposal_.reset();
        // Preserve the status pointer's static lifetime and avoid allocation on repeated rejection.
        static const auto stale_result_status = stale_action_message("Tap timing result");
        status_ = stale_result_status.c_str();
        return {.revision = history.document.revision};
    }
    auto result = wormedit::apply_proposal(history, *captured, timing_locked);
    if (result) reset();
    else status_ = "Apply rejected; check timing lock and tap again.";
    return result;
}

auto tap_timing_runtime_t::assist_publication() const -> tap_timing_assist_publication_t {
    tap_timing_assist_publication_t result{.active = visible_};
    if (!clock_) return result;
    result.epoch = clock_->epoch;
    const auto estimate = estimator_.estimate();
    if (!visible_ || !clock_->playing || !clock_->source_frame ||
        !clock_configuration_valid(*clock_) || !estimate) return result;
    result.grid = audio::preview_assist_click_grid_t{
        .bpm = estimate->bpm,
        .phase_source_frame = estimate->phase_frame,
        .sample_rate = clock_->sample_rate,
        .beats_per_measure = clock_->beats_per_measure,
        .playback_rate = clock_->playback_rate};
    return result;
}

} // namespace wormedit::sys
