#include "ui/nuklear/compose_recipe_form.h"

#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/compose_panel_internal.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace {

// Digits and an optional leading minus: values may be negative, counts may not, and the core
// validator owns every range, so the filter only keeps the text well-formed.
nk_bool signed_filter(const struct nk_text_edit*, nk_rune rune) {
    return (rune >= '0' && rune <= '9') || rune == '-';
}

// Edits one fixed-size text buffer; true when its content changed.
template <std::size_t Size>
bool edit_text(nk_context* context, char (&buffer)[Size], nk_plugin_filter filter) {
    std::array<char, Size> before{};
    std::memcpy(before.data(), buffer, Size);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, buffer, static_cast<int>(Size), filter);
    return std::memcmp(before.data(), buffer, Size) != 0;
}

// "label  n / d": an exact fraction typed as two integers.
bool rational_field(nk_context* context, const char* label, wormedit::ui::timing_recipe_rational_text_t& text) {
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 4);
    nk_layout_row_push(context, 0.34f);
    nk_label(context, label, NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.30f);
    bool changed = edit_text(context, text.numerator, signed_filter);
    nk_layout_row_push(context, 0.06f);
    nk_label(context, "/", NK_TEXT_CENTERED);
    nk_layout_row_push(context, 0.30f);
    changed |= edit_text(context, text.denominator, nk_filter_decimal);
    nk_layout_row_end(context);
    return changed;
}

bool count_field(nk_context* context, const char* label, char* buffer, std::size_t size) {
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    nk_layout_row_push(context, 0.34f);
    nk_label(context, label, NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.66f);
    std::array<char, 64> before{};
    std::memcpy(before.data(), buffer, std::min(size, before.size()));
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, buffer, static_cast<int>(size),
                                         nk_filter_decimal);
    nk_layout_row_end(context);
    return std::memcmp(before.data(), buffer, std::min(size, before.size())) != 0;
}

const char* output_label(wormedit::sv_recipe_output_t output) {
    switch (output) {
    case wormedit::sv_recipe_output_t::sv: return "SV";
    case wormedit::sv_recipe_output_t::scrolls: return "Scrolls";
    case wormedit::sv_recipe_output_t::speeds: return "Speeds";
    }
    return "SV";
}

const char* curve_label(wormedit::sv_recipe_curve_t curve) {
    switch (curve) {
    case wormedit::sv_recipe_curve_t::constant: return "Constant";
    case wormedit::sv_recipe_curve_t::linear: return "Linear";
    case wormedit::sv_recipe_curve_t::exponential: return "Exponential";
    case wormedit::sv_recipe_curve_t::bump: return "Bump";
    case wormedit::sv_recipe_curve_t::lua_expression: return "Lua expression";
    }
    return "Linear";
}

// A button showing the current choice that steps to the next of `count` on click.
bool cycle_button(nk_context* context, const char* label, const char* current, int& value, int count) {
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    nk_layout_row_push(context, 0.34f);
    nk_label(context, label, NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.66f);
    const bool clicked = nk_button_label(context, current);
    nk_layout_row_end(context);
    if (clicked) value = (value + 1) % count;
    return clicked;
}

bool curve_values(nk_context* context, wormedit::ui::timing_recipe_form_t& form) {
    bool changed = false;
    switch (form.curve) {
    case wormedit::sv_recipe_curve_t::constant:
        changed |= rational_field(context, "Value", form.value_from);
        break;
    case wormedit::sv_recipe_curve_t::linear:
        changed |= rational_field(context, "From", form.value_from);
        changed |= rational_field(context, "To", form.value_to);
        break;
    case wormedit::sv_recipe_curve_t::exponential:
        changed |= rational_field(context, "From", form.value_from);
        changed |= rational_field(context, "To", form.value_to);
        changed |= rational_field(context, "Shape", form.shape);
        break;
    case wormedit::sv_recipe_curve_t::bump:
        changed |= rational_field(context, "Start", form.bump_start);
        changed |= rational_field(context, "Length", form.bump_length);
        changed |= rational_field(context, "Inside", form.bump_x);
        changed |= rational_field(context, "Outside", form.bump_y);
        break;
    case wormedit::sv_recipe_curve_t::lua_expression:
        break;
    }
    return changed;
}

