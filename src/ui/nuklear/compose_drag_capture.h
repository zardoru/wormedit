#pragma once

#include "ui/frontend.h"

#include <cmath>
#include <cstdint>

// Screen pixels keep the drag threshold stable across viewport scaling and zoom.
// The RC1 registry owns cancellation; each emitter owns its verb-specific commit hook.
struct compose_drag_capture_t {
    std::uint64_t project{}, chart{}, revision{}, asset{};
    std::uint32_t sample_rate{};
    double anchor_x{}, anchor_y{};
    bool active{}, audio_source{};
    static constexpr double threshold_pixels = 4.0;
    static constexpr double edge_tolerance_pixels = 6.0;

    auto begin(const WormUiComposeView& view, double x, double y, bool locked,
               bool capture_audio = false) -> bool {
        cancel();
        if (!view.scene_available || view.input_occluded || locked) return false;
        project = view.project_id; chart = view.chart_id; revision = view.document_revision;
        asset = view.selected_audio_asset_id; sample_rate = view.waveform_overview_sample_rate;
        anchor_x = x; anchor_y = y; audio_source = capture_audio; active = true;
        return true;
    }
    auto matches(const WormUiComposeView& view, bool locked = false) const -> bool {
        return active && view.scene_available && !view.input_occluded && !locked &&
               project == view.project_id && chart == view.chart_id && revision == view.document_revision &&
               (!audio_source || (asset == view.selected_audio_asset_id &&
                                   sample_rate == view.waveform_overview_sample_rate));
    }
    auto moved_past_threshold(double x, double y, bool horizontal = false) const -> bool {
        return std::abs(y - anchor_y) >= threshold_pixels ||
               (horizontal && std::abs(x - anchor_x) >= threshold_pixels);
    }
    void cancel() { *this = {}; }
};
