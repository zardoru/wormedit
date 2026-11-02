#pragma once

#include <cstdint>
#include <optional>

namespace wormedit::ui {

// Source-audio milliseconds, independent of chart rows, playback rate and cuts.
// The loop owner maps these endpoints onto its existing transport frame axis.
struct storyboard_loop_region_t {
    std::int64_t begin_ms{}, end_ms{};
    [[nodiscard]] auto is_playable() const -> bool {
        return begin_ms >= 0 && begin_ms < end_ms;
    }
    auto operator==(const storyboard_loop_region_t&) const -> bool = default;
};

struct storyboard_transport_view_t {
    std::optional<storyboard_loop_region_t> selection;
    bool available{}, loop_enabled{};
    std::optional<double> frame_ms; // Last completed frame from the common timing owner.
    [[nodiscard]] auto can_toggle_loop() const -> bool {
        return available && (loop_enabled || (selection && selection->is_playable()));
    }
    auto operator==(const storyboard_transport_view_t&) const -> bool = default;
};

// No interval means disable the shared loop. The enclosing storyboard action
// supplies the existing project/revision/session capture at the UI boundary.
struct storyboard_transport_action_t {
    std::optional<storyboard_loop_region_t> loop;
    auto operator==(const storyboard_transport_action_t&) const -> bool = default;
};

} // namespace wormedit::ui
