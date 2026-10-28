#pragma once

// Included only by the headless event-harness target. The application does not
// compile recording calls or storage into its native icon cells.
#include "ui/field/rect.h"
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
        const bool fully_visible = bounds.x >= clip.x && bounds.y >= clip.y &&
            bounds.x + bounds.w <= clip.x + clip.w && bounds.y + bounds.h <= clip.y + clip.h;
        bounds = rect_intersection(bounds, clip);
        if (bounds.w <= 0 || bounds.h <= 0) return;
        widgets_.push_back({.tooltip = std::string{tooltip}, .bounds = bounds,
                            .fully_visible = fully_visible});
    }

    [[nodiscard]] auto center_of(std::string_view tooltip) const -> std::pair<float, float> {
        const auto& bounds = widget_for(tooltip).bounds;
        return {bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f};
    }

    [[nodiscard]] auto is_fully_visible(std::string_view tooltip) const -> bool {
        return widget_for(tooltip).fully_visible;
    }

private:
    struct widget_t {
        std::string tooltip;
        SDL_FRect bounds;
        bool fully_visible;
    };

    [[nodiscard]] auto widget_for(std::string_view tooltip) const -> const widget_t& {
        const widget_t* found = nullptr;
        for (const auto& widget : widgets_) {
            if (widget.tooltip != tooltip) continue;
            if (found)
                throw std::runtime_error("Ambiguous icon tooltip: " + std::string{tooltip});
            found = &widget;
        }
        if (!found)
            throw std::runtime_error("No visible icon with tooltip: " + std::string{tooltip});
        return *found;
    }

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
