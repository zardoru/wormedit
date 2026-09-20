#include <wormedit/core/bga_preview.h>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <tuple>
#include <utility>

namespace wormedit {
namespace {

auto draw_ordered_before(const preview_visual_event_t& left, const preview_visual_event_t& right)
    -> bool {
    return std::tie(left.sample_frame, left.draw_order, left.source_order, left.object.value) <
           std::tie(right.sample_frame, right.draw_order, right.source_order, right.object.value);
}

auto layer_slot(visual_event_kind_t kind) -> std::optional<std::size_t> {
    switch (kind) {
    case visual_event_kind_t::background:
        return 0;
    case visual_event_kind_t::poor:
        return 1;
    case visual_event_kind_t::layer:
        return 2;
    case visual_event_kind_t::video:
        return 3;
    case visual_event_kind_t::storyboard:
        return 4;
    }
    return std::nullopt;
}

auto valid_kind(visual_event_kind_t kind) -> bool {
    return static_cast<std::uint8_t>(kind) <=
           static_cast<std::uint8_t>(visual_event_kind_t::storyboard);
}

auto layer_value(bga_layer_state_t& state, std::size_t slot)
    -> std::optional<preview_visual_event_t>& {
    switch (slot) {
    case 0:
        return state.background;
    case 1:
        return state.poor;
    case 2:
        return state.layer;
    case 3:
        return state.video;
    default:
        return state.storyboard;
    }
}

} // namespace

auto bga_preview_queue_t::start(const preview_schedule_t& schedule, std::uint64_t sample_frame)
    -> bga_preview_position_result_t {
    if (schedule.sample_rate == 0 || schedule.bga.size() > maximum_events_ ||
        !std::ranges::is_sorted(schedule.bga, draw_ordered_before) ||
        !std::ranges::all_of(schedule.bga,
                             [](const auto& event) { return valid_kind(event.kind); })) {
        return {
            .status = schedule.bga.size() > maximum_events_
                          ? bga_preview_status_t::capacity_exceeded
                          : bga_preview_status_t::invalid_schedule,
        };
    }

    layer_indices_t candidate_indices;
    for (std::size_t index = 0; index < schedule.bga.size(); ++index) {
        if (const auto slot = layer_slot(schedule.bga[index].kind); slot.has_value()) {
            candidate_indices[*slot].push_back(index);
        }
    }

    const auto token = begin_session(sample_frame);
    if (!token.has_value()) {
        return {.status = bga_preview_status_t::generation_exhausted};
    }
    schedule_ = &schedule;
    layer_indices_ = std::move(candidate_indices);
    set_position(sample_frame);
    return {
        .status = bga_preview_status_t::ready,
        .token = token,
        .layers = layers_,
    };
}

auto bga_preview_queue_t::seek(bga_preview_token_t token, std::uint64_t sample_frame)
    -> bga_preview_position_result_t {
    if (schedule_ == nullptr || !active_) {
        return {.status = bga_preview_status_t::not_started};
    }
    if (!is_current(token)) {
        return {.status = bga_preview_status_t::stale_generation};
    }
    const auto new_token = begin_session(sample_frame);
    if (!new_token.has_value()) {
        schedule_ = nullptr;
        layer_indices_ = {};
        head_ = {};
        layers_ = {};
        return {.status = bga_preview_status_t::generation_exhausted};
    }
    set_position(sample_frame);
    return {
        .status = bga_preview_status_t::ready,
        .token = new_token,
        .layers = layers_,
    };
}

void bga_preview_queue_t::stop() noexcept {
    active_ = false;
    if (generation_ != std::numeric_limits<std::uint64_t>::max()) {
        ++generation_;
    }
    schedule_ = nullptr;
    layer_indices_ = {};
    head_ = {};
    layers_ = {};
    last_advanced_end_ = 0;
}

auto bga_preview_queue_t::advance(bga_preview_token_t token, std::uint64_t exclusive_end_frame)
    -> bga_preview_advance_result_t {
    if (schedule_ == nullptr || !active_) {
        return {.status = bga_preview_status_t::not_started};
    }
    if (!is_current(token)) {
        return {.status = bga_preview_status_t::stale_generation};
    }
    if (exclusive_end_frame < last_advanced_end_) {
        return {
            .status = bga_preview_status_t::non_monotonic_window,
            .layers = layers_,
        };
    }

    const auto events = advance_preview_queue(
        std::span<const preview_visual_event_t>{schedule_->bga}, head_, exclusive_end_frame);
    apply(events);
    last_advanced_end_ = exclusive_end_frame;
    return {
        .status = bga_preview_status_t::ready,
        .events = events,
        .layers = layers_,
    };
}

auto bga_preview_queue_t::current_layers(bga_preview_token_t token) const noexcept
    -> std::optional<bga_layer_state_t> {
    if (schedule_ == nullptr || !is_current(token)) {
        return std::nullopt;
    }
    return layers_;
}

auto bga_preview_queue_t::begin_session(std::uint64_t sample_frame)
    -> std::optional<bga_preview_token_t> {
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        active_ = false;
        return std::nullopt;
    }
    ++generation_;
    active_ = true;
    last_advanced_end_ = sample_frame;
    head_ = {};
    layers_ = {};
    return bga_preview_token_t{generation_};
}

void bga_preview_queue_t::set_position(std::uint64_t sample_frame) {
    const auto events = std::span<const preview_visual_event_t>{schedule_->bga};
    seek_preview_queue(events, head_, sample_frame);
    for (std::size_t slot = 0; slot < layer_indices_.size(); ++slot) {
        const auto& indices = layer_indices_[slot];
        const auto boundary =
            std::ranges::lower_bound(indices, sample_frame, {}, [this](std::size_t index) {
                return schedule_->bga[index].sample_frame;
            });
        if (boundary != indices.begin()) {
            const auto& event = events[*std::prev(boundary)];
            layer_value(layers_, slot) = event;
        }
    }
}

void bga_preview_queue_t::apply(std::span<const preview_visual_event_t> events) noexcept {
    for (const auto& event : events) {
        const auto slot = layer_slot(event.kind);
        if (!slot.has_value()) {
            continue;
        }
        layer_value(layers_, *slot) = event;
    }
}

auto bga_preview_queue_t::is_current(bga_preview_token_t token) const noexcept -> bool {
    return active_ && token.generation == generation_;
}

} // namespace wormedit
