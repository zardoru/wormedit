#include <wormedit/audio/preview_bridge.h>
#include <wormedit/audio/decoder.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

namespace wormedit::audio {
namespace {

enum class queue_kind_t : std::uint8_t {
    playable,
    bgm,
    metronome,
    music,
};

struct event_candidate_t {
    std::uint64_t sample_frame{};
    queue_kind_t queue{queue_kind_t::playable};
    object_id_t object;
    metronome_accent_t accent{metronome_accent_t::regular};
    std::uint64_t source_start_frame{};
    std::uint64_t frame_count{};
};

struct preview_queue_cursor_t {
    std::span<const preview_audio_event_t> playable;
    std::span<const preview_audio_event_t> bgm;
    std::span<const preview_metronome_event_t> clicks;
    std::size_t playable_index{};
    std::size_t bgm_index{};
    std::size_t click_index{};
    bool music_pending{};
    std::uint64_t music_timeline_frame{};
    std::uint64_t music_source_frame{};
    std::uint64_t music_frame_count{};
};

auto next_candidate(const preview_queue_cursor_t& queues) -> std::optional<event_candidate_t> {
    std::optional<event_candidate_t> candidate;
    const auto consider = [&candidate](event_candidate_t next) {
        if (!candidate.has_value() || next.sample_frame < candidate->sample_frame) {
            candidate = next;
        }
    };
    if (queues.playable_index < queues.playable.size()) {
        const auto& event = queues.playable[queues.playable_index];
        consider({.sample_frame = event.sample_frame,
                  .queue = queue_kind_t::playable,
                  .object = event.object});
    }
    if (queues.bgm_index < queues.bgm.size()) {
        const auto& event = queues.bgm[queues.bgm_index];
        consider({.sample_frame = event.sample_frame,
                  .queue = queue_kind_t::bgm,
                  .object = event.object});
    }
    if (queues.click_index < queues.clicks.size()) {
        const auto& event = queues.clicks[queues.click_index];
        consider({.sample_frame = event.sample_frame,
                  .queue = queue_kind_t::metronome,
                  .object = {},
                  .accent = event.accent});
    }
    if (queues.music_pending)
        consider({.sample_frame = queues.music_timeline_frame,
                  .queue = queue_kind_t::music,
                  .object = object_id_t{std::numeric_limits<std::uint64_t>::max()},
                  .source_start_frame = queues.music_source_frame,
                  .frame_count = queues.music_frame_count});
    return candidate;
}

struct candidate_sources_t {
    std::span<const preview_voice_source_t> sources;
    voice_kind_t kind{voice_kind_t::playable_note};
};

auto sources_for_candidate(const event_candidate_t& candidate,
                           const std::vector<preview_object_source_binding_t>& objects,
                           const std::vector<preview_metronome_source_binding_t>& metronome)
    -> candidate_sources_t {
    if (candidate.queue == queue_kind_t::metronome) {
        const auto binding = std::ranges::lower_bound(metronome, candidate.accent, {},
                                                      &preview_metronome_source_binding_t::accent);
        if (binding != metronome.end() && binding->accent == candidate.accent) {
            return {std::span<const preview_voice_source_t>{&binding->voice, 1},
                    voice_kind_t::click_preview};
        }
        return {{}, voice_kind_t::click_preview};
    }
    const auto kind = candidate.queue == queue_kind_t::music ? voice_kind_t::music
                      : candidate.queue == queue_kind_t::bgm ? voice_kind_t::bgm
                                                            : voice_kind_t::playable_note;
    const auto binding = std::ranges::lower_bound(objects, candidate.object, {},
                                                  &preview_object_source_binding_t::object);
    if (binding != objects.end() && binding->object == candidate.object) {
        return {binding->voices, kind};
    }
    return {{}, kind};
}

void advance_candidate(const event_candidate_t& candidate, preview_queue_head_t& playable,
                       preview_queue_head_t& bgm, preview_queue_head_t& metronome,
                       bool& music_pending) {
    switch (candidate.queue) {
    case queue_kind_t::playable: ++playable.index; break;
    case queue_kind_t::bgm: ++bgm.index; break;
    case queue_kind_t::metronome: ++metronome.index; break;
    case queue_kind_t::music: music_pending = false; break;
    }
}

auto candidate_voices(const event_candidate_t& candidate,
                      std::span<const preview_voice_source_t> sources, voice_kind_t kind,
                      std::uint64_t first_voice_id) -> std::vector<transport_voice_t> {
    std::vector<transport_voice_t> voices;
    voices.reserve(sources.size());
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto& source = sources[index];
        const bool music = candidate.queue == queue_kind_t::music;
        voices.push_back({.id = first_voice_id + index,
                          .kind = kind,
                          .source = source.source,
                          .timeline_start_frame = candidate.sample_frame,
                          .source_start_frame = music ? candidate.source_start_frame
                                                      : source.source_start_frame,
                          .frame_count = music ? candidate.frame_count : source.frame_count,
                          .gain = source.gain});
    }
    return voices;
}

auto pump_error_for_schedule(voice_schedule_status_t status)
    -> std::optional<preview_bridge_pump_status_t> {
    switch (status) {
    case voice_schedule_status_t::scheduled:
        return std::nullopt;
    case voice_schedule_status_t::capacity_exceeded:
        return preview_bridge_pump_status_t::capacity_limited;
    case voice_schedule_status_t::stale_generation:
        return preview_bridge_pump_status_t::stale_generation;
    default:
        return preview_bridge_pump_status_t::transport_rejected_voice;
    }
}

auto valid_source(const audio_source_identity_t& source) -> bool {
    return !source.asset_identity.empty() && !source.source_identity.empty();
}

auto valid_voice_source(const preview_voice_source_t& source) -> bool {
    return valid_source(source.source) && source.frame_count != 0 &&
           source.frame_count <=
               std::numeric_limits<std::uint64_t>::max() - source.source_start_frame &&
           std::isfinite(source.gain) && source.gain >= 0.0F;
}

auto valid_accent(metronome_accent_t accent) -> bool {
    return static_cast<std::uint8_t>(accent) <=
           static_cast<std::uint8_t>(metronome_accent_t::measure);
}

auto sorted_events(const auto& events) -> bool {
    return std::ranges::is_sorted(events, {}, &preview_audio_event_t::sample_frame);
}

auto sorted_metronome_events(const auto& events) -> bool {
    return std::ranges::is_sorted(events, {}, &preview_metronome_event_t::sample_frame);
}

} // namespace

