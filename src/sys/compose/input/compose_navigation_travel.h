#pragma once

#include <wormedit/core/interaction_cursor_pose.h>
#include <algorithm>
#include <cmath>
#include <optional>

namespace wormedit::sys {

// Keep logical cursor/range changes immediate; only the viewport trails the destination.
struct compose_navigation_travel_t {
    std::optional<double> target;
    double expected_scroll{}, scale{}, remaining_seconds{};
    wormedit::chart_id_t chart;
    std::uint64_t revision{};
    wormedit::cursor_pose_t destination_cursor;

    static constexpr double travel_seconds = 0.075;

    void cancel() noexcept { target.reset(); }
    void begin(wormedit::editor_interaction_state_t& state, std::uint64_t document_revision,
               double previous_scroll) noexcept {
        destination_cursor = wormedit::capture_cursor_pose(state);
        target = state.viewport_scroll_pixels;
        expected_scroll = previous_scroll;
        scale = state.pixels_per_standard_measure;
        remaining_seconds = travel_seconds;
        chart = state.chart;
        revision = document_revision;
        wormedit::set_viewport_scroll_pixels(state, previous_scroll);
    }
    auto owns_cursor(const wormedit::editor_interaction_state_t& state) const noexcept -> bool {
        auto pose = wormedit::capture_cursor_pose(state);
        pose.viewport_scroll_pixels = destination_cursor.viewport_scroll_pixels;
        return pose == destination_cursor;
    }
    // Complete interrupted travel while retaining the already chosen logical cursor/range.
    void finish(wormedit::editor_interaction_state_t& state,
                std::uint64_t document_revision) noexcept {
        if (target && owns_cursor(state) && chart == state.chart && revision == document_revision &&
            scale == state.pixels_per_standard_measure &&
            expected_scroll == state.viewport_scroll_pixels && std::isfinite(*target))
            wormedit::set_viewport_scroll_pixels(state, *target);
        cancel();
    }
    void advance(wormedit::editor_interaction_state_t& state, std::uint64_t document_revision,
                 bool playing, double delta_seconds) noexcept {
        if (!target) return;
        if (playing) {
            finish(state, document_revision);
            return;
        }
        if (!owns_cursor(state) || chart != state.chart || revision != document_revision ||
            scale != state.pixels_per_standard_measure ||
            expected_scroll != state.viewport_scroll_pixels ||
            !std::isfinite(*target) || !std::isfinite(scale) || scale <= 0.0) {
            cancel();
            return;
        }
        if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0) return;
        // Finish each destination within 75 ms, before the next held repeat.
        const auto distance = *target - expected_scroll;
        const auto elapsed = std::min(delta_seconds, 0.1);
        const auto fraction = std::min(elapsed / remaining_seconds, 1.0);
        expected_scroll = fraction == 1.0 ? *target : expected_scroll + distance * fraction;
        remaining_seconds = std::max(remaining_seconds - elapsed, 0.0);
        wormedit::set_viewport_scroll_pixels(state, expected_scroll);
        if (expected_scroll == *target) cancel();
    }
};

} // namespace wormedit::sys
