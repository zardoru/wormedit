#include "ui/nuklear/audio/tap_timing_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/dialog.h"
#include "ui/nuklear/render/theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>

namespace {
namespace chrome = wormedit::ui::chrome;
namespace theme = wormedit::ui::theme;
void text(nk_command_buffer* canvas, struct nk_rect bounds, const char* label,
          const nk_user_font* font, nk_color tint) {
    nk_draw_text(canvas, bounds, label, static_cast<int>(std::strlen(label)), font,
                 theme::ui(theme::UiColor::surface), tint);
}
void draw_ring(nk_command_buffer* canvas, struct nk_rect bounds, const wormedit::ui::tap_timing_view_t& view) {
    const auto center_x = bounds.x + 48;
    const auto center_y = bounds.y + 48;
    const auto count = std::clamp<unsigned>(view.beats_per_measure, 1, 32);
    const auto angle = 2.0f * std::numbers::pi_v<float> / count;
    for (unsigned index = 0; index < count; ++index) {
        auto role = theme::UiColor::surface_hover;
        if (view.playing && view.estimate_available && index == view.active_segment)
            role = view.downbeat_flash ? theme::UiColor::selected : theme::UiColor::accent;
        if (view.tap_flash && index == view.tap_segment) role = theme::UiColor::selected;
        const auto begin = std::fmod(index * angle + std::numbers::pi_v<float> * 1.5f + 0.03f,
                                     2.0f * std::numbers::pi_v<float>);
        nk_fill_arc(canvas, center_x, center_y, 46, begin, begin + angle - 0.06f, theme::ui(role));
    }
    nk_fill_circle(canvas, nk_rect(center_x - 28, center_y - 28, 56, 56),
                    theme::ui(theme::UiColor::surface));
}
void draw_readout(nk_context* context, const wormedit::ui::tap_timing_view_t& view) {
    nk_layout_row_dynamic(context, 96, 1);
    struct nk_rect bounds{};
    (void)nk_widget(&bounds, context);
    auto* canvas = nk_window_get_canvas(context);
    draw_ring(canvas, bounds, view);
    const auto x = bounds.x + 120;
    const auto width = std::max(1.0f, bounds.w - 120);
    auto* display = theme::display_font ? theme::display_font : context->style.font;
    char value[48]{};
    if (view.estimate_available && view.tap_count >= 4) std::snprintf(value, sizeof(value), "%.2f", view.bpm);
    else std::snprintf(value, sizeof(value), "—");
    text(canvas, nk_rect(x, bounds.y + 12, width, 34), value, display, theme::ui(theme::UiColor::text_bright));
    const auto value_width = display->width(display->userdata, display->height, value,
                                          static_cast<int>(std::strlen(value)));
    text(canvas, nk_rect(x + value_width + 8, bounds.y + 25, 38, 20), "BPM",
         context->style.font, theme::ui(theme::UiColor::text_muted));
    if (!view.estimate_available || view.tap_count < 4) return;
    char detail[96]{};
    std::snprintf(detail, sizeof(detail), "±%.1f ms · %s", view.deviation_ms, view.stable ? "stable" : "keep tapping");
    text(canvas, nk_rect(x, bounds.y + 50, width, 20), detail, context->style.font,
         theme::ui(view.stable ? theme::UiColor::accent : theme::UiColor::text_muted));
    std::snprintf(detail, sizeof(detail), "beat 0 at %.1f ms", view.offset_ms);
    text(canvas, nk_rect(x, bounds.y + 73, width, 20), detail, context->style.font,
         theme::ui(theme::UiColor::text_muted));
}
void draw_progress(nk_context* context, const wormedit::ui::tap_timing_view_t& view) {
    nk_layout_row_dynamic(context, 18, 1);
    struct nk_rect bounds{};
    (void)nk_widget(&bounds, context);
    auto* canvas = nk_window_get_canvas(context);
    const auto count = std::clamp<std::uint64_t>(view.tap_capacity, 1, 128);
    const auto width = std::max(1.0f, bounds.w - 106);
    const auto step = width / static_cast<float>(count);
    for (std::uint64_t i = 0; i < count; ++i)
        nk_fill_rect(canvas, nk_rect(bounds.x + static_cast<float>(i) * step, bounds.y + 5,
                     std::max(1.0f, step - 3), 8), 2,
                     theme::ui(i < view.tap_count ? theme::UiColor::selected : theme::UiColor::surface_hover));
    char label[48]{};
    std::snprintf(label, sizeof(label), "%llu / %llu taps",
                  static_cast<unsigned long long>(view.tap_count),
                  static_cast<unsigned long long>(view.tap_capacity));
    text(canvas, nk_rect(bounds.x + width + 8, bounds.y, 100, 18), label,
         context->style.font, theme::ui(theme::UiColor::text_muted));
}
auto hint(const wormedit::ui::tap_timing_view_t& view) -> const char* {
    if (!view.status.empty()) return view.status.data();
    if (!view.music_available) return "Add a music track to tap";
    if (!view.playing) return "Start playback to tap";
    if (!view.estimate_available || view.tap_count < 4) return "Play the song and tap T or Space on the beat";
    return "Tap T or Space on the beat";
}
auto apply_reason(const wormedit::ui::tap_timing_view_t& view) -> const char* {
    if (view.timing_locked) return "Unlock timing to apply the estimate.";
    if (!view.music_available) return "Add a music track to tap.";
    if (!view.playing) return "Start playback and tap on the beat.";
    if (!view.estimate_available || view.tap_count < 4) return "At least four taps are needed for an estimate.";
    if (!view.stable) return "Keep tapping until the estimate is stable.";
    return "The estimate is not available for this chart.";
}
void draw_rounding(nk_context* context, const wormedit::ui::tap_timing_view_t& view, wormedit::ui::tap_timing_action_t& action) {
    static const char* choices[]{"Raw", "1", "0.5"};
    nk_layout_row_template_begin(context, 28);
    nk_layout_row_template_push_static(context, 72);
    nk_layout_row_template_push_static(context, 168);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_end(context);
    nk_label_colored(context, "Round to", NK_TEXT_LEFT, theme::ui(theme::UiColor::text_muted));
    const auto selected = chrome::Segmented{choices, 3, std::clamp<int>(view.bpm_choice, 0, 2)}.render(context);
    if (selected != view.bpm_choice) {
        action.choose_bpm = true;
        action.bpm_choice = static_cast<std::uint8_t>(selected);
    }
    struct nk_rect unused{};
    (void)nk_widget(&unused, context);
}
void draw_footer(nk_context* context, const wormedit::ui::tap_timing_view_t& view, wormedit::ui::tap_timing_action_t& action) {
    chrome::section_separator(context);
    nk_layout_row_template_begin(context, 28);
    nk_layout_row_template_push_static(context, 72);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 152);
    nk_layout_row_template_end(context);
    if (chrome::Button{"Reset", chrome::ButtonKind::ghost, "Clear the tap estimate"}.render(context)) action.reset = true;
    nk_spacing(context, 1);
    char label[64]{}, tooltip[320]{};
    char bpm[32]{};
    std::snprintf(bpm, sizeof(bpm), "%.2f", view.proposed_bpm);
    auto length = std::strlen(bpm);
    while (length && bpm[length - 1] == '0') bpm[--length] = '\0';
    if (length && bpm[length - 1] == '.') bpm[length - 1] = '\0';
    std::snprintf(label, sizeof(label), view.apply_available ? "Apply %s BPM" : "Apply BPM", bpm);
    std::snprintf(tooltip, sizeof(tooltip), "%s%sFirst tap sets the downbeat; later BPM changes keep their musical positions.",
                  view.apply_available ? "" : apply_reason(view), view.apply_available ? "" : "\n");
    if (chrome::Button{label, chrome::ButtonKind::primary, tooltip,
                       view.apply_available && !action.choose_bpm}.render(context)) {
        action.apply = true;
        action.apply_revision = view.apply_revision;
    }
}
} // namespace

void worm_ui_nuklear_render_tap_timing_panel(nk_context* context,
    const wormedit::ui::tap_timing_view_t& view, wormedit::ui::tap_timing_action_t& action) {
    if (!context || !view.visible) return;
    static chrome::Dialog dialog;
    if (chrome::dialog_begin(dialog, context, "Tap timing", "Tap tempo",
        {.modal = false, .closable = true, .width = 420, .escape_cancels = !view.input_blocked && view.close_key_pressed, .no_input = view.input_blocked,
         .enter_confirms = false, .movable = true})) {
        draw_readout(context, view);
        draw_progress(context, view);
        chrome::dialog_text(dialog, context, hint(view), theme::ui(theme::UiColor::text));
        draw_rounding(context, view, action);
        draw_footer(context, view, action);
    }
    if (chrome::dialog_end(dialog, context) == chrome::DialogResult::cancel) action.close = true;
}