auto preview_transport_bridge_t::start(
    const preview_schedule_t& schedule, std::span<const preview_object_source_binding_t> objects,
    std::span<const preview_metronome_source_binding_t> metronome, std::uint64_t seek_frame)
    -> preview_bridge_start_result_t {
    if (schedule.sample_rate != output_sample_rate) {
        return {.status = preview_bridge_start_status_t::unsupported_sample_rate};
    }
    if (!sorted_events(schedule.playable) || !sorted_events(schedule.bgm) ||
        !sorted_metronome_events(schedule.metronome)) {
        return {.status = preview_bridge_start_status_t::invalid_schedule};
    }
    std::size_t total_voices{};
    for (const auto& binding : objects) {
        if (binding.voices.size() > maximum_layers_per_object_ ||
            binding.voices.size() > maximum_total_voices_ - total_voices) {
            return {.status = preview_bridge_start_status_t::source_binding_capacity_exceeded};
        }
        total_voices += binding.voices.size();
    }
    if (objects.size() > maximum_object_bindings_ || total_voices > maximum_total_voices_) {
        return {.status = preview_bridge_start_status_t::source_binding_capacity_exceeded};
    }
    if (!std::ranges::all_of(objects,
                             [](const auto& binding) {
                                 return static_cast<bool>(binding.object) &&
                                        !binding.voices.empty() &&
                                        std::ranges::all_of(binding.voices, valid_voice_source);
                             }) ||
        !std::ranges::all_of(metronome, [](const auto& binding) {
            return valid_accent(binding.accent) && valid_voice_source(binding.voice);
        })) {
        return {.status = preview_bridge_start_status_t::invalid_source_binding};
    }

    std::vector<preview_object_source_binding_t> sorted_objects{objects.begin(), objects.end()};
    std::ranges::sort(sorted_objects, {}, &preview_object_source_binding_t::object);
    if (std::ranges::adjacent_find(sorted_objects, {}, &preview_object_source_binding_t::object) !=
        sorted_objects.end()) {
        return {.status = preview_bridge_start_status_t::duplicate_source_binding};
    }

    std::vector<preview_metronome_source_binding_t> sorted_metronome{metronome.begin(),
                                                                     metronome.end()};
    std::ranges::sort(sorted_metronome, [](const auto& left, const auto& right) {
        return left.accent < right.accent;
    });
    if (std::ranges::adjacent_find(sorted_metronome, [](const auto& left, const auto& right) {
            return left.accent == right.accent;
        }) != sorted_metronome.end()) {
        return {.status = preview_bridge_start_status_t::duplicate_source_binding};
    }

    schedule_ = &schedule;
    objects_ = std::move(sorted_objects);
    metronome_ = std::move(sorted_metronome);
    return begin_session(seek_frame);
}

auto preview_transport_bridge_t::seek(std::uint64_t sample_frame) -> preview_bridge_start_result_t {
    if (schedule_ == nullptr) {
        return {.status = preview_bridge_start_status_t::not_started};
    }
    return begin_session(sample_frame);
}

