#include "ui/nuklear/storyboard/scenario.h"
#include "ui/nuklear/chrome/text_input.h"
#include <array>

namespace wormedit::ui {
void render_storyboard_scenario(nk_context* context, const storyboard_view_t& view,
    storyboard_action_t& action, bool input_available) {
    if (!context) return;
    const bool enabled = input_available && !chrome::has_active_text_edit(context);
    struct choice_t { const char* label; storyboard_conditional_layer_t value; };
    static constexpr std::array choices{
        choice_t{.label = "Pass", .value = storyboard_conditional_layer_t::pass},
        choice_t{.label = "Fail", .value = storyboard_conditional_layer_t::fail},
        choice_t{.label = "Neither", .value = storyboard_conditional_layer_t::neither}};
    nk_layout_row_template_begin(context, 26);
    nk_layout_row_template_push_static(context, 42);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 78);
    nk_layout_row_template_end(context);
    nk_label(context, "State", NK_TEXT_LEFT);
    if (!enabled) nk_widget_disable_begin(context);
    for (const auto& choice : choices) {
        const bool selected = view.state.conditional_layer == choice.value;
        const bool clicked = nk_option_label(context, choice.label, selected) != 0;
        if (!clicked || selected || !enabled || action.kind != storyboard_action_kind_t::none) continue;
        action = storyboard_action_t::from(view, storyboard_action_kind_t::set_conditional_layer);
        action.conditional_layer = choice.value;
    }
    if (!enabled) nk_widget_disable_end(context);
}
} // namespace wormedit::ui
