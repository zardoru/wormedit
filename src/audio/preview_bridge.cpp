#include <wormedit/audio/decoder.h>
#include <wormedit/audio/preview_bridge.h>

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
    resumed_playable,
    resumed_bgm,
    metronome,
    assist,
    layer_tick,
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
    std::span<const preview_audio_event_t> resumed_playable;
    std::span<const preview_audio_event_t> resumed_bgm;
    std::span<const preview_metronome_event_t> clicks;
    std::size_t playable_index{};
    std::size_t bgm_index{};
    std::size_t resumed_playable_index{};
    std::size_t resumed_bgm_index{};
    std::size_t click_index{};
    bool music_pending{};
    std::uint64_t music_timeline_frame{};
    std::uint64_t music_source_frame{};
    std::uint64_t music_frame_count{};
    std::span<const preview_metronome_event_t> assist_clicks;
    std::size_t assist_index{};
    std::span<const std::uint64_t> layer_ticks;
    std::size_t layer_tick_index{};
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
    if (queues.resumed_playable_index < queues.resumed_playable.size()) {
        const auto& event = queues.resumed_playable[queues.resumed_playable_index];
        consider({.sample_frame = event.sample_frame,
                  .queue = queue_kind_t::resumed_playable,
                  .object = event.object});
    }
    if (queues.resumed_bgm_index < queues.resumed_bgm.size()) {
        const auto& event = queues.resumed_bgm[queues.resumed_bgm_index];
        consider({.sample_frame = event.sample_frame,
                  .queue = queue_kind_t::resumed_bgm,
                  .object = event.object});
    }
    if (queues.click_index < queues.clicks.size()) {
        const auto& event = queues.clicks[queues.click_index];
        consider({.sample_frame = event.sample_frame,
                  .queue = queue_kind_t::metronome,
                  .object = {},
                  .accent = event.accent});
    }
    if (queues.assist_index < queues.assist_clicks.size()) {
        const auto& event = queues.assist_clicks[queues.assist_index];
        consider({.sample_frame = event.sample_frame, .queue = queue_kind_t::assist,
                  .accent = event.accent});
    }
    if (queues.layer_tick_index < queues.layer_ticks.size())
        consider({.sample_frame = queues.layer_ticks[queues.layer_tick_index],
                  .queue = queue_kind_t::layer_tick});
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
                           const std::vector<preview_object_source_binding_t>& resumed_objects,
                           const std::vector<preview_metronome_source_binding_t>& metronome)
    -> candidate_sources_t {
    if (candidate.queue == queue_kind_t::metronome || candidate.queue == queue_kind_t::assist ||
        candidate.queue == queue_kind_t::layer_tick) {
        const auto binding = std::ranges::lower_bound(metronome, candidate.accent, {},
                                                      &preview_metronome_source_binding_t::accent);
        if (binding != metronome.end() && binding->accent == candidate.accent) {
            return {std::span<const preview_voice_source_t>{&binding->voice, 1},
                    candidate.queue == queue_kind_t::layer_tick ? voice_kind_t::layer_assist
                                                               : voice_kind_t::click_preview};
        }
        return {{}, voice_kind_t::click_preview};
    }
    const bool resumed = candidate.queue == queue_kind_t::resumed_playable ||
                         candidate.queue == queue_kind_t::resumed_bgm;
    const auto& source_objects = resumed ? resumed_objects : objects;
    const auto kind = candidate.queue == queue_kind_t::music ? voice_kind_t::music
                      : candidate.queue == queue_kind_t::bgm || candidate.queue == queue_kind_t::resumed_bgm ? voice_kind_t::bgm
                                                            : voice_kind_t::playable_note;
    const auto binding = std::ranges::lower_bound(source_objects, candidate.object, {},
                                                  &preview_object_source_binding_t::object);
    if (binding != source_objects.end() && binding->object == candidate.object) {
        return {binding->voices, kind};
    }
    return {{}, kind};
}

