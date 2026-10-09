#include "ui/nuklear/timing_settings_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/dialog.h"
#include <array>
#include <cstdint>
#include <cstdio>

static void render_timing_lane_visibility(nk_context* context, const WormUiWorkspaceView& workspace, WormUiWorkspaceAction* action) {
    const auto* view = &workspace.compose;
    const auto* timing_view = &workspace.timing;
    nk_layout_row_dynamic(context, 15, 1);
    nk_label_colored(context, "Timing lanes", NK_TEXT_LEFT,
                     wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::text_muted));
    constexpr std::array<const char*, WORM_UI_TIMING_LANE_COUNT> labels{
        "BPM", "SV", "SPD", "STP", "DLY", "WRP", "KIA", "ANC"};
    for (std::size_t row = 0; row < 4; ++row) {
        nk_layout_row_begin(context, NK_DYNAMIC, wormedit::ui::metrics::control_default, 2);
        for (std::size_t column = 0; column < 2; ++column) {
            const auto lane = row * 2 + column;
            const auto bit = static_cast<std::uint8_t>(1U << lane);
            const auto visible = (view->timing_lane_visibility_mask & bit) != 0;
            const auto editing_lane = timing_view->stage_active &&
                                      static_cast<std::size_t>(timing_view->stage_lane) == lane;
            char label[16]{};
            (void)snprintf(label, sizeof(label), "%s: %s", labels[lane], visible ? "On" : "Off");
            nk_layout_row_push(context, wormedit::ui::chrome::property_label_ratio);
            const auto disabled = !view->scene_available || (editing_lane && visible);
            if (disabled) nk_widget_disable_begin(context);
            const auto clicked = nk_button_label(context, label);
            if (disabled) nk_widget_disable_end(context);
            if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_timing_lane = static_cast<std::uint8_t>(lane);
                action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY;
            }
        }
        nk_layout_row_end(context);
    }

}

static void render_metronome_controls(nk_context* context, const WormUiWorkspaceView& view,
                                      WormUiWorkspaceAction& action) {
    constexpr auto row = wormedit::ui::metrics::control_default;
    nk_layout_row_dynamic(context, row, 1);
    nk_bool enabled = view.transport_metronome_enabled;
    if (!view.transport_metronome_available) nk_widget_disable_begin(context);
    if (nk_checkbox_label(context, "Metronome", &enabled)) {
        action.timing_settings.change_enable = true;
        action.timing_settings.enabled = enabled;
    }
    nk_layout_row_dynamic(context, row, 3);
    nk_labelf(context, NK_TEXT_LEFT, "Volume: %u%%", view.metronome_volume);
    if (nk_button_label(context, "Lower volume")) {
        action.timing_settings.change_volume = true;
        action.timing_settings.volume = view.metronome_volume > 5 ? view.metronome_volume - 5 : 0;
    }
    if (nk_button_label(context, "Raise volume")) {
        action.timing_settings.change_volume = true;
        action.timing_settings.volume = view.metronome_volume < 95 ? view.metronome_volume + 5 : 100;
    }
    const auto count_in_label = nk_widget_bounds(context);
    nk_labelf(context, NK_TEXT_LEFT, "Count-in: %u", view.metronome_count_in);
    if (wormedit::ui::nuklear::tooltip::hovered(context, count_in_label))
        wormedit::ui::nuklear::tooltip::text(context,
            "The count-in applies on the next start. Clicks before audio frame zero are omitted, "
            "so a chart with no preroll can have no count-in clicks.");
    if (nk_button_label(context, "Less count-in")) {
        action.timing_settings.change_count_in = true;
        action.timing_settings.count_in = view.metronome_count_in ? view.metronome_count_in - 1 : 0;
    }
    if (nk_button_label(context, "More count-in")) {
        action.timing_settings.change_count_in = true;
        action.timing_settings.count_in = view.metronome_count_in < 4 ? view.metronome_count_in + 1 : 4;
    }
    if (!view.transport_metronome_available) nk_widget_disable_end(context);
}

void worm_ui_nuklear_render_timing_settings_panel(nk_context* context,
    const WormUiWorkspaceView& view, WormUiWorkspaceAction& action) {
    namespace chrome = wormedit::ui::chrome;
    if (!context || !view.timing_settings_visible) return;
    static chrome::Dialog dialog;
    if (chrome::dialog_begin(dialog, context, "Timing Settings", "Timing Settings",
                             {.modal = false, .closable = true, .width = 420.0f})) {
        render_metronome_controls(context, view, action);
        render_timing_lane_visibility(context, view, &action);
    }
    if (chrome::dialog_end(dialog, context) == chrome::DialogResult::cancel)
        action.timing_settings.close = true;
}
