#include "ui/nuklear/compose/compose_recipe_form.h"

#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/compose/compose_panel_internal.h"

#include "ui/nuklear/chrome/file_dialog_slot.h"
#include <SDL3/SDL.h>

#include <algorithm>
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
    case wormedit::sv_recipe_curve_t::lua_expression: return "Lua function";
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

bool text_field(nk_context* context, const char* label, auto& buffer) {
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, label, NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 24, 1);
    return edit_text(context, buffer, nk_filter_default);
}

wormedit::ui::file_dialog_slot_t module_picker;
struct module_picker_origin_t { std::uint64_t project{}, chart{}, revision{}, recipe{}; };
module_picker_origin_t module_picker_origin;
std::string module_picker_error;

void SDLCALL module_picked(void* userdata, const char* const* paths, int) {
    const auto generation = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userdata));
    const bool cancelled = paths && !paths[0];
    module_picker.complete(generation, paths && paths[0] ? std::string{paths[0]} : std::string{},
        paths ? std::string{} : std::string{SDL_GetError()}, cancelled);
}

bool module_file(nk_context* context, const wormedit::ui::compose_view_t& view,
                 wormedit::ui::timing_recipe_form_t& form) {
    bool changed = text_field(context, "Module file", form.lua_source_reference);
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Choose module…")) {
        if (const auto generation = module_picker.begin()) {
            module_picker_origin = {view.project_id.value, view.chart_id.value, view.document_revision, view.recipe_form.recipe_id.value};
            static constexpr SDL_DialogFileFilter filter{"Lua modules", "lua"};
            SDL_ShowOpenFileDialog(module_picked, reinterpret_cast<void*>(static_cast<std::uintptr_t>(*generation)),
                SDL_GetKeyboardFocus(), &filter, 1, nullptr, false);
        }
    }
    if (nk_button_label(context, "Reload")) { form.lua_reload = true; changed = true; }
    if (auto picked = module_picker.take(); picked &&
        module_picker_origin.project == view.project_id.value && module_picker_origin.chart == view.chart_id.value && module_picker_origin.revision == view.document_revision &&
        module_picker_origin.recipe == view.recipe_form.recipe_id.value) {
        if (!*picked) module_picker_error = picked->error().message;
        else if (!picked->value().canceled) {
            std::snprintf(form.lua_source_reference, sizeof(form.lua_source_reference), "%s", picked->value().path.c_str());
            form.lua_reload = true;
            module_picker_error.clear();
            changed = true;
        }
    }
    if (!module_picker_error.empty()) {
        nk_layout_row_dynamic(context, 36, 1);
        nk_label_wrap(context, module_picker_error.c_str());
    }
    return changed;
}

bool lua_parameter_fields(nk_context* context, wormedit::ui::timing_recipe_form_t& form) {
    bool changed = false;
    for (auto& parameter : form.lua_parameters) {
        changed |= text_field(context, "Parameter name", parameter.name);
        changed |= rational_field(context, "Value", parameter.value);
    }
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Add parameter") && form.lua_parameters.size() < 64) {
        wormedit::ui::timing_recipe_lua_parameter_form_t parameter;
        std::snprintf(parameter.name, sizeof(parameter.name), "parameter%zu", form.lua_parameters.size() + 1);
        std::snprintf(parameter.value.numerator, sizeof(parameter.value.numerator), "0");
        std::snprintf(parameter.value.denominator, sizeof(parameter.value.denominator), "1");
        form.lua_parameters.push_back(parameter);
        changed = true;
    }
    if (nk_button_label(context, "Remove last") && !form.lua_parameters.empty()) {
        form.lua_parameters.pop_back(); changed = true;
    }
    return changed;
}

bool lua_cycle_fields(nk_context* context, wormedit::ui::timing_recipe_form_t& form) {
    bool changed = false;
    for (auto& count : form.lua_cycles) {
        int value = static_cast<int>(count);
        nk_layout_row_dynamic(context, 24, 1);
        nk_property_int(context, "Cycle emissions", 1, &value, 4096, 1, 1);
        changed |= value != static_cast<int>(count);
        count = static_cast<std::uint32_t>(value);
    }
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Add cycle") && form.lua_cycles.size() < 4096) {
        form.lua_cycles.push_back(1); changed = true;
    }
    if (nk_button_label(context, "Remove cycle") && !form.lua_cycles.empty()) {
        form.lua_cycles.pop_back(); changed = true;
    }
    return changed;
}