void advance_candidate(const event_candidate_t& candidate, preview_queue_head_t& playable,
                       preview_queue_head_t& bgm, preview_queue_head_t& resumed_playable,
                       preview_queue_head_t& resumed_bgm, preview_queue_head_t& metronome,
                       bool& music_pending, std::size_t& assist_head, std::size_t& layer_tick_head) {
    switch (candidate.queue) {
    case queue_kind_t::playable: ++playable.index; break;
    case queue_kind_t::bgm: ++bgm.index;
        break;
    case queue_kind_t::resumed_playable:
        ++resumed_playable.index;
        break;
    case queue_kind_t::resumed_bgm:
        ++resumed_bgm.index; break;
    case queue_kind_t::metronome: ++metronome.index; break;
    case queue_kind_t::assist: ++assist_head; break;
    case queue_kind_t::layer_tick: ++layer_tick_head; break;
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
                          .gain = source.gain,
                          .mix_group = source.mix_group});
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

auto is_valid_source(const audio_source_identity_t& source) -> bool {
    return !source.asset_identity.empty() && !source.source_identity.empty();
}

auto resumed_binding_for(const preview_audio_event_t& event,
                         const preview_object_source_binding_t& original, std::uint64_t seek_frame)
    -> std::optional<preview_object_source_binding_t> {
    const auto elapsed = seek_frame - event.sample_frame;
    preview_object_source_binding_t resumed{.object = event.object};
    for (const auto& voice : original.voices) {
        const auto duration = voice.frame_count;
        if (elapsed >= duration ||
            voice.source_start_frame > std::numeric_limits<std::uint64_t>::max() - elapsed)
            continue;
        auto active = voice;
        active.source_start_frame += elapsed;
        active.frame_count = duration - elapsed;
        resumed.voices.push_back(std::move(active));
    }
    if (resumed.voices.empty())
        return std::nullopt;
    return resumed;
}

void prepare_resumes(std::span<const preview_audio_event_t> events, std::uint64_t seek_frame,
                     const std::vector<preview_object_source_binding_t>& objects,
                     std::vector<preview_object_source_binding_t>& resumed_objects,
                     std::vector<preview_audio_event_t>& resumed_events) {
    for (const auto& event : events) {
        if (event.sample_frame >= seek_frame)
            break;
        const auto binding = std::ranges::lower_bound(objects, event.object, {},
                                                      &preview_object_source_binding_t::object);
        if (binding == objects.end() || binding->object != event.object)
            continue;
        auto resumed = resumed_binding_for(event, *binding, seek_frame);
        if (!resumed)
            continue;
        resumed_objects.push_back(std::move(*resumed));
        resumed_events.push_back({.sample_frame = seek_frame,
                                  .object = event.object,
                                  .resource = event.resource,
                                  .lane = event.lane,
                                  .source_order = event.source_order,
                                  .secondary_order = event.secondary_order});
    }
}

auto is_valid_voice_source(const preview_voice_source_t& source) -> bool {
    return is_valid_source(source.source) && source.frame_count != 0 &&
           source.frame_count <=
               std::numeric_limits<std::uint64_t>::max() - source.source_start_frame &&
           std::isfinite(source.gain) && source.gain >= 0.0F;
}

auto is_valid_accent(metronome_accent_t accent) -> bool {
    return static_cast<std::uint8_t>(accent) <=
           static_cast<std::uint8_t>(metronome_accent_t::measure);
}

auto is_sorted_events(const auto& events) -> bool {
    return std::ranges::is_sorted(events, {}, &preview_audio_event_t::sample_frame);
}

auto is_sorted_metronome_events(const auto& events) -> bool {
    return std::ranges::is_sorted(events, {}, &preview_metronome_event_t::sample_frame);
}

auto is_valid_assist_grid(const preview_assist_click_grid_t& grid) -> bool {
    return std::isfinite(grid.bpm) && grid.bpm > 0 &&
        static_cast<long double>(grid.sample_rate) * 60 / grid.bpm >= 1 &&
        std::isfinite(grid.phase_source_frame) && grid.sample_rate == output_sample_rate &&
        grid.beats_per_measure > 0 && grid.beats_per_measure <= 32 &&
        grid.playback_rate.numerator != 0 && grid.playback_rate.denominator != 0;
}

