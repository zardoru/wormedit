#include "ui/nuklear/tap_timing_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/theme.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
void draw_tap_circle(nk_context* context, const WormUiTapTimingView& view) {
    nk_layout_row_dynamic(context, 140, 1);
    struct nk_rect bounds{};
    (void)nk_widget(&bounds, context);
    auto* canvas = nk_window_get_canvas(context);
    const auto center_x = bounds.x + bounds.w * 0.5f;
    const auto center_y = bounds.y + bounds.h * 0.5f;
    constexpr float radius = 58;
    const auto count = std::clamp<unsigned>(view.beats_per_measure, 1, 32);
    const auto angle = 2.0f * std::numbers::pi_v<float> / count;
    for (unsigned index = 0; index < count; ++index) {
        auto role = wormedit::ui::theme::UiColor::surface_hover;
        if (view.playing && view.estimate_available && index == view.active_segment)
            role = view.downbeat_flash ? wormedit::ui::theme::UiColor::selected
                                       : wormedit::ui::theme::UiColor::accent;
        const auto begin = std::fmod(index * angle + std::numbers::pi_v<float> * 1.5f + 0.03f,
                                     2.0f * std::numbers::pi_v<float>);
        nk_fill_arc(canvas, center_x, center_y, radius, begin, begin + angle - 0.06f,
                    wormedit::ui::theme::ui(role));
    }
    nk_fill_circle(canvas, nk_rect(center_x - 32, center_y - 32, 64, 64),
        wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::surface));
}
void draw_tap_estimate(nk_context* context, const WormUiTapTimingView& view) {
    nk_layout_row_dynamic(context, 24, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Taps %llu/%llu", static_cast<unsigned long long>(view.tap_count),
              static_cast<unsigned long long>(view.tap_capacity));
    if (!view.estimate_available) {
        nk_label(context, "Four taps are needed for an estimate.", NK_TEXT_LEFT);
        return;
    }
    nk_labelf(context, NK_TEXT_LEFT, "BPM %.3f  |  %.3f ms per beat", view.bpm, view.ms_per_beat);
    nk_labelf(context, NK_TEXT_LEFT, "Offset %.3f ms  |  Deviation %.3f ms", view.offset_ms, view.deviation_ms);
    nk_labelf(context, NK_TEXT_LEFT, "%s  |  Confidence %.0f%%", view.stable ? "Stable" : "Keep tapping",
              view.confidence * 100);
}
} // namespace

void worm_ui_nuklear_render_tap_timing_panel(nk_context* context,
    const WormUiTapTimingView& view, WormUiTapTimingAction& action) {
    if (!context || !view.visible) return;
    if (nk_window_is_closed(context, "Tap timing")) nk_window_show(context, "Tap timing", NK_SHOWN);
    if (nk_begin(context, "Tap timing", nk_rect(300, 110, 470, 370),
        NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        draw_tap_circle(context, view);
        nk_layout_row_dynamic(context, 24, 1);
        if (!view.playing) nk_label(context, "Play the song, then tap on the beat", NK_TEXT_LEFT);
        else if (!view.source_available) nk_label(context, "The preview needs a music track for tapping.", NK_TEXT_LEFT);
        else nk_label(context, "Tap T, or Space while this panel is focused.", NK_TEXT_LEFT);
        draw_tap_estimate(context, view);
        if (view.status) nk_label(context, view.status, NK_TEXT_LEFT);
        nk_layout_row_dynamic(context, 28, 1);
        if (wormedit::ui::chrome::mode_button(context, "Reset", "Clear the tap estimate", false))
            action.reset = true;
    }
    nk_end(context);
    if (nk_window_is_closed(context, "Tap timing")) action.close = true;
}