bool lua_fields(nk_context* context, const wormedit::ui::compose_view_t& view,
                wormedit::ui::timing_recipe_form_t& form) {
    bool changed = module_file(context, view, form);
    changed |= text_field(context, "Entry point", form.lua_entry_point);
    changed |= count_field(context, "Seed", form.lua_seed, sizeof(form.lua_seed));
    int mode = static_cast<int>(form.lua_mode);
    changed |= cycle_button(context, "Execution", mode == 0 ? "Pure" : "Ordered", mode, 2);
    form.lua_mode = static_cast<wormedit::sv_recipe_lua_mode_t>(mode);
    changed |= lua_parameter_fields(context, form);
    if (form.lua_mode == wormedit::sv_recipe_lua_mode_t::ordered)
        changed |= lua_cycle_fields(context, form);
    return changed;
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
    action.compose_input_project_id.value = view.project_id.value;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
    action.compose_recipe_id.value = view.recipe_form.recipe_id.value;
    action.compose_recipe_form = state.recipe_editor.recipe_edit_form;
}

void queue_recipe_input(const wormedit::ui::compose_view_t& view,
                        wormedit::ui::workspace_action_t& action, wormedit::recipe_id_t id,
                        WormUiComposeInputKind kind, wormedit::ui::compose_recipe_command_t command = {}) {
    append_compose_input(&action, kind, 0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, id.value, 0, false,
                         wormedit::timing_lane_kind_t::scroll, view.chart_id.value, view.document_revision);
    if (!action.compose_inputs.empty())
        action.compose_inputs.back().span_edge = static_cast<std::uint8_t>(command);
}

void render_recipe_order(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    if (view.recipe_entries.empty()) return;
    const auto selected = std::ranges::find(view.recipe_entries, view.recipe_form.recipe_id,
                                            &wormedit::ui::compose_recipe_entry_view_t::id);
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Recipe evaluation order", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 24, 1);
    const bool has_selection = selected != view.recipe_entries.end();
    const auto offset = has_selection ? std::size_t{0} : std::size_t{1};
    static std::vector<const char*> names;
    names.resize(view.recipe_entries.size() + offset);
    if (!has_selection) names[0] = "Select recipe";
    for (std::size_t i = 0; i < view.recipe_entries.size(); ++i)
        names[i + offset] = view.recipe_entries[i].label.c_str();
    const auto current = has_selection ? static_cast<int>(selected - view.recipe_entries.begin()) : 0;
    const auto choice = wormedit::ui::chrome::safe_combo(context, names.data(),
        static_cast<int>(names.size()), current, 24, nk_vec2(nk_widget_width(context), 200));
    if (choice != current && choice >= static_cast<int>(offset))
        queue_recipe_input(view, *inspector.action, view.recipe_entries[choice - offset].id,
                           WORM_UI_COMPOSE_INPUT_RECIPE_SELECT);
    if (selected == view.recipe_entries.end()) return;
    using command = wormedit::ui::compose_recipe_command_t;
    nk_layout_row_dynamic(context, 24, 3);
    if (nk_button_label(context, "Duplicate"))
        queue_recipe_input(view, *inspector.action, selected->id, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, command::duplicate);
    const bool first = selected == view.recipe_entries.begin();
    if (first) nk_widget_disable_begin(context);
    if (nk_button_label(context, "Earlier"))
        queue_recipe_input(view, *inspector.action, selected->id, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, command::earlier);
    if (first) nk_widget_disable_end(context);
    const bool last = selected + 1 == view.recipe_entries.end();
    if (last) nk_widget_disable_begin(context);
    if (nk_button_label(context, "Later"))
        queue_recipe_input(view, *inspector.action, selected->id, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, command::later);
    if (last) nk_widget_disable_end(context);
}