auto assist_timeline_offset(const preview_schedule_t& schedule) -> long double {
    if (!schedule.music) return 0;
    return static_cast<long double>(schedule.music->sample_frame) - schedule.music->source_start_frame;
}

auto assist_first_beat(const preview_assist_click_grid_t& grid, long double offset,
                       std::uint64_t minimum) -> std::optional<std::int64_t> {
    const auto period = static_cast<long double>(grid.sample_rate) * 60 / grid.bpm;
    const auto beat = std::ceil((static_cast<long double>(minimum) - offset -
                                grid.phase_source_frame - 0.5L) / period);
    if (!std::isfinite(beat) || beat < std::numeric_limits<std::int64_t>::min() ||
        beat > std::numeric_limits<std::int64_t>::max()) return std::nullopt;
    return static_cast<std::int64_t>(beat);
}

auto assist_beat_frame(const preview_assist_click_grid_t& grid, long double offset,
                       std::int64_t beat) -> std::optional<std::uint64_t> {
    const auto period = static_cast<long double>(grid.sample_rate) * 60 / grid.bpm;
    const auto frame = std::floor(offset + grid.phase_source_frame + beat * period + 0.5L);
    if (!std::isfinite(frame) || frame < 0 ||
        frame > std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    return static_cast<std::uint64_t>(frame);
}

auto fill_assist_clicks(const preview_assist_click_grid_t& grid,
                        const preview_schedule_t& schedule, std::uint64_t minimum,
                        std::uint64_t end,
                        std::array<preview_metronome_event_t, maximum_assist_clicks>& clicks)
    -> std::size_t {
    const auto offset = assist_timeline_offset(schedule);
    const auto first = assist_first_beat(grid, offset, minimum);
    if (!first) return 0;
    std::size_t count{};
    auto beat = *first;
    while (count < clicks.size()) {
        const auto frame = assist_beat_frame(grid, offset, beat);
        if (!frame || *frame >= end) break;
        if (*frame >= minimum) {
            const auto meter = static_cast<std::int64_t>(grid.beats_per_measure);
            const auto pulse = (beat % meter + meter) % meter;
            clicks[count++] = {.sample_frame = *frame, .measure = {},
                .pulse = static_cast<std::uint32_t>(pulse),
                .accent = pulse == 0 ? metronome_accent_t::measure : metronome_accent_t::regular};
        }
        if (beat == std::numeric_limits<std::int64_t>::max()) break;
        ++beat;
    }
    return count;
}

} // namespace