auto preview_transport_bridge_t::begin_session(std::uint64_t seek_frame)
    -> preview_bridge_start_result_t {
    const auto token = transport_.start();
    if (!token.has_value()) {
        token_.reset();
        return {.status = preview_bridge_start_status_t::transport_start_failed};
    }
    token_ = token;
    seek_preview_queue(std::span<const preview_audio_event_t>{schedule_->playable}, playable_head_,
                       seek_frame);
    seek_preview_queue(std::span<const preview_audio_event_t>{schedule_->bgm}, bgm_head_,
                       seek_frame);
    seek_preview_queue(std::span<const preview_metronome_event_t>{schedule_->metronome},
                       metronome_head_, seek_frame);
    music_pending_ = false;
    if (schedule_->music) {
        const auto& music = *schedule_->music;
        if (music.frame_count <= std::numeric_limits<std::uint64_t>::max() - music.sample_frame) {
            const auto music_end = music.sample_frame + music.frame_count;
            const auto begin = std::max(music.sample_frame, seek_frame);
            if (music_end > begin) {
                const auto skipped = begin - music.sample_frame;
                music_pending_ = true;
                music_timeline_frame_ = begin;
                music_source_frame_ = music.source_start_frame + skipped;
                music_frame_count_ = music.frame_count - skipped;
            }
        }
    }
    next_voice_id_ = 1;
    last_materialized_end_ = seek_frame;
    return {.status = preview_bridge_start_status_t::started, .token = token};
}

auto preview_transport_bridge_t::stop() noexcept -> transport_stop_result_t {
    auto result = transport_stop_result_t{};
    if (token_.has_value() && transport_.is_current(*token_)) {
        result = transport_.stop();
    }
    token_.reset();
    schedule_ = nullptr;
    objects_.clear();
    metronome_.clear();
    playable_head_ = {};
    bgm_head_ = {};
    metronome_head_ = {};
    music_pending_ = false;
    music_timeline_frame_ = 0;
    music_source_frame_ = 0;
    music_frame_count_ = 0;
    next_voice_id_ = 1;
    last_materialized_end_ = 0;
    return result;
}

auto preview_transport_bridge_t::materialize_until(std::uint64_t exclusive_end_frame)
    -> preview_bridge_pump_result_t {
    if (schedule_ == nullptr || !token_.has_value()) {
        return {.status = preview_bridge_pump_status_t::not_started};
    }
    if (!transport_.is_current(*token_)) {
        return {.status = preview_bridge_pump_status_t::stale_generation};
    }
    if (exclusive_end_frame < last_materialized_end_) {
        return {.status = preview_bridge_pump_status_t::non_monotonic_window};
    }

    auto result = preview_bridge_pump_result_t{
        .status = preview_bridge_pump_status_t::ready,
    };
    const auto playable = std::span<const preview_audio_event_t>{schedule_->playable};
    const auto bgm = std::span<const preview_audio_event_t>{schedule_->bgm};
    const auto clicks = std::span<const preview_metronome_event_t>{schedule_->metronome};

    while (true) {
        const auto candidate = next_candidate(
            {playable, bgm, clicks, playable_head_.index, bgm_head_.index,
             metronome_head_.index, music_pending_, music_timeline_frame_, music_source_frame_,
             music_frame_count_});
        if (!candidate.has_value() || candidate->sample_frame >= exclusive_end_frame) {
            break;
        }

        const auto selected_sources = sources_for_candidate(*candidate, objects_, metronome_);
        const auto sources = selected_sources.sources;
        const auto voice_kind = selected_sources.kind;
        if (sources.empty()) {
            ++result.missing_sources;
            advance_candidate(*candidate, playable_head_, bgm_head_, metronome_head_, music_pending_);
            continue;
        }
        if (next_voice_id_ == 0 ||
            sources.size() - 1 > std::numeric_limits<std::uint64_t>::max() - next_voice_id_) {
            result.status = preview_bridge_pump_status_t::voice_id_exhausted;
            break;
        }

        const auto music_segment = candidate->queue == queue_kind_t::music
                                       ? std::min<std::uint64_t>(
                                             candidate->frame_count,
                                             maximum_read_frames -
                                                 candidate->sample_frame % maximum_read_frames)
                                       : candidate->frame_count;
        auto segment = *candidate;
        segment.frame_count = music_segment;
        auto voices = candidate_voices(segment, sources, voice_kind, next_voice_id_);
        const auto status = transport_.schedule_voices(*token_, voices);
        if (const auto error = pump_error_for_schedule(status)) {
            result.status = *error;
            break;
        }

        ++result.scheduled_events;
        if (candidate->queue == queue_kind_t::music) {
            music_timeline_frame_ += music_segment;
            music_source_frame_ += music_segment;
            music_frame_count_ -= music_segment;
            music_pending_ = music_frame_count_ != 0;
        } else {
            advance_candidate(*candidate, playable_head_, bgm_head_, metronome_head_, music_pending_);
        }
        const auto last_voice_id = next_voice_id_ + sources.size() - 1;
        if (last_voice_id == std::numeric_limits<std::uint64_t>::max()) {
            next_voice_id_ = 0;
        } else {
            next_voice_id_ = last_voice_id + 1;
        }
    }
    last_materialized_end_ = exclusive_end_frame;
    return result;
}

auto preview_transport_bridge_t::exhausted() const noexcept -> bool {
    return schedule_ != nullptr && playable_head_.index == schedule_->playable.size() &&
           bgm_head_.index == schedule_->bgm.size() &&
           metronome_head_.index == schedule_->metronome.size() && !music_pending_;
}

} // namespace wormedit::audio