void render_lua_preview(nk_context* context, const wormedit::ui::compose_view_t& view, bool lua) {
    if (view.recipe_form.evaluation_busy) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Evaluating…", NK_TEXT_LEFT);
    }
    if (lua && !view.recipe_form.sampled_points.empty()) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_labelf(context, NK_TEXT_LEFT, "%zu sampled points", view.recipe_form.sampled_points.size());
        nk_layout_row_dynamic(context, 20, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Composed curve: %zu points", view.recipe_form.composed_point_count);
        if (nk_tree_push(context, NK_TREE_TAB, "Sampled points", NK_MINIMIZED)) {
            for (const auto& point : view.recipe_form.sampled_points) {
                nk_layout_row_dynamic(context, 18, 1);
                nk_labelf(context, NK_TEXT_LEFT, "Row %llu: %.8g",
                    static_cast<unsigned long long>(point.absolute_row), point.value);
            }
            nk_tree_pop(context);
        }
    }
}

bool impulse_fields(nk_context* context, wormedit::ui::timing_recipe_form_t& form) {
    bool changed = false;
    if (nk_tree_push(context, NK_TREE_TAB, "Impulses", NK_MINIMIZED)) {
        for (auto& impulse : form.impulses) {
            changed |= rational_field(context, "Position p", impulse.at);
            changed |= rational_field(context, "Signed area", impulse.area);
        }
        changed |= rational_field(context, "Width (ms)", form.finite_box_width_milliseconds);
        nk_layout_row_dynamic(context, 24, 2);
        if (nk_button_label(context, "Add impulse") && form.impulses.size() < wormedit::maximum_sv_recipe_impulses) {
            wormedit::ui::timing_recipe_impulse_form_t impulse;
            std::snprintf(impulse.at.numerator, sizeof(impulse.at.numerator), "0");
            std::snprintf(impulse.at.denominator, sizeof(impulse.at.denominator), "1");
            std::snprintf(impulse.area.numerator, sizeof(impulse.area.numerator), "1");
            std::snprintf(impulse.area.denominator, sizeof(impulse.area.denominator), "1");
            form.impulses.push_back(impulse);
            changed = true;
        }
        if (nk_button_label(context, "Remove last") && !form.impulses.empty()) {
            form.impulses.pop_back();
            changed = true;
        }
        nk_tree_pop(context);
    }
    return changed;
}

void render_impulse_preview(nk_context* context, const wormedit::ui::compose_view_t& view) {
    if (view.recipe_form.impulse_boxes.empty()) return;
    if (nk_tree_push(context, NK_TREE_TAB, "Compiled impulse boxes", NK_MINIMIZED)) {
        for (const auto& box : view.recipe_form.impulse_boxes) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_labelf(context, NK_TEXT_LEFT, "Rows %llu to %llu (%.6g ms)",
                static_cast<unsigned long long>(box.entry_row), static_cast<unsigned long long>(box.exit_row),
                box.width_milliseconds);
            nk_layout_row_dynamic(context, 20, 1);
            nk_labelf(context, NK_TEXT_LEFT, "Area %.8g; error %.3g", box.area, box.area_error);
        }
        nk_tree_pop(context);
    }
}

} // namespace

void render_compose_recipe_form(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    render_recipe_order(inspector);
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
    int curve = static_cast<int>(form.curve);
    changed |= cycle_button(context, "Curve", curve_label(form.curve), curve, 5);
    form.curve = static_cast<wormedit::sv_recipe_curve_t>(curve);
    const bool lua = form.curve == wormedit::sv_recipe_curve_t::lua_expression;
    changed |= lua ? lua_fields(context, view, form) : curve_values(context, form);
    changed |= sampling_values(context, form);
    changed |= transition_values(context, form);
    changed |= impulse_fields(context, form);
    if (changed) {
        state.recipe_editor.recipe_edit_dirty = true;
        queue(view, *action, WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM);
        form.lua_reload = false;
    }
    for (const auto label : view.recipe_form.postprocess_labels) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_text(context, label.data(), static_cast<int>(label.size()), NK_TEXT_LEFT);
    }
    if (!view.recipe_form.status_text.empty()) {
        nk_layout_row_dynamic(context, 36, 1);
        nk_label_wrap(context, view.recipe_form.status_text.data());
    }
    render_lua_preview(context, view, lua);
    render_impulse_preview(context, view);
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