auto preview_transport_bridge_t::start(
    const preview_schedule_t& schedule, std::span<const preview_object_source_binding_t> objects,
    std::span<const preview_metronome_source_binding_t> metronome, std::uint64_t seek_frame)
    -> preview_bridge_start_result_t {
    if (schedule.sample_rate != output_sample_rate) {
        return {.status = preview_bridge_start_status_t::unsupported_sample_rate};
    }
    if (!is_sorted_events(schedule.playable) || !is_sorted_events(schedule.bgm) ||
        !is_sorted_metronome_events(schedule.metronome)) {
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
                                        std::ranges::all_of(binding.voices, is_valid_voice_source);
                             }) ||
        !std::ranges::all_of(metronome, [](const auto& binding) {
            return is_valid_accent(binding.accent) && is_valid_voice_source(binding.voice);
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
    resumed_objects_.clear();
    resumed_playable_.clear();
    resumed_bgm_.clear();
    resumed_playable_head_ = {};
    resumed_bgm_head_ = {};
    prepare_resumes(schedule_->playable, seek_frame, objects_, resumed_objects_, resumed_playable_);
    prepare_resumes(schedule_->bgm, seek_frame, objects_, resumed_objects_, resumed_bgm_);
    std::ranges::sort(resumed_objects_, {}, &preview_object_source_binding_t::object);
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
    layer_tick_head_ = static_cast<std::size_t>(std::ranges::lower_bound(layer_tick_frames_, seek_frame) -
                                               layer_tick_frames_.begin());
    assist_grid_.reset();
    assist_count_ = assist_head_ = 0;
    last_assist_handed_frame_.reset();
    assist_begin_frame_ = seek_frame;
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
    resumed_objects_.clear();
    resumed_playable_.clear();
    resumed_bgm_.clear();
    metronome_.clear();
    playable_head_ = {};
    bgm_head_ = {};
    resumed_playable_head_ = {};
    resumed_bgm_head_ = {};
    metronome_head_ = {};
    music_pending_ = false;
    music_timeline_frame_ = 0;
    music_source_frame_ = 0;
    music_frame_count_ = 0;
    assist_grid_.reset();
    assist_count_ = assist_head_ = 0;
    last_assist_handed_frame_.reset();
    assist_begin_frame_ = 0;
    next_voice_id_ = 1;
    last_materialized_end_ = 0;
    return result;
}

void preview_transport_bridge_t::set_metronome_enabled(bool enabled, std::uint64_t current_frame) {
    transport_.set_clicks_enabled(enabled);
    const auto previous = metronome_enabled_.exchange(enabled, std::memory_order_acq_rel);
    if (!enabled) last_assist_handed_frame_.reset();
    if (enabled && !previous && schedule_) {
        seek_preview_queue(std::span<const preview_metronome_event_t>{schedule_->metronome},
                           metronome_head_, current_frame);
    }
}

auto preview_transport_bridge_t::set_layer_tick_frames(std::span<const std::uint64_t> frames,
                                                        std::uint64_t current_frame) -> bool {
    if (frames.size() > 200'000 || !std::ranges::is_sorted(frames) ||
        std::ranges::adjacent_find(frames) != frames.end() ||
        (!frames.empty() && frames.back() > std::numeric_limits<std::uint64_t>::max() - maximum_read_frames))
        return false;
    if (std::ranges::equal(frames, layer_tick_frames_)) return true;
    std::vector<std::uint64_t> replacement(frames.begin(), frames.end());
    transport_.clear_voices(voice_kind_t::layer_assist);
    layer_tick_frames_ = std::move(replacement);
    layer_tick_head_ = static_cast<std::size_t>(std::ranges::lower_bound(layer_tick_frames_, current_frame) -
                                               layer_tick_frames_.begin());
    return true;
}

auto preview_transport_bridge_t::set_metronome_volume(std::uint8_t volume_percent) noexcept -> bool {
    return transport_.set_click_preview_volume(volume_percent);
}

auto preview_transport_bridge_t::set_assist_grid(bool active,
    std::optional<preview_assist_click_grid_t> grid, std::uint64_t current_frame) -> bool {
    if (grid && !is_valid_assist_grid(*grid)) return false;
    const bool reset_voices = active != assist_active_ || (assist_grid_ && !grid);
    if (reset_voices) {
        transport_.set_clicks_enabled(false);
        transport_.set_clicks_enabled(is_metronome_enabled());
        last_assist_handed_frame_.reset();
        if (!active && schedule_)
            seek_preview_queue(std::span<const preview_metronome_event_t>{schedule_->metronome},
                               metronome_head_, current_frame);
    }
    assist_active_ = active;
    assist_grid_ = active ? grid : std::nullopt;
    assist_begin_frame_ = current_frame;
    assist_count_ = assist_head_ = 0;
    return true;
}

void preview_transport_bridge_t::record_assist_handoff(bool assist, std::uint64_t frame) {
    if (assist) last_assist_handed_frame_ = frame;
}

void preview_transport_bridge_t::refill_assist_clicks(std::uint64_t end, std::uint64_t current_frame) {
    assist_count_ = assist_head_ = 0;
    assist_begin_frame_ = std::max(assist_begin_frame_, current_frame);
    if (!assist_active_ || !assist_grid_ || !is_metronome_enabled()) return;
    if (last_assist_handed_frame_ == std::numeric_limits<std::uint64_t>::max()) return;
    const auto minimum = last_assist_handed_frame_
        ? std::max(assist_begin_frame_, *last_assist_handed_frame_ + 1) : assist_begin_frame_;
    const auto& grid = *assist_grid_;
    const auto horizon = std::max(1.0L, std::floor(static_cast<long double>(grid.sample_rate) *
        grid.playback_rate.numerator / grid.playback_rate.denominator / 5));
    auto bounded_end = std::min(static_cast<long double>(end),
                               static_cast<long double>(assist_begin_frame_) + horizon);
    if (schedule_->music) {
        const auto& music = *schedule_->music;
        bounded_end = std::min(bounded_end, static_cast<long double>(music.sample_frame) +
                                           music.frame_count);
    }
    assist_count_ = fill_assist_clicks(grid, *schedule_, minimum,
                                      static_cast<std::uint64_t>(bounded_end), assist_clicks_);
}

auto preview_transport_bridge_t::materialize_until(std::uint64_t exclusive_end_frame,
                                                        std::uint64_t current_frame)
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

    refill_assist_clicks(exclusive_end_frame, current_frame);
    auto result = preview_bridge_pump_result_t{
        .status = preview_bridge_pump_status_t::ready,
    };
    const auto playable = std::span<const preview_audio_event_t>{schedule_->playable};
    const auto bgm = std::span<const preview_audio_event_t>{schedule_->bgm};
    const auto clicks = std::span<const preview_metronome_event_t>{schedule_->metronome};
    if (!is_metronome_enabled() || assist_active_)
        seek_preview_queue(clicks, metronome_head_, exclusive_end_frame);

    while (true) {
        const auto candidate = next_candidate(
            {playable, bgm, resumed_playable_, resumed_bgm_, clicks, playable_head_.index, bgm_head_.index, resumed_playable_head_.index, resumed_bgm_head_.index,
             metronome_head_.index, music_pending_, music_timeline_frame_, music_source_frame_,
             music_frame_count_, std::span{assist_clicks_.data(), assist_count_}, assist_head_,
             layer_tick_frames_, layer_tick_head_});
        if (!candidate.has_value() || candidate->sample_frame >= exclusive_end_frame) {
            break;
        }

        const auto selected_sources = sources_for_candidate(*candidate, objects_, resumed_objects_, metronome_);
        const auto sources = selected_sources.sources;
        const auto voice_kind = selected_sources.kind;
        if (sources.empty()) {
            ++result.missing_sources;
            advance_candidate(*candidate, playable_head_, bgm_head_, resumed_playable_head_,
                              resumed_bgm_head_, metronome_head_, music_pending_, assist_head_, layer_tick_head_);
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
        record_assist_handoff(candidate->queue == queue_kind_t::assist, candidate->sample_frame);
        if (candidate->queue == queue_kind_t::music) {
            music_timeline_frame_ += music_segment;
            music_source_frame_ += music_segment;
            music_frame_count_ -= music_segment;
            music_pending_ = music_frame_count_ != 0;
        } else {
            advance_candidate(*candidate, playable_head_, bgm_head_, resumed_playable_head_,
                              resumed_bgm_head_, metronome_head_, music_pending_, assist_head_, layer_tick_head_);
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

auto preview_transport_bridge_t::is_exhausted() const noexcept -> bool {
    return schedule_ != nullptr && playable_head_.index == schedule_->playable.size() &&
           bgm_head_.index == schedule_->bgm.size() &&
           resumed_playable_head_.index == resumed_playable_.size() &&
           resumed_bgm_head_.index == resumed_bgm_.size() &&
           metronome_head_.index == schedule_->metronome.size() && !music_pending_ &&
           assist_head_ == assist_count_ && layer_tick_head_ == layer_tick_frames_.size();
}

} // namespace wormedit::audio
