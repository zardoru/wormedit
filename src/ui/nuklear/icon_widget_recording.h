#pragma once

// Included only by the headless event-harness target. The application does not
// compile recording calls or storage into its native icon cells.
#include "ui/field/rect.h"
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit::ui::testing {

class icon_widget_snapshot_t {
public:
    void clear() { widgets_.clear(); }

    void append(std::string_view tooltip, SDL_FRect bounds, SDL_FRect clip) {
        if (tooltip.empty()) return;
        bounds = rect_intersection(bounds, clip);
        if (bounds.w <= 0 || bounds.h <= 0) return;
        widgets_.push_back({.tooltip = std::string{tooltip}, .bounds = bounds});
    }

    [[nodiscard]] auto center_of(std::string_view tooltip) const -> std::pair<float, float> {
        std::optional<SDL_FRect> found;
        for (const auto& widget : widgets_) {
            if (widget.tooltip != tooltip) continue;
            if (found)
                throw std::runtime_error("Ambiguous icon tooltip: " + std::string{tooltip});
            found = widget.bounds;
        }
        if (!found)
            throw std::runtime_error("No visible icon with tooltip: " + std::string{tooltip});
        return {found->x + found->w * 0.5f, found->y + found->h * 0.5f};
    }

private:
    struct widget_t {
        std::string tooltip;
        SDL_FRect bounds;
    };
    std::vector<widget_t> widgets_;
};

inline icon_widget_snapshot_t* icon_widget_target{};

class icon_widget_recording_scope_t {
public:
    explicit icon_widget_recording_scope_t(icon_widget_snapshot_t& snapshot)
        : previous_(icon_widget_target) {
        snapshot.clear();
        icon_widget_target = &snapshot;
    }
    ~icon_widget_recording_scope_t() { icon_widget_target = previous_; }
    icon_widget_recording_scope_t(const icon_widget_recording_scope_t&) = delete;
    auto operator=(const icon_widget_recording_scope_t&)
        -> icon_widget_recording_scope_t& = delete;
private:
    icon_widget_snapshot_t* previous_;
};

} // namespace wormedit::ui::testing
