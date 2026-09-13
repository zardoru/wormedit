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
    metronome,
};

struct event_candidate_t {
    std::uint64_t sample_frame{};
    queue_kind_t queue{queue_kind_t::playable};
    resource_id_t resource;
    metronome_accent_t accent{metronome_accent_t::regular};
};

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
    const preview_schedule_t& schedule,
    std::span<const preview_resource_source_binding_t> resources,
    std::span<const preview_metronome_source_binding_t> metronome, std::uint64_t seek_frame)
    -> preview_bridge_start_result_t {
    if (schedule.sample_rate != output_sample_rate) {
        return {.status = preview_bridge_start_status_t::unsupported_sample_rate};
    }
    if (!sorted_events(schedule.playable) || !sorted_events(schedule.bgm) ||
        !sorted_metronome_events(schedule.metronome)) {
        return {.status = preview_bridge_start_status_t::invalid_schedule};
    }
    if (!std::ranges::all_of(resources,
                             [](const auto& binding) {
                                 return static_cast<bool>(binding.resource) &&
                                        valid_voice_source(binding.voice);
                             }) ||
        !std::ranges::all_of(metronome, [](const auto& binding) {
            return valid_accent(binding.accent) && valid_voice_source(binding.voice);
        })) {
        return {.status = preview_bridge_start_status_t::invalid_source_binding};
    }

    std::vector<preview_resource_source_binding_t> sorted_resources{resources.begin(),
                                                                    resources.end()};
    std::ranges::sort(sorted_resources, {}, &preview_resource_source_binding_t::resource);
    if (std::ranges::adjacent_find(sorted_resources, {},
                                   &preview_resource_source_binding_t::resource) !=
        sorted_resources.end()) {
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
    resources_ = std::move(sorted_resources);
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
    resources_.clear();
    metronome_.clear();
    playable_head_ = {};
    bgm_head_ = {};
    metronome_head_ = {};
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
        std::optional<event_candidate_t> candidate;
        const auto consider = [&candidate](event_candidate_t next) {
            if (!candidate.has_value() || next.sample_frame < candidate->sample_frame) {
                candidate = next;
            }
        };
        if (playable_head_.index < playable.size()) {
            const auto& event = playable[playable_head_.index];
            consider({.sample_frame = event.sample_frame,
                      .queue = queue_kind_t::playable,
                      .resource = event.resource});
        }
        if (bgm_head_.index < bgm.size()) {
            const auto& event = bgm[bgm_head_.index];
            consider({.sample_frame = event.sample_frame,
                      .queue = queue_kind_t::bgm,
                      .resource = event.resource});
        }
        if (metronome_head_.index < clicks.size()) {
            const auto& event = clicks[metronome_head_.index];
            consider({.sample_frame = event.sample_frame,
                      .queue = queue_kind_t::metronome,
                      .accent = event.accent});
        }
        if (!candidate.has_value() || candidate->sample_frame >= exclusive_end_frame) {
            break;
        }

        const auto advance_candidate = [&] {
            switch (candidate->queue) {
            case queue_kind_t::playable:
                ++playable_head_.index;
                break;
            case queue_kind_t::bgm:
                ++bgm_head_.index;
                break;
            case queue_kind_t::metronome:
                ++metronome_head_.index;
                break;
            }
        };

        const preview_voice_source_t* source = nullptr;
        auto voice_kind = voice_kind_t::playable_note;
        if (candidate->queue == queue_kind_t::metronome) {
            voice_kind = voice_kind_t::click_preview;
            const auto binding = std::ranges::lower_bound(
                metronome_, candidate->accent, {}, &preview_metronome_source_binding_t::accent);
            if (binding != metronome_.end() && binding->accent == candidate->accent) {
                source = &binding->voice;
            }
        } else {
            voice_kind = candidate->queue == queue_kind_t::bgm ? voice_kind_t::bgm
                                                               : voice_kind_t::playable_note;
            const auto binding = std::ranges::lower_bound(
                resources_, candidate->resource, {}, &preview_resource_source_binding_t::resource);
            if (binding != resources_.end() && binding->resource == candidate->resource) {
                source = &binding->voice;
            }
        }
        if (source == nullptr) {
            ++result.missing_sources;
            advance_candidate();
            continue;
        }
        if (next_voice_id_ == 0) {
            result.status = preview_bridge_pump_status_t::voice_id_exhausted;
            break;
        }

        const auto status =
            transport_.schedule_voice(*token_, {.id = next_voice_id_,
                                                .kind = voice_kind,
                                                .source = source->source,
                                                .timeline_start_frame = candidate->sample_frame,
                                                .source_start_frame = source->source_start_frame,
                                                .frame_count = source->frame_count,
                                                .gain = source->gain});
        if (status == voice_schedule_status_t::capacity_exceeded) {
            result.status = preview_bridge_pump_status_t::capacity_limited;
            break;
        }
        if (status == voice_schedule_status_t::stale_generation) {
            result.status = preview_bridge_pump_status_t::stale_generation;
            break;
        }
        if (status != voice_schedule_status_t::scheduled) {
            result.status = preview_bridge_pump_status_t::transport_rejected_voice;
            break;
        }

        ++result.scheduled_events;
        advance_candidate();
        if (next_voice_id_ == std::numeric_limits<std::uint64_t>::max()) {
            next_voice_id_ = 0;
        } else {
            ++next_voice_id_;
        }
    }
    last_materialized_end_ = exclusive_end_frame;
    return result;
}

} // namespace wormedit::audio