bool sampling_values(nk_context* context, wormedit::ui::timing_recipe_form_t& form) {
    int mode = static_cast<int>(form.sampling_mode);
    bool changed = cycle_button(context, "Sampling",
                                form.sampling_mode == wormedit::sv_recipe_sampling_mode_t::fixed ? "Fixed" : "Adaptive",
                                mode, 2);
    form.sampling_mode = static_cast<wormedit::sv_recipe_sampling_mode_t>(mode);
    if (form.sampling_mode == wormedit::sv_recipe_sampling_mode_t::fixed) {
        int extent = static_cast<int>(form.fixed_extent);
        const bool points = form.fixed_extent == wormedit::ui::timing_recipe_fixed_extent_t::point_count;
        changed |= cycle_button(context, "Fixed by", points ? "Point count" : "Step rows", extent, 2);
        form.fixed_extent = static_cast<wormedit::ui::timing_recipe_fixed_extent_t>(extent);
        changed |= form.fixed_extent == wormedit::ui::timing_recipe_fixed_extent_t::point_count
                       ? count_field(context, "Points", form.fixed_point_count, sizeof(form.fixed_point_count))
                       : count_field(context, "Step rows", form.fixed_step_rows, sizeof(form.fixed_step_rows));
        return changed;
    }
    changed |= rational_field(context, "Tolerance", form.tolerance);
    changed |= count_field(context, "Max points", form.adaptive_max_points, sizeof(form.adaptive_max_points));
    return changed;
}

bool transition_values(nk_context* context, wormedit::ui::timing_recipe_form_t& form) {
    if (form.output != wormedit::sv_recipe_output_t::speeds) return false;
    bool changed = rational_field(context, "Transition", form.transition_duration);
    int seconds = form.transition_is_seconds ? 1 : 0;
    changed |= cycle_button(context, "Unit", seconds != 0 ? "Seconds" : "Beats", seconds, 2);
    form.transition_is_seconds = seconds != 0;
    return changed;
}

// Keep the edit buffers on the selected recipe: reload them when the selection changes, and when
// the document moved on after an apply or while nothing was being edited.
void synchronize_buffers(const wormedit::ui::compose_view_t& view) {
    const auto& stored = view.recipe_form;
    const bool selection_changed = state.recipe_editor.recipe_edit_id != stored.recipe_id.value;
    const bool revision_changed = state.recipe_editor.recipe_edit_revision != view.document_revision;
    if (selection_changed ||
        (revision_changed && (!state.recipe_editor.recipe_edit_dirty || state.recipe_editor.recipe_edit_apply_pending))) {
        state.recipe_editor.recipe_edit_form = stored.form;
        state.recipe_editor.recipe_edit_dirty = false;
        state.recipe_editor.recipe_edit_apply_pending = false;
    }
    state.recipe_editor.recipe_edit_id = stored.recipe_id.value;
    state.recipe_editor.recipe_edit_revision = view.document_revision;
}

void queue(const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action, WormUiComposeActionKind kind) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = kind;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
    action.compose_recipe_id.value = view.recipe_form.recipe_id.value;
    action.compose_recipe_form = state.recipe_editor.recipe_edit_form;
}

} // namespace

void render_compose_recipe_form(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (!view.recipe_form.visible) {
        state.recipe_editor.recipe_edit_id = 0;
        state.recipe_editor.recipe_edit_dirty = false;
        return;
    }
    synchronize_buffers(view);
    auto& form = state.recipe_editor.recipe_edit_form;
    nk_layout_row_dynamic(context, 20, 1);
    nk_labelf(context, NK_TEXT_LEFT, "SV recipe: %s%s", view.recipe_form.name.data(),
              view.recipe_form.enabled ? "" : " (off)");
    bool changed = false;
    int output = static_cast<int>(form.output);
    changed |= cycle_button(context, "Output", output_label(form.output), output, 3);
    form.output = static_cast<wormedit::sv_recipe_output_t>(output);
    const bool lua = form.curve == wormedit::sv_recipe_curve_t::lua_expression;
    if (lua) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Curve: Lua expression (not evaluated yet)", NK_TEXT_LEFT);
        nk_layout_row_dynamic(context, 54, 1);
        nk_label_wrap(context, view.recipe_form.expression_source.data());
    } else {
        int curve = static_cast<int>(form.curve);
        changed |= cycle_button(context, "Curve", curve_label(form.curve), curve, 4);
        form.curve = static_cast<wormedit::sv_recipe_curve_t>(curve);
        changed |= curve_values(context, form);
    }
    changed |= sampling_values(context, form);
    changed |= transition_values(context, form);
    if (changed) {
        state.recipe_editor.recipe_edit_dirty = true;
        queue(view, *action, WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM);
    }
    if (!view.recipe_form.status_text.empty()) {
        nk_layout_row_dynamic(context, 36, 1);
        nk_label_wrap(context, view.recipe_form.status_text.data());
    }
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Apply")) {
        state.recipe_editor.recipe_edit_apply_pending = true;
        queue(view, *action, WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM);
    }
    if (nk_button_label(context, "Revert")) {
        state.recipe_editor.recipe_edit_form = view.recipe_form.form;
        state.recipe_editor.recipe_edit_dirty = false;
        state.recipe_editor.recipe_edit_apply_pending = false;
        queue(view, *action, WORM_UI_COMPOSE_ACTION_CLEAR_SV_RECIPE_PREVIEW);
    }
}
