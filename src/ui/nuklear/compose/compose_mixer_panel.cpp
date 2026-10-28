#include "ui/nuklear/compose/compose_mixer_panel.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/tooltip.h"
#include "ui/nuklear/chrome/ui_metrics.h"
#include "ui/nuklear/render/theme.h"

#include "wormedit/nuklear_config.h"

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace {
namespace chrome = wormedit::ui::chrome;
namespace metrics = wormedit::ui::metrics;
using wormedit::ui::chrome::color;
using wormedit::ui::theme::UiColor;

constexpr float name_width = 84.0f;
constexpr float level_width = 52.0f;

// A bus that cannot be heard (muted, or outside the solo set) reads like disabled text.
auto strip_text(bool silenced) -> nk_color {
    return silenced ? chrome::text_faint() : color(UiColor::text);
}

struct MixerPanel {
    nk_context* context;
    const wormedit::preview_mix_t& current;
    wormedit::ui::workspace_action_t& action;
    bool available;

    // Edits accumulate on the pending request so several controls in one frame all apply.
    [[nodiscard]] auto next() const -> wormedit::preview_mix_t {
        return action.preview_mix.value_or(current);
    }
};

void begin_strip_row(nk_context* context) {
    nk_layout_row_template_begin(context, metrics::control_default);
    nk_layout_row_template_push_static(context, name_width);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, level_width);
    nk_layout_row_template_push_static(context, chrome::MixToggle::width);
    nk_layout_row_template_push_static(context, chrome::MixToggle::width);
    nk_layout_row_template_end(context);
}

void render_strip_name(nk_context* context, std::string_view title, std::string_view description,
                       bool silenced) {
    const auto bounds = nk_widget_bounds(context);
    nk_label_colored(context, std::string{title}.c_str(), NK_TEXT_LEFT,
                     strip_text(silenced));
    if (wormedit::ui::nuklear::tooltip::hovered(context, bounds))
        wormedit::ui::nuklear::tooltip::text(context, std::string{description}.c_str());
}

// Returns the new level when the slider moved.
auto render_level(nk_context* context, std::uint8_t level, bool silenced) -> std::optional<std::uint8_t> {
    int value = level;
    const auto changed = nk_slider_int(context, 0, &value, wormedit::maximum_mix_level, 1) != 0;
    const auto decibels = wormedit::mix_level_decibels(static_cast<std::uint8_t>(value));
    if (decibels)
        nk_labelf_colored(context, NK_TEXT_RIGHT, strip_text(silenced), "%.0f dB", std::round(*decibels) + 0.0f);
    else
        nk_label_colored(context, "-inf dB", NK_TEXT_RIGHT, strip_text(silenced));
    if (!changed || value == level) return std::nullopt;
    return static_cast<std::uint8_t>(value);
}

void render_bus(const MixerPanel& panel, const wormedit::mix_bus_definition_t& bus) {
    auto* context = panel.context;
    const auto& strip = panel.current.bus(bus.id);
    const auto silenced = panel.current.is_silenced(bus.id);
    begin_strip_row(context);
    render_strip_name(context, bus.title, bus.description, silenced);
    if (const auto level = render_level(context, strip.level, silenced)) {
        auto next = panel.next();
        next.bus(bus.id).level = *level;
        panel.action.preview_mix = next;
    }
    const auto title = std::string{bus.title};
    const auto mute_tip = (strip.muted ? "Unmute " : "Mute ") + title;
    if (chrome::MixToggle{chrome::MixToggleKind::mute, strip.muted, mute_tip.c_str()}.render(context)) {
        auto next = panel.next();
        next.bus(bus.id).muted = !strip.muted;
        panel.action.preview_mix = next;
    }
    const auto solo_tip = strip.solo ? "Remove " + title + " from solo mix" : "Solo " + title + " (additive)";
    if (chrome::MixToggle{chrome::MixToggleKind::solo, strip.solo, solo_tip.c_str()}.render(context)) {
        auto next = panel.next();
        next.bus(bus.id).solo = !strip.solo;
        panel.action.preview_mix = next;
    }
}

void render_master(const MixerPanel& panel) {
    auto* context = panel.context;
    begin_strip_row(context);
    render_strip_name(context, "Master", "Every playback voice, including metronome clicks", false);
    if (const auto level = render_level(context, panel.current.master, false)) {
        auto next = panel.next();
        next.master = *level;
        panel.action.preview_mix = next;
    }
    nk_spacing(context, 2);
}

void render_header(const MixerPanel& panel) {
    auto* context = panel.context;
    nk_layout_row_template_begin(context, metrics::control_default);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 64.0f);
    nk_layout_row_template_end(context);
    chrome::panel_header_cell(context,
        wormedit::dock_panel_catalog[static_cast<std::size_t>(wormedit::dock_panel_t::mixer)].title.data());
    if (chrome::Button{"Reset", chrome::ButtonKind::ghost, "Restore every level and clear mute and solo",
                       panel.available && !panel.current.is_default()}.render(context))
        panel.action.preview_mix = wormedit::preview_mix_t{};
}

void render_separator(nk_context* context) {
    nk_layout_row_dynamic(context, metrics::gap_medium, 1);
    const auto bounds = nk_widget_bounds(context);
    nk_spacing(context, 1);
    const auto y = bounds.y + bounds.h * 0.5f;
    nk_stroke_line(nk_window_get_canvas(context), bounds.x, y, bounds.x + bounds.w, y, 1.0f,
                   color(UiColor::separator));
}
} // namespace

void worm_ui_nuklear_render_compose_mixer(struct nk_context* context,
                                          const wormedit::ui::compose_view_t* view,
                                          wormedit::ui::workspace_action_t* action) {
    if (!context || !view || !action) return;
    const MixerPanel panel{context, view->preview_mix, *action, view->preview_mix_available};
    render_header(panel);
    chrome::group_gap(context);
    if (!view->preview_mix_available) nk_widget_disable_begin(context);
    for (const auto& bus : wormedit::mix_bus_catalog) render_bus(panel, bus);
    render_separator(context);
    render_master(panel);
    if (!view->preview_mix_available) nk_widget_disable_end(context);
    nk_layout_row_dynamic(context, 36, 1);
    nk_label_colored_wrap(context, "Playback only; exports keep the authored mix.", color(UiColor::text_muted));
}
