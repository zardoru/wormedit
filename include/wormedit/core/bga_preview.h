#pragma once

#include <wormedit/core/result.h>
#include <wormedit/core/timing.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace wormedit {

struct bga_preview_token_t {
    std::uint64_t generation{};

    auto operator==(const bga_preview_token_t&) const -> bool = default;
};

struct bga_layer_state_t {
    std::optional<preview_visual_event_t> background;
    std::optional<preview_visual_event_t> poor;
    std::optional<preview_visual_event_t> layer;
    std::optional<preview_visual_event_t> layer2;
    std::optional<preview_visual_event_t> video;
    std::optional<preview_visual_event_t> storyboard;

    auto operator==(const bga_layer_state_t&) const -> bool = default;
};

enum class bga_preview_status_t : std::uint8_t {
    ready,
    not_started,
    stale_generation,
    invalid_schedule,
    capacity_exceeded,
    non_monotonic_window,
    generation_exhausted,
};

using bga_preview_position_outcome_t = result_t<bga_preview_token_t, bga_preview_status_t>;
using bga_preview_advance_outcome_t =
    result_t<std::span<const preview_visual_event_t>, bga_preview_status_t>;

// Renderer-neutral cursor over the immutable BGA schedule. The schedule must
// outlive this consumer's active session. Calls are made by one control/render
// thread: begin/reposition may allocate index storage, while advance only changes the
// queue head and current layer values.
class bga_preview_queue_t {
  public:
    explicit bga_preview_queue_t(std::size_t maximum_events = 100'000)
        : maximum_events_(maximum_events) {}

    // Primary outcomes return the token/event span. Layer state belongs to this
    // queue and can be queried through current_layers when a snapshot is needed.
    [[nodiscard]] auto begin(const preview_schedule_t& schedule, std::uint64_t sample_frame = 0)
        -> bga_preview_position_outcome_t;
    [[nodiscard]] auto reposition(bga_preview_token_t token, std::uint64_t sample_frame)
        -> bga_preview_position_outcome_t;
    [[nodiscard]] auto advance_events(bga_preview_token_t token, std::uint64_t exclusive_end_frame)
        -> bga_preview_advance_outcome_t;

    void stop() noexcept;

    [[nodiscard]] auto current_layers(bga_preview_token_t token) const noexcept
        -> std::optional<bga_layer_state_t>;

  private:
    using layer_indices_t = std::array<std::vector<std::size_t>, 6>;

    [[nodiscard]] auto begin_session(std::uint64_t sample_frame)
        -> std::optional<bga_preview_token_t>;
    void set_position(std::uint64_t sample_frame);
    void apply(std::span<const preview_visual_event_t> events) noexcept;
    [[nodiscard]] auto is_current(bga_preview_token_t token) const noexcept -> bool;

    std::size_t maximum_events_{};
    const preview_schedule_t* schedule_{};
    layer_indices_t layer_indices_;
    preview_queue_head_t head_;
    bga_layer_state_t layers_;
    std::uint64_t generation_{};
    std::uint64_t last_advanced_end_{};
    bool active_{};
};

} // namespace wormedit
