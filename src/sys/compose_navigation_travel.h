#pragma once

#include <wormedit/core/interaction.h>
#include <algorithm>
#include <cmath>
#include <optional>

namespace wormedit::sys {

// Keep logical cursor/range changes immediate; only the viewport trails the destination.
struct compose_navigation_travel_t {
    std::optional<double> target;
    double expected_scroll{}, scale{};
    wormedit::chart_id_t chart;
    std::uint64_t revision{};
    wormedit::cursor_pose_t destination_cursor;

    void cancel() noexcept { target.reset(); }
    void begin(wormedit::editor_interaction_state_t& state, std::uint64_t document_revision,
               double previous_scroll) noexcept {
        destination_cursor = wormedit::capture_cursor_pose(state);
        target = state.viewport_scroll_pixels;
        expected_scroll = previous_scroll;
        scale = state.pixels_per_standard_measure;
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
        if (playing || !owns_cursor(state) || chart != state.chart || revision != document_revision ||
            scale != state.pixels_per_standard_measure ||
            expected_scroll != state.viewport_scroll_pixels ||
            !std::isfinite(*target) || !std::isfinite(scale) || scale <= 0.0) {
            cancel();
            return;
        }
        if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0) return;
        // Ten standard measures/second: about 100 ms of travel followed by a resting interval.
        const auto distance = *target - expected_scroll;
        const auto step = scale * 10.0 * std::min(delta_seconds, 0.1);
        expected_scroll += std::clamp(distance, -step, step);
        wormedit::set_viewport_scroll_pixels(state, expected_scroll);
        if (expected_scroll == *target) cancel();
    }
};

} // namespace wormedit::sys
